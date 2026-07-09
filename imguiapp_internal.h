// dear imgui app, v0.5.0 WIP
// (internal structures/api -- the imgui_internal.h analog for the applayer)

// You may use this file to debug, understand or extend the applayer; no guarantee of forward compatibility.

/*

Index of this file:

// [SECTION] Header mess
// [SECTION] Forward declarations
// [SECTION] Macros
// [SECTION] AV meta stream + recorder + run artifacts (was imguiapp_av.h)
// [SECTION] Meta stream (embedded in the video)
// [SECTION] Meta-only recorder + stream stats
// [SECTION] Run index (F62): tick-keyed landing over a reconstructed meta stream
// [SECTION] State-at-tick (F64): restore nearest snapshot + replay inputs to tick N
// [SECTION] Transport source (F63): the App-time transport's two frame sources
// [SECTION] Design-phase node drafts (ImGuiAppFieldType, ImGuiAppFieldDesc, ImGuiAppNodeDraft)
// [SECTION] Typed graph kinds (node/layer/port/edge discriminators)
// [SECTION] Graph topology and persistence (ImGuiAppNodeLink, link capture, save/load)
// [SECTION] Typed node graph (ports, nodes, bindings, scope memory, view state, prefabs, undo)
// [SECTION] Editor + composer value types (registry, issues, chrome/theme, spans)
// [SECTION] ImGuiAppEditorState, ImGuiAppGraph (the composer document + session)
// [SECTION] Embeddable Composer control (generated-shell bootstrap)
// [SECTION] Animation builtins (was imguiapp_anim.h)
// [SECTION] Headless test harness (was imguiapp_testharness.h)
// [SECTION] Process state root (GImGuiAppState)
// [SECTION] ImGui applayer internal API (core)
// -- #ifndef IMGUIX_DISABLE_TOOLS (a lean build cuts everything below) --
// [SECTION][TOOLS] Canvas UI (was imguiapp_canvas.h)
// [SECTION][TOOLS] ImGui applayer internal API -- canvas UI, DLL preview surface, editor UI

*/

#pragma once

//-----------------------------------------------------------------------------
// [SECTION] Header mess
//-----------------------------------------------------------------------------
// Internal + tool-facing interfaces on top of the public imguiapp.h. Everything under IMGUIX_DISABLE_TOOLS
// (the tail) compiles out in a lean build. Public runtime + AV recording types live in imguiapp.h.

#include "imguiapp.h"
#ifndef IMGUI_DISABLE
#include "imgui_internal.h"   // ImFormatString
#include <format>             // std::format_to_n (reflection layer, docs/house-style-audit.md Δ2)
#include <string_view>        // std::string_view (reflection layer, Δ2)
#include <type_traits>        // std::is_trivially_copyable_v (storage/snapshot contracts, Δ2)

//-----------------------------------------------------------------------------
// [SECTION] Forward declarations
//-----------------------------------------------------------------------------

// Opaque sessions + externals (held by pointer; defined in their own TUs)
struct ImGuiAppPreview;         // per-document interpreter session (imguiapp_preview.h)
struct ImGuiAppPreviewDll;      // DLL preview session (imguiapp_preview_dll.h)
struct ImGuiTestEngine;         // imgui_test_engine (test-harness impl only)

// AV meta stream + recorder + run artifacts
struct ImGuiAppAVStreamStats;   // integrity ladder recomputed from a meta stream
struct ImGuiAppMetaRecorder;    // meta-only run recorder (no video)
struct ImGuiAppRunCommand;      // one dispatched command at a tick
struct ImGuiAppRunIndex;        // tick-keyed index over a reconstructed stream
struct ImGuiAppRunMeta;         // per-run header decoded from the stream
struct ImGuiAppRunState;        // reconstructed values at a scrubbed tick
struct ImGuiAppRunTick;         // one tick's landing (offset + payload)
struct ImGuiAppRunView;         // App-time transport view (LiveRing vs FileRun)

// Graph model + editor
struct ImGuiAppCommandDesc;     // authored command on a node
struct ImGuiAppDragStick;       // drag-gesture state
struct ImGuiAppEditorUndo;      // one undo entry
struct ImGuiAppEventDesc;       // authored event (edge + action)
struct ImGuiAppFieldBinding;    // field-to-producer wiring
struct ImGuiAppFieldDesc;       // one field of a design-phase node draft
struct ImGuiAppGraphViewState;  // transient per-frame editor view state
struct ImGuiAppGroupFrame;      // group frame around nodes
struct ImGuiAppKeyBinding;      // key-to-command binding
struct ImGuiAppNode;            // one node in the graph
struct ImGuiAppNodeDraft;       // node whose backing C++ type does not exist yet
struct ImGuiAppNodeLink;        // one edge in the typed graph
struct ImGuiAppNodePort;        // one pin on a node
struct ImGuiAppOpOperand;       // one operand of an op-fold expression
struct ImGuiAppPrefab;          // reusable node cluster
struct ImGuiAppScopeCamera;     // per-scope camera
struct ImGuiAppScopeOrder;      // sequence order within a scope
struct ImGuiAppScopePlacement;  // per-scope node placement
struct ImGuiAppTrunkRoute;      // routed wire trunk
struct ImGuiAppTrunkSeg;        // one segment of a wire trunk

struct ImGuiAppEditorState;     // per-graph editor session state
struct ImGuiAppGraph;           // the authored document graph

// Embeddable Composer control
struct ImGuiAppComposerControl;
struct ImGuiAppComposerControlData;

// Animation builtins
struct ImAppPulse;   struct ImAppPulseData;   struct ImAppPulseTempData;
struct ImAppSpring;  struct ImAppSpringData;  struct ImAppSpringTempData;
struct ImAppTimer;   struct ImAppTimerData;   struct ImAppTimerTempData;
struct ImAppTween;   struct ImAppTweenData;   struct ImAppTweenTempData;

// Headless test harness
struct ImGuiAppTestHarnessConfig;

// Canvas UI [TOOLS]
struct ImGuiAppCanvasIO;           // per-frame canvas input
struct ImGuiAppCanvasNodeRec;      // submitted node record
struct ImGuiAppCanvasPinRec;       // submitted pin record
struct ImGuiAppCanvasState;        // canvas engine state (opaque to callers)
struct ImGuiAppCanvasStyle;        // canvas visual style
struct ImGuiAppCanvasWireRec;      // per-frame wire submission

struct ImGuiAppGraphIssue;     // fwd (declared with the validation API below)

// Enumerations / Flags (full lists at their [SECTION]s)
typedef int ImGuiAppAVMetaRecordType;  // -> enum ImGuiAppAVMetaRecordType_  // meta-stream record type
typedef int ImGuiAppEdgeKind;          // -> enum ImGuiAppEdgeKind_          // edge discriminator
typedef int ImGuiAppEventAction;       // -> enum ImGuiAppEventAction_       // event action
typedef int ImGuiAppEventEdge;         // -> enum ImGuiAppEventEdge_         // event trigger edge
typedef int ImGuiAppFieldType;         // -> enum ImGuiAppFieldType_         // draft field scalar type
typedef int ImGuiAppHoverSource;       // -> enum ImGuiAppHoverSource_       // Enum: what the editor hover targets
typedef int ImGuiAppLayerType;         // -> enum ImGuiAppLayerType_         // layer discriminator
typedef int ImGuiAppPortKind;          // -> enum ImGuiAppPortKind_          // port discriminator
typedef int ImGuiAppTransportSource;   // -> enum ImGuiAppTransportSource_   // LiveRing vs FileRun

//-----------------------------------------------------------------------------
// [SECTION] Macros
//-----------------------------------------------------------------------------

// Buffer-size census (F27): recurring de-facto sizes used WITHOUT per-site comments -- 560-byte
// path buffers (filesystem path + suffix headroom) and 1200-byte compiler command lines. Any
// other off-census fixed size carries its reason at the site.
#define IMGUIAPP_CONTROL_BODY_MAX 2048   // ImGuiAppNodeDraft per-method custom C++ body cap (fixed buffer -> draft stays memcpy-safe)

#ifndef IMGUIAPP_PREVIEW_ABI
#define IMGUIAPP_PREVIEW_ABI 20260713u   // host<->DLL preview ABI tag (F78); bump on any layout/vtable/signature change. Since 0.4.1 (July 2026, 401)
#endif

// Private command range: dispatch ids above the public ImGuiAppCommand_COUNT.
enum ImGuiAppCommandPrivate : int
{
    ImGuiAppCommand_PrivateBegin_ = ImGuiAppCommand_COUNT,
};

//-----------------------------------------------------------------------------
// [SECTION] AV meta stream + recorder + run artifacts (was imguiapp_av.h)
//-----------------------------------------------------------------------------
// Frame encode-to-video + run artifacts (docs/designs.md (av-design)). SEAM only: encoder implementations
// live in backends/imguiapp_impl_*.h; this header never references a provider. The recording BEHAVIOR
// types are public (imguiapp.h); here: the meta-record stream format, meta-only recorder, and the read side.

//-----------------------------------------------------------------------------
// [SECTION] Meta stream (embedded in the video)
//-----------------------------------------------------------------------------

// Typed-record stream written by the recorder (uniform across providers): fixed header
// (magic "IMAVMETA", version, fps, start TSC + QPC Hz), then {u32 type, u32 size, payload}.
enum ImGuiAppAVMetaRecordType_
{
    ImGuiAppAVMetaRecordType_None  = 0,      // invalid/unset
    ImGuiAppAVMetaRecordType_Frame = 1,      // frame_index, tsc, time_sec, user blob
    ImGuiAppAVMetaRecordType_InputHdr,       // ImGuiAppInputLog layout (composition id, slot table); once per take. OPT-IN (AppRecordAttachInputLog)
    ImGuiAppAVMetaRecordType_InputFrame,     // frame_index + one input-log frame (TempData + dt) + state hash. OPT-IN derived checkpoint
    ImGuiAppAVMetaRecordType_StateSnapshot,  // composition id + snapshottable-state bytes (ImGuiAppStateHistory layout)
    // Raw input, recorded EVERY frame by default (the source events; O(1) per frame). Payload frozen --
    // docs/designs.md (av-design) record catalog: mouse/wheel/keys/chars + state_hash + splice-evident
    // chain (chain_k = ImHashData(&state_hash_k, 4, chain_{k-1}), seeded by the Identity schema hash).
    ImGuiAppAVMetaRecordType_IoFrame,
    // Take identity, emitted ONCE after the stream header (before any Frame/IoFrame). Replay classifies
    // mismatches against it: identity differs -> version/schema mismatch (refused before replay); identity
    // matches but the hash chain diverges at k -> nondeterminism or corruption at k. Payload: av-design catalog.
    ImGuiAppAVMetaRecordType_Identity,
    // Take completeness proof, the stream's FINAL record: u64 stream_bytes | u32 digest over exactly those
    // bytes. Presence = complete take; absence = truncation (crash) -- WAL-style honesty made checkable.
    // Rides the final frame's chunk; a take whose last frame lacks chunk room truncates it like any tail.
    ImGuiAppAVMetaRecordType_Digest,
    ImGuiAppAVMetaRecordType_AudioPcm,       // RESERVED: frame_index + sample format header + PCM chunk (no producer yet)
};

//-----------------------------------------------------------------------------
// [SECTION] Meta-only recorder + stream stats
//-----------------------------------------------------------------------------
// The video recorder (ImGuiAppRecorder) + its config are public -- see imguiapp.h. Here: the F70
// meta-only export (no video, same TLV records) and the verify-side reconstructed-stream stats.

struct ImGuiAppMetaRecorder
{
    ImGuiAppRecorder Rec;
    ImVector<char>   Meta;        // the growing IMAVMETA buffer (header + framed records)
    int              EmbedRows;   // declared strip depth carried in the Identity record

    ImGuiAppMetaRecorder() { EmbedRows = 0; }
};

// Result of AppAVMetaVerify: the full integrity ladder recomputed from a reconstructed
// meta stream. CorruptFrames is extraction-level knowledge (the stream walk cannot see
// frames the extractor dropped) -- callers with an extractor count overwrite it.
struct ImGuiAppAVStreamStats
{
    int   Frames;
    int   IoFrames;
    int   InputHdrs;
    int   InputFrames;
    int   Snapshots;
    int   Identities;
    ImU64 FirstTick;
    ImU64 LastTick;
    int   TickGaps;
    bool  ChainOk;
    int   ChainDivergesAt; // io-frame ordinal of the first divergence; -1 = none
    int   DigestState;     // 0 ok, 1 missing (truncated take), 2 mismatch (corruption)
    int   CorruptFrames;

    ImGuiAppAVStreamStats() { Frames = 0; IoFrames = 0; InputHdrs = 0; InputFrames = 0; Snapshots = 0; Identities = 0; FirstTick = 0; LastTick = 0; TickGaps = 0; ChainOk = false; ChainDivergesAt = -1; DigestState = 1; CorruptFrames = 0; }
};

//-----------------------------------------------------------------------------
// [SECTION] Run index (F62): tick-keyed landing over a reconstructed meta stream
//-----------------------------------------------------------------------------
// One walk of the reconstructed meta buffer (the traversal AppAVMetaVerify performs), recording each
// record's tick + payload offset instead of only counting. The playback debugger's index (docs/designs.md
// (playback-debugger-design) section 3): no new byte format; all offsets index the buffer the index owns.

// Decoded Identity record: the trust gate for state reconstruction (F64). A reconstruction
// app is legal only when its composition id and schema hash equal these.
struct ImGuiAppRunMeta
{
    ImU32   ApplayerVersion;
    ImU32   ImguiVersion;
    ImGuiID CompositionID;
    ImU32   SchemaHash;
    ImU32   EmbedRows;
    ImU16   BlockSize;

    ImGuiAppRunMeta() { ApplayerVersion = 0; ImguiVersion = 0; CompositionID = 0; SchemaHash = 0; EmbedRows = 0; BlockSize = 0; }
};

// One tick (FirstTick..LastTick). The Frame spine plus byte offsets to THIS tick's
// IoFrame / InputFrame / StateSnapshot record payloads (-1 when the tick has none).
struct ImGuiAppRunTick
{
    ImU64  Tick;           // == Frame/IoFrame frame_index (the single correlation key)
    ImU64  Tsc;            // Frame.tsc
    double TimeSec;        // Frame.time_sec (PTS; also QOI index.tsv)
    int    FrameImage;     // frame ordinal for decode (mp4 sample / QOI NNNNNN); -1 = none
    int    IoOffset;       // payload offset of this tick's IoFrame record (-1 if none)
    ImU32  StateHash;      // IoFrame.state_hash (recorded fingerprint)
    ImU32  Chain;          // IoFrame.chain recomputed from the Identity seed (divergence lives here)
    int    InputOffset;    // InputFrame payload offset (-1 if not opt-in this tick)
    int    SnapshotOffset; // StateSnapshot payload offset at this tick (-1 if none)
    int    WalFirst;       // slice start into ImGuiAppRunIndex::Commands (this tick's dispatches); -1 = none
    int    WalCount;       // command dispatches at this tick (0 until AppRunAttachWal parses a sibling WAL)

    ImGuiAppRunTick() { Tick = 0; Tsc = 0; TimeSec = 0.0; FrameImage = -1; IoOffset = -1; StateHash = 0; Chain = 0; InputOffset = -1; SnapshotOffset = -1; WalFirst = -1; WalCount = 0; }
};

// One WAL-correlated command dispatch: the "execute command %d" line's tick + command id
// (imguiapp.cpp Command layer). AppRunAttachWal buckets these by [tick:N]; the per-tick slice
// is ImGuiAppRunTick::WalFirst..+WalCount. Read-only annotation -- the recording is authoritative.
struct ImGuiAppRunCommand
{
    ImU64 Tick;
    int   CommandId;

    ImGuiAppRunCommand() { Tick = 0; CommandId = 0; }
};

// F62 build product: the per-tick index, the snapshot list, and the verify ladder, all from
// ONE walk. Heap-owned by AppRunOpen; AppRunClose frees. Opaque to the snapshot contract.
struct ImGuiAppRunIndex
{
    ImVector<char>            Meta;          // owned copy of the reconstructed meta buffer (offsets index into it)
    ImGuiAppAVMetaHeader      Header;        // fps + start clocks
    ImGuiAppRunMeta           Identity;      // decoded Identity (composition_id, schema_hash, versions)
    ImVector<ImGuiAppRunTick> Ticks;         // FirstTick..LastTick in emission order
    ImVector<int>             SnapshotTicks; // ascending tick-array indices where SnapshotOffset>=0 (nearest lookup)
    ImVector<ImGuiAppRunCommand> Commands;   // WAL command dispatches, tick-sorted (AppRunAttachWal); empty until attached
    ImGuiAppAVStreamStats     Stats;         // == AppAVMetaVerify output over the same bytes
};

//-----------------------------------------------------------------------------
// [SECTION] State-at-tick (F64): restore nearest snapshot + replay inputs to tick N
//-----------------------------------------------------------------------------
// docs/designs.md (playback-debugger-design) section 5: restore the nearest StateSnapshot <= N, then
// AppInputReplay the reconstructed input frames (S,N] into a reconstruction app. Legal only when the recon
// app's composition + schema equal the take's Identity -- refused on mismatch, never faked; raw-io-only takes degrade.

// Result of AppRunStateAtTick. Values live in the recon app's storage after a successful call;
// this struct carries the reconstruction's provenance + the exactness check the debugger surfaces.
struct ImGuiAppRunState
{
    int     TickIndex;          // requested N (index into ImGuiAppRunIndex::Ticks)
    ImU64   Tick;               // Ticks[N].Tick
    int     SnapshotTickIndex;  // tick-array index of the nearest snapshot S<=N restored from (-1 = none)
    ImU64   SnapshotTick;       // Ticks[S].Tick
    int     ReplayedFrames;     // input frames replayed forward (S,N]; 0 when N is itself a snapshot tick
    int     FirstDivergence;    // AppInputReplay's first divergent replayed frame (-1 = exact reproduction)
    ImU32   RecordedStateHash;  // Ticks[N].StateHash (the recorded ground truth)
    ImGuiID ReconstructedHash;  // AppStateHash(recon) after restore+replay (== RecordedStateHash on exact)
    int     CmdFirst;           // Ticks[N].WalFirst -- this tick's command dispatches (needs AppRunAttachWal)
    int     CmdCount;           // Ticks[N].WalCount
    bool    Reconstructed;      // identity gate + snapshot restore + replay all succeeded

    ImGuiAppRunState() { TickIndex = -1; Tick = 0; SnapshotTickIndex = -1; SnapshotTick = 0; ReplayedFrames = 0; FirstDivergence = -1; RecordedStateHash = 0; ReconstructedHash = 0; CmdFirst = -1; CmdCount = 0; Reconstructed = false; }
};

//-----------------------------------------------------------------------------
// [SECTION] Transport source (F63): the App-time transport's two frame sources
//-----------------------------------------------------------------------------
// The F29 transport (a live state ring) gains a SOURCE switch behind the design's Count()/Show(int) view
// (docs/designs.md (playback-debugger-design) section 4). LiveRing restores snapshotted bytes into the
// running app; FileRun decodes the recorded frame image at a tick and blits its pixels -- no app driven.
enum ImGuiAppTransportSource_
{
    ImGuiAppTransportSource_LiveRing = 0,   // the ImGuiAppComposerTransport state ring + the live mirror
    ImGuiAppTransportSource_FileRun,        // a recorded run opened by AppRunOpen (this file's index)
};

// Per-backend single-frame decode (the seam: this core header never names a provider). Fills
// out_rgba with tightly packed RGBA8 for frame_ordinal (mp4 sample / QOI NNNNNN). The QOI
// provider is ImGuiApp_ImplQoi_DecodeFrame; the caller adapts it to this signature.
typedef bool (*ImGuiAppRunFrameDecodeFn)(void* user, int frame_ordinal, ImVector<char>* out_rgba, int* out_w, int* out_h);

// FILE-mode transport source (F63): a borrowed run index + frame decoder behind Count()/Show(int). Show(i)
// records ShownTick = Ticks[i].Tick (a scrub/step ALWAYS lands an exact tick, no interpolation) and decodes
// that tick's frame image into Pixels. State rides the caller's transport object; nothing here is global.
struct ImGuiAppRunView
{
    ImGuiAppRunIndex*        Run;        // borrowed (AppRunOpen'd + AppRunClose'd by the owner), not owned here
    ImGuiAppRunFrameDecodeFn Decode;     // per-backend frame decode; null = tick-only scrub (no blit)
    void*                    DecodeUser; // provider context (e.g. the recording directory)
    int                      Scrub;      // current scrub index (0..Count-1)
    ImVector<char>           Pixels;     // RGBA8 of the frame at ShownImage (Width*Height*4 bytes)
    int                      Width;
    int                      Height;
    ImU64                    ShownTick;  // tick of the frame currently in Pixels (== Ticks[Scrub].Tick)
    int                      ShownImage; // frame ordinal decoded into Pixels (-1 = none decoded)

    ImGuiAppRunView() { Run = nullptr; Decode = nullptr; DecodeUser = nullptr; Scrub = 0; Width = 0; Height = 0; ShownTick = 0; ShownImage = -1; }
};

//-----------------------------------------------------------------------------
// [SECTION] Design-phase node drafts (ImGuiAppFieldType, ImGuiAppFieldDesc, ImGuiAppNodeDraft)
//-----------------------------------------------------------------------------
// Reflection-driven node tooling (graph model + codegen + editor-UI decls; was imguiapp_nodes.h). Uses the
// applayer's reflection port + some STL kept out of imguiapp.h; public entry points stay imgui-shaped
// (pointer params, char[] buffers, ImGuiID, ImVector). Reflection subset: aggregates only; raw arrays count as one member.

// A draft describes a node whose backing C++ type does not exist yet; codegen emits a
// reflection-capable aggregate from it. Fields use the plain-scalar vocabulary codegen can emit.
enum ImGuiAppFieldType_
{
    ImGuiAppFieldType_Float = 0,
    ImGuiAppFieldType_Int,
    ImGuiAppFieldType_Bool,
    ImGuiAppFieldType_Double,
    ImGuiAppFieldType_Vec2,    // ImVec2
    ImGuiAppFieldType_Vec4,    // ImVec4
    ImGuiAppFieldType_String,  // char[ArraySize]
    ImGuiAppFieldType_Struct,  // a nested struct, named by StructType
    ImGuiAppFieldType_COUNT,
};

// Graph-model types are aggregates (default member initializers, no ctors): the build-time
// reflection walk (imguiapp.h) materializes their manifests for live mirrors and codegen.
struct ImGuiAppFieldDesc
{
    char              Name[IM_LABEL_SIZE]       = "";
    ImGuiAppFieldType Type                      = ImGuiAppFieldType_Float;
    int               ArraySize                 = 128; // buffer length for ImGuiAppFieldType_String; ignored otherwise
    char              StructType[IM_LABEL_SIZE] = "";  // referenced struct type name for ImGuiAppFieldType_Struct
};

// The control's four authorable virtual methods (F78.5). A hand-written body opts one method out of the
// modeled/stub codegen: the emitter emits the body verbatim, and the DLL preview compiles + runs it.
typedef int ImGuiAppControlMethod;   // -> enum ImGuiAppControlMethod_   // Enum: authorable control virtual method
enum ImGuiAppControlMethod_
{
    ImGuiAppControlMethod_OnInitialize = 0,
    ImGuiAppControlMethod_OnGetCommand,
    ImGuiAppControlMethod_OnUpdate,
    ImGuiAppControlMethod_OnDraw,
    ImGuiAppControlMethod_COUNT
};

// One drafted control: a name, its persisted and per-frame field sets, and optional hand-written method bodies.
struct ImGuiAppNodeDraft
{
    char                        Name[IM_LABEL_SIZE] = "NewControl";
    ImVector<ImGuiAppFieldDesc> PersistFields;
    ImVector<ImGuiAppFieldDesc> TempFields;
    // F78.5: optional custom C++ per method (indexed by ImGuiAppControlMethod_). Empty = modeled/stub codegen;
    // non-empty is emitted verbatim as that method's body (sees the generated params + Data/TempData fields).
    // Fixed buffers keep the draft trivially copyable inside node vectors. Interpreter reflects, not runs.
    char                        MethodBody[ImGuiAppControlMethod_COUNT][IMGUIAPP_CONTROL_BODY_MAX] = {};
};

//-----------------------------------------------------------------------------
// [SECTION] Typed graph kinds (node/layer/port/edge discriminators)
//-----------------------------------------------------------------------------

// ImGuiAppNodeKind (node discriminator) lives in imguiapp.h: the runtime stamps it on every
// pushed node (ImGuiAppNodeBase::Kind), so the public header owns the vocabulary.

// The five CORE layer classes are permanent, one each, immutable type -- codegen emits
// PushAppLayer<ImGuiAppXxxLayer>. Custom is a user-authored ImGuiAppLayer subclass: the NODE'S NAME is
// its class name, any number may exist, codegen emits the subclass skeleton plus PushAppLayer<Name>.
enum ImGuiAppLayerType_
{
    ImGuiAppLayerType_Task = 0,
    ImGuiAppLayerType_Command,
    ImGuiAppLayerType_Status,
    ImGuiAppLayerType_Layout,
    ImGuiAppLayerType_Display,
    ImGuiAppLayerType_Custom,
    ImGuiAppLayerType_COUNT,
};

// DataOut/DataIn carry the runtime data flow; ChildOut/ChildIn carry containment. Layers are root
// composition slots with no containment sockets. A Control has one DataOut (its PersistData), one
// multi-link DataIn (the runtime keys app->Data by PersistData TYPE), and one ChildOut.
enum ImGuiAppPortKind_
{
    ImGuiAppPortKind_DataOut = 0,
    ImGuiAppPortKind_DataIn,
    ImGuiAppPortKind_ChildOut,
    ImGuiAppPortKind_ChildIn,
    ImGuiAppPortKind_COUNT,
};

// Data dependency (producer -> consumer) or containment (child -> parent); derived from the linked
// ports' kinds at capture time.
enum ImGuiAppEdgeKind_
{
    ImGuiAppEdgeKind_Data = 0,
    ImGuiAppEdgeKind_Containment,
    ImGuiAppEdgeKind_COUNT,
};

//-----------------------------------------------------------------------------
// [SECTION] Graph topology and persistence (ImGuiAppNodeLink, link capture, save/load)
//-----------------------------------------------------------------------------

// A user-created edge between two node ports (source -> target). StartAttr/EndAttr are STABLE port ids
// (ImGuiAppNodePort::Id), never array-derived, so they survive node reorder/delete. Kind's default
// member initializer keeps { id, start, end } brace-init and the legacy save/load format working.
struct ImGuiAppNodeLink
{
    int              Id;
    int              StartAttr;
    int              EndAttr;
    ImGuiAppEdgeKind Kind = ImGuiAppEdgeKind_Data;
    bool             Soft = false; // Optional data dependency (live mirror): drawn dimmed; transient, never saved
};

//-----------------------------------------------------------------------------
// [SECTION] Typed node graph (ports, nodes, bindings, scope memory, view state, prefabs, undo)
//-----------------------------------------------------------------------------

// One pin on a node, stored (never index-derived) so its id is stable across reorder/delete.
struct ImGuiAppNodePort
{
    int              Id                  = 0; // from ImGuiAppGraph::NextId; == canvas pin id
    ImGuiAppPortKind Kind                = ImGuiAppPortKind_DataOut;
    char             Name[IM_LABEL_SIZE] = "";
    ImGuiID          DataTypeId          = 0; // ImGuiAppType<PersistData>::ID for DataOut/DataIn (the data-flow key); 0 otherwise
};

struct ImGuiAppCommandDesc
{
    char Name[IM_LABEL_SIZE] = "NewCommand";
};

// Op node inline operand (F55): the token an operand pin folds to when NOT wired to a producer -- an
// expression primary the AppEventExprCheck grammar accepts (field ref, literal, dep ref). Parallel to the
// operator's DataIn pins by index; a wired pin (a nested Op result) overrides its token, a missing entry is empty.
struct ImGuiAppOpOperand
{
    char Text[IM_LABEL_SIZE] = "";
};

// OnDraw records raw input into TempData (zeroed every frame); OnUpdate receives BOTH this frame's
// TempData and last frame's, deriving events by comparing them. An edge names which comparison the
// generated OnUpdate guards with.
enum ImGuiAppEventEdge_
{
    ImGuiAppEventEdge_Rising = 0,   // temp && !last   -- became true this frame
    ImGuiAppEventEdge_Falling,      // !temp && last   -- became false this frame
    ImGuiAppEventEdge_Changed,      // temp ^ last     -- either transition
    ImGuiAppEventEdge_Active,       // temp            -- level: every frame while true
    ImGuiAppEventEdge_COUNT,
};

// SetField mutates PersistData (OnUpdate is the sole mutator); EmitCommand latches a persist bool in
// OnUpdate that OnGetCommand emits.
enum ImGuiAppEventAction_
{
    ImGuiAppEventAction_SetField = 0,
    ImGuiAppEventAction_EmitCommand,
    ImGuiAppEventAction_COUNT,
};

// One authored event on a Control: "when <TempField> <edge> -> <action>". Codegen emits a guarded
// block in OnUpdate (plus, for EmitCommand, the latch + OnGetCommand emission).
struct ImGuiAppEventDesc
{
    char                TempField[IM_LABEL_SIZE] = ""; // watched TempData field
    ImGuiAppEventEdge   Edge                     = ImGuiAppEventEdge_Changed;
    ImGuiAppEventAction Action                   = ImGuiAppEventAction_SetField;
    char                DstField[IM_LABEL_SIZE]  = ""; // SetField: PersistData destination
    char                Expr[IM_LABEL_SIZE]      = ""; // SetField: source expression (emitted verbatim); empty -> temp_data-><TempField>
    char                Command[IM_LABEL_SIZE]   = ""; // EmitCommand: one of the control's selected commands
};

// One node in the authored graph. Embeds ImGuiAppNodeDraft so the rename/field-edit/codegen helpers
// apply verbatim and a legacy "[Draft]" maps 1:1 to a Control node. Most fields are kind-specific.
struct ImGuiAppNode
{
    int                            Id                          = 0;                      // from NextId; == canvas node id
    ImGuiAppNodeKind               Kind                        = ImGuiAppNodeKind_Control;
    ImGuiAppNodeDraft              Draft;                                                // Draft.Name is the node label; PersistFields/TempFields used by Control
    bool                           IsBuiltin                   = false;                  // true: backed by a compiled C++ type (palette), not drafted
    char                           TypeName[IM_LABEL_SIZE]     = "";                     // C++ type to Push<> (builtin window/sidebar/layer/control)
    char                           DataTypeName[IM_LABEL_SIZE] = "";                     // builtin control PersistData type name; empty => "<Name>Data"
    ImGuiAppLayerType              LayerType                   = ImGuiAppLayerType_Task; // Layer nodes only
    bool                           HasInitialPlacement         = false;                  // Window/Sidebar first-use placement
    ImVec2                         InitialPos                  = ImVec2(0.0f, 0.0f);
    ImVec2                         InitialSize                 = ImVec2(0.0f, 0.0f);
    ImGuiDir                       DockDir                     = ImGuiDir_Down;          // Sidebar dock direction; Layout node: split side (F57)
    float                          DockSize                    = 0.0f;                   // Sidebar size; Layout node: split fraction/size (F57)
    char                           RegionRef[IM_LABEL_SIZE]    = "";                     // Window/Sidebar: name of the Layout region node it docks into (F57); empty = none
    ImGuiWindowFlags               Flags                       = ImGuiWindowFlags_None;  // Window/Sidebar flags
    ImVec2                         GridPos                     = ImVec2(0.0f, 0.0f);     // persisted canvas position
    bool                           HasGridPos                  = false;
    bool                           _NeedsPlace                 = false;                  // apply GridPos to the canvas before the next submission
    int                            BodyAttrId                  = 0;                      // dedicated non-port static-attribute id for the node body
    bool                           IsLive                      = false;                  // mirrored from a running app object (read-only)
    bool                           IsPromoted                  = false;                  // design control whose emitted data type matches a live node (transient)
    ImGuiID                        LiveKey                     = 0;                      // stable upsert key for a live node (so its position survives re-mirroring)
    ImVector<ImGuiAppCommandDesc>  Commands;                                             // CommandLayer: definitions. Control: selected commands emitted by OnGetCommand.
    ImVector<ImGuiAppEventDesc>    Events;                                               // Control: authored temp-vs-last-temp events (see ImGuiAppEventDesc)
    ImVector<ImGuiAppStyleModDesc> StyleMods;                                            // Window/Sidebar/Control: authored style-var overrides (emitted into SetupApp)
    ImVector<ImGuiAppColorModDesc> ColorMods;                                            // Window/Sidebar/Control: authored style-color overrides (same lifecycle)
    ImVector<ImGuiAppNodePort>     Ports;
    ImVector<ImGuiAppOpOperand>    OpOperands;                                           // Op node: inline operand token per operand pin (by index); a wired pin overrides it (F55)
    int                            FieldList                   = 0;                      // Field node: which list it belongs to on its owner (0 = Persist, 1 = Temp)
    int                            PersistStructId             = -1;                     // Control: Struct node its PersistData was exploded into (-1 = inline)
    int                            TempStructId                = -1;                     // Control: Struct node its TempData was exploded into (-1 = inline)
    bool                           GroupCollapsed              = false;                  // descendants hidden behind the group title bar (transient, not serialized)
    bool                           Hidden                      = false;                  // not submitted to the canvas (transient, not serialized)
    ImVec2                         NoteSize                    = ImVec2(320.0f, 180.0f); // Note kind: annotation-frame footprint (model units)
    ImU32                          NoteColor                   = 0;                      // Note kind: frame tint (0 = default kind hue)
};

// Per-data-edge field assignment: emits one "data->Dst = dep->Src;" line in OnUpdate. Keyed by LinkId,
// kept off the link (an ImVector member would break ImGuiAppNodeLink's aggregate brace-init).
struct ImGuiAppFieldBinding
{
    int  LinkId                  = 0;
    char DstField[IM_LABEL_SIZE] = "";
    char SrcField[IM_LABEL_SIZE] = "";
};

// One user keymap override (F74): a chord (Key+Mods) rebound to an editor command Id. SPARSE diff -- only
// changed verbs appear; Key == ImGuiKey_None encodes an explicit unbind. Keyed by the stable command Id,
// never an array index, so reordering the registry never corrupts a saved keymap.
struct ImGuiAppKeyBinding
{
    int      CmdId = -1;
    ImGuiKey Key   = ImGuiKey_None;
    int      Mods  = 0;
};

// Per-branch camera memory: the pan/zoom a drill-down scope was LEFT with, keyed by the scope
// node's id (-1 = root) -- never by depth, so sibling branches keep independent cameras.
// Transient editor state carried by the graph (like ViewScope), not serialized.
struct ImGuiAppScopeCamera
{
    int    ScopeId = -1;
    ImVec2 Pan     = ImVec2(0.0f, 0.0f);
    float  Zoom    = 1.0f;
};

// Scope-local node placement: a member's position INSIDE a drilled scope, keyed by (scope node, member
// node). Each interior owns its own arrangement -- a move inside a group never moves the composition root
// (GridPos) and vice versa; first entry falls back to GridPos. Serialized (layout is model state).
struct ImGuiAppScopePlacement
{
    int    ScopeId = -1;
    int    NodeId  = -1;
    ImVec2 Pos     = ImVec2(0.0f, 0.0f);
};

// Scope-local member ORDER (F58): the authored push order of a scope's members, one record per scope (an
// order IS a sequence). ScopeId == -1 is the composition root. Serialized; AppScopeSequenceIds returns this
// order when a record exists. The core phase layers may never be reordered here (AppGraphValidate rejects).
struct ImGuiAppScopeOrder
{
    int           ScopeId = -1;
    ImVector<int> NodeIds;
};

// One cached trunk-route drawing primitive, in MODEL units (line-to / arc / cubic). The route is
// computed once from model inputs and only re-derived when those inputs move -- the camera never
// re-routes a settled link (transient, not serialized).
struct ImGuiAppTrunkSeg
{
    int    Kind = 0;                        // 0 = line-to P0; 1 = arc (P0 center, R, A0..A1); 2 = cubic (P0,P1 cps, P2 end)
    ImVec2 P0   = ImVec2(0.0f, 0.0f);
    ImVec2 P1   = ImVec2(0.0f, 0.0f);
    ImVec2 P2   = ImVec2(0.0f, 0.0f);
    float  R    = 0.0f;
    float  A0   = 0.0f;
    float  A1   = 0.0f;
};

struct ImGuiAppTrunkRoute
{
    int                       OwnerId = 0;
    ImVec2                    StartM  = ImVec2(0.0f, 0.0f);   // model endpoints + obstacle key: recompute triggers
    ImVec2                    EndM    = ImVec2(0.0f, 0.0f);
    ImVec4                    KeyA    = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    ImVec4                    KeyB    = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    ImVector<ImGuiAppTrunkSeg> Segs;
};

// One cluster's original position captured at layer-drag start, so groups pushed by a dragged
// edge stay STUCK to it and return with it within the same drag (transient, not serialized).
struct ImGuiAppDragStick
{
    int   NodeId = 0;
    float OrigY  = 0.0f;
};

// One group frame as PUBLISHED by the frames pass, in model units -- the single producer of
// group geometry. Consumers (drag clamping) read last frame's publication: a coherent T-1 pair
// (docs/bug-classes.md rule 1). Transient, not serialized.
struct ImGuiAppGroupFrame
{
    int    OwnerId = 0;
    ImVec2 MinM = ImVec2(0.0f, 0.0f);
    ImVec2 MaxM = ImVec2(0.0f, 0.0f);
};

// Canvas view settings: presentation-only, never model state. Not serialized by Save/Load; the
// host may persist them across sessions.
struct ImGuiAppGraphViewState
{
    bool  SnapGrid;
    bool  OvGrid;
    bool  OvBands;
    bool  OvFrames;
    bool  OvMinimap;
    bool  TreeOpen;  // outliner sidebar shown; the host keeps its width across a collapse
    bool  InspOpen;  // inspector sidebar shown; the host keeps its width across a collapse
    float Zoom; // canvas zoom, wheel-driven, [0.3, 2.5]; authored positions stay zoom-independent
};

// Undo history: serialized-graph snapshots, linear with a cursor at the live state; pushing after
// an undo truncates the redo tail. Snapshot/label strings are owned heap allocations.
// A saved subtree: serialized nodes + links, instantiable into any graph.
struct ImGuiAppPrefab
{
    char  Name[64];
    char* Data;   // serialized subtree, owned
};

struct ImGuiAppEditorUndo
{
    ImVector<char*>             Snaps;           // owned snapshot strings, oldest..newest
    ImVector<char*>             Labels;          // owned operation names, parallel to Snaps (derived by diffing)
    int                         Cursor = -1;     // index of the snapshot matching the live graph (-1 = empty)
    const struct ImGuiAppGraph* Owner = nullptr; // identity guard
    ImGuiID                     LiveHash = 0;    // hash of Snaps[Cursor], for cheap change detection
};

//-----------------------------------------------------------------------------
// [SECTION] Editor + composer value types (registry, issues, chrome/theme, spans)
//-----------------------------------------------------------------------------

// Editor command registry (F34): one table is the single source for the editor's verbs. Each declares
// the surfaces it appears on (bitmask); the Space palette renders from it, and the four-roads
// completeness test iterates it to check every verb is reachable from every surface it declares.
typedef int ImGuiAppCmdSurface;   // -> enum ImGuiAppCmdSurface_   // Flags: command availability surfaces
enum ImGuiAppCmdSurface_
{
    ImGuiAppCmdSurface_None     = 0,
    ImGuiAppCmdSurface_Palette  = 1 << 0,   // the Space operator palette
    ImGuiAppCmdSurface_Menu     = 1 << 1,   // a right-click context menu
    ImGuiAppCmdSurface_Shortcut = 1 << 2,   // a keyboard shortcut
    ImGuiAppCmdSurface_Gizmo    = 1 << 3,   // an on-canvas gizmo/overlay button
};

typedef int ImGuiAppCommandSource;   // -> enum ImGuiAppCommandSource_   // Enum: who owns a palette entry
enum ImGuiAppCommandSource_
{
    ImGuiAppCommandSource_Editor = 0, // built-in verb (s_editor_commands; dispatched by the palette run() switch)
    ImGuiAppCommandSource_Host,       // host verb (AppGraphSetHostCommands each frame, pointers outliving it; a pick comes back through AppGraphConsumeHostCommand, one-frame latency)
};

// One palette entry, editor-built-in or host-registered (Source discriminates).
struct ImGuiAppEditorCommand
{
    int                   Id       = 0;                          // dispatch id (editor: the run() switch; host: returned by AppGraphConsumeHostCommand)
    const char*           Icon     = "";                         // FontAwesome glyph, or "" if none
    const char*           Label    = "";                         // "Edit: Delete selection"
    const char*           Shortcut = "";                         // display string, e.g. "Ctrl+Z" or ""
    ImGuiKey              Key      = ImGuiKey_None;              // shortcut key
    int                   Mods     = 0;                          // ImGuiMod_* for the shortcut
    ImGuiAppCmdSurface    Surfaces = ImGuiAppCmdSurface_Palette; // availability bitmask
    ImGuiAppNodeKind      AddKind  = ImGuiAppNodeKind_COUNT;     // add verbs carry their kind
    ImGuiAppCommandSource Source   = ImGuiAppCommandSource_Editor;
};

// One problem found by AppGraphValidate. Severity: 1 = warning, 2 = error. NodeId is the node to
// reveal (-1 for whole-graph issues).
struct ImGuiAppGraphIssue
{
    int  NodeId;
    int  Severity;
    char Text[256];

    ImGuiAppGraphIssue() { NodeId = -1; Severity = 1; Text[0] = 0; }
};

// Brushing across coordinated views: any panel reports the datum under the mouse; every panel reads
// LAST frame's report and highlights it (one-frame latency). The source lets a view skip echoing its
// own hover. Single-editor-instance state.

enum ImGuiAppHoverSource_
{
    ImGuiAppHoverSource_None = 0,
    ImGuiAppHoverSource_Canvas,      // the node editor
    ImGuiAppHoverSource_Tree,        // the outliner
    ImGuiAppHoverSource_Inspector,   // inspector / binding rows
    ImGuiAppHoverSource_External,    // problems list, code panel, any host-app panel
};

// Composer chrome palette, derived from the current ImGuiStyle theme (AppComposerStyleFromTheme):
// neutrals ride the WindowBg -> Text axis, semantic hues are pulled toward Text for light-theme
// legibility. Fields are final packed IM_COL32 values; overwrite after derivation to customize.
struct ImGuiAppComposerStyle
{
    // Node kind accents
    ImU32 KindLayer;
    ImU32 KindWindow;
    ImU32 KindSidebar;
    ImU32 KindControl;
    ImU32 KindStruct;
    ImU32 KindField;
    ImU32 KindOp;
    ImU32 KindDefault;
    // Layer-type accents
    ImU32 LayerTask;
    ImU32 LayerCommand;  // also marks hidden nodes
    ImU32 LayerStatus;
    ImU32 LayerLayout;
    ImU32 LayerDisplay;
    ImU32 AccentNeutral; // fallback accent for typeless rows/ports
                         // Pins
    ImU32 PinData;
    ImU32 PinChild;
    ImU32 PinTie;
    ImU32 PinDefault;
    // Diagnostics
    ImU32 SevError;
    ImU32 SevWarn;
    ImU32 ErrorText;
    ImU32 Danger;
    // Live mirror / promotion
    ImU32 OriginLive;
    ImU32 OriginPromoted;
    ImU32 DotLive;
    ImU32 DotPromoted;
    ImU32 DotDrift;
    // Overlay accents
    ImU32 Gold;
    // Field chrome (flat draw-list widgets)
    ImU32 FieldBg;
    ImU32 FieldBgHovered;
    ImU32 FieldBgEdit;
    ImU32 FieldBorder;
    ImU32 FieldText;
    ImU32 TextMuted;    // idle glyphs (disclosure chevrons)
    ImU32 TextOnAccent; // near-black text/numerals over accent fills
    ImU32 DarkOutline;  // near-black rings over accent fills
                        // Group boxes + numbered rail
    ImU32 GroupFill;
    ImU32 GroupOutline;
    ImU32 GroupTitleBg; // opaque: grid must not bleed through text
    ImU32 RailLine;
};

// Composer chrome scalar idiom -- one table for the motion (F38) and type/space ladders (F39). Not
// theme-derived; the single source so no overlay hard-codes an alpha, and no chrome text hard-codes
// an off-ladder size. Type tiers are RATIOS of the body font size; spacing steps in SpaceQuantum em.
struct ImGuiAppComposerMotion
{
    // Motion + quietness (F38)
    float OverlayRest    = 0.55f;    // an idle canvas overlay sits quiet
    float OverlayHover   = 1.00f;    // ... brightens to full when the pointer rests on it
    float OverlayGesture = 0.20f;    // ... and recedes during a wire-drag / marquee (get out of the way)
    float FadeMs         = 150.0f;   // single linear alpha fade between those states (and for transient chrome)
    // Typography ladder (F39): chrome text tiers, strictly descending
    float TypeBody       = 1.00f;    // primary chrome text
    float TypeSecondary  = 0.90f;    // secondary tier (chip labels, sub-rows)
    float TypeCaption    = 0.80f;    // smallest tier (dense readouts, code gutter, scope strip)
    // Spacing quantum (F39): chrome gaps step in this em fraction
    float SpaceQuantum   = 0.25f;
};

// The composer chrome's push-stack palette, exposed read-write (stable pointer): the project
// inspector's Theme section edits it live. Col slots are semantic and fixed; Value/Active are the
// editable half. Seeded from AppComposerGetStyle.
struct ImGuiAppChromeTheme
{
    ImGuiAppColorModDesc Combo[8]; // dropdown fields (enum combos, struct picker): field + popup + rows
    ImGuiAppColorModDesc Edit[4];  // in-place editors (InputText/InputInt): transparent frame over the drawn bg
};

// Scope interior: a data edge crossing the current drill-down boundary docks on the wall as a
// portal chip (inbound producers left, outbound consumers right). Derived per frame from
// Links + ViewScope -- no ids, no persistence, no codegen, no validation.
struct ImGuiAppScopePortal
{
    int  LinkId;        // the crossing data edge
    int  InsidePortId;  // the in-scope endpoint's port (the chip wires to this pin)
    int  OutsideNodeId; // the off-scope node the chip names; click jumps to its scope
    bool Inbound;       // true: outside producer -> inside consumer (left wall); false: right wall

    ImGuiAppScopePortal() { LinkId = -1; InsidePortId = -1; OutsideNodeId = -1; Inbound = true; }
};

// One node's contribution to the last whole-graph emission: [LineBegin, LineEnd) in the generated
// text. A node may own several spans (type definitions AND its bring-up line in SetupApp).
struct ImGuiAppCodeSpan
{
    int NodeId;
    int LineBegin;
    int LineEnd;

    ImGuiAppCodeSpan() { NodeId = -1; LineBegin = 0; LineEnd = 0; }
};

//-----------------------------------------------------------------------------
// [SECTION] ImGuiAppEditorState, ImGuiAppGraph (the composer document + session)
//-----------------------------------------------------------------------------

// Editor session state, one per graph (the document): the editor's cross-frame values ride the
// model object they describe, like Selection/ViewScope/ScopeCams. All transient, not serialized.
struct ImGuiAppEditorState
{
    ImGuiAppCanvasState*              Canvas = nullptr;                   // this graph's canvas engine (created on first editor frame; freed with the process)
    bool                           DragWasDetach = false;
    ImVec2                         DropGrid = ImVec2(0.0f, 0.0f);      // pin-drag drop point (model units), consumed by the palette pick that follows
    int                            HoverNode = -1;                     // brushing bus: render-phase reports (TempData), read next frame
    int                            HoverLink = -1;
    int                            HoverNodeSrc = 0;
    int                            HoverLinkSrc = 0;
    int                            HoverPrevNode = -1;                 // ... and last frame's, what readers see
    int                            HoverPrevLink = -1;
    int                            HoverPrevNodeSrc = 0;
    int                            HoverPrevLinkSrc = 0;
    int                            HoverFrame = -1;
    ImGuiAppGraphViewState         View = { false, true, true, true, true, true, true, 1.0f };  // snap off; overlays + sidebars on; 1:1
    ImVector<ImGuiAppGraphIssue>   IssuesCache;
    ImGuiID                        IssuesSig = 0;
    bool                           IssuesValid = false;
    ImGuiStorage                   IssuesSeverity;                     // node id -> worst severity
    ImVector<int>                  PoolIds;                            // node ids the canvas holds
    ImVector<int>                  PrevPoolIds;
    char                           StatusHint[256] = "";
    int                            StatusSev = 0;
    float                          OverlayAlpha = 0.55f;               // F38: animated overlay opacity (rest->hover->gesture, faded per motion table)
    ImVec2                         GizmoRectMin = ImVec2(0.0f, 0.0f);  // F38: last-drawn gizmo cluster rect (screen), drives the hover ladder + test
    ImVec2                         GizmoRectMax = ImVec2(0.0f, 0.0f);
    ImVec2                         EditorRectMin = ImVec2(0.0f, 0.0f); // F38: last editor canvas rect (screen), for gesture detection + test
    ImVec2                         EditorRectMax = ImVec2(0.0f, 0.0f);
    ImVec2                         GizmoCenters[8] = {};               // F40: viewport gizmo centres (screen), in draw order, for the click-path test
    int                            GizmoCount = 0;
    const ImGuiAppEditorCommand*   HostCmds = nullptr;                 // registered per frame; host-owned (Source_Host entries)
    int                            HostCmdCount = 0;
    int                            HostCmdPicked = -1;
    bool                           AddPaletteRequest = false;          // one-shot
    bool                           CmdPaletteRequest = false;          // one-shot: open the Space operator palette (F34)
    int                            KeymapCapture = -1;                 // F75: command id whose chord the keymap editor is capturing, or -1
    bool                           AlignMenuRequest = false;           // one-shot: open the Shift+A align/distribute submenu (F48/R3)
    bool                           FitAllRequest = false;              // one-shot
    int                            AutoLayoutCountdown = 2;            // launch default is a TIDIED layout: fires once real sizes exist (frame 2)
    float                          UniformCardW = 0.0f;                // one normalized width for every non-layer node (model units; grows to fit the widest need, deadbanded)
    bool                           HelpOverlay = false;                // F1 shortcut cheat sheet
    bool                           QuickInspector = false;             // N: floating quick inspector
    bool                           QuickInspectorPin = false;          // F41: pinned -> stays on QuickInspectorNode instead of following the selection
    int                            QuickInspectorNode = -1;            // F41: the pinned node (valid only while QuickInspectorPin)
    bool                           PrevShowLive = true;
    bool                           TitleEditing = false;               // one node renames at a time
    ImVec2                         AddPopupGrid = ImVec2(0.0f, 0.0f);
    int                            CtxNodeId = -1;
    int                            CtxLinkId = -1;
    int                            FitAllCountdown = 0;
    ImGuiTextFilter                AddFilter;                          // add-palette search
    ImGuiTextFilter                CmdFilter;                          // command-palette search
    int                            ErrSeqSeen = 0;
    double                         ErrTime = -1000.0;
    int                            DropSrcAttr = -1;
    int                            AppliedSel = -1;
    int                            OutlinerRename = -1;                // node id being renamed in the tree, -1 = none
    bool                           OutlinerRenameFocus = false;
    bool                           OutlinerKindVis[ImGuiAppNodeKind_COUNT] = { true, true, true, true, true, true, true, true, true, true, true };
    IM_STATIC_ASSERT(ImGuiAppNodeKind_COUNT == 11); // OutlinerKindVis initializer is hand-counted: extend it with the enum (shorter form zero-fills the tail invisible)
    ImGuiTextFilter                OutlinerFilter;
    bool                           OutputShowErr = true;               // Output panel severity filters
    bool                           OutputShowWarn = true;
    bool                           OutputShowInfo = true;
    ImGuiTextFilter                OutputFilter;                       // Output panel text filter
    ImVector<ImGuiAppStyleModDesc> StyleClipMods;                      // style-section clipboard (session-lived, value-typed)
    ImVector<ImGuiAppColorModDesc> StyleClipCols;
    bool                           StyleClipHas = false;
    char*                          ClipText = nullptr;                 // serialized partial graph, or null (owned)
    int                            ClipPaste = 0;                      // cascade counter so repeated pastes fan out
    ImVector<ImGuiAppPrefab>       Prefabs;                            // saved subtrees (owned strings)
    ImFont*                        CodeFont = nullptr;                 // code panels; null -> UI font
    ImGuiAppEditorUndo             Undo;
    ImVec4                         ScopeWallRect = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);  // scope-interior walls incl. face/end bands, model units (min.xy, max.xy); published by the walls pass, consumed same-frame by the strip + portal passes
    ImVec4                         ScopeStripRow = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);  // the face band's order-strip row, model units
    bool                           ScopeWallValid = false;             // rects above valid this frame (window/sidebar scope only)
    int                            ScopeWallScope = -1;                // scope the wall rect belongs to (hysteresis resets on change)
    ImVector<ImVec4>               ScopeStripRects;                    // order-strip chip rects, screen space (published per frame for hit-tests/tests)
    ImVector<int>                  ScopeStripNodes;                    // node id per chip, parallel to ScopeStripRects
    ImVec4                         ScopeVoid[4] = {};                  // figure-ground carve: the four dim bands outside the walls, screen space (top/bottom/left/right); published per frame for tests
    bool                           ScopeVoidValid = false;             // ScopeVoid valid this frame (drawn with the walls)
    int                            TelegraphPin = -1;                  // F50: pin hovered as a wire-drag target this frame (-1 = none)
    bool                           TelegraphOk = false;                // F50: would the hovered target connect legally to the drag source
    int                            StripDragNode = -1;                 // F60: order-strip chip being drag-reordered (node id; -1 = idle). Latched pre-submission, hit-tested against last frame's ScopeStripRects
    ImGuiAppPreview*               Preview = nullptr;                  // F68: live preview interpreter session for this document (heap; opaque; freed with the process / rebuilt on Reinit)
    bool                           PreviewRun = true;                  // F68: run vs pause the previewed model (widgets stay interactive while paused)
    ImGuiID                        PreviewSig = 0;                     // F68: AppGraphSignature the preview was last reconciled at; a change reconciles (preserve-by-field) next frame
    ImGuiAppMetaRecorder*          PreviewRec = nullptr;               // F70: active preview-session take (meta-only run container), or null
    ImGuiAppInputLog               PreviewRecInput;                    // F70: opt-in replay layer recorded alongside this take (AppInputRecord per driven frame)
    ImU64                          PreviewRecTick = 0;                 // F70: ticks recorded this take (== the exported run's tick spine)
    int                            PreviewRecSnapEvery = 30;           // F70: StateSnapshot cadence in ticks (a nearest-snapshot restore point every N)
    char                           PreviewRecPath[256] = "headless-artifacts/preview-session.meta";  // F70: exported container path
    bool                           PreviewUseDll = false;              // F78.5: preview backend selector -- DLL (compiled real program) vs the interpreter (default)
    ImGuiAppPreviewDll*            PreviewDll = nullptr;               // F78.5: compiled-DLL preview session (heap; opaque; created lazily, freed on Reinit / process exit)
    ImGuiID                        PreviewDllSig = 0;                  // F78.5: AppGraphSignature the DLL preview was last compiled at; a change recompiles + hot-swaps
    ImTextureData*                 PreviewDllTex = nullptr;            // F78.5: host texture holding the CPU-rasterized DLL frame (created per panel size)
    int                            PreviewDllTexW = 0;                 // F78.5: PreviewDllTex width
    int                            PreviewDllTexH = 0;                 // F78.5: PreviewDllTex height
    ImVector<unsigned char>        PreviewDllRgba;                     // F78.5: reused RGBA32 scratch the DLL frame rasterizes into
    char                           PreviewDllErr[192] = "";            // F78.5: last DLL compile/load diagnostic (shown in the panel; empty = ok)
};

// The whole authored graph. One monotonic id allocator shared by every node/port/body-attr/link:
// ids are globally unique, never reused.
struct ImGuiAppGraph
{
    ImVector<ImGuiAppNode>         Nodes;
    ImVector<ImGuiAppNodeLink>     Links;
    ImVector<ImGuiAppFieldBinding> Bindings;
    ImVector<ImGuiAppKeyBinding>   Keymap;                               // F74: user input->command overrides (serialized; sparse diff from the registry-default chords)
    ImVector<int>                  Selection;                            // multi-selection (node ids); the single selected_node_id is primary
    ImVector<int>                  ViewScope;                            // drill-down scope stack (node ids, outer->inner); empty = whole app; transient, not serialized
    ImVector<ImGuiAppScopeCamera>  ScopeCams;                            // per-branch camera memory (transient, not serialized)
    ImVector<ImGuiAppScopePlacement> ScopePlacements;                    // scope-local member positions (serialized; root layout stays in GridPos)
    ImVector<ImGuiAppScopeOrder>   ScopeOrders;                          // per-scope authored member order (F58; serialized; empty = derive the sequence)
    ImVector<ImGuiAppTrunkRoute>   _TrunkRoutes;                         // cached trunk routes, model units (transient, not serialized)
    ImVector<ImGuiAppDragStick>    _DragStick;                           // cluster originals for the active layer drag (transient)
    int                            _DragStickAnchor           = 0;       // layer node the sticks belong to (0 = no active capture)
    ImVector<ImVec4>               _GroupDragOrig;                       // (id, x, y) member originals for the active group drag (transient)
    ImVec2                         _GroupDragMouse0           = ImVec2(0.0f, 0.0f); // model-space mouse origin of that drag (transient)
    ImVec4                         _GroupDragFrame0           = ImVec4(0.0f, 0.0f, 0.0f, 0.0f); // the dragged group's frame at drag start, model (transient)
    ImVec2                         _GroupDragApplied          = ImVec2(0.0f, 0.0f); // displacement actually granted so far (greedy catch-up; transient)
    bool                           _GroupDragMoved            = false;   // gesture latch: drag vs fold-click (transient)
    int                            _GroupDragPending          = -1;      // owner whose settled group-drag write defers to the post-CanvasEnd update pass; -1 = none (transient)
    bool                           _LayerBoxValid             = false;   // this frame's layer-column box, the deferred group-drag clamp's solid obstacle, MODEL units (transient)
    ImVec2                         _LayerBoxMin               = ImVec2(0.0f, 0.0f);
    ImVec2                         _LayerBoxMax               = ImVec2(0.0f, 0.0f);
    ImVector<ImGuiAppGroupFrame>   _GroupFrames;                         // THIS frame's published group frames, model units (transient)
    ImVector<ImGuiAppGroupFrame>   _GroupFramesPrev;                     // last frame's publication, what consumers read (transient)
    mutable ImGuiAppEditorState*   _Ed                        = nullptr; // editor session state; opaque to reflection/serialization (created on first editor use, freed with the process)
    float                          _LayerUniformW             = 0.0f;    // uniform layer-column content width, model units; 0 = floor (transient)
    int                            _LayerDragId               = 0;       // layer node under an active vertical drag, 0 = none (transient)
    float                          _LayerDragMouseY0          = 0.0f;    // screen-y mouse origin of that drag (transient)
    float                          _LayerDragNodeY0           = 0.0f;    // dragged layer's model y at grab (transient)
    float                          _LayerDragMaxY             = 0.0f;    // clamp edges captured at grab, model units (transient)
    float                          _LayerDragMinY             = 0.0f;
    int                            NextId                     = 1;
    int                            EditingNodeId              = -1;      // node whose title is being renamed inline, or -1
    char                           LastLinkErr[IM_LABEL_SIZE] = "";      // last editor notice (refused link/composition), a full sentence; transient, NOT in Save/Load
    int                            LastLinkErrSeq             = 0;       // bumped on every notice
    ImGuiApp*                      LiveApp                    = nullptr; // running app this graph mirrors (set by BuildAppLiveGraph, read-only to
                                                                           // codegen); null = no live source; transient, NOT in Save/Load
    int                            _ScopeSig                  = -1;      // editor scope-change detector (transient; -1 = first frame)
    int                            _ScopeCamId                = -1;      // scope id the camera currently shows (-1 = root; transient)
    int                            _PendingFit                = 0;       // deferred fit-all countdown after a scope change (transient)
    int                            Revision                   = 0;       // monotonic authored-content version; AppGraphSyncRevision bumps it when the signature changes (transient, session-local)
    ImGuiID                        _SigCache                  = 0;       // last signature seen by AppGraphSyncRevision (revision bookkeeping; transient)
    ImGuiID                        GenSignature               = 0;       // AppGraphSignature captured at the last codegen; 0 = never generated this session. Code is STALE when the live signature differs (transient, NOT serialized)
};

//-----------------------------------------------------------------------------
// [SECTION] Embeddable Composer control (generated-shell bootstrap)
//-----------------------------------------------------------------------------
// Composer packaged as a window-hosted ImGuiAppControl: owns the graph it edits, drives
// ShowAppGraphEditor inside its host window. Host = the running app (live mirror); null = design-only.
struct ImGuiAppComposerControlData
{
    ImGuiApp*     Host     = nullptr;
    ImGuiAppGraph Graph;
    int           Selected = -1;
};

struct ImGuiAppComposerControl : ImGuiAppControl<ImGuiAppComposerControlData, ImGuiAppNoTempData>
{
    virtual void OnInitialize(ImGuiApp* app, ImGuiAppComposerControlData* data) const override;
    virtual void OnDraw(const ImGuiAppComposerControlData* data, ImGuiAppNoTempData* temp_data) const override;
};

// Interpreter core (a second graph backend beside codegen; API in the core namespace below) has no
// header-visible types beyond the opaque ImGuiAppPreview forward-declared at the top.

//-----------------------------------------------------------------------------
// [SECTION] Animation builtins (was imguiapp_anim.h)
//-----------------------------------------------------------------------------
// The dt-driven Task-phase animators; each a compiled ImGuiAppTask node (N4), accumulator in
// PersistData (so snapshot/replay reproduces it). Registered via AppGraphAddBuiltin.

// Ease selector for ImAppTween (no typedef pair: ImAppEase names the curve function below).
enum ImAppEase_
{
    ImAppEase_Linear = 0,
    ImAppEase_Smoothstep,
};

// Interpolation curve on t in [0,1].
static inline float ImAppEase(int ease, float t)
{
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return ease == ImAppEase_Smoothstep ? t * t * (3.0f - 2.0f * t) : t;
}

//-----------------------------------------------------------------------------
// Tween: eases a->b over duration seconds; restarts on a rising trigger.
//-----------------------------------------------------------------------------
struct ImAppTweenData
{
    float A;         // start value (param)
    float B;         // end value (param)
    float Duration;  // seconds (param)
    int   Ease;      // ImAppEase_ (param)
    float T;         // accumulator [0,1]
    float Value;     // DataOut: eased A->B at T
    bool  Done;      // DataOut: T reached 1
};
struct ImAppTweenTempData
{
    bool Trigger;    // rising edge restarts
};
struct ImAppTween : ImGuiAppTask<ImAppTweenData, ImAppTweenTempData>
{
    virtual void OnInitialize(ImGuiApp* app, ImAppTweenData* data) const override final;
    virtual void OnUpdate(float dt, ImAppTweenData* data, const ImAppTweenTempData* temp_data, const ImAppTweenTempData* last_temp_data) const override final;
};

//-----------------------------------------------------------------------------
// Timer: counts elapsed seconds; done latches at duration; restarts on a rising trigger.
//-----------------------------------------------------------------------------
struct ImAppTimerData
{
    float Duration;  // seconds (param)
    float Elapsed;   // accumulator
    bool  Done;      // DataOut: Elapsed >= Duration
};
struct ImAppTimerTempData
{
    bool Trigger;    // rising edge restarts
};
struct ImAppTimer : ImGuiAppTask<ImAppTimerData, ImAppTimerTempData>
{
    virtual void OnInitialize(ImGuiApp* app, ImAppTimerData* data) const override final;
    virtual void OnUpdate(float dt, ImAppTimerData* data, const ImAppTimerTempData* temp_data, const ImAppTimerTempData* last_temp_data) const override final;
};

//-----------------------------------------------------------------------------
// Spring: damped-harmonic integration toward target; {value,velocity} is the accumulator.
//-----------------------------------------------------------------------------
struct ImAppSpringData
{
    float Target;     // param
    float Stiffness;  // param (k)
    float Damping;    // param (c)
    float Value;      // DataOut
    float Velocity;   // accumulator
};
struct ImAppSpringTempData
{
};
struct ImAppSpring : ImGuiAppTask<ImAppSpringData, ImAppSpringTempData>
{
    virtual void OnInitialize(ImGuiApp* app, ImAppSpringData* data) const override final;
    virtual void OnUpdate(float dt, ImAppSpringData* data, const ImAppSpringTempData* temp_data, const ImAppSpringTempData* last_temp_data) const override final;
};

//-----------------------------------------------------------------------------
// Pulse: free-running phase in [0,1); pulse is a one-frame flag on each wrap.
//-----------------------------------------------------------------------------
struct ImAppPulseData
{
    float Period;   // seconds per cycle (param)
    float Phase;    // accumulator [0,1)
    bool  Pulse;    // DataOut: one-frame wrap flag
};
struct ImAppPulseTempData
{
};
struct ImAppPulse : ImGuiAppTask<ImAppPulseData, ImAppPulseTempData>
{
    virtual void OnInitialize(ImGuiApp* app, ImAppPulseData* data) const override final;
    virtual void OnUpdate(float dt, ImAppPulseData* data, const ImAppPulseTempData* temp_data, const ImAppPulseTempData* last_temp_data) const override final;
};

//-----------------------------------------------------------------------------
// [SECTION] Headless test harness (was imguiapp_testharness.h)
//-----------------------------------------------------------------------------
// ImGuiAppTestHarness: Test Engine + headless render + frame encode + WAL in one entry point. Its
// IMPL (imguiapp.cpp) is the only applayer code that drags the imgui test engine (IMGUI_ENABLE_TEST_ENGINE).
struct ImGuiAppTestHarnessConfig
{
    const char*          Name;
    const char*          ArtifactDir;
    ImGuiAppHeadlessMode Headless;
    bool                 RecordVideo;
    bool                 KeepArtifactsOnPass;
    ImGuiAppPacerMode    PacerMode;
    float                Fps;
    ImGuiAppAVTimingMode Timing;
    ImGuiAppAVEncoder*   Encoder;
    ImGuiAppWALLevel     WALLevel;
    const char*          TestFilter;
    void (*RegisterTests)(ImGuiTestEngine* engine);
    bool                 VerifyRecording;
    ImGuiAppHeadlessMode* EffectiveHeadless;

    ImGuiAppTestHarnessConfig()
    {
        Name = nullptr; ArtifactDir = nullptr; Headless = ImGuiAppHeadlessMode_Offscreen; RecordVideo = true;
        KeepArtifactsOnPass = false; PacerMode = ImGuiAppPacerMode_Fixed; Fps = 60.0f; Timing = ImGuiAppAVTimingMode_Auto;
        Encoder = nullptr; WALLevel = ImGuiAppWALLevel_Frame; TestFilter = nullptr; RegisterTests = nullptr;
        VerifyRecording = true; EffectiveHeadless = nullptr;
    }
};

//-----------------------------------------------------------------------------
// [SECTION] Process state root (GImGuiAppState)
//-----------------------------------------------------------------------------

// Assert-time forensics state, read from the assert handler (ImAppAssertFail) -- a context-free
// callback with no `this`, so it lives on the process-state root below.
struct ImGuiAppAssertState
{
    ImGuiAppWAL*                WAL = nullptr;         // sink for the assert line; null = stderr only
    ImVector<ImGuiAppRecorder*> RingRecorders;        // armed flight-recorder rings to dump on assert (F15)
};

// Per-viewport present deadlines (secondary platform windows; main viewport never skips).
struct ImGuiAppViewportPace
{
    ImGuiID ViewportId;
    double  NextDeadline;
    ImU64   LastSeenFrame; // FrameID.FrameIndex; feeds lazy pruning of vanished viewports
};

// Pacer bookkeeping the ImGuiAppPacer struct doesn't carry. Single slot: one paced app
// per process; the deadline chain re-anchors when a different app starts pacing.
struct ImGuiAppPacerState
{
    const ImGuiApp* App          = nullptr;
    double          NextDeadline = -1.0; // on the funcs' monotonic clock; < 0 = chain not started
    double          LastEnter    = -1.0; // previous AppPacerWait entry (feeds LastFrameMs)
    ImVector<ImGuiAppViewportPace> ViewportPace; // secondary-window present deadlines
};

// THE process-wide state root (the applayer's GImGui analog): every formerly ad-hoc mutable
// static lives here as a member. Anchored by one constant-initialized pointer + lazy IM_NEW,
// so cross-TU static initializers (control ctors registering type schemas) stay ordered-safe.
struct ImGuiAppProcessState
{
    ImGuiAppAssertState                 Assert;
    ImGuiAppPacerState                  Pacer;
    ImVector<const ImGuiAppTypeSchema*> TypeSchemas;
    const ImGuiAppThreadFuncs*          ThreadFuncs         = nullptr; // null = resolve the default at use
    const ImGuiAppFileSystemFuncs*      FileSystemFuncs     = nullptr; // null = resolve the default at use
    double                              RunEpoch            = 0.0;     // 0 = unset (first stamped frame sets it)
    ImU64                               QpcHz               = 0;       // 0 = unset (win32 QPC frequency)
    bool                                AssertSymReady      = false;   // win32 sym handler initialized
    int                                 MediaFoundationRefs = 0;       // MFStartup/COM process refcount (mf backend)
    ImGuiApp*                           DemoHost            = nullptr; // ShowAppDemo's current host
    ImGuiApp*                           DemoFallbackApp     = nullptr; // demo-owned app for host-less callers
    ImGuiApp*                           DemoComposed        = nullptr; // the one composed app per process
    ImGuiAppComposerStyle               ComposerStyle;                  // derived theme cache, gated by ComposerStyleVersion
    int                                 ComposerStyleVersion = 0;       // 0 = not yet derived; bumped by each global re-derive
    ImGuiAppComposerMotion              ComposerMotion;                 // F38 motion table (struct defaults)
    ImGuiAppChromeTheme                 ChromeTheme;                    // inspector-editable; reseeded when ComposerStyleVersion moves
    int                                 ChromeThemeSeededVersion = -1;
    char                                PreviewVcvars[1024] = { 1 };    // vcvars64.bat path; [0]==1 => not yet searched, empty = no toolset
};

extern IMGUI_API ImGuiAppProcessState* GImGuiAppState;   // defined in imguiapp.cpp; constant-initialized: safe before main

inline ImGuiAppProcessState& AppState()
{
    if (GImGuiAppState == nullptr)
        GImGuiAppState = IM_NEW(ImGuiAppProcessState)();
    return *GImGuiAppState;
}

inline ImGuiAppAssertState& AppAssert() { return AppState().Assert; }

// Push-count receipt for a paired style pop (the item render loop and the Blender-field widgets):
// the returned counts pop exactly what was pushed, so stacks stay balanced even if an entry's
// Active toggles mid-frame.
struct ImGuiAppStyleScope
{
    int Vars   = 0;
    int Colors = 0;
};

//-----------------------------------------------------------------------------
// [SECTION] ImGui applayer internal API (core)
//-----------------------------------------------------------------------------

// Best-effort symbolized backtrace of the caller as "  #N name (file:line)" lines; returns characters
// written. skip_frames drops innermost frames (0 = caller). Win32 only; other platforms write "".
IMGUI_API int ImAppStackTrace(char* out, int out_size, int skip_frames = 0);

// IM_ASSERT sink (wired via IMGUI_USER_CONFIG): logs expr/file/line + ImAppStackTrace to the SetAppAssertWAL
// sink and stderr, flushes, then __debugbreak()s under a debugger or exits with code 3 -- never the CRT popup.
IMGUI_API void ImAppAssertFail(const char* expr, const char* file, int line);

// fprintf analog over the ImFile* seam (formats, then ImFileWrite); returns bytes written.
IMGUI_API int ImFilePrintf(ImFileHandle file, const char* fmt, ...) IM_FMTARGS(2);

namespace ImGui
{
// Active filesystem backend: the SetAppFileSystemFuncs table, else the libc + std::filesystem
// default (asserts if IMGUIAPP_DISABLE_DEFAULT_FILESYSTEM_FUNCS stripped it and none was set).
IMGUI_API const ImGuiAppFileSystemFuncs* AppFileSystemFuncs();
} // namespace ImGui

namespace ImGui
{
// Nodes sorted by the resolved dependency wiring: every producer before its consumers, composition
// order among independents; rebuilt on composition change. ONLY UpdateApp's data-node pass iterates
// this -- command collection and rendering stay composition order.
IMGUI_API const ImVector<ImGuiAppNodeBase*>* AppRebuildUpdateOrder(ImGuiApp* app);

// Shut down + free every node in `nodes` (OnShutdown, unregister its storage, delete). Internal:
// called only by the Pop* composition functions (imguiapp.cpp).
IMGUI_API void ShutdownAppNodes(ImGuiApp* app, ImVector<ImGuiAppDisplayNodeBase*>& nodes);
IMGUI_API void ShutdownAppNodes(ImGuiApp* app, ImVector<ImGuiAppNodeBase*>& nodes);

// AV: encoder teardown + image codec + meta-stream verify/parse
// Close (if open) then Destroy any provider's encoder via its vtable. Null-safe.
IMGUI_API void AppAVDestroyEncoder(ImGuiAppAVEncoder* encoder);

// The single generic image-codec entry on the ImGuiAppAV interface (QOI-backed): encode one RGBA8
// frame to compressed bytes / decode it back. The frame-sequence encoder providers AND the
// flight-recorder ring both go through this -- one codec interface, no separate raw API.
IMGUI_API bool AppAVImageEncode(const void* rgba, int width, int height, int pitch_bytes, ImVector<char>* out);
IMGUI_API bool AppAVImageDecode(const void* bytes, int size, ImVector<char>* out_rgba, int* out_width, int* out_height);

// Recompute the integrity ladder over a reconstructed meta stream: tick contiguity
// (Frame records), io completeness (IoFrames == Frames), hash chain from the
// Identity seed, end-of-stream digest. True only when every criterion holds.
IMGUI_API bool AppAVMetaVerify(const void* meta, int meta_size, ImGuiAppAVStreamStats* out_stats);

// Memory-stream parsers over a reconstructed meta stream (40-byte header + framed records). Extraction is
// per-backend: ImGuiApp_ImplLibav_ExtractEmbeddedMeta (mp4), ImGuiApp_ImplQoi_ExtractEmbeddedMeta (frame
// sequence). A truncated tail parses as end-of-stream.
IMGUI_API bool AppAVMetaDump(const void* meta, int meta_size);   // TSV to stdout (debug helper)

// Reproduction: restore the snapshot, then AppInputReplay (imguiapp.h) -- its
// divergence check pinpoints any non-determinism.
IMGUI_API bool AppAVMetaReadInputLog(const void* meta, int meta_size, ImGuiAppInputLog* out_log);
IMGUI_API bool AppAVMetaReadStateSnapshot(const void* meta, int meta_size, ImVector<char>* out_bytes, ImGuiID* out_composition_id);


// Run artifacts (F62): file loader + tick index + state-at-tick
// Build the tick index (playback-debugger-design section 3) from a reconstructed meta buffer -- ONE linear
// walk reusing AppAVMetaVerify's TLV traversal; the path->buffer step is the per-backend extractor, this
// core seam never names a provider. Returns a heap index (AppRunClose frees) or null on a bad/absent header.
IMGUI_API ImGuiAppRunIndex*      AppRunOpen(const void* meta, int meta_size);
IMGUI_API void                   AppRunClose(ImGuiAppRunIndex* run);
IMGUI_API int                    AppRunTickCount(const ImGuiAppRunIndex* run);          // == Ticks.Size; 0 on null
IMGUI_API const ImGuiAppRunTick* AppRunTickAt(const ImGuiAppRunIndex* run, int i);      // null when out of range

// F64 state-at-tick (playback-debugger-design section 5): restore the nearest snapshot <= tick_index into
// recon_app, then AppInputReplay forward to it. On success recon_app's storage holds the app AT that tick;
// false (out->Reconstructed false) when the identity gate fails or no snapshot/input reaches N. out may be null.
IMGUI_API bool                   AppRunStateAtTick(ImGuiApp* recon_app, const ImGuiAppRunIndex* run, int tick_index, ImGuiAppRunState* out);

// Correlate the sibling <name>.wal's command dispatches to ticks: parse "[tick:N] ... execute command %d"
// lines, fill run->Commands tick-sorted, set each tick's WalFirst/WalCount slice. Optional -- the recording
// is authoritative; a missing WAL just clears the annotation. False on a null run or unreadable path.
IMGUI_API bool                   AppRunAttachWal(ImGuiAppRunIndex* run, const char* wal_path);


// Transport source (F63)
// FILE-mode transport view over a run index (playback-debugger-design section 4). Count == AppRunTickCount;
// Show(view, i) sets ShownTick = Ticks[i].Tick and decodes its FrameImage into view->Pixels via view->Decode.
// False only on an invalid index or hard decode failure; a ring-dump gap or null Decode still lands the tick.
IMGUI_API int               AppRunViewCount(const ImGuiAppRunView* view);
IMGUI_API bool              AppRunViewShow(ImGuiAppRunView* view, int i);

// encoder is REQUIRED (providers live in backends/imguiapp_impl_*.h; the core seam
// cannot pick one). Fails (null) when the platform backend has no CaptureFrame.
IMGUI_API ImGuiAppRecorder* AppRecordBegin(ImGuiApp* app, ImGuiAppAVEncoder* encoder, const ImGuiAppAVEncodeConfig* config);

// Per-frame pump: capture -> queue for encode -> meta stream chunk. Call once per frame
// between render and present (the double-buffered CaptureFrame makes the exact
// position forgiving). Null-safe; no-op when inactive.
IMGUI_API void              AppRecordPump(ImGuiAppRecorder* rec);
IMGUI_API bool              AppRecordIsActive(const ImGuiAppRecorder* rec);
IMGUI_API ImU64             AppRecordFrameCount(const ImGuiAppRecorder* rec);   // frames accepted this take (the video frame ordinal); 0 on null
IMGUI_API void              AppRecordSetQueuePolicy(ImGuiAppRecorder* rec, ImGuiAppRecordQueuePolicy policy, int depth /*= 3*/);
IMGUI_API void              AppRecordEnd(ImGuiAppRecorder* rec);   // flush queue, Close provider, free rec

// This frame's user blob (copied immediately; lands in the meta stream Frame record).
IMGUI_API void              AppRecordSetFrameData(ImGuiAppRecorder* rec, const void* data, int size);

// Serialize the app's snapshottable state (ImGuiAppStateHistory byte layout) as this
// frame's blob: video frame N <-> app state N; restoring the bytes IS time travel.
IMGUI_API void              AppRecordSnapshotState(ImGuiAppRecorder* rec, ImGuiApp* app);

// OPT-IN derived checkpoint layer: raw io records by default (IoFrame); attaching a live input log
// ADDITIONALLY serializes its TempData frames (InputHdr/InputFrame), enabling render-free replay +
// per-control divergence attribution. Cost O(sum of TempData) per frame -- attach deliberately.
IMGUI_API void              AppRecordAttachInputLog(ImGuiAppRecorder* rec, const ImGuiAppInputLog* log);

IMGUI_API ImGuiAppRecorder* AppRecordBeginRing(ImGuiApp* app, ImGuiAppAVEncoder* encoder, const ImGuiAppAVEncodeConfig* config, const ImGuiAppRingConfig* ring);

// Encode the ring's contents to disk NOW (assert hook, test failure, hotkey); reason lands in the WAL and
// a stream marker record. The ring keeps recording; repeated dumps get "-2", "-3" suffixes. When a ring
// recorder exists, the IM_ASSERT sink (ImAppAssertFail) dumps it with the failed expression as reason.
IMGUI_API bool              AppRecordDumpRing(ImGuiAppRecorder* rec, const char* reason);

// Assert forensics (F15): dump every live ring recorder (each auto-registered by AppRecordBeginRing)
// with `reason`. ImAppAssertFail calls this before exit so the flight recording lands beside the
// assert WAL. Returns the number of rings successfully dumped.
IMGUI_API int               AppDumpAssertRings(const char* reason);


// Meta-only recorder (F70): a preview session records without video (previewer-design section 10). The
// bytes are exactly what the video recorder embeds (same AvBuild* writers) minus the pixel pipeline;
// AppRunOpen + AppRunStateAtTick read the result unchanged. All state rides the caller's recorder object.
IMGUI_API ImGuiAppMetaRecorder* AppMetaRecordBegin(ImGuiApp* app, float fps, int embed_rows);

// Attach a caller-owned input log (same opt-in semantics as AppRecordAttachInputLog): with it,
// each tick ALSO serializes InputHdr/InputFrame so AppRunStateAtTick can replay non-snapshot
// ticks. Keep calling AppInputRecord once per driven frame as usual.
IMGUI_API void                  AppMetaRecordAttachInputLog(ImGuiAppMetaRecorder* mr, const ImGuiAppInputLog* log);

// Record one tick: a Frame + IoFrame (state hash + chain), this tick's InputFrame when an input
// log is attached, and a StateSnapshot when snapshot is true. Call once per driven frame AFTER
// UpdateApp/RenderApp/EndFrame (and AppInputRecord if attaching input). tick == FrameID.FrameIndex.
IMGUI_API void                  AppMetaRecordTick(ImGuiAppMetaRecorder* mr, ImU64 tick, double time_sec, bool snapshot);

// Finalize: append the Digest (completeness proof over every preceding byte), hand the full meta
// buffer to out_meta (opens directly via AppRunOpen), and free the recorder. Null-safe.
IMGUI_API void                  AppMetaRecordEnd(ImGuiAppMetaRecorder* mr, ImVector<char>* out_meta);

// Reflection field UI: read-only render + type-dispatched editor for one reflected field; dispatches on the
// ImGuiAppIsCharArray / ImGuiAppIsFormattable traits, enumerates members via ImGui::VisitAppFields (imguiapp_reflect.h).
// Read-only render of one reflected field value.
template <typename T>
inline void DrawAppField(const char* label, const T* value)
{
    IM_ASSERT(value != nullptr);

    if constexpr (ImGuiAppIsCharArray<T>)
    {
        Text("%s: %s", label, *value);
    }
    else if constexpr (ImGuiAppIsFormattable<T>)
    {
        char buf[IM_LABEL_SIZE];
        std::string s = std::format("{}", *value);
        ImFormatString(buf, IM_ARRAYSIZE(buf), "%s", s.c_str());
        Text("%s: %s", label, buf);
    }
    else
    {
        std::string_view tn = type_name(*value);
        Text("%s: <%.*s>", label, (int)tn.size(), tn.data());
    }
}

// Editable widget for one reflected field value. Returns true if the value changed.
// Falls back to a read-only render for types with no editor.
template <typename T>
inline bool EditAppField(const char* label, T* value)
{
    IM_ASSERT(value != nullptr);

    if constexpr (ImGuiAppIsCharArray<T>)
        return InputText(label, *value, std::extent_v<T>);
    else if constexpr (std::is_same_v<T, bool>)
        return Checkbox(label, value);
    else if constexpr (std::is_same_v<T, float>)
        return DragFloat(label, value);
    else if constexpr (std::is_same_v<T, double>)
        return InputDouble(label, value);
    else if constexpr (std::is_same_v<T, ImVec2>)
        return DragFloat2(label, &value->x);
    else if constexpr (std::is_same_v<T, ImVec4>)
        return ColorEdit4(label, &value->x);
    else if constexpr (std::is_integral_v<T>)
    {
        int v = (int)*value;
        bool changed = DragInt(label, &v);
        if (changed)
            *value = (T)v;
        return changed;
    }
    else
    {
        DrawAppField(label, value);
        return false;
    }
}

// Node rendering (canvas)
// Canvas-engine node scaffold (see imguiapp_canvas.h): titled node between CanvasBegin/End.
IMGUI_API void        AppNodeBegin(ImGuiAppCanvasState* c, int id, const char* title);
IMGUI_API void        EndAppNode(ImGuiAppCanvasState* c);

// Renamable node scaffold: the title bar shows *name, turns into an inline text box when clicked.
// Commits on Enter or focus loss, cancels on Escape. *editing_node_id is caller-owned (-1 = none);
// set on title click, cleared when the edit ends. Pair with EndAppNode().
IMGUI_API void        AppNodeBeginRenamable(ImGuiAppCanvasState* c, int id, char* name, int name_size, int* editing_node_id);

// Render every reflected field of an aggregate as read-only labelled rows in the current node.
template <typename T>
inline void DrawAppNodeFields(const T* data)
{
    IM_ASSERT(data != nullptr);

    VisitAppFields(data, [](int idx, std::string_view name, const auto& value)
                   {
      IM_UNUSED(idx);
      char label[IM_LABEL_SIZE];
      ImFormatString(label, IM_ARRAYSIZE(label), "%.*s", (int)name.size(), name.data());
      DrawAppField(label, &value);
    });
}

// Editable reflected field rows inside the current node. Returns true if any value changed.
template <typename T>
inline bool EditAppNodeFields(T* data)
{
    IM_ASSERT(data != nullptr);

    bool changed = false;
    VisitAppFields(data, [&changed](int idx, std::string_view name, auto& value)
                   {
      IM_UNUSED(idx);
      char label[IM_LABEL_SIZE];
      ImFormatString(label, IM_ARRAYSIZE(label), "%.*s", (int)name.size(), name.data());
      SetNextItemWidth(GetFontSize() * 6.0f);
      changed |= EditAppField(label, &value);
    });
    return changed;
}

// C++ base type spelling for a field type.
IMGUI_API const char* AppFieldTypeName(ImGuiAppFieldType type);

// Draft field-list mutators.
IMGUI_API void        AppNodeDraftAddField(ImVector<ImGuiAppFieldDesc>* fields, const char* name, ImGuiAppFieldType type);
IMGUI_API void        AppNodeDraftRemoveField(ImVector<ImGuiAppFieldDesc>* fields, int index);


// Design-phase node drafts
// Inspector UI: rename the draft and add/remove/edit its persist and temp fields.
IMGUI_API void AppNodeDraftEdit(ImGuiAppNodeDraft* draft);

// Persist/temp field editors without the Name input, for hosts that edit the name elsewhere.
IMGUI_API void AppNodeDraftFieldsEdit(ImGuiAppNodeDraft* draft);

// Render a draft's fields as read-only node rows (no reflection: the type does not exist yet).
IMGUI_API void DrawAppNodeDraft(const ImGuiAppNodeDraft* draft);

// Draw the model's links as wires on the canvas (between CanvasBegin/End).
IMGUI_API void DrawAppNodeLinks(ImGuiAppCanvasState* c, const ImVector<ImGuiAppNodeLink>* links);

// After CanvasEnd: fold the canvas's wire events into the link model. New links take ids from
// *next_link_id (incremented). Returns true if the model changed.
IMGUI_API bool CaptureAppNodeLinks(ImGuiAppCanvasState* c, ImVector<ImGuiAppNodeLink>* links, int* next_link_id);

// Persist / restore a draft and its links as imgui-style text. Return false on file error.
IMGUI_API bool SaveAppNodeGraph(const char* path, const ImGuiAppNodeDraft* draft, const ImVector<ImGuiAppNodeLink>* links);
IMGUI_API bool LoadAppNodeGraph(const char* path, ImGuiAppNodeDraft* draft, ImVector<ImGuiAppNodeLink>* links);

// Multi-draft variants, same text format (one "[Draft]" section per draft): a single-draft file
// loads as a one-element vector. *drafts is cleared before loading.
IMGUI_API bool SaveAppNodeGraphMulti(const char* path, const ImVector<ImGuiAppNodeDraft>* drafts, const ImVector<ImGuiAppNodeLink>* links);
IMGUI_API bool LoadAppNodeGraphMulti(const char* path, ImVector<ImGuiAppNodeDraft>* drafts, ImVector<ImGuiAppNodeLink>* links);

// Typed graph model: ids, nodes, links, validate, signature
// AddNode/AddBuiltin stamp the kind's mandatory ports and a body-attr id; the returned pointer is
// valid only until the next node is added (Nodes may reallocate). Find* resolve by search, never by
// index. RemoveNode also sweeps incident links and orphaned bindings.
IMGUI_API int               AppGraphAllocId(ImGuiAppGraph* g);
IMGUI_API ImGuiAppNode*     AppGraphAddNode(ImGuiAppGraph* g, ImGuiAppNodeKind kind, const char* name);
IMGUI_API ImGuiAppNode*     AppGraphAddBuiltin(ImGuiAppGraph* g, ImGuiAppNodeKind kind, const char* type_name, const char* data_type_name);
// Add a logic Op node (F54). op_token is one of the AppEventExprCheck-checkable operators (AND/OR/XOR/NOT,
// ==/!=/</<=/>/>=, select, min/max); null or unknown => the default (AND). TypeName carries the operator and
// fixes the operand-pin arity; the result DataOut is stamped DataTypeId=0 (opts out of one-producer-per-type).
IMGUI_API ImGuiAppNode*     AppGraphAddOp(ImGuiAppGraph* g, const char* op_token);
IMGUI_API void              AppGraphRemoveNode(ImGuiAppGraph* g, int node_id);
IMGUI_API const ImGuiAppNode* AppGraphFindNode(const ImGuiAppGraph* g, int node_id);
IMGUI_API int                 AppGraphFindNodeIndex(const ImGuiAppGraph* g, int node_id); // write path: index into g->Nodes
IMGUI_API const ImGuiAppNodePort* AppGraphFindPort(const ImGuiAppGraph* g, int port_id, const ImGuiAppNode** out_owner);
IMGUI_API bool              AppGraphHasLayerType(const ImGuiAppGraph* g, ImGuiAppLayerType type);
IMGUI_API void              AppNodeAddCommand(ImGuiAppNode* n, const char* name);
IMGUI_API void              AppNodeRemoveCommand(ImGuiAppNode* n, int index);

// Runtime data-flow key for a node named <node_name>: ConstantHash of the sanitized "<Name>Data"
// codegen emits == ImGuiAppType<<Name>Data>::ID, so a design DataOut port shares the live storage key.
IMGUI_API ImGuiID           AppNodeStructTypeId(const char* node_name);

// Stable fold of the codegen-determining authored (!IsLive) state: changes iff the emitted C++ would
// change. Excludes positions/ids/live-mirror churn. char[] hashed as NUL-terminated string (ctors
// zero only byte 0, so ImHashData over the fixed buffer would be unstable).
IMGUI_API ImGuiID           AppGraphSignature(const ImGuiAppGraph* g);

// What kinds compose into a drilled scope (F28): the single truth behind the interior palette and the
// empty-scope wall caption. A live non-layer scope (window/sidebar/control/struct mirror) takes nothing
// -- read-only -- so this returns false for every kind; the authored twin admits its members.
IMGUI_API bool              AppScopeIsKindComposable(const ImGuiAppGraph* g, int scope_id, ImGuiAppNodeKind kind);

// Origin vocabulary (F26): the one colour shared by the canvas title-bar dot, outliner row tint and demo
// legend. Live and Promoted each get a distinct mark; plain design returns 0 (no push -> default row
// colour). Single source, so the three surfaces cannot drift.
IMGUI_API ImU32             AppGraphOriginColor(const ImGuiAppNode* n);

// Codegen freshness (F17), the signature as single source of truth: AppGraphSyncRevision folds it once per
// frame and bumps Revision on content change; AppGraphMarkGenerated stamps it at codegen; STALE while the
// live signature differs from the stamp. GenSignature/Revision are session-local: a loaded graph reads never-generated.
IMGUI_API int               AppGraphSyncRevision(ImGuiAppGraph* g);
IMGUI_API void              AppGraphMarkGenerated(ImGuiAppGraph* g);
IMGUI_API bool              AppGraphIsCodeStale(const ImGuiAppGraph* g);
IMGUI_API bool              AppGraphIsCodeFresh(const ImGuiAppGraph* g);

// Count the codegen self-diagnostics embedded in generated text (F19): the "// WARNING" comments and the
// "// codegen aborted" banner. Scans the emitted C++ itself (single source: the emitter), never re-deriving
// the conditions. out_list (optional) receives each marker line trimmed of leading indent, one per line.
IMGUI_API int               AppScanCodegenWarnings(const char* code, ImGuiTextBuffer* out_list);

// CanLink validates an attempted edge (kind pairing, no self/dup, no duplicate dep type, no cycle),
// writing a reason to err on rejection. CaptureAppGraphLinks folds the canvas wire events, refusing
// illegal creations; returns true if the model changed.
IMGUI_API bool              AppGraphCanLink(ImGuiAppGraph* g, int start_port, int end_port, char* err, int err_size);
IMGUI_API bool              CaptureAppGraphLinks(ImGuiAppGraph* g, char* err, int err_size);

// Per-edge field-binding editor for one data link (call inside an attribute).
IMGUI_API void              EditAppDataEdgeBindings(ImGuiAppGraph* g, int link_id);


// Graph editor UI: canvas, inspector, tree, keymap
// Render the whole typed graph inside the current window. app may be null (design-only); non-null lets
// builtin bodies reflect live data. *selected_node_id is caller-owned (-1 = none), reconciled both ways
// (canvas<->tree), dangling ids cleared. show_live hides (never deletes) live-mirror nodes when false.
IMGUI_API void                                ShowAppGraphEditor(ImGuiApp* app, ImGuiAppGraph* g, int* selected_node_id, bool show_live);


// Code generation
// Emit an ImGuiAppControl from a draft: <Name>Data, <Name>TempData, and the control skeleton with
// stubbed overrides. A String field emits char[N], which falls outside the reflection subset.
// Appends to *out.
IMGUI_API void AppControlCodeGenerate(const ImGuiAppNodeDraft* draft, ImGuiTextBuffer* out);

#ifndef IMGUI_DISABLE_DEBUG_TOOLS
// Debug introspection (Metrics-style TreeNode dumps).
IMGUI_API void                                DebugNodeAppGraph(const ImGuiAppGraph* g, const char* label);
IMGUI_API void                                DebugNodeAppNode(const ImGuiAppNode* n);
IMGUI_API void                                DebugNodeCanvas(const ImGuiAppCanvasState* c, const char* label);
#endif // #ifndef IMGUI_DISABLE_DEBUG_TOOLS

IMGUI_API int                                 AppGraphEditorCommandCount();
IMGUI_API const ImGuiAppEditorCommand*        AppGraphEditorCommandAt(int index);
IMGUI_API bool                                AppGraphIsEditorCommandAvailable(const ImGuiAppGraph* g, const ImGuiAppEditorCommand* c);

// Remappable input->command binding (F74). The registry Key/Mods are the factory DEFAULT chord; the graph's
// Keymap holds sparse user overrides. Dispatch resolves a pressed chord through the effective map and runs
// it on the palette's path. Delete/Tab/Esc keep dedicated handlers; Space / Ctrl+P (palette openers) reserved.
IMGUI_API void                                AppKeymapDefaultChord(int cmd_id, ImGuiKey* out_key, int* out_mods);
IMGUI_API void                                AppKeymapEffectiveChord(const ImGuiAppGraph* g, int cmd_id, ImGuiKey* out_key, int* out_mods);
IMGUI_API bool                                AppKeymapIsCommandRebindable(int cmd_id);
IMGUI_API bool                                AppKeymapIsChordReserved(ImGuiKey key, int mods);
IMGUI_API bool                                AppKeymapRebind(ImGuiAppGraph* g, int cmd_id, ImGuiKey key, int mods);   // false if not rebindable or the chord is reserved
IMGUI_API void                                AppKeymapReset(ImGuiAppGraph* g, int cmd_id);                            // drop the override (back to the default)
IMGUI_API void                                AppKeymapResetAll(ImGuiAppGraph* g);
IMGUI_API int                                 AppKeymapConflict(const ImGuiAppGraph* g, ImGuiKey key, int mods, int except_cmd_id);   // colliding verb id, or -1
IMGUI_API int                                 AppKeymapResolveChord(const ImGuiAppGraph* g, ImGuiKey key, int mods);   // command id bound to this chord, or -1
IMGUI_API void                                AppGraphShowKeymapEditor(ImGuiAppGraph* g);                              // F75: rebind UI (call inside your own window)

// Render a design Control's effective Persist/Temp fields as a mock panel of real ImGui widgets
// (values are scratch), or -- for a LIVE node with live_app -- the running control's reflected
// members with their current values. node_id < 0 -> hint.
IMGUI_API void                                AppGraphDrawMockPanel(ImGuiAppGraph* g, int node_id, ImGuiApp* live_app = nullptr);

// Inspector for one node's authored data. Live nodes are read-only. node_id < 0 -> hint. Edits
// mutate the graph in place.
IMGUI_API void                                EditAppNodeInspector(ImGuiAppGraph* g, int node_id);
IMGUI_API void                                EditAppNodeInspectorEx(ImGuiAppGraph* g, int node_id, ImGuiApp* live_app);   // + live style write-back (see workbench §3.5)
IMGUI_API void                                EditAppNodesInspectorMulti(ImGuiAppGraph* g);   // multi-selection: intersection editing (style across all selected)

// Inspector section header (workbench 5.1): collapse triangle + icon + label, optional enable checkbox,
// optional kebab answered by the caller's own popup; open state is session-lived per window. persist_seed != 0
// keys the open state by that seed (e.g. node kind) so a collapse persists across same-kind nodes (F41).
IMGUI_API bool                                AppInspectorSection(const char* str_id, const char* icon, const char* label, bool* enabled, bool* kebab_clicked, ImGuiID persist_seed = 0);

// Origin breadcrumb for a selected node: "sel: MainWindow > Mixer [design]" / "[live]" /
// "[promoted]" / "sel: -" when id < 0 or unknown.
IMGUI_API void                                AppGraphSelectionBreadcrumb(const ImGuiAppGraph* g, int node_id, char* buf, int buf_size);

// Mirror the running app's controls into *g: remove all prior live nodes, add one read-only live
// Control node per pushed control (keyed by GetDataID) plus their data edges, and flag design
// controls whose emitted data type matches a live node. Design nodes untouched. Safe every frame.
IMGUI_API void                                BuildAppLiveGraph(const ImGuiApp* app, ImGuiAppGraph* g);

// Outliner panel (call inside your own child/window): the running app's composition plus the graph's
// authored nodes. Clicking a graph row selects that node in the editor. *selected_node_id is
// caller-owned (-1 = none). show_live false hides (never deletes) live-mirror rows.
IMGUI_API void                                ShowAppGraphTree(const ImGuiApp* app, ImGuiAppGraph* g, int* selected_node_id, bool show_live = true);

// Topologically order the Control nodes by data dependency (producers before consumers); false + err on a
// cycle; out_control_ids receives node ids in push order. include_live false = authored domain only; true =
// the full mirrored composition (codegen). priority (F59): optional push-order preference among ready controls; null = plain sort.
IMGUI_API bool                                AppGraphTopoOrder(const ImGuiAppGraph* g, ImVector<int>* out_control_ids, char* err, int err_size, bool include_live = false, ImVector<int>* out_cycle = nullptr, const ImVector<int>* priority = nullptr);

// Data-dependency (topo) cycle surfacing (F21). Fills out_nodes with the controls the topo sort could
// not schedule -- the cycle plus anything it blocks -- and out_name (optional) with a member's name.
// Returns the count (0 when acyclic). The Select verb jumps the selection to out_nodes.
IMGUI_API int                                 AppGraphDependencyCycle(const ImGuiAppGraph* g, ImVector<int>* out_nodes, char* out_name, int name_size);


// Static-analyze the design graph, appending problems to *out (clears nothing). Live-mirror nodes
// are skipped.
IMGUI_API void                                AppGraphValidate(const ImGuiAppGraph* g, ImVector<ImGuiAppGraphIssue>* out);

// Context keymap hint + transient refused-link errors, computed by ShowAppGraphEditor every frame.
// Render it OUTSIDE the canvas (host status bar). severity: 0 = plain hint, 2 = error (refused
// link). Valid for the frame after the editor ran.
IMGUI_API const char*                         AppGraphStatusHint(const ImGuiAppGraph* g, int* out_severity);

IMGUI_API void                                AppGraphHoverNode(const ImGuiAppGraph* g, int node_id, ImGuiAppHoverSource source);
IMGUI_API void                                AppGraphHoverLink(const ImGuiAppGraph* g, int link_id, ImGuiAppHoverSource source);
IMGUI_API int                                 AppGraphHoveredNode(const ImGuiAppGraph* g, ImGuiAppHoverSource* out_source);   // -1 = none; out_source may be null
IMGUI_API int                                 AppGraphHoveredLink(const ImGuiAppGraph* g, ImGuiAppHoverSource* out_source);

IMGUI_API void                                AppGraphSetHostCommands(const ImGuiAppGraph* g, const ImGuiAppEditorCommand* cmds, int count);
IMGUI_API int                                 AppGraphConsumeHostCommand(const ImGuiAppGraph* g);   // picked host cmd id since last call, or -1

// One-shot: open the add-node palette at the canvas center on the editor's next submission (same
// palette as RMB / Space / the + gizmo).
IMGUI_API void                                AppGraphRequestAddPalette(const ImGuiAppGraph* g);
IMGUI_API void                                AppGraphRequestCmdPalette(const ImGuiAppGraph* g);   // open the Space operator palette (F34)

// One-shot: frame the whole graph on the editor's next submission.
IMGUI_API void                                AppGraphRequestFitAll(const ImGuiAppGraph* g);

IMGUI_API ImGuiAppComposerStyle*              AppComposerGetStyle();
// Recompute all composer colors from the current ImGuiStyle theme (requires a live context).
// Runs lazily on first AppComposerGetStyle; call again after switching themes. Re-deriving the
// global style also reseeds AppGraphChromeTheme, discarding live inspector edits.
IMGUI_API void                                AppComposerStyleFromTheme(ImGuiAppComposerStyle* style);

IMGUI_API ImGuiAppComposerMotion*             AppComposerGetMotion();

// F38 read-backs: the animated overlay alpha this frame, and the last-drawn gizmo cluster / editor
// canvas rects (screen). Exposed so the motion ladder is on-camera verifiable.
IMGUI_API float                               AppGraphEditorOverlayAlpha(const ImGuiAppGraph* g);
IMGUI_API void                                AppGraphEditorGizmoRect(const ImGuiAppGraph* g, ImVec2* out_min, ImVec2* out_max);
IMGUI_API void                                AppGraphEditorCanvasRect(const ImGuiAppGraph* g, ImVec2* out_min, ImVec2* out_max);

// F40: viewport gizmo centres (screen), in draw order (0 Add, 1 Frame, 2 Fit, 3 Tidy, 4 Snap,
// 5 Overlays, 6 View-scope). Draw-list buttons carry no id, so the click-path test targets these.
IMGUI_API int                                 AppGraphEditorGizmoCount(const ImGuiAppGraph* g);
IMGUI_API ImVec2                              AppGraphEditorGizmoCenter(const ImGuiAppGraph* g, int index);

// F41: open the quick inspector pinned to a node ("Inspect here"); read back the pinned node (-1 when
// unpinned or the quick inspector is closed).
IMGUI_API void                                AppGraphInspectHere(const ImGuiAppGraph* g, int node_id);
IMGUI_API int                                 AppGraphEditorQuickInspectNode(const ImGuiAppGraph* g);

IMGUI_API ImGuiAppChromeTheme*                AppGraphChromeTheme();

// Canvas view settings live on the graph (`_Ed.View`); stable pointer for host persistence.
IMGUI_API ImGuiAppEditorState*                AppGraphEditorState(const ImGuiAppGraph* g);   // created on first use
IMGUI_API ImGuiAppGraphViewState*             AppGraphViewState(ImGuiAppGraph* g);

// The graph's canvas-engine state (see imguiapp_canvas.h): hosts and tests can query geometry
// or drive the camera. Created on the graph's first editor frame.
IMGUI_API ::ImGuiAppCanvasState*                 AppGraphEditorCanvas(const ImGuiAppGraph* g);

IMGUI_API void                                AppScopeCollectPortals(const ImGuiAppGraph* g, ImVector<ImGuiAppScopePortal>* out);

// One-line owner readout for the scope wall title bar: a window's placement
// ("320x240 @ (64,48) . AlwaysAutoResize"), a sidebar's dock ("dock Down . auto . AutoResize").
// Empty for kinds without placement/dock config.
IMGUI_API void                                AppNodeConfigSummary(const ImGuiAppNode* n, char* buf, int buf_size);

// Cached validation, keyed by AppGraphSignature (+ bindings): cheap enough to query every frame.
// Worst per-node severity: 0 = clean, 1 = warning, 2 = error.
IMGUI_API const ImVector<ImGuiAppGraphIssue>* AppGraphIssuesCached(const ImGuiAppGraph* g);
IMGUI_API int                                 AppGraphNodeSeverity(const ImGuiAppGraph* g, int node_id);

// Type-check one authored event's Expr against the control's effective field lists. Grammar: field refs,
// nested '.' members, bool/int/float literals, parens, scalar operators at C precedence; the result must
// fit DstField; empty Expr is valid (codegen copies the watched temp field). True when well-typed, else err.
IMGUI_API bool                                AppEventExprCheck(const ImGuiAppGraph* g, const ImGuiAppNode* n, const ImGuiAppEventDesc* ev, char* err, int err_size);


// Whole-graph codegen: data structs + controls with derived DataDependencies (topo order) + one
// bring-up function pushing layers, then windows/sidebars, then controls. Appends to *out. The Ex
// variant also records the per-node source map (out_spans may be null; cleared first).
IMGUI_API void                                AppGraphCodeGenerate(const ImGuiAppGraph* g, ImGuiTextBuffer* out);
IMGUI_API void                                GenerateAppGraphCodeEx(const ImGuiAppGraph* g, ImGuiTextBuffer* out, ImVector<ImGuiAppCodeSpan>* out_spans);

// Generated-shell bootstrap: the composition body from AppGraphCodeGenerate (the SINGLE emitter) wrapped in
// the host scaffold a standalone program needs -- a concrete ImGuiApp composing via the emitted SetupApp on
// its first initialized frame, plus main(). A Composer-hosting composition emits a shell that runs the Composer.
IMGUI_API void                                AppShellCodeGenerate(const ImGuiAppGraph* g, ImGuiTextBuffer* out);

// Interpreter (F66/F67): module codegen + session
// DLL preview module (F78): the shell composition body + host scaffold, but with a C-ABI entry point
// (extern "C" ImGuiAppPreview_Create/_Destroy/_ABI) instead of main(); the module hands the host a composed
// ImGuiApp as a framework base pointer. IMGUIAPP_PREVIEW_ABI is baked into both sides; a load-time mismatch is refused.
IMGUI_API void             AppPreviewModuleCodeGenerate(const ImGuiAppGraph* g, ImGuiTextBuffer* out);

// Per-node codegen: emits only the code one node produces -- a Control's struct(s) with derived
// deps, the CommandLayer's AppCommand enum + dispatch, a bring-up line, or (App node) the whole
// composition. Appends to *out.
IMGUI_API void             AppNodeCodeGenerate(const ImGuiAppGraph* g, const ImGuiAppNode* n, ImGuiTextBuffer* out, ImGuiApp* live_app = nullptr);   // live control + live_app -> reflected real structs

// Persist / restore the whole graph as imgui-style text. AppGraphLoad also ingests the legacy
// single/multi "[Draft]" format (each becomes a Control node). Return false on file error.
IMGUI_API bool             AppGraphSave(const char* path, const ImGuiAppGraph* g);
IMGUI_API bool             AppGraphLoad(const char* path, ImGuiAppGraph* g);

// Headless codegen: load a graph file, write its generated C++ to out_header_path. Returns false on
// a load or write error.
IMGUI_API bool             AppGraphGenerateToFiles(const char* graph_path, const char* out_header_path);

// Line diff of two graphs' generated C++ (' ' context, '-' only in a, '+' only in b). Appends to *out.
IMGUI_API void             AppGraphDiffCode(const ImGuiAppGraph* a, const ImGuiAppGraph* b, ImGuiTextBuffer* out);

// Parse C `struct <Name> { <type> <field>; ... };` blocks out of C++ source, adding one Struct node
// per struct (fields, types mapped back). New nodes laid out at `origin`. Returns structs added.
IMGUI_API int              AppGraphImportStructsFromCode(ImGuiAppGraph* g, const char* code, ImVec2 origin);

// Parse the F16 control emitter's output back into Control nodes (F22): each `struct <Name> : ImGuiAppControl<...>`
// becomes a Control node with Persist/TempFields read from the referenced Data/TempData struct blocks. New
// nodes laid out at `origin`; returns controls added. (Deps, command selections, event blocks import in later passes.)
IMGUI_API int              AppGraphImportControlsFromCode(ImGuiAppGraph* g, const char* code, ImVec2 origin);

// Whole-program import (F23): seed the foundation layers, then import controls (and, as they land,
// custom layers, standalone structs, windows/sidebars and hosting) so that emit -> import -> emit is a
// byte-equal fixed point. Returns controls added.
IMGUI_API int              AppGraphImportProgram(ImGuiAppGraph* g, const char* code);

// Undo / redo. The editor calls AppGraphCheckpoint once per frame; snapshots coalesce while the
// mouse is held or a widget is being edited, so a drag or inline rename folds into one step.
// Ctrl+Z / Ctrl+Y are handled inside ShowAppGraphEditor; Undo/Redo are also exposed for a toolbar.
IMGUI_API void             AppGraphCheckpoint(ImGuiAppGraph* g);
IMGUI_API void             AppGraphUndo(ImGuiAppGraph* g);
IMGUI_API void             AppGraphRedo(ImGuiAppGraph* g);
IMGUI_API bool             AppGraphCanUndo(const ImGuiAppGraph* g);
IMGUI_API bool             AppGraphCanRedo(const ImGuiAppGraph* g);

// Undo-history introspection: Count = snapshots, Cursor = the one the live graph matches
// (0..Count-1, -1 if empty), Goto = jump to a snapshot index.
IMGUI_API int              AppGraphHistoryCount(const ImGuiAppGraph* g);
IMGUI_API int              AppGraphHistoryCursor(const ImGuiAppGraph* g);
IMGUI_API void             AppGraphHistoryGoto(ImGuiAppGraph* g, int index);
IMGUI_API const char*      AppGraphHistoryLabel(const ImGuiAppGraph* g, int index);   // derived operation name ("Add Mixer", "Rename A -> B"); "" if unknown

// Prefabs: SavePrefab serializes the roots' containment subtrees under a name (replacing one of the
// same name); Instantiate stamps a saved prefab into g with fresh ids at `origin` and selects it.
// Count/Name enumerate the registry (in-memory, session-lived).
IMGUI_API void             AppGraphSavePrefab(const ImGuiAppGraph* g, const ImVector<int>& roots, const char* name);
IMGUI_API int              AppGraphPrefabCount(const ImGuiAppGraph* g);
IMGUI_API const char*      AppGraphPrefabName(const ImGuiAppGraph* g, int index);
IMGUI_API int              AppGraphInstantiatePrefab(ImGuiAppGraph* g, int index, ImVec2 origin);

// Seed the starter prefab library (producer/consumer pair, event->command control) when the registry
// is empty. The registry itself persists in a "<graph>.prefabs" sidecar written/read by Save/AppGraphLoad.
IMGUI_API void             AppGraphSeedStarterPrefabs(ImGuiAppGraph* g);

// Ensure the four foundation layers (Window, Task, Command, Status) exist in g -- adds missing,
// never duplicates. They are permanent (AppGraphRemoveNode refuses them). Call on new/empty graphs
// and after load.
IMGUI_API void             AppGraphEnsureFoundation(ImGuiAppGraph* g);

// One-shot tidy layout: design nodes as a left-to-right containment tree, siblings stacked, parents
// centered. L key inside the editor; also exposed for a toolbar. Layers keep their own column
// packing; live-mirror nodes are left in place.
IMGUI_API void             AppGraphAutoLayout(ImGuiAppGraph* g, bool show_live);
// F44: drilled-scope tidy -- members left->right in execution order, this scope's placements only
// (root GridPos untouched); falls back to AppGraphAutoLayout at root / non-sequential scopes.
IMGUI_API void             AppScopeSequenceTidy(ImGuiAppGraph* g, bool show_live);
// Direct members of the current scope in execution order; an authored F58 order overrides the derivation.
IMGUI_API void             AppScopeSequenceIds(const ImGuiAppGraph* g, ImVector<int>* out);
// F60 write gestures: reorder a scope member by authoring the F58 order record. MoveMember drops node_id at
// new_slot; Nudge shifts it one slot (dir<0 earlier, dir>0 later). Both act on the CURRENT drilled scope,
// refuse a move that permutes the core phase layers, and return true when the record changed.
IMGUI_API bool             AppScopeOrderMoveMember(ImGuiAppGraph* g, int node_id, int new_slot);
IMGUI_API bool             AppScopeOrderNudge(ImGuiAppGraph* g, int node_id, int dir);

// Build a session from an authored graph: push the framework core layers (Task/Command/Window), then one
// interpreter control per Control node in dependency-topo order with its storage registered. Returns null
// on a dependency cycle (reason in err). The graph is borrowed read-only and never mutated by running it.
IMGUI_API ImGuiAppPreview* AppPreviewCreate(const ImGuiAppGraph* graph, char* err, int err_size);
IMGUI_API void             AppPreviewDestroy(ImGuiAppPreview* session);

// The interpreter's real ImGuiApp -- for contract-suite reuse (F69) and inspection.
IMGUI_API ImGuiApp*        AppPreviewApp(ImGuiAppPreview* session);

// Drive one frame at dt (UpdateApp then RenderApp): Task in topo order, Command collect/dispatch-once,
// Window render. Advances the session tick.
IMGUI_API void             AppPreviewFrame(ImGuiAppPreview* session, float dt);

// Scripted input seam: the headless equivalent of a widget recording into TempData during OnDraw.
// Sticky until changed. F68 replaces this with real widget input on the composed window surface.
// Returns false when the node id / temp field name is unknown. Value is coerced to the field's type.
IMGUI_API bool             AppPreviewSetInput(ImGuiAppPreview* session, int node_id, const char* temp_field, double value);

// Read a running control's live Persist field, coerced to double. False when the node/field is unknown.
IMGUI_API bool             AppPreviewGetPersist(ImGuiAppPreview* session, int node_id, const char* persist_field, double* out_value);

// Dispatched-command log (design 4.3): one entry per dispatch in first-emission order.
IMGUI_API int              AppPreviewDispatchCount(const ImGuiAppPreview* session);
IMGUI_API int              AppPreviewDispatchCommandAt(const ImGuiAppPreview* session, int index);   // command value, -1 out of range
IMGUI_API const char*      AppPreviewCommandName(const ImGuiAppPreview* session, int command_value); // "" if unknown

// Edit-while-running reconcile (F67 interpreter CORE). The F68 preview SURFACE + brushing API is UI and
// lives below, gated behind IMGUIX_DISABLE_TOOLS (Phase A3).

// Edit-while-running reconciliation (design 7): rebuild the population from the (edited) borrowed graph,
// preserving every surviving (sanitized name, ImGuiAppFieldType) Persist/LastTemp slot byte-for-byte and
// default-initialising the rest. Refuses (running population intact) on a dependency cycle, reason in err.
IMGUI_API bool             AppPreviewReconcile(ImGuiAppPreview* session, char* err, int err_size);


// Headless test harness
// Runs queued tests to completion and returns a ctest-ready exit code (see docs/designs.md (av-design)).
IMGUI_API int AppTestHarnessRun(ImGuiApp* app, const ImGuiAppTestHarnessConfig* config);

// Media Foundation MFStartup/COM process refcount slot (used by imguiapp_impl_mediafoundation).
IMGUI_API int&             AppMediaFoundationStartupRefs();
} // namespace ImGui

#ifndef IMGUIX_DISABLE_TOOLS

//-----------------------------------------------------------------------------
// [SECTION][TOOLS] Canvas UI (was imguiapp_canvas.h)
//-----------------------------------------------------------------------------
struct ImGuiAppCanvasStyle
{
    // Colors: set directly, no push/pop stack. CanvasCreate derives them from the current
    // ImGuiStyle theme (CanvasStyleFromTheme); hosts may overwrite after.
    ImU32 GridBg;
    ImU32 GridLine;
    ImU32 GridLinePrimary;
    ImU32 NodeBg;
    ImU32 NodeBgHovered;
    ImU32 NodeBgSelected;
    ImU32 NodeOutline;
    ImU32 NodeOutlineSelected;
    ImU32 TitleBar;       // per-node override via CanvasNextNodeTitleColor
    ImU32 TitleBarHovered;
    ImU32 TitleBarSelected;
    ImU32 TitleText;
    ImU32 TitleEditBg;    // InputText field bg while renaming a node title
    ImU32 Wire;
    ImU32 WireHovered;
    ImU32 WireSelected;
    ImU32 PinData;        // circle pins (data edges)
    ImU32 PinContainment; // square pins (containment edges)
    ImU32 PinHovered;
    ImU32 MiniMapBg;
    ImU32 MiniMapBgHovered;
    ImU32 MiniMapOutline;
    ImU32 MiniMapNodeBg;
    ImU32 MiniMapNodeBgHovered;
    ImU32 MiniMapNodeBgSelected;
    ImU32 MiniMapNodeOutline;
    ImU32 MiniMapLink;
    ImU32 MiniMapCanvas;  // the current-view rect inside the map
    ImU32 MiniMapCanvasOutline;

    // Metrics in MODEL units (the engine zooms them; hosts never pre-scale).
    float  GridSpacing;
    float  NodeRounding;
    ImVec2 NodePadding;
    float  NodeBorder;
    float  WireThickness;
    float  PinRadius;
    float  PinHoverRadius;

    bool GridLines;
    bool GridSnap; // snap node origins to GridSpacing while dragging
};

struct ImGuiAppCanvasIO
{
    // Bindings policy.
    bool  LmbPansEmptyCanvas; // LMB-drag on empty canvas pans (no box select)
    bool  RmbPans;            // RMB-drag pans; a short RMB click reports CanvasMenuRequest*
    bool  WheelZooms;         // cursor-centered; Ctrl+wheel zooms even over nodes/items
    float ZoomMin, ZoomMax;   // clamp (defaults 0.3 / 2.5)
};

struct ImGuiAppCanvasNodeRec
{
    int    Id;
    ImVec2 Pos;        // model units
    ImVec2 Size;       // model units, measured same-frame at submission
    ImU32  TitleColor; // 0 = style default
    char   Title[64];
    char   KindTag[24];  // muted right-aligned title-bar tag (the node's kind word); empty = none
    char   Badge[16];    // small framed title-bar text after the name; empty = none
    ImU32  OriginDot;    // leading title-bar dot color; 0 = none
    bool   DotRing;      // origin dot drawn as a ring instead of filled
    float  Rounding;     // corner rounding in MODEL units; < 0 = style default
    float  FixedWidth;   // normalized plate width in MODEL units; <= 0 = content-sized
    float  NeededW;      // content-derived width need (MODEL units), measured before FixedWidth applies
    int    HeaderRule;   // rule under the title band: 0 none, 1 solid, 2 dashed
    ImU32  HeaderRuleColor;
    int    StripeSide;   // ImGuiAppCanvasPinSide_ of an edge stripe; -1 = none
    ImU32  StripeColor;
    float  StripeThick;  // stripe thickness, MODEL units
    float  Alpha;        // plate + content opacity multiplier (disabled look); 1 = normal
    bool   Draggable;
    bool   Solid;      // cannot be dragged into overlap with other Solid nodes (slide to contact)
    int    LastFrame;  // ImGui frame count of the last submission (cull/hit bookkeeping)
};

struct ImGuiAppCanvasPinRec
{
    int    Id;
    int    NodeId;
    int    Kind;       // ImGuiAppCanvasPinKind_In / _Out (interaction role)
    int    Side;       // ImGuiAppCanvasPinSide_ (which node edge; Left/Right = data, Top/Bottom = containment)
    int    Shape;      // circle (data) / square (containment)
    ImU32  Color;      // 0 = style (by shape); set via CanvasNextPinColor
    ImVec2 Anchor;     // MODEL units: pin center at the node edge, row-centered
    int    LastFrame;
    int    WiredCount; // wires touching this pin THIS frame (filled pin glyph)
};

struct ImGuiAppCanvasWireRec  // per-frame submission, rebuilt every frame
{
    int   Id;
    int   PinA;
    int   PinB;
    ImU32 Color;  // 0 = style
    bool  Dashed; // optional dependency: dashed body (form carrier; dimming is the color coat)
};

typedef int ImGuiAppCanvasInteraction;   // -> enum ImGuiAppCanvasInteraction_   // Enum: canvas interaction mode
enum ImGuiAppCanvasInteraction_
{
    ImGuiAppCanvasInteraction_None = 0,
    ImGuiAppCanvasInteraction_Pan,
    ImGuiAppCanvasInteraction_DragNodes,
    ImGuiAppCanvasInteraction_DragWire,      // from FromPin; release on a pin = created, on empty = dropped
    ImGuiAppCanvasInteraction_MenuPending,   // RMB down, not yet travelled: release = menu, travel = pan
};

struct ImGuiAppCanvasState
{
    ImGuiAppCanvasStyle Style;
    ImGuiAppCanvasIO    IO;

    // Camera
    ImVec2 Pan;
    float  Zoom;
    float  FontRatio; // host font scale (DPI * user scale) captured at CanvasBegin; geometry scale = Zoom * FontRatio

    // Nodes
    ImVector<ImGuiAppCanvasNodeRec> Nodes;
    ImGuiStorage                 NodeIdx;        // id -> index + 1
    ImVector<int>                SubmitOrder;    // last frame's submission order (z-order; hit-test walks it backward)
    ImVector<int>                SubmitOrderNow; // rebuilt during the current frame

    // Pins + wires
    ImVector<ImGuiAppCanvasPinRec>  Pins;
    ImGuiStorage                 PinIdx;      // id -> index + 1
    ImVector<ImGuiAppCanvasWireRec> Wires;       // rebuilt per frame between CanvasBegin/End
    ImVector<ImGuiAppCanvasWireRec> WiresPrev;   // last frame's wires: press decisions run at CanvasBegin
    ImVector<int>                CurNodePins; // pin indices submitted inside the current node (anchor.x resolves at EndNode)

    // Host-declared solid regions (model units, x0 y0 x1 y1): obstacles for solid-node drags.
    // Submitted between Begin/End, consumed by the NEXT frame's drag FSM -- the same T+1 posture
    // as the node geometry the drag itself runs on.
    ImVector<ImVec4> SolidRects;
    ImVector<ImVec4> SolidRectsPrev;

    // Selection + hover
    ImVector<int> Selection;
    int           HoveredNode;  // resolved against current camera + model geometry
    int           HoveredPin;   // resolved in CanvasEnd (needs this frame's wires/pins); -1 = none
    int           HoveredWire;
    int           SelectedWire; // single wire selection (click); -1 = none

    // Per-frame canvas geometry
    ImVec2             Origin;   // canvas child top-left (screen)
    ImVec2             CanvasSize;
    ImDrawList*        DrawList;
    ImDrawListSplitter Splitter; // ch0 = grid + wires, ch1 = node plates, ch2 = node content
    bool               InsideCanvas;

    // Submission intent -- this canvas's per-frame TempData: latched by the CanvasNext* calls,
    // consumed by the next Begin*, reset there. Per instance, never file scope.
    int    CurNode;       // index into Nodes during Begin/EndNode, -1 otherwise
    ImVec2 CurNodeScreen; // this frame's screen pos of the node origin
    int    CurPin;        // index into Pins during Begin/EndPin, -1 otherwise
    float  CurPinY0;      // row top at CanvasBeginPin (row center resolves at EndPin)
    char   NextTitle[64];
    ImU32  NextTitleColor;
    char   NextTitleTag[24];
    char   NextBadge[16];
    ImU32  NextOriginDot;
    bool   NextDotRing;
    float  NextRounding;      // model units; < 0 = style default
    float  NextFixedWidth;    // model units; <= 0 = content-sized
    int    NextHeaderRule;    // 0 none, 1 solid, 2 dashed
    ImU32  NextHeaderRuleColor;
    int    NextStripeSide;    // ImGuiAppCanvasPinSide_; -1 none
    ImU32  NextStripeColor;
    float  NextStripeThick;   // model units
    float  NextAlpha;         // plate + content opacity multiplier; 1 = normal
    ImU32  NextPinColor;
    int    NextPinSide;       // -1 -> derive from Kind (In->Left, Out->Right)
    char*  NextEditBuf;
    int    NextEditBufSize;
    bool*  NextEditFlag;
    bool   NextWireDashed;

    // Interaction FSM
    int              Interaction;
    ImVec2           GestureStartMouse;
    ImVec2           GestureStartPan;
    ImVector<ImVec2> DragStartPos;    // model pos of each selected node at drag start
    ImVector<int>    DragNodes;
    ImVec2           DragAppliedDisp; // solid drags: displacement actually granted so far (greedy catch-up)
    int              DragWireFromPin; // DragWire: the fixed end

    // Rename hook: per-frame pointers captured by CanvasNextNodeTitleEditable (host-owned storage).
    char* EditBuf;
    int   EditBufSize;
    bool* EditFlag;
    int   EditNodeIdx;
    bool  EditFocusPending;
    int   LastEditingNodeId; // focus-once tracking across frames

    // Minimap
    bool   MiniMapReq;
    float  MiniMapFraction;
    ImVec2 MiniRectMin;    // background rect incl. padding ring (screen); the FSM keeps out of it
    ImVec2 MiniRectMax;
    ImVec2 MiniContentMin; // content rect origin (screen); the mapping's anchor
    ImVec2 MiniModelMin;   // content bounds min (model); the mapping's other anchor
    float  MiniScale;      // model units -> minimap pixels

    // Latched events (valid from CanvasEnd until the next CanvasBegin)
    bool   NodeDblClickReq;
    int    NodeDblClickId;
    bool   MenuNodeReq;
    int    MenuNodeId;
    bool   MenuWireReq;
    int    MenuWireId;
    bool   MenuEmptyReq;
    ImVec2 MenuEmptyModel;
    bool   WireCreatedReq;
    int    CreatedPinA;
    int    CreatedPinB;
    bool   WireDroppedReq;
    int    DroppedFromPin;
    ImVec2 DroppedModel;
    bool   WireDetachedReq;
    int    DetachedWireId;
    int    DetachedGrabbedPin;

    ImGuiAppCanvasState()
    {
        memset(&Style, 0, sizeof(Style));   // colors filled by CanvasStyleFromTheme in CanvasCreate
        Style.GridSpacing    = 24.0f;
        Style.NodeRounding   = 4.0f;
        Style.NodePadding    = ImVec2(8.0f, 6.0f);
        Style.NodeBorder     = 1.0f;
        Style.WireThickness  = 2.5f;
        Style.PinRadius      = 4.0f;
        Style.PinHoverRadius = 8.0f;
        Style.GridLines      = true;
        Style.GridSnap       = false;

        IO.LmbPansEmptyCanvas = true;
        IO.RmbPans            = true;
        IO.WheelZooms         = true;
        IO.ZoomMin            = 0.3f;
        IO.ZoomMax            = 2.5f;

        Pan                 = ImVec2(0.0f, 0.0f);
        Zoom                = 1.0f;
        FontRatio           = 1.0f;
        HoveredNode = HoveredPin = HoveredWire = SelectedWire = -1;
        Origin = CanvasSize = ImVec2(0.0f, 0.0f);
        DrawList            = nullptr;
        InsideCanvas        = false;
        CurNode             = -1;
        CurNodeScreen       = ImVec2(0.0f, 0.0f);
        CurPin              = -1;
        CurPinY0            = 0.0f;
        NextTitle[0]        = 0;
        NextTitleColor      = 0;
        NextTitleTag[0]     = 0;
        NextBadge[0]        = 0;
        NextOriginDot       = 0;
        NextDotRing         = false;
        NextRounding        = -1.0f;
        NextFixedWidth      = -1.0f;
        NextHeaderRule      = 0;
        NextHeaderRuleColor = 0;
        NextStripeSide      = -1;
        NextStripeColor     = 0;
        NextStripeThick     = 0.0f;
        NextAlpha           = 1.0f;
        NextPinColor        = 0;
        NextPinSide         = -1;
        NextEditBuf         = nullptr;
        NextEditBufSize     = 0;
        NextEditFlag        = nullptr;
        NextWireDashed      = false;
        Interaction         = ImGuiAppCanvasInteraction_None;
        GestureStartMouse = GestureStartPan = ImVec2(0.0f, 0.0f);
        DragWireFromPin     = -1;
        EditBuf = nullptr; EditBufSize = 0; EditFlag = nullptr; EditNodeIdx = -1; EditFocusPending = false;
        LastEditingNodeId   = -1;
        MiniMapReq          = false;
        MiniMapFraction     = 0.2f;
        MiniRectMin = MiniRectMax = ImVec2(0.0f, 0.0f);
        MiniContentMin = MiniModelMin = ImVec2(0.0f, 0.0f);
        MiniScale           = 0.0f;
        NodeDblClickReq = false; NodeDblClickId = -1;
        MenuNodeReq = MenuWireReq = MenuEmptyReq = false;
        MenuNodeId = MenuWireId = -1;
        MenuEmptyModel      = ImVec2(0.0f, 0.0f);
        WireCreatedReq = WireDroppedReq = WireDetachedReq = false;
        CreatedPinA = CreatedPinB = DroppedFromPin = DetachedWireId = DetachedGrabbedPin = -1;
        DroppedModel        = ImVec2(0.0f, 0.0f);
    }
};

//-----------------------------------------------------------------------------
// [SECTION][TOOLS] ImGui applayer internal API -- canvas UI, DLL preview surface, editor UI
//-----------------------------------------------------------------------------
namespace ImGui
{
// Canvas engine: lifecycle, nodes, pins, wires, interaction
IMGUI_API ImGuiAppCanvasState* CanvasCreate();
IMGUI_API void              CanvasDestroy(ImGuiAppCanvasState* c);
IMGUI_API ImGuiAppCanvasStyle* CanvasGetStyle(ImGuiAppCanvasState* c);
IMGUI_API ImGuiAppCanvasIO*    CanvasGetIO(ImGuiAppCanvasState* c);
// Recompute all style colors from the current ImGuiStyle theme (requires a live context).
// Called by CanvasCreate; call again after switching themes.
IMGUI_API void              CanvasStyleFromTheme(ImGuiAppCanvasStyle* style);

// ---- frame ----------------------------------------------------------------------------------
// CanvasBegin opens the child (fills the available region unless size is given), draws the grid, applies
// camera input per the IO policy. Submit nodes/pins/wires between Begin/End; CanvasEnd draws wires + pins
// from THIS frame's geometry, resolves hover, runs the interaction FSM, latches events (valid until next CanvasBegin).
// CanvasBegin/CanvasEnd: ALWAYS paired, unconditionally -- Begin is void and End must run even for
// a fully culled canvas (it resolves hover + retires per-frame wire/pin state).
IMGUI_API void              CanvasBegin(ImGuiAppCanvasState* c, const char* str_id, ImVec2 size /*= 0,0*/);
IMGUI_API void              CanvasEnd(ImGuiAppCanvasState* c);   // always call (pairs CanvasBegin)

// Decoration hook: returns the canvas draw list switched to the BACKGROUND channel (above the grid,
// below node plates; wires land in the same channel later, so they draw over these decorations).
// Call between CanvasBegin and the first node; the next CanvasBeginNode restores the content channel.
IMGUI_API ImDrawList*       CanvasBackgroundDrawList(ImGuiAppCanvasState* c);

// Annotation hook: the canvas child's draw list AFTER CanvasEnd. Appended commands render above
// every merged channel (grid, plates, content) but stay inside the child's z-order -- annotations
// ride the canvas, they never paint over other windows (the viewport foreground list does).
IMGUI_API ImDrawList*       CanvasAnnotationDrawList(ImGuiAppCanvasState* c);

// ---- camera (pan in pixels, zoom unitless; screen = origin + pan + model * zoom) -------------
IMGUI_API ImVec2            CanvasGetPan(const ImGuiAppCanvasState* c);
IMGUI_API void              CanvasSetPan(ImGuiAppCanvasState* c, ImVec2 pan);
IMGUI_API float             CanvasGetZoom(const ImGuiAppCanvasState* c);
// Model -> screen scale (Zoom x host font DPI/user factor). Use for ALL model<->pixel
// conversions; CanvasGetZoom is the logical camera only (font pushes, persisted view state).
IMGUI_API float             CanvasGetScale(const ImGuiAppCanvasState* c);
IMGUI_API void              CanvasSetZoom(ImGuiAppCanvasState* c, float zoom, ImVec2 keep_screen_pos);   // keeps that screen point fixed
IMGUI_API ImVec2            CanvasToScreen(const ImGuiAppCanvasState* c, ImVec2 model);
IMGUI_API ImVec2            CanvasFromScreen(const ImGuiAppCanvasState* c, ImVec2 screen);
IMGUI_API void              CanvasCenterOn(ImGuiAppCanvasState* c, ImVec2 model_pos);
IMGUI_API void              CanvasFitRect(ImGuiAppCanvasState* c, ImVec2 model_min, ImVec2 model_max, float margin_px);
IMGUI_API void              CanvasFitNodes(ImGuiAppCanvasState* c, const int* node_ids, int count, float margin_px);
IMGUI_API void              CanvasFitAll(ImGuiAppCanvasState* c, float margin_px);                       // over nodes submitted last frame

// Minimap: call between CanvasBegin/CanvasEnd; drawn by CanvasEnd as a bottom-right inset fitted
// to the node content's aspect within size_fraction of the canvas. Holding LMB over the map
// continuously recenters the camera. Its rect is excluded from the FSM.
IMGUI_API void              CanvasMiniMap(ImGuiAppCanvasState* c, float size_fraction);

// ---- nodes (geometry in MODEL units, always) --------------------------------------------------
// Between CanvasBeginNode/CanvasEndNode submit ordinary ImGui widgets; the engine renders them
// under the zoomed font + scaled layout metrics and measures the node the SAME frame.
IMGUI_API void              CanvasNextNodeTitle(ImGuiAppCanvasState* c, const char* title, ImU32 title_color /*= 0 -> style*/);
// Per-node presentation intent, all consumed by the next CanvasBeginNode: kind word (muted, right-aligned),
// origin dot (ring form = promoted), framed badge after the name, corner rounding in model units (< 0 =
// style), title-band rule (0 none / 1 solid / 2 dashed), and an edge stripe on one ImGuiAppCanvasPinSide_.
IMGUI_API void              CanvasNextNodeTitleTag(ImGuiAppCanvasState* c, const char* tag);
IMGUI_API void              CanvasNextNodeOriginDot(ImGuiAppCanvasState* c, ImU32 color, bool ring);
IMGUI_API void              CanvasNextNodeTitleBadge(ImGuiAppCanvasState* c, const char* badge);
IMGUI_API void              CanvasNextNodeRounding(ImGuiAppCanvasState* c, float model_rounding);
IMGUI_API void              CanvasNextNodeWidth(ImGuiAppCanvasState* c, float model_width);          // normalized plate width; <= 0 = content-sized
IMGUI_API float             CanvasNodeNeededWidth(const ImGuiAppCanvasState* c, int node_id);        // content-derived width need (0 until measured)
IMGUI_API void              CanvasNextNodeHeaderRule(ImGuiAppCanvasState* c, int rule, ImU32 color);
IMGUI_API void              CanvasNextNodeEdgeStripe(ImGuiAppCanvasState* c, int side, ImU32 color, float model_thickness);
IMGUI_API void              CanvasNextNodeAlpha(ImGuiAppCanvasState* c, float alpha);   // < 1 dims plate + content (disabled look); consumed by the next CanvasBeginNode
// Editable variant (host-driven rename): while *editing, the engine renders an InputText in the
// title bar bound to buf and clears *editing when it deactivates. Pair with CanvasNodeDoubleClicked
// to enter the state (the host decides whether a double-click renames or drills).
IMGUI_API void              CanvasNextNodeTitleEditable(ImGuiAppCanvasState* c, char* buf, int buf_size, bool* editing, ImU32 title_color);
IMGUI_API bool              CanvasBeginNode(ImGuiAppCanvasState* c, int node_id);   // false if culled (off-screen): body may be skipped, geometry persists; ALWAYS call CanvasEndNode regardless
IMGUI_API void              CanvasEndNode(ImGuiAppCanvasState* c);   // always call (pairs CanvasBeginNode, even when it returned false)
IMGUI_API ImVec2            CanvasNodePos(const ImGuiAppCanvasState* c, int node_id);        // model
IMGUI_API void              CanvasSetNodePos(ImGuiAppCanvasState* c, int node_id, ImVec2 model_pos);
IMGUI_API ImVec2            CanvasNodeSize(const ImGuiAppCanvasState* c, int node_id);       // model; this frame if submitted, else last known
IMGUI_API const char*       CanvasNodeTitleBadge(const ImGuiAppCanvasState* c, int node_id);   // title-bar badge text ("" if none / unknown node)
IMGUI_API void              CanvasSetNodeDraggable(ImGuiAppCanvasState* c, int node_id, bool draggable);
IMGUI_API void              CanvasSetNodeSolid(ImGuiAppCanvasState* c, int node_id, bool solid);   // solid nodes cannot be dragged into overlap with other solid nodes (slide to contact)
// Declare a solid region (model units) for THIS frame: solid-node drags cannot enter it (the
// next frame's drag consumes it, like node geometry). Call between CanvasBegin/CanvasEnd.
IMGUI_API void              CanvasAddSolidRect(ImGuiAppCanvasState* c, ImVec2 model_min, ImVec2 model_max);

// ---- pins + wires -----------------------------------------------------------------------------
// Kind is the interaction role (which end pairs with which when wiring). Side is the node edge the
// pin sits on and the direction its wire leaves -- orthogonal to Kind. Left/Right give the classic
// horizontal data read; Top/Bottom give a vertical owner-over-child containment read.
typedef int ImGuiAppCanvasPinKind;   // -> enum ImGuiAppCanvasPinKind_
enum ImGuiAppCanvasPinKind_ { ImGuiAppCanvasPinKind_In = 0, ImGuiAppCanvasPinKind_Out = 1 };
typedef int ImGuiAppCanvasPinShape;  // -> enum ImGuiAppCanvasPinShape_
enum ImGuiAppCanvasPinShape_ { ImGuiAppCanvasPinShape_Circle = 0, ImGuiAppCanvasPinShape_Square = 1 };
typedef int ImGuiAppCanvasPinSide;   // -> enum ImGuiAppCanvasPinSide_
enum ImGuiAppCanvasPinSide_ { ImGuiAppCanvasPinSide_Left = 0, ImGuiAppCanvasPinSide_Right = 1, ImGuiAppCanvasPinSide_Top = 2, ImGuiAppCanvasPinSide_Bottom = 3 };
IMGUI_API void              CanvasNextPinColor(ImGuiAppCanvasState* c, ImU32 color);   // 0 -> style (by shape); consumed by the next CanvasBeginPin
IMGUI_API void              CanvasNextPinSide(ImGuiAppCanvasState* c, int side);   // override the next pin's edge; default derives from Kind (In->Left, Out->Right)
IMGUI_API void              CanvasBeginPin(ImGuiAppCanvasState* c, int pin_id, int kind /*In|Out*/, int shape);
IMGUI_API void              CanvasEndPin(ImGuiAppCanvasState* c);
// Row-less edge pin: an at-most-one-per-edge singleton (e.g. containment parent/children) placed at
// the center of its Side edge. Submits no widget and consumes no cursor -- call between BeginNode/EndNode.
IMGUI_API void              CanvasEdgePin(ImGuiAppCanvasState* c, int pin_id, int kind /*In|Out*/, int shape, int side);
IMGUI_API void              CanvasNextWireDashed(ImGuiAppCanvasState* c);   // the next CanvasWire draws dashed (optional dependency)
IMGUI_API void              CanvasWire(ImGuiAppCanvasState* c, int wire_id, int pin_a, int pin_b, ImU32 color /*= 0 -> style*/);
IMGUI_API bool              CanvasHasWire(const ImGuiAppCanvasState* c, int wire_id);      // wire submitted this frame (query after CanvasEnd)
IMGUI_API ImVec2            CanvasPinPos(const ImGuiAppCanvasState* c, int pin_id);           // model

// ---- selection + hover ------------------------------------------------------------------------
IMGUI_API int               CanvasNumSelectedNodes(const ImGuiAppCanvasState* c);
IMGUI_API void              CanvasGetSelectedNodes(const ImGuiAppCanvasState* c, int* out, int cap);
IMGUI_API void              CanvasSelectNode(ImGuiAppCanvasState* c, int node_id, bool additive);
IMGUI_API void              CanvasClearSelection(ImGuiAppCanvasState* c);
IMGUI_API int               CanvasHoveredNode(const ImGuiAppCanvasState* c);                  // -1 = none (valid after CanvasEnd)
IMGUI_API int               CanvasHoveredWire(const ImGuiAppCanvasState* c);
IMGUI_API int               CanvasHoveredPin(const ImGuiAppCanvasState* c);
IMGUI_API int               CanvasWireDragSource(const ImGuiAppCanvasState* c);               // pin an active wire-drag started from, else -1 (for drag-time can-link telegraph)
IMGUI_API int               CanvasSelectedWire(const ImGuiAppCanvasState* c);                 // single click-selected wire; -1 = none
IMGUI_API void              CanvasClearWireSelection(ImGuiAppCanvasState* c);

// ---- events (latched by CanvasEnd; valid until the next CanvasBegin) ---------------------------
IMGUI_API bool              CanvasWireCreated(const ImGuiAppCanvasState* c, int* out_pin_a, int* out_pin_b);   // drag completed pin->pin (snap or release)
IMGUI_API bool              CanvasWireDropped(const ImGuiAppCanvasState* c, int* out_from_pin, ImVec2* out_model_pos);   // released on empty canvas
IMGUI_API bool              CanvasWireDetached(const ImGuiAppCanvasState* c, int* out_wire_id, int* out_grabbed_end_pin); // endpoint dragged off a pin
IMGUI_API bool              CanvasNodeDoubleClicked(const ImGuiAppCanvasState* c, int* out_node_id); // LMB double-click on a node
IMGUI_API bool              CanvasMenuRequestNode(const ImGuiAppCanvasState* c, int* out_node_id);   // short RMB click resolution
IMGUI_API bool              CanvasMenuRequestWire(const ImGuiAppCanvasState* c, int* out_wire_id);
IMGUI_API bool              CanvasMenuRequestEmpty(const ImGuiAppCanvasState* c, ImVec2* out_model_pos);


// DLL preview surface (F78)
// True when a pinned cl.exe + its environment were located on this box (cached). When false,
// AppPreviewDllCreate returns null with a note and the composer uses the interpreter.
IMGUI_API bool                AppPreviewDllIsToolsetAvailable();

// Emit the module for `graph`, compile it into `scratch_dir` (created if absent) as a self-contained DLL,
// verify its ImGuiAppPreview_ABI() matches the host, and create the running instance. Null on no-toolset or
// any compile/load/ABI failure (reason in `err`). Graph borrowed read-only; free with AppPreviewDllDestroy.
IMGUI_API ImGuiAppPreviewDll* AppPreviewDllCreate(const ImGuiAppGraph* graph, const char* scratch_dir, char* err, int err_size);
IMGUI_API void                AppPreviewDllDestroy(ImGuiAppPreviewDll* session);

// Advance the DLL instance one frame at dt, inside its own context.
IMGUI_API void                AppPreviewDllTick(ImGuiAppPreviewDll* session, float dt);

// Copy a control's bytes across the boundary by label. `temp` true = the TempData input range, false =
// Persist+LastTemp state. CopyOut returns bytes read into `out` (<= cap); CopyIn returns bytes written
// from `in` (<= the range). 0 = control/label unknown or opaque (not snapshottable).
IMGUI_API int                 AppPreviewDllCopyOut(ImGuiAppPreviewDll* session, const char* control_label, bool temp, void* out, int cap);
IMGUI_API int                 AppPreviewDllCopyIn(ImGuiAppPreviewDll* session, const char* control_label, bool temp, const void* in, int size);

// Recompile the (edited) graph into a new self-contained DLL and hot-swap, preserving each surviving
// control's Persist bytes by COPY (out of the old instance, into the new by label) -- the F68 preserve
// policy on the DLL path. A compile failure keeps the last-good instance running and returns false (err set).
IMGUI_API bool                AppPreviewDllReload(ImGuiAppPreviewDll* session, const ImGuiAppGraph* graph, char* err, int err_size);

// F78.5 in-panel render: close the "live" loop by moving the DLL app's rendered frame across the boundary as
// COPIED BYTES (draw data + font atlas), then CPU-rasterizing it host-side -- no GPU/context/pointer shared.
// SetDisplaySize resizes the DLL's own viewport so its NEXT tick renders at the panel size.
IMGUI_API void                AppPreviewDllSetDisplaySize(ImGuiAppPreviewDll* session, int w, int h);

// Copy the last-ticked frame's draw data + font atlas out of the DLL and rasterize it into `out_rgba` (a
// w*h*4 RGBA32 buffer, resized as needed), cleared to `clear_col` then filled with the DLL's triangles.
// True when any geometry landed; false when the module lacks the frame ABI or produced nothing. Call after AppPreviewDllTick.
IMGUI_API bool                AppPreviewDllRasterizeFrame(ImGuiAppPreviewDll* session, int w, int h, unsigned int clear_col, ImVector<unsigned char>* out_rgba);


// Composer editor hooks + introspection accessors
// The demo Composer's document graph inside `host` (its ImGuiAppGraphDocData storage entry), or null before
// composition. Test harnesses drive the editor camera through it. (Was in imguiapp.h; tool-coupled.)
IMGUI_API ::ImGuiAppGraph* AppLayerDemoGraph(ImGuiApp* host);

// Monospace font for the generated-code inspector (space-padded alignment needs fixed width). Register
// at font-init; null leaves the inspector on the UI font. (Was in imguiapp.h; tool-coupled.)
IMGUI_API void             SetAppCodeFont(ImGuiAppGraph* g, ImFont* font);

// Composer introspection accessors (relocated from imguiapp.h; tool-coupled, "exposed for tests").
// App-time transport (F29): number of state snapshots the running composer has recorded of its
// snapshottable controls (0 = none). Drives the toolbar scrubber; exposed for the headless scrub test.
IMGUI_API int              AppComposerAppTimeFrames(ImGuiApp* host);

// App-time transport source (F63): ImGuiAppTransportSource_ (LiveRing vs FileRun). Exposed for tests.
IMGUI_API int              AppComposerTransportSource(ImGuiApp* host);

// Tick shown by the FILE-mode transport (F63): the decoded frame's tick at the current scrub index
// (0 when no recorded run is open). Exposed for the scrub-to-tick acceptance.
IMGUI_API ImU64            AppComposerFileRunShownTick(ImGuiApp* host);

// Composer outliner column width (F32): >0 shown, 0 hidden. Exposed for the status-bar zone test.
IMGUI_API float            AppComposerOutlinerWidth(ImGuiApp* host);

// Composer layout-preset visibilities (F36): bitmask tree(1)|insp(2)|code(4)|live(8). Exposed for the
// preset-switch test; a Compose/Review/Observe pick sets a fixed combination.
IMGUI_API int              AppComposerLayoutFlags(ImGuiApp* host);

// F68 preview SURFACE + brushing (relocated from imguiapp_preview.h, Phase A3). Definitions live in the
// imguiapp.cpp preview region under the same IMGUIX_DISABLE_TOOLS guard.


// F68 preview surface + brushing
// On-camera surface (design 8.1): with it enabled, each interpreted control's OnDraw submits its
// manifest-bound field widgets into the CURRENT ImGui window (the composer's Preview panel). Disabled
// (the default / F67 CORE path) the controls issue no ImGui calls, so the interpreter runs headless.
IMGUI_API void AppPreviewSetSurface(ImGuiAppPreview* session, bool on);

// Render the interpreter's controls without advancing the model -- RenderApp only, no Task/Command pass.
// The paused half of run/pause: widgets still show (and can be poked) but the simulation is frozen.
IMGUI_API void AppPreviewRender(ImGuiAppPreview* session);

// Selection brushing (design 8.2), composer -> preview: the selected/hovered node's widget group haloes
// in the surface. Pass -1 for none. Set before the frame; consumed while the controls render.
IMGUI_API void AppPreviewSetBrush(ImGuiAppPreview* session, int selected_node_id, int hover_node_id);

// Preview -> composer: the node whose widget group the mouse rested over this frame (-1 = none). Cheap to
// read after AppPreviewFrame/Render; the composer forwards it to AppGraphHoverNode so the canvas haloes.
IMGUI_API int  AppPreviewHoveredNode(const ImGuiAppPreview* session);

// Preview -> composer: the node whose widget group was clicked (latched until taken). The composer takes
// it once per frame and promotes it to the primary selection; -1 = no pending click.
IMGUI_API int  AppPreviewTakeClickedNode(ImGuiAppPreview* session);

// --- Editor-UI decls relocated from imguiapp_nodes.h land below (Phase A2). ---
} // namespace ImGui

#endif // IMGUIX_DISABLE_TOOLS

#endif // #ifndef IMGUI_DISABLE
