# Flicker-Free Modal Resize on DXGI Composition Swapchains — Backend Contract

**Purpose.** This document specifies, renderer-agnostically, how `imguiapp_impl_win32_d2ddxgi` achieves
flicker-free live modal resize (including the hard cases: left/top/corner edges that move the window
origin) and how it implements secondary imgui viewports on DirectComposition. A `gldxgi`, `d3d12dxgi`, or
`vkdxgi` sibling that satisfies every invariant below reproduces the exact same behavior. The canonical
reference implementation is `ImmersiveWindow.c` (opus-source); the compositor model is documented in
[DirectComposition: Architecture and components](https://learn.microsoft.com/en-us/windows/win32/directcomp/architecture-and-components).

---

## 1. Why composition windows flicker on resize

Two independent actors update what the user sees:

1. **The window manager** commits the window's new frame geometry (origin + size) as part of the
   `SetWindowPos` transaction driven by the modal size loop.
2. **The application** presents new client content into a swapchain whose pixels the compositor (DWM)
   samples.

The DirectComposition engine produces **one frame per vertical blank**. At each frame start it atomically
latches *everything pending*: committed DirectComposition batches, presented swapchain buffers, and the
window manager's geometry updates. Anything submitted before that instant is in the frame; anything after
waits for the next one.

Flicker is exactly one thing: **a compositor frame that latched the new geometry but a stale present** (or
vice versa). For right/bottom edge resizes this is invisible — the origin is fixed, so stale content is
pixel-identical over the surviving region. For left/top/corner resizes the origin moves: stale content
appears translated by the origin delta for one frame, then snaps. That translation is the flicker.

Corollary: the fix is not "render faster." Rendering is the one step whose latency cannot be bounded (a
debug build of a heavy app can take longer than a compositor interval), so any scheme whose correctness
depends on a re-render winning a race against the next compositor latch WILL flicker under load. The
guarantee has three legs: **ordering** (content latched before its geometry where possible), **rationing**
(never let a newer present cancel content the compositor was about to sample), and — decisive for the
origin-moving edges — **self-healing** (when geometry unavoidably outruns content, compensate with a
composition transform instead of a render; see R6).

## 2. The main swapchain: never resize it

The main window's swapchain is created with `CreateSwapChainForComposition` at **primary desktop
resolution** and is **never resized**. `ResizeBuffers` is the single most expensive and flicker-prone
operation in a live resize (it invalidates every buffer reference and serializes app against GPU); the
reference removes it from the loop entirely. The window draws into the top-left sub-rect sized to the
client area; the window bounds clip the rest.

Creation parameters (verbatim from the reference; see the backend for the InitInfo overrides):

| Parameter | Value |
|---|---|
| Size | `GetDeviceCaps(DESKTOPHORZRES/DESKTOPVERTRES)` of the primary monitor |
| Format / Alpha | `B8G8R8A8_UNORM`, `DXGI_ALPHA_MODE_PREMULTIPLIED` |
| SwapEffect / Scaling | `FLIP_DISCARD`, `DXGI_SCALING_STRETCH` |
| BufferCount | 5 |
| Flags | `ALLOW_TEARING \| ALLOW_MODE_SWITCH \| FRAME_LATENCY_WAITABLE_OBJECT` |

Because the swapchain never resizes, **buffer 0 can be bound once, forever**: in D3D11 flip-model, buffer 0
aliases the current back buffer, so a single RTV (and a single D2D target bitmap) created at init stays
valid for the swapchain's lifetime. The DComp wiring is also once-only:
`DCompositionCreateDevice(dxgiDevice)` → `CreateTargetForHwnd(hwnd, TRUE)` → `CreateVisual` →
`SetContent(swapchain)` → `SetRoot` → `Commit`. Subsequent presents flow to the compositor without further
`Commit` calls.

The window itself must be created with `WS_EX_NOREDIRECTIONBITMAP` (no GDI redirection surface — the DComp
visual is the only content source) and borderless-ified via `WM_NCCALCSIZE` (see §4).

## 3. The present ladder

Every present is a **two-step ladder** (the reference's `EndImmersivePaint(fRestart, fVsync)`):

```
Present(0, ALLOW_TEARING | DO_NOT_WAIT | (fRestart ? RESTART : 0));   // flush the front buffer now
if (fRestart || fVsync)
    Present(1, DO_NOT_SEQUENCE);                                      // replace the queued frame, synced
else
    Present(0, ALLOW_TEARING | DO_NOT_WAIT);                          // queue the next immediate frame
```

`DXGI_PRESENT_RESTART` cancels any queued-but-unsampled presents; `DXGI_PRESENT_DO_NOT_SEQUENCE` presents
the current buffer *ahead of and instead of* the would-be next one. Together they implement last-writer-wins
within a compositor frame interval: the newest content supersedes anything still queued, and the compositor
samples exactly one coherent frame. `DXGI_ERROR_WAS_STILL_DRAWING` from `DO_NOT_WAIT` is benign (skip).

The three flavors in use, per path:

| Path | (fRestart, fVsync) | Rationale |
|---|---|---|
| Run-loop frame (default `PresentMode`) | (1, 0) | cancel stale queue, replace synced |
| `WM_NCCALCSIZE` repaint | (1, 0) | pre-geometry content must supersede everything |
| `WM_WINDOWPOSCHANGED` repaint | (1, 1) | post-geometry correction, replace synced |
| Modal timer / exit-size-move | (0, 1) | steady modal cadence, no cancellation |

## 4. The modal repaint protocol (the actual no-flicker guarantee)

Repaints during modal loops are driven from the WndProc, not the run loop. Five rules, in order of
importance:

**R1 — Paint BEFORE geometry, inside `WM_NCCALCSIZE`.** `WM_NCCALCSIZE` (wParam = TRUE) runs *inside* the
`SetWindowPos` transaction, before the window manager commits the new rect. Render a full frame, run the
(1, 0) ladder there. This is the reference's load-bearing trick: content for the tick is queued before the
geometry that accompanies it.

**R2 — Latch content with `Commit` + `WaitForCommitCompletion` before returning from `WM_NCCALCSIZE`**
(when `GetGUIThreadInfo().hwndMoveSize` says a size-move loop is live). Batches and presents are consumed
atomically at compositor frame start; `IDCompositionDevice::WaitForCommitCompletion` blocks until the
engine has processed the last commit — i.e., until a frame start has passed that also latched the present
queued in R1. Returning after this wait means the geometry commit can *never* overtake its content: the
strict order is content → (frame start) → geometry. This replaces the reference's `DwmFlush` with the
sharper, documented primitive for the same purpose.

**R3 — Correct AFTER geometry, immediately, in `WM_WINDOWPOSCHANGED`.** The post-commit repaint (flavor
(1, 1)) redraws at the *new* client size. It must run with **no waits of any kind** — no vblank wait, no
frame-latency wait: it starts as late as possible relative to the geometry it corrects. Do not forward
`WM_WINDOWPOSCHANGED` to `DefWindowProc` (suppresses `WM_SIZE`/`WM_MOVE` generation, per the reference).
Note R3 alone is NOT a guarantee — if this render exceeds the time to the next compositor latch, that
frame pairs new geometry with old content. R6 is what closes the hole.

**R4 — Ration: at most ONE rendered present per compositor frame per surface.** Under rapid mouse movement,
resize ticks and the modal timer outpace the compositor. Extra repaints are pure queue churn — worse, a
later `RESTART` can cancel content the compositor was about to sample, re-pairing new geometry with older
content. Coalesce with `IDCompositionDevice::GetFrameStatistics`: stamp
`DCOMPOSITION_FRAME_STATISTICS::nextEstimatedFrameTime` plus the client size each repaint draws; skip any
modal repaint whose target frame AND client size match the stamp. `WM_NCCALCSIZE` skips only the render —
the R2 ordering wait always runs. A size change always invalidates the stamp (the `WM_WINDOWPOSCHANGED`
correction after a resize tick must never be skipped).

**R5 — Steady cadence via the modal timer.** `WM_ENTERSIZEMOVE`/`WM_ENTERMENULOOP` start a
`USER_TIMER_MINIMUM` timer; each `WM_TIMER` runs a (0, 1) repaint (subject to R4) + `DwmFlush` when not
moving; `WM_EXITSIZEMOVE`/`WM_EXITMENULOOP` kill it and repaint once. This keeps animations alive and
guarantees a repaint source during pure-move loops (where `WM_NCCALCSIZE` never fires; note that a pure
move needs no content repaint for correctness — the DComp visual travels with the window).

**R6 — Content pin: self-healing against geometry that outruns content (THE origin-moving-edge
guarantee).** The presented content was rendered for a specific window origin. Whenever a
`WM_WINDOWPOSCHANGED` reports a geometry change that RESIZED the window (client size differs from the last
presented frame's size — a pure move is excluded, content correctly travels with the window), the origin
may have moved out from under the content. Before triggering any repaint:

```
delta = content_render_origin - current_window_origin      // both from GetWindowRect, screen coords
visual->SetOffsetX(delta.x); visual->SetOffsetY(delta.y);   // the swapchain's DComp visual
device->Commit();                                           // microseconds; no rendering
```

This pins the old content at the exact screen position it was rendered for. Compositor frames that latch
before the R3 re-render completes now show: pinned old content (pixel-stationary — right/bottom-anchored
UI never jumps) + the new window bounds (a grow exposes a transparent strip at the moving edge showing the
blur-behind backdrop; a shrink clips — both are how well-behaved apps look mid-resize). When any present of
fresh content runs, reset the offsets to (0, 0) and `Commit()` **immediately after the present ladder** —
present and offset-reset are then pending together and latch in the same compositor frame, so fresh content
never shows pre-translated. Re-anchor the recorded content origin/size after every present.

This converts the unwinnable render-vs-latch race into a bounded property update: the pin is O(µs) inside
the same synchronous message that delivered the geometry, so the race window collapses from
"render time vs vblank" to effectively zero. It is also self-healing — no matter how late the re-render
lands, every intermediate compositor frame is visually consistent.

**Re-entrancy latches (required for R1/R3 to be safe):**
- `InModalRepaint`: a modal repaint must never nest another.
- `InFrame` (set at backend NewFrame, cleared at PresentFrame): viewport create/destroy inside
  `ImGui::NewFrame`/`UpdatePlatformWindows` sends `WM_WINDOWPOSCHANGED`/`WM_ACTIVATE` to the main window
  *synchronously, mid-frame*; a modal repaint from there would re-enter the frame. Block it.

A "modal repaint" is by default a full app frame (`app->Frame()`) with the present flavor pinned to the
path's (fRestart, fVsync) pair; a render-only mode may just re-run the ladder.

## 5. Porting to another renderer (gldxgi / vkdxgi / d3d12dxgi)

Nothing in §§2–4 is renderer-specific. The swapchain, the DComp tree, the present ladder, and the WndProc
protocol stay exactly as specified — **keep them on DXGI/DComp regardless of the rendering API**. The only
renderer-specific problem is landing pixels in the composition swapchain's buffer 0 before the ladder runs:

- **D3D11** (current backend): bind the once-created RTV of buffer 0; render directly.
- **D3D12**: flip-model rules differ — buffer 0 does NOT alias the current back buffer; use
  `GetCurrentBackBufferIndex()` and per-buffer RTVs (still no `ResizeBuffers`, ever). Present ladder flags
  are identical.
- **Vulkan**: create the D3D11 (or D3D12) device solely to own the composition swapchain; share buffer 0
  (or an intermediate D3D texture) into Vulkan via `VK_KHR_external_memory_win32` +
  `IDXGIResource1::CreateSharedHandle`, synchronized with `VK_KHR_external_semaphore_win32`/keyed mutex;
  render in Vulkan, then the D3D side presents the ladder. Alternative: render to a Vulkan image and copy
  through the shared texture.
- **OpenGL**: `WGL_NV_DX_interop2` — register buffer 0 (or an intermediate) as a GL renderbuffer/texture
  (`wglDXRegisterObjectNV`), bracket GL rendering with `wglDXLock/UnlockObjectsNV`, present the ladder from
  the D3D side. (This is the "cwgl.h" pattern sketched in the reference tree.)

In every case the *presenting* device is the DXGI device that created the composition swapchain — the
render API only fills pixels. `WaitForCommitCompletion`, `GetFrameStatistics`, the ladder flags, the R6
content pin (a pure DComp visual property, renderer-agnostic by construction), and the WndProc protocol are
untouched by the port.

Renderer-side requirements the modal path imposes:
- Rendering must be re-entrant from the WndProc (single-threaded, synchronous — a modal repaint completes
  before the message handler returns).
- The frame must tolerate the client rect changing between repaints without swapchain reallocation
  (viewport/scissor from the draw data, not from cached surface size).

## 6. Secondary imgui viewports on DirectComposition

Secondary (multi-viewport) windows each get their **own** composition swapchain + DComp target, replacing
the hooks the wrapped imgui renderer backend installed (its per-hwnd blt swapchains would fight the
composition tree). One shared `IDCompositionDevice`; per-viewport `IDCompositionTarget` + visual.

Secondary windows have their own flicker anatomy. imgui resizes them itself from
`UpdatePlatformWindows` — there is no Win32 modal size loop and none of §4's messages fire on our WndProc.
Three separate defects make a naive implementation flicker on origin-moving edge resizes:

1. imgui applies a move+resize as **two separate OS transactions** (`Platform_SetWindowPos`, then
   `Platform_SetWindowSize`) — a compositor latch between them shows a moved window at the old size;
2. both run **early in the frame**, a full render+present away from the content that matches them;
3. an eager `ResizeBuffers` in `Renderer_SetWindowSize` **destroys the presented content** the compositor
   may still need to sample until the fresh present lands.

The cure is the main window's design, scaled down — over-allocation instead of desktop-sizing, and hook
wrapping instead of WndProc handling:

- **Grow-only over-allocated swapchains.** Create the swapchain rounded UP to a 256-px granularity;
  `Renderer_SetWindowSize` never calls `ResizeBuffers` while the size fits capacity (the window clips the
  unused buffer region — the same reason the main window's desktop-sized buffer works). Only growth beyond
  capacity reallocates, and that is *deferred* to `Renderer_RenderWindow`, immediately before the render +
  present, so the buffer is content-less for the smallest possible window. Never shrink.
- **Coalesced, late geometry.** Wrap `Platform_SetWindowPos`/`Platform_SetWindowSize` (chain to the
  platform backend's originals): the wrappers only *record* the pending pos/size. The flush runs
  back-to-back (pos, then size — upstream order) at the top of `Platform_RenderWindow`, adjacent to the
  render + present that produce the matching content, instead of a frame-phase away.
- **Per-viewport content pin (R6, per viewport).** After flushing a geometry change that included a
  RESIZE, compare the window's new origin against the origin recorded at that viewport's last present; if
  it moved, `SetOffsetX/Y(delta)` + `Commit()` on the viewport's own visual. `Renderer_SwapBuffers` unpins
  (offsets to 0 + `Commit()`) right after its `Present(0, 0)` — same-latch coupling — and re-anchors the
  recorded origin/size. Pure moves record no pin (content travels with the window).

Per-viewport lifecycle (installed on `ImGuiPlatformIO` after the wrapped renderer's Init):

- **`Renderer_CreateWindow`**: `CreateSwapChainForComposition` at rounded-up capacity — BGRA8
  premultiplied, `FLIP_DISCARD`, BufferCount 3, **flags 0** (no tearing, no waitable: secondary windows
  present through the compositor; tearing flags are meaningless and the waitable adds nothing). Then RTV of
  buffer 0, `CreateTargetForHwnd(viewport_hwnd, TRUE)`, `CreateVisual`, `SetContent(swapchain)`, `SetRoot`,
  `Commit()`. HWND comes from `viewport->PlatformHandleRaw ? PlatformHandleRaw : PlatformHandle`.
- **`Renderer_SetWindowSize`**: capacity bookkeeping only (grow-only; mark pending growth).
- **`Platform_SetWindowPos` / `Platform_SetWindowSize`** (wrappers): record pending geometry; chain the
  originals directly for viewports without renderer data.
- **`Platform_RenderWindow`** (wrapper; the first per-viewport hook `RenderPlatformWindowsDefault` runs):
  flush pending geometry → content pin if the resize moved the origin → make the pacing decision ONCE per
  viewport per frame (`AppPacerViewportShouldPresent`) into a viewport-ID-keyed skip map.
- **`Renderer_RenderWindow`**: honor the skip; apply pending capacity growth (release RTV,
  `ResizeBuffers` to capacity, recreate RTV); bind the viewport RTV, clear unless
  `ImGuiViewportFlags_NoRendererClear`, render the viewport's draw data.
- **`Renderer_SwapBuffers`**: honor the skip; `Present(0, 0)`; unpin + re-anchor the content origin/size.
- **`Renderer_DestroyWindow`**: release visual, target, RTV, swapchain; main viewport's RendererUserData is
  null by contract (the app owns it).

Known-correct caveats:

- **Redirection bitmap**: the platform backend (`imgui_impl_win32`) creates secondary windows *without*
  `WS_EX_NOREDIRECTIONBITMAP` (the style cannot be added post-creation). The DComp visual composes above
  the redirection surface; since imgui forces secondary-window backgrounds opaque, this is invisible. A
  translucent secondary viewport would need a platform-side window-creation override.
- **Destroy-time re-entrancy**: viewport destruction (`::DestroyWindow` of an owned window) runs inside
  `ImGui::NewFrame` and synchronously activates the main window. This is the reason the `InFrame` latch in
  §4 exists; without it the main window's repaint paths re-enter the frame. Regression:
  `tests/imguiapp_d2ddxgi_viewport_tests.cpp` (programmatic outside/inside churn, 6 create/destroy cycles).
- **Deferred geometry visibility**: between a wrapper call and its flush, `Platform_GetWindowPos/Size`
  report the not-yet-flushed values for at most the remainder of the same frame; imgui re-reads window
  state at the next `NewFrame`, after the flush. Minimized viewports skip `Platform_RenderWindow`, so
  their pending geometry flushes on the next visible frame.
- **OS-decorated secondary viewports** (`ImGuiConfigFlags`/window flags yielding real thick frames) resize
  through a Win32 modal loop on imgui's *platform* WndProc, not ours — §4's protocol does not run for
  them. The default imgui configuration (undecorated secondaries, imgui-drawn borders) is the supported
  no-flicker path.

## 7. Dirty-rect presentation (the FLIP_SEQUENTIAL variant)

`ImGuiApp_ImplWin32D2DDXGI_InitInfo::PresentDirtyRects = true` switches every swapchain (main +
secondary viewports) from `FLIP_DISCARD` full-frame presents to `FLIP_SEQUENTIAL` + `Present1` with an
explicit damage region. Nothing in §§2–6 changes — the present ladder keeps its flags and step structure,
the pin/coalesce/latch machinery is orthogonal — each `Present` call simply becomes a `Present1` carrying
`DXGI_PRESENT_PARAMETERS`.

Why this pairs perfectly with the never-resized/over-allocated buffer design: the buffers are much larger
than the visible window, and a full-frame present tells the compositor the whole desktop-sized surface
changed. Declaring the client rect as the only dirty region lets the compositor recompose just that
sub-rect. The swap effect must be `FLIP_SEQUENTIAL` because dirty rects are a promise about the rest of
the buffer, and only SEQUENTIAL preserves buffer contents across presents.

The DXGI rules a port must honor:

- **The outside-the-dirty-rects contract.** Everything outside the declared rects must be unchanged
  relative to the *previously presented* frame. This backend redraws the full client rect every frame
  (imgui always re-renders), so "client rect" IS the frame's damage. Beyond the client rect the buffers
  hold their creation-time contents (DXGI zero-initializes: transparent premultiplied black) in every
  buffer, so the contract holds there trivially.
- **First present is full-frame.** After swapchain creation — and after every `ResizeBuffers` — present
  once with `DirtyRectsCount = 0` (full frame) before any dirty-rect present (`PresentedOnce` /
  per-viewport `PresentedOnce` flags). The main swapchain never resizes, so it pays this exactly once;
  secondary viewports pay it after each capacity growth.
- **Buffer staleness under SEQUENTIAL.** With BufferCount N, the buffer you receive after a present holds
  the frame from N presents ago. A future *sub-client* damage strategy (per-imgui-window rects, damage
  diffing) must render the union of the last N−1 frames' damage into each buffer — the classic
  flip-sequential accumulation rule — while still *declaring* only this frame's damage to `Present1`.
  The current implementation sidesteps this entirely by re-rendering the whole client rect; the client
  rect is therefore both the render region and the declared damage.
- **Ladder interaction (validated against the runtime).** `DXGI_PRESENT_ALLOW_TEARING` is INVALID
  alongside partial-presentation parameters — dirty-rect mode drops it from the present flags (it is
  meaningless through the compositor anyway). The `DO_NOT_SEQUENCE` replace step re-presents the SAME
  buffer and rejects partial parameters — it passes an empty (full-frame) `DXGI_PRESENT_PARAMETERS`;
  only the first ladder step declares the damage. `RESTART` and `DO_NOT_WAIT` combine with `Present1`
  unchanged. The R6 pin needs no adjustment: dirty rects are buffer-space, the pin is a visual-space
  transform.

## 8. Verification checklist

- Live-resize each edge/corner; then again with rapid, erratic mouse movement. Left/top/bottomleft/topleft
  must never show content translated by the origin delta (the R6 pin); the worst permitted artifact is a
  transient backdrop strip at the growing edge while a slow re-render catches up.
- Undock a window (secondary viewport) and repeat the edge/corner resize matrix on it, dragging its
  imgui-drawn borders rapidly: same standard — no translated content, no black flash on growth (grow-only
  capacity means `ResizeBuffers` fires only when crossing a 256-px capacity boundary, adjacent to the
  fresh present).
- Drag the window rapidly (pure move): content glued, no repaint storm (R4 coalescing active).
- Undock a window past the main viewport edge (secondary viewport spawns), redock it (viewport destroyed):
  no crash, no frame hitch (`InFrame` latch; run the regression test).
- Hold a menu/`TrackPopupMenu` open: animations keep running (modal timer), no queue churn (R4).
- Recording (`CaptureFrame`) during a live resize: encode phase reads buffer 0 between render and present —
  unaffected by the modal protocol.
