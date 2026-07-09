// dear imgui: Renderer Backend for Direct2D (ID2D1DeviceContext)
// This needs to be used along with a Platform Backend (e.g. Win32)

// Implemented features:
//  [X] Renderer: User texture binding. Use 'ID2D1Bitmap1*' as texture identifier. Read the FAQ about ImTextureID/ImTextureRef!
//  [X] Renderer: Large meshes support (64k+ vertices) even with 16-bit indices (ImGuiBackendFlags_RendererHasVtxOffset).
//  [X] Renderer: Texture updates support for dynamic font atlas (ImGuiBackendFlags_RendererHasTextures).
//  [X] Renderer: Textured axis-aligned quads (glyphs, images) drawn natively (FillOpacityMask / DrawBitmap).
// Missing features or Issues:
//  [ ] Renderer: Per-vertex color gradients are approximated (flat fill per triangle with the averaged vertex color).
//  [ ] Renderer: Multi-viewports support (secondary windows are the host's business; see imguiapp_impl_win32_dcomp).

// Important: RenderDrawData must be called between BeginDraw()/EndDraw() on the device context passed to
// Init, with the target bitmap already set -- Direct2D owns the pass bracket, this backend only records into it.

// You can use unmodified imgui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire imgui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2026-07-08: Initial version: draw-data walker classifying primitives into solid rects / textured quads / flat triangles; ImTextureData -> premultiplied BGRA ID2D1Bitmap1.

#include "imgui.h"
#ifndef IMGUI_DISABLE
#include "imgui_impl_d2d.h"

// Direct2D
#include <d2d1_1.h>   // ID2D1DeviceContext, ID2D1Bitmap1, ID2D1SolidColorBrush, ID2D1PathGeometry
#ifdef _MSC_VER
#pragma comment(lib, "d2d1")
#endif

// Direct2D data
struct ImGui_ImplD2D_Data
{
    ID2D1DeviceContext*       D2DDeviceContext;
    ID2D1Factory*             D2DFactory;      // from the device context; geometry creation
    ID2D1SolidColorBrush*     SolidBrush;      // shared, recolored per primitive
    ImGui_ImplD2D_RenderState RenderStateInstance;
    ImVector<unsigned int>    UploadScratch;   // straight RGBA -> premultiplied BGRA conversion buffer

    ImGui_ImplD2D_Data()      { memset((void*)this, 0, sizeof(*this)); }
};

// Backend data stored in io.BackendRendererUserData to allow support for multiple Dear ImGui contexts
// It is STRONGLY preferred that you use docking branch with multi-viewports (== single Dear ImGui context + multiple windows) instead of multiple Dear ImGui contexts.
static ImGui_ImplD2D_Data* ImGui_ImplD2D_GetBackendData()
{
    return ImGui::GetCurrentContext() ? (ImGui_ImplD2D_Data*)ImGui::GetIO().BackendRendererUserData : nullptr;
}

// Functions
static inline float Min(float lhs, float rhs) { return lhs < rhs ? lhs : rhs; }
static inline float Max(float lhs, float rhs) { return lhs > rhs ? lhs : rhs; }

// Straight-alpha RGBA32 (ImTextureData) -> premultiplied BGRA32 (the universally supported D2D bitmap format).
static void ImGui_ImplD2D_ConvertPixelRow(unsigned int* dst, const unsigned int* src, int count)
{
    for (int i = 0; i < count; i++)
    {
        const unsigned int v = src[i];
        const unsigned int a = (v >> 24) & 0xFF;
        unsigned int r = (v >> 0) & 0xFF;
        unsigned int g = (v >> 8) & 0xFF;
        unsigned int b = (v >> 16) & 0xFF;
        r = (r * a + 127) / 255;
        g = (g * a + 127) / 255;
        b = (b * a + 127) / 255;
        dst[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
}

static void ImGui_ImplD2D_DestroyTexture(ImTextureData* tex)
{
    if (ID2D1Bitmap1* bitmap = (ID2D1Bitmap1*)tex->BackendUserData)
    {
        IM_ASSERT(bitmap == (ID2D1Bitmap1*)(intptr_t)tex->TexID);
        bitmap->Release();

        // Clear identifiers and mark as destroyed (in order to allow e.g. calling InvalidateDeviceObjects while running)
        tex->SetTexID(ImTextureID_Invalid);
        tex->BackendUserData = nullptr;
    }
    tex->SetStatus(ImTextureStatus_Destroyed);
}

void ImGui_ImplD2D_UpdateTexture(ImTextureData* tex)
{
    ImGui_ImplD2D_Data* bd = ImGui_ImplD2D_GetBackendData();
    if (tex->Status == ImTextureStatus_WantCreate)
    {
        // Create and upload new texture to graphics system
        IM_ASSERT(tex->TexID == ImTextureID_Invalid && tex->BackendUserData == nullptr);
        IM_ASSERT(tex->Format == ImTextureFormat_RGBA32);
        bd->UploadScratch.resize(tex->Width * tex->Height);
        for (int y = 0; y < tex->Height; y++)
            ImGui_ImplD2D_ConvertPixelRow(bd->UploadScratch.Data + y * tex->Width, (const unsigned int*)tex->GetPixelsAt(0, y), tex->Width);

        D2D1_BITMAP_PROPERTIES1 props = {};
        props.pixelFormat.format    = DXGI_FORMAT_B8G8R8A8_UNORM;
        props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
        ID2D1Bitmap1* bitmap = nullptr;
        bd->D2DDeviceContext->CreateBitmap(D2D1::SizeU((UINT32)tex->Width, (UINT32)tex->Height), bd->UploadScratch.Data, (UINT32)tex->Width * 4, &props, &bitmap);
        IM_ASSERT(bitmap != nullptr && "Backend failed to create texture!");

        // Store identifiers
        tex->SetTexID((ImTextureID)(intptr_t)bitmap);
        tex->SetStatus(ImTextureStatus_OK);
        tex->BackendUserData = bitmap;
    }
    else if (tex->Status == ImTextureStatus_WantUpdates)
    {
        // Update selected blocks. We only ever write to textures regions which have never been used before!
        // This backend choose to use tex->Updates[] but you can use tex->UpdateRect to upload a single region.
        ID2D1Bitmap1* bitmap = (ID2D1Bitmap1*)tex->BackendUserData;
        IM_ASSERT(bitmap == (ID2D1Bitmap1*)(intptr_t)tex->TexID);
        for (ImTextureRect& r : tex->Updates)
        {
            bd->UploadScratch.resize((int)r.w * (int)r.h);
            for (int y = 0; y < r.h; y++)
                ImGui_ImplD2D_ConvertPixelRow(bd->UploadScratch.Data + y * r.w, (const unsigned int*)tex->GetPixelsAt(r.x, r.y + y), r.w);
            const D2D1_RECT_U dst = { (UINT32)r.x, (UINT32)r.y, (UINT32)(r.x + r.w), (UINT32)(r.y + r.h) };
            bitmap->CopyFromMemory(&dst, bd->UploadScratch.Data, (UINT32)r.w * 4);
        }
        tex->SetStatus(ImTextureStatus_OK);
    }
    if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0)
        ImGui_ImplD2D_DestroyTexture(tex);
}

static bool ImGui_ImplD2D_CreateDeviceObjects()
{
    ImGui_ImplD2D_Data* bd = ImGui_ImplD2D_GetBackendData();
    if (bd->D2DDeviceContext == nullptr)
        return false;
    if (bd->SolidBrush == nullptr)
        if (bd->D2DDeviceContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &bd->SolidBrush) < 0)
            return false;
    return true;
}

static void ImGui_ImplD2D_InvalidateDeviceObjects()
{
    ImGui_ImplD2D_Data* bd = ImGui_ImplD2D_GetBackendData();
    if (bd->D2DDeviceContext == nullptr)
        return;

    // Destroy all textures
    for (ImTextureData* tex : ImGui::GetPlatformIO().Textures)
        if (tex->RefCount == 1)
            ImGui_ImplD2D_DestroyTexture(tex);

    if (bd->SolidBrush) { bd->SolidBrush->Release(); bd->SolidBrush = nullptr; }
}

static void ImGui_ImplD2D_SetupRenderState(ImDrawData* draw_data, ID2D1DeviceContext* ctx)
{
    // Draw in imgui coordinates: translate DisplayPos (top-left of the visible imgui space) to the target origin.
    ctx->SetTransform(D2D1::Matrix3x2F::Translation(-draw_data->DisplayPos.x, -draw_data->DisplayPos.y));
    ctx->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

static inline D2D1_COLOR_F ImGui_ImplD2D_ColorF(ImU32 col)
{
    return D2D1::ColorF(
        (float)((col >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f,
        (float)((col >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f,
        (float)((col >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f,
        (float)((col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f);
}

// Two triangles (a,b,c) + (a,c,d) over an axis-aligned position + uv rect with one color: the shape
// ImDrawList::PrimRect/PrimRectUV emits for glyphs, images and un-antialiased fills. Draw it natively.
static bool ImGui_ImplD2D_TryDrawQuad(ImGui_ImplD2D_Data* bd, ID2D1Bitmap1* bitmap, const ImDrawVert* vtx, const ImDrawIdx* idx)
{
    if (idx[3] != idx[0] || idx[4] != idx[2])
        return false;
    const ImDrawVert& v0 = vtx[idx[0]];
    const ImDrawVert& v1 = vtx[idx[1]];
    const ImDrawVert& v2 = vtx[idx[2]];
    const ImDrawVert& v3 = vtx[idx[5]];
    if (v0.col != v1.col || v0.col != v2.col || v0.col != v3.col)
        return false;
    if (v0.pos.y != v1.pos.y || v1.pos.x != v2.pos.x || v2.pos.y != v3.pos.y || v3.pos.x != v0.pos.x)
        return false;
    if (v0.uv.y != v1.uv.y || v1.uv.x != v2.uv.x || v2.uv.y != v3.uv.y || v3.uv.x != v0.uv.x)
        return false;

    ID2D1DeviceContext* ctx = bd->D2DDeviceContext;
    const D2D1_RECT_F dst = D2D1::RectF(Min(v0.pos.x, v2.pos.x), Min(v0.pos.y, v2.pos.y), Max(v0.pos.x, v2.pos.x), Max(v0.pos.y, v2.pos.y));
    if ((v0.uv.x == v2.uv.x && v0.uv.y == v2.uv.y) || bitmap == nullptr)
    {
        // Constant uv (the atlas white texel): a solid rectangle
        if ((v0.col & IM_COL32_A_MASK) == 0)
            return true;
        bd->SolidBrush->SetColor(ImGui_ImplD2D_ColorF(v0.col));
        ctx->FillRectangle(&dst, bd->SolidBrush);
        return true;
    }

    const D2D1_SIZE_U  tex_size = bitmap->GetPixelSize();
    const D2D1_RECT_F  src      = D2D1::RectF(
        Min(v0.uv.x, v2.uv.x) * (float)tex_size.width, Min(v0.uv.y, v2.uv.y) * (float)tex_size.height,
        Max(v0.uv.x, v2.uv.x) * (float)tex_size.width, Max(v0.uv.y, v2.uv.y) * (float)tex_size.height);
    if ((v0.col | IM_COL32_A_MASK) == IM_COL32_WHITE)
    {
        // White-tinted (possibly translucent) texture sample: the bitmap as-is
        ctx->DrawBitmap(bitmap, &dst, (float)((v0.col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &src);
    }
    else
    {
        // Tinted sample (glyphs): brush color through the bitmap's alpha. Exact for white-RGB atlas
        // textures; an approximation for arbitrary images. FillOpacityMask requires aliased mode.
        bd->SolidBrush->SetColor(ImGui_ImplD2D_ColorF(v0.col));
        ctx->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        ctx->FillOpacityMask(bitmap, bd->SolidBrush, &dst, &src);
        ctx->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    }
    return true;
}

// Everything the quad path rejects: flat-fill the triangle with the averaged vertex color
// (gradient approximation -- see the header's missing-features note). FIXME-OPT: one path geometry per triangle.
static void ImGui_ImplD2D_DrawTriangle(ImGui_ImplD2D_Data* bd, const ImDrawVert* vtx, const ImDrawIdx* idx)
{
    const ImDrawVert& v0 = vtx[idx[0]];
    const ImDrawVert& v1 = vtx[idx[1]];
    const ImDrawVert& v2 = vtx[idx[2]];
    const unsigned int a = (((v0.col >> IM_COL32_A_SHIFT) & 0xFF) + ((v1.col >> IM_COL32_A_SHIFT) & 0xFF) + ((v2.col >> IM_COL32_A_SHIFT) & 0xFF)) / 3;
    if (a == 0)
        return;
    const unsigned int r = (((v0.col >> IM_COL32_R_SHIFT) & 0xFF) + ((v1.col >> IM_COL32_R_SHIFT) & 0xFF) + ((v2.col >> IM_COL32_R_SHIFT) & 0xFF)) / 3;
    const unsigned int g = (((v0.col >> IM_COL32_G_SHIFT) & 0xFF) + ((v1.col >> IM_COL32_G_SHIFT) & 0xFF) + ((v2.col >> IM_COL32_G_SHIFT) & 0xFF)) / 3;
    const unsigned int b = (((v0.col >> IM_COL32_B_SHIFT) & 0xFF) + ((v1.col >> IM_COL32_B_SHIFT) & 0xFF) + ((v2.col >> IM_COL32_B_SHIFT) & 0xFF)) / 3;

    ID2D1PathGeometry* geometry = nullptr;
    if (bd->D2DFactory->CreatePathGeometry(&geometry) < 0)
        return;
    ID2D1GeometrySink* sink = nullptr;
    if (geometry->Open(&sink) >= 0)
    {
        sink->BeginFigure(D2D1::Point2F(v0.pos.x, v0.pos.y), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(v1.pos.x, v1.pos.y));
        sink->AddLine(D2D1::Point2F(v2.pos.x, v2.pos.y));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        sink->Release();
        bd->SolidBrush->SetColor(D2D1::ColorF((float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f, (float)a / 255.0f));
        bd->D2DDeviceContext->FillGeometry(geometry, bd->SolidBrush);
    }
    geometry->Release();
}

// Render function
void ImGui_ImplD2D_RenderDrawData(ImDrawData* draw_data)
{
    // Avoid rendering when minimized
    if (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f)
        return;

    ImGui_ImplD2D_Data* bd = ImGui_ImplD2D_GetBackendData();
    ID2D1DeviceContext* ctx = bd->D2DDeviceContext;

    // Catch up with texture updates. Most of the times, the list will have 1 element with an OK status, aka nothing to do.
    // (This almost always points to ImGui::GetPlatformIO().Textures[] but is part of ImDrawData to allow overriding or disabling texture updates).
    if (draw_data->Textures != nullptr)
        for (ImTextureData* tex : *draw_data->Textures)
            if (tex->Status != ImTextureStatus_OK)
                ImGui_ImplD2D_UpdateTexture(tex);

    // Backup the D2D state we modify (the pass bracket -- BeginDraw/EndDraw/target -- belongs to the caller)
    D2D1_MATRIX_3X2_F   old_transform;
    ctx->GetTransform(&old_transform);
    const D2D1_ANTIALIAS_MODE old_antialias_mode = ctx->GetAntialiasMode();

    // Setup desired D2D state
    ImGui_ImplD2D_SetupRenderState(draw_data, ctx);

    // Setup render state structure (for callbacks and custom texture bindings)
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    ImGui_ImplD2D_RenderState* render_state = &bd->RenderStateInstance;
    render_state->DeviceContext = ctx;
    platform_io.Renderer_RenderState = render_state;

    for (const ImDrawList* draw_list : draw_data->CmdLists)
    {
        for (int cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &draw_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != nullptr)
            {
                // User callback, registered via ImDrawList::AddCallback()
                if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                    ImGui_ImplD2D_SetupRenderState(draw_data, ctx);
                else
                    pcmd->UserCallback(draw_list, pcmd);
            }
            else
            {
                // Project scissor/clipping rectangles into framebuffer space
                ImVec2 clip_min(pcmd->ClipRect.x, pcmd->ClipRect.y);
                ImVec2 clip_max(pcmd->ClipRect.z, pcmd->ClipRect.w);
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                    continue;

                // Apply scissor/clipping rectangle (in imgui coordinates; the transform carries DisplayPos)
                ctx->PushAxisAlignedClip(D2D1::RectF(clip_min.x, clip_min.y, clip_max.x, clip_max.y), D2D1_ANTIALIAS_MODE_ALIASED);

                // Walk the triangle list, drawing quads natively and everything else as flat triangles
                ID2D1Bitmap1* bitmap = (ID2D1Bitmap1*)(intptr_t)pcmd->GetTexID();
                const ImDrawVert* vtx = draw_list->VtxBuffer.Data + pcmd->VtxOffset;
                const ImDrawIdx*  idx = draw_list->IdxBuffer.Data + pcmd->IdxOffset;
                for (unsigned int i = 0; i + 3 <= pcmd->ElemCount; )
                {
                    if (i + 6 <= pcmd->ElemCount && ImGui_ImplD2D_TryDrawQuad(bd, bitmap, vtx, idx + i))
                    {
                        i += 6;
                        continue;
                    }
                    ImGui_ImplD2D_DrawTriangle(bd, vtx, idx + i);
                    i += 3;
                }

                ctx->PopAxisAlignedClip();
            }
        }
    }
    platform_io.Renderer_RenderState = nullptr;

    // Restore modified D2D state
    ctx->SetAntialiasMode(old_antialias_mode);
    ctx->SetTransform(&old_transform);
}

bool ImGui_ImplD2D_Init(ID2D1DeviceContext* device_context)
{
    ImGuiIO& io = ImGui::GetIO();
    IMGUI_CHECKVERSION();
    IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");
    IM_ASSERT(device_context != nullptr);

    // Setup backend capabilities flags
    ImGui_ImplD2D_Data* bd = IM_NEW(ImGui_ImplD2D_Data)();
    io.BackendRendererUserData = (void*)bd;
    io.BackendRendererName = "imgui_impl_d2d";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;   // We can honor ImGuiPlatformIO::Textures[] requests during render.

    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    platform_io.Renderer_TextureMaxWidth = platform_io.Renderer_TextureMaxHeight = (int)device_context->GetMaximumBitmapSize();

    bd->D2DDeviceContext = device_context;
    bd->D2DDeviceContext->AddRef();
    bd->D2DDeviceContext->GetFactory(&bd->D2DFactory);
    return true;
}

void ImGui_ImplD2D_Shutdown()
{
    ImGui_ImplD2D_Data* bd = ImGui_ImplD2D_GetBackendData();
    IM_ASSERT(bd != nullptr && "No renderer backend to shutdown, or already shutdown?");
    ImGuiIO& io = ImGui::GetIO();
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();

    ImGui_ImplD2D_InvalidateDeviceObjects();
    if (bd->D2DFactory)       { bd->D2DFactory->Release(); }
    if (bd->D2DDeviceContext) { bd->D2DDeviceContext->Release(); }

    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
    platform_io.ClearRendererHandlers();
    IM_DELETE(bd);
}

void ImGui_ImplD2D_NewFrame()
{
    ImGui_ImplD2D_Data* bd = ImGui_ImplD2D_GetBackendData();
    IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call ImGui_ImplD2D_Init()?");

    if (!bd->SolidBrush)
        if (!ImGui_ImplD2D_CreateDeviceObjects())
            IM_ASSERT(0 && "ImGui_ImplD2D_CreateDeviceObjects() failed!");
}

#endif // #ifndef IMGUI_DISABLE
