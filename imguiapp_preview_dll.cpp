// dear imgui app, v0.5.0 WIP
// (DLL preview backend -- tool UI)

#ifndef IMGUIX_DISABLE_TOOLS   // TOOL (UI): compiled out in a lean build (Phase A4)

// DLL preview backend (F78; docs/designs.md (dll-preview-design)). Copy-marshalling: the preview DLL owns
// its entire runtime; the host only compiles/loads it and moves bytes across the C-ABI (emitted surface in
// AppPreviewModuleCodeGenerate) -- no shared context/allocator/pointer, link-agnostic. Build-baked paths
// (imguix/CMakeLists.txt file(GENERATE), Windows/MSVC only): IMGUIX_PREVIEW_CL_ARGS (include/define flags
// matching imguix) + IMGUIX_PREVIEW_LIBS (static libs the module links); absent -> disabled path (interpreter).
//
// Index of this file (search for "[SECTION]"):
// [SECTION] Module state + toolset discovery (vcvars64 probe)
// [SECTION] Compile / load / unload (cl /LD emit + LoadLibrary)
// [SECTION] Public API (create / destroy / tick / copy in-out / reload)
// [SECTION] Software rasterizer (barycentric coverage -> RGBA32)
// [SECTION] Disabled-path stubs (no toolset baked)

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imguiapp.h"
#ifndef IMGUI_DISABLE
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 26495) // [Static Analyzer] uninitialized member (type.6); memset ctors
#endif
#include "imguiapp_internal.h"

#if defined(__has_include)
#  if __has_include("imguiapp_preview_dll_config.h")
#    include "imguiapp_preview_dll_config.h"
#  endif
#endif

#if defined(_WIN32) && defined(IMGUIX_PREVIEW_LIBS) && defined(IMGUIX_PREVIEW_CL_ARGS)
#define IMGUIX_PREVIEW_DLL_ENABLED 1
#include <stdio.h>
#include <string.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#define IMGUIX_PREVIEW_DLL_ENABLED 0
#endif

//-----------------------------------------------------------------------------
// [SECTION] Module state + toolset discovery (vcvars64 probe)
//-----------------------------------------------------------------------------

struct ImGuiAppPreviewDll
{
#if IMGUIX_PREVIEW_DLL_ENABLED
    typedef unsigned int (*AbiFn)();
    typedef void*        (*CreateFn)(int, int);
    typedef void         (*DestroyFn)(void*);
    typedef void         (*TickFn)(void*, float);
    typedef int          (*CopyOutFn)(void*, const char*, int, void*, int);
    typedef int          (*CopyInFn)(void*, const char*, int, const void*, int);
    typedef void         (*SetSizeFn)(void*, int, int);
    typedef int          (*CopyAtlasFn)(void*, unsigned char*, int, int*, int*);
    typedef int          (*CopyDrawFn)(void*, void*, int, int*);

    HMODULE     Dll = nullptr;
    void*       Handle = nullptr;    // opaque instance owned by the module
    AbiFn       Abi = nullptr;
    CreateFn    Create = nullptr;
    DestroyFn   Destroy = nullptr;
    TickFn      Tick = nullptr;
    CopyOutFn   CopyOut = nullptr;
    CopyInFn    CopyIn = nullptr;
    SetSizeFn   SetSize = nullptr;
    CopyAtlasFn CopyAtlas = nullptr;
    CopyDrawFn  CopyDraw = nullptr;
    int         Counter = 0;         // monotonic DLL name -- never overwrite a mapped DLL
    char        ScratchDir[1024] = "";
    ImVector<unsigned char> AtlasBuf;   // reused RGBA32 font-atlas copy (frame render)
    int         AtlasW = 0;
    int         AtlasH = 0;
    ImVector<char> DrawBuf;             // reused draw-data blob (frame render)
    ImVector<ImDrawVert> VtxScratch;    // aligned per-list vertex copy out of the byte blob
    ImVector<ImDrawIdx>  IdxScratch;    // aligned per-list index copy out of the byte blob
#endif // #if IMGUIX_PREVIEW_DLL_ENABLED
};

namespace ImGui
{
#if IMGUIX_PREVIEW_DLL_ENABLED

// vcvars64.bat located once via vswhere, cached. Empty = search failed (no toolset).
static const char* AppPreviewDllVcvars()
{
    char (&s_vcvars)[1024] = AppState().PreviewVcvars;   // [0]==1 => not yet searched
    if (s_vcvars[0] != 1)
        return s_vcvars;
    s_vcvars[0] = 0;

    const char* pf86 = getenv("ProgramFiles(x86)");
    if (pf86 == nullptr)
        return s_vcvars;
    char vswhere[1024];
    ImFormatString(vswhere, IM_ARRAYSIZE(vswhere), "%s\\Microsoft Visual Studio\\Installer\\vswhere.exe", pf86);
    if (GetFileAttributesA(vswhere) == INVALID_FILE_ATTRIBUTES)
        return s_vcvars;

    char cmd[1200];
    ImFormatString(cmd, IM_ARRAYSIZE(cmd),
                   "\"%s\" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath",
                   vswhere);
    FILE* p = _popen(cmd, "r");
    if (p == nullptr)
        return s_vcvars;
    char install[1024] = "";
    if (fgets(install, IM_ARRAYSIZE(install), p) != nullptr)
    {
        size_t n = strlen(install);
        while (n > 0 && (install[n - 1] == '\n' || install[n - 1] == '\r'))
            install[--n] = 0;
    }
    _pclose(p);
    if (install[0] == 0)
        return s_vcvars;

    char cand[1024];
    ImFormatString(cand, IM_ARRAYSIZE(cand), "%s\\VC\\Auxiliary\\Build\\vcvars64.bat", install);
    if (GetFileAttributesA(cand) != INVALID_FILE_ATTRIBUTES)
        ImStrncpy(s_vcvars, cand, IM_ARRAYSIZE(s_vcvars));
    return s_vcvars;
}

//-----------------------------------------------------------------------------
// [SECTION] Compile / load / unload (cl /LD emit + LoadLibrary)
//-----------------------------------------------------------------------------

// Emit + compile the module into preview_<n>.dll (self-contained static link). dll path in out_dll on
// success; else "" with the compiler diagnostics tail in err.
static bool AppPreviewDllCompile(ImGuiAppPreviewDll* s, const ImGuiAppGraph* graph, char* out_dll, int out_dll_size, char* err, int err_size)
{
    const char* vcvars = AppPreviewDllVcvars();
    if (vcvars[0] == 0)
    {
        ImFormatString(err, err_size, "no MSVC toolset (vswhere/vcvars64 not found)");
        return false;
    }

    const int n = ++s->Counter;
    char cpp_path[1200];
    char dll_path[1200];
    char bat_path[1200];
    char log_path[1200];
    ImFormatString(cpp_path, IM_ARRAYSIZE(cpp_path), "%s\\preview_%d.cpp", s->ScratchDir, n);
    ImFormatString(dll_path, IM_ARRAYSIZE(dll_path), "%s\\preview_%d.dll", s->ScratchDir, n);
    ImFormatString(bat_path, IM_ARRAYSIZE(bat_path), "%s\\preview_%d.bat", s->ScratchDir, n);
    ImFormatString(log_path, IM_ARRAYSIZE(log_path), "%s\\preview_%d.log", s->ScratchDir, n);

    ImGuiTextBuffer module;
    AppPreviewModuleCodeGenerate(graph, &module);
    ImFileHandle f = ImFileOpen(cpp_path, "wb");
    if (f == nullptr)
    {
        ImFormatString(err, err_size, "cannot write %s", cpp_path);
        return false;
    }
    ImFileWrite(module.c_str(), 1, (ImU64)module.size(), f);
    ImFileClose(f);

    // Enter the toolset env, then a self-contained /LD compile. The module has its OWN imgui context, so nothing
    // crosses the boundary except copied bytes -- but it links the same imguix/imgui_te libs, so its dynamic CRT
    // MUST match the host build's (a /MDd host + /MD module fails with _ITERATOR_DEBUG_LEVEL mismatches). CRT from _DEBUG.
#ifdef _DEBUG
    const char* preview_crt = "/MDd /Od";
#else
    const char* preview_crt = "/MD /O2";
#endif
    ImFileHandle b = ImFileOpen(bat_path, "wb");
    if (b == nullptr)
    {
        ImFormatString(err, err_size, "cannot write %s", bat_path);
        return false;
    }
    ImFilePrintf(b, "@echo off\r\n");
    ImFilePrintf(b, "call \"%s\" >nul\r\n", vcvars);
    ImFilePrintf(b, "cl /nologo /LD /std:c++20 %s /EHsc %s \"%s\" /Fe:\"%s\" /link /OPT:REF %s > \"%s\" 2>&1\r\n",
                 preview_crt, IMGUIX_PREVIEW_CL_ARGS, cpp_path, dll_path, IMGUIX_PREVIEW_LIBS, log_path);
    ImFileClose(b);

    // The batch redirects cl's output to the log itself, so system() just runs it (absolute paths inside).
    char run[1400];
    ImFormatString(run, IM_ARRAYSIZE(run), "\"%s\"", bat_path);
    const int rc = system(run);

    if (rc != 0 || GetFileAttributesA(dll_path) == INVALID_FILE_ATTRIBUTES)
    {
        err[0] = 0;
        ImFileHandle lg = ImFileOpen(log_path, "rb");
        if (lg != nullptr)
        {
            fseek(lg, 0, SEEK_END);
            long sz = ftell(lg);
            long from = sz > (long)err_size - 1 ? sz - (err_size - 1) : 0;
            fseek(lg, from, SEEK_SET);
            size_t rd = (size_t)ImFileRead(err, 1, (ImU64)(err_size - 1), lg);
            err[rd] = 0;
            ImFileClose(lg);
        }
        if (err[0] == 0)
            ImFormatString(err, err_size, "compile failed (rc=%d), no diagnostics", rc);
        return false;
    }

    ImStrncpy(out_dll, dll_path, out_dll_size);
    return true;
}

static bool AppPreviewDllLoadCreate(ImGuiAppPreviewDll* s, const char* dll_path, char* err, int err_size)
{
    HMODULE m = LoadLibraryA(dll_path);
    if (m == nullptr)
    {
        ImFormatString(err, err_size, "LoadLibrary failed (err=%lu) for %s", (unsigned long)GetLastError(), dll_path);
        return false;
    }
    ImGuiAppPreviewDll::AbiFn     abi     = (ImGuiAppPreviewDll::AbiFn)    GetProcAddress(m, "ImGuiAppPreview_ABI");
    ImGuiAppPreviewDll::CreateFn  create  = (ImGuiAppPreviewDll::CreateFn) GetProcAddress(m, "ImGuiAppPreview_Create");
    ImGuiAppPreviewDll::DestroyFn destroy = (ImGuiAppPreviewDll::DestroyFn)GetProcAddress(m, "ImGuiAppPreview_Destroy");
    ImGuiAppPreviewDll::TickFn    tick    = (ImGuiAppPreviewDll::TickFn)   GetProcAddress(m, "ImGuiAppPreview_Tick");
    ImGuiAppPreviewDll::CopyOutFn cout    = (ImGuiAppPreviewDll::CopyOutFn)GetProcAddress(m, "ImGuiAppPreview_CopyOut");
    ImGuiAppPreviewDll::CopyInFn  cin     = (ImGuiAppPreviewDll::CopyInFn) GetProcAddress(m, "ImGuiAppPreview_CopyIn");
    ImGuiAppPreviewDll::SetSizeFn   ssz   = (ImGuiAppPreviewDll::SetSizeFn)  GetProcAddress(m, "ImGuiAppPreview_SetDisplaySize");
    ImGuiAppPreviewDll::CopyAtlasFn catl  = (ImGuiAppPreviewDll::CopyAtlasFn)GetProcAddress(m, "ImGuiAppPreview_CopyFontAtlas");
    ImGuiAppPreviewDll::CopyDrawFn  cdraw = (ImGuiAppPreviewDll::CopyDrawFn) GetProcAddress(m, "ImGuiAppPreview_CopyDrawData");
    if (abi == nullptr || create == nullptr || destroy == nullptr || tick == nullptr || cout == nullptr || cin == nullptr
     || ssz == nullptr || catl == nullptr || cdraw == nullptr)
    {
        FreeLibrary(m);
        ImFormatString(err, err_size, "preview module missing a C-ABI export");
        return false;
    }
    const unsigned int module_abi = abi();
    if (module_abi != IMGUIAPP_PREVIEW_ABI)
    {
        FreeLibrary(m);
        ImFormatString(err, err_size, "ABI mismatch: module %u, host %u (stale headers / wrong toolset)", module_abi, (unsigned)IMGUIAPP_PREVIEW_ABI);
        return false;
    }
    void* handle = create(1280, 720);
    if (handle == nullptr)
    {
        FreeLibrary(m);
        ImFormatString(err, err_size, "ImGuiAppPreview_Create returned null");
        return false;
    }
    s->Dll = m;
    s->Handle = handle;
    s->Abi = abi;
    s->Create = create;
    s->Destroy = destroy;
    s->Tick = tick;
    s->CopyOut = cout;
    s->CopyIn = cin;
    s->SetSize = ssz;
    s->CopyAtlas = catl;
    s->CopyDraw = cdraw;
    return true;
}

// Destroy the instance (its vtables live in the DLL) BEFORE unloading, then FreeLibrary.
static void AppPreviewDllUnload(ImGuiAppPreviewDll* s)
{
    if (s->Handle != nullptr && s->Destroy != nullptr)
        s->Destroy(s->Handle);
    s->Handle = nullptr;
    if (s->Dll != nullptr)
        FreeLibrary(s->Dll);
    s->Dll = nullptr;
}

//-----------------------------------------------------------------------------
// [SECTION] Public API (create / destroy / tick / copy in-out / reload)
//-----------------------------------------------------------------------------

bool AppPreviewDllIsToolsetAvailable()
{
    return AppPreviewDllVcvars()[0] != 0;
}

ImGuiAppPreviewDll* AppPreviewDllCreate(const ImGuiAppGraph* graph, const char* scratch_dir, char* err, int err_size)
{
    IM_ASSERT(graph != nullptr);
    if (err != nullptr && err_size > 0)
        err[0] = 0;
    if (!AppPreviewDllIsToolsetAvailable())
    {
        if (err != nullptr) ImFormatString(err, err_size, "no MSVC toolset -- interpreter fallback");
        return nullptr;
    }
    ImGuiAppPreviewDll* s = IM_NEW(ImGuiAppPreviewDll)();
    // Resolve to an absolute path -- vcvars64.bat may change the working directory, so every path the compile
    // batch names (cpp / dll / log) must be absolute.
    const char* want = scratch_dir != nullptr ? scratch_dir : ".";
    char abs_scratch[1024];
    if (GetFullPathNameA(want, IM_ARRAYSIZE(abs_scratch), abs_scratch, nullptr) == 0)
        ImStrncpy(abs_scratch, want, IM_ARRAYSIZE(abs_scratch));
    ImStrncpy(s->ScratchDir, abs_scratch, IM_ARRAYSIZE(s->ScratchDir));
    CreateDirectoryA(s->ScratchDir, nullptr);

    char dll_path[1200] = "";
    char cerr[2048] = "";
    if (!AppPreviewDllCompile(s, graph, dll_path, IM_ARRAYSIZE(dll_path), cerr, IM_ARRAYSIZE(cerr))
     || !AppPreviewDllLoadCreate(s, dll_path, cerr, IM_ARRAYSIZE(cerr)))
    {
        if (err != nullptr) ImStrncpy(err, cerr, err_size);
        IM_DELETE(s);
        return nullptr;
    }
    return s;
}

void AppPreviewDllDestroy(ImGuiAppPreviewDll* s)
{
    if (s == nullptr)
        return;
    AppPreviewDllUnload(s);
    IM_DELETE(s);
}

void AppPreviewDllTick(ImGuiAppPreviewDll* s, float dt)
{
    if (s != nullptr && s->Tick != nullptr && s->Handle != nullptr)
        s->Tick(s->Handle, dt);
}

int AppPreviewDllCopyOut(ImGuiAppPreviewDll* s, const char* label, bool temp, void* out, int cap)
{
    if (s == nullptr || s->CopyOut == nullptr || s->Handle == nullptr)
        return 0;
    return s->CopyOut(s->Handle, label, temp ? 1 : 0, out, cap);
}

int AppPreviewDllCopyIn(ImGuiAppPreviewDll* s, const char* label, bool temp, const void* in, int size)
{
    if (s == nullptr || s->CopyIn == nullptr || s->Handle == nullptr)
        return 0;
    return s->CopyIn(s->Handle, label, temp ? 1 : 0, in, size);
}

bool AppPreviewDllReload(ImGuiAppPreviewDll* s, const ImGuiAppGraph* graph, char* err, int err_size)
{
    IM_ASSERT(s != nullptr && graph != nullptr);
    if (err != nullptr && err_size > 0)
        err[0] = 0;

    // Compile first; a failure keeps the running instance untouched (design 5).
    char dll_path[1200] = "";
    char cerr[2048] = "";
    if (!AppPreviewDllCompile(s, graph, dll_path, IM_ARRAYSIZE(dll_path), cerr, IM_ARRAYSIZE(cerr)))
    {
        if (err != nullptr) ImStrncpy(err, cerr, err_size);
        return false;
    }

    // Preserve every control's Persist bytes across the swap: copied OUT keyed by label, then back IN.
    // ImGuiAppSavedControl is POD (fixed buffer, not a nested ImVector) because ImVector::push_back memcpys its
    // element -- a heap-owning member would alias then double-free / dangle (the F58 nested-ImVector use-after-free).
    struct ImGuiAppSavedControl
    {
        char Label[IM_LABEL_SIZE];
        char Bytes[1024];
        int  Len;
    };
    ImVector<ImGuiAppSavedControl> saved;
    for (int i = 0; i < graph->Nodes.Size; i++)
    {
        const ImGuiAppNode* nd = &graph->Nodes.Data[i];
        if (!ImAppNodeKindIsData(nd->Kind) || nd->IsLive)
            continue;
        ImGuiAppSavedControl sc;
        ImStrncpy(sc.Label, nd->Draft.Name, IM_ARRAYSIZE(sc.Label));
        sc.Len = s->CopyOut(s->Handle, sc.Label, 0, sc.Bytes, (int)sizeof(sc.Bytes));
        if (sc.Len > 0)
            saved.push_back(sc);
    }

    AppPreviewDllUnload(s);
    if (!AppPreviewDllLoadCreate(s, dll_path, cerr, IM_ARRAYSIZE(cerr)))
    {
        if (err != nullptr) ImStrncpy(err, cerr, err_size);
        return false;
    }
    for (int i = 0; i < saved.Size; i++)
        s->CopyIn(s->Handle, saved[i].Label, 0, saved[i].Bytes, saved[i].Len);
    return true;
}

void AppPreviewDllSetDisplaySize(ImGuiAppPreviewDll* s, int w, int h)
{
    if (s != nullptr && s->SetSize != nullptr && s->Handle != nullptr)
        s->SetSize(s->Handle, w, h);
}

//-----------------------------------------------------------------------------
// [SECTION] Software rasterizer (barycentric coverage -> RGBA32)
//-----------------------------------------------------------------------------

// Rasterize one imgui triangle into a w*h RGBA32 buffer: barycentric coverage (either winding), nearest
// atlas sample, per-vertex color interpolation, alpha-over compositing. Buffer-space vertices; bbox clamped
// to the command's clip rect. Preview fidelity, not a pixel-exact backend (no MSAA / gamma).
static void RasterTri(unsigned char* dst, int w, int h,
                      const ImDrawVert* a, const ImDrawVert* b, const ImDrawVert* c,
                      const unsigned char* atlas, int aw, int ah,
                      int clip_x0, int clip_y0, int clip_x1, int clip_y1)
{
    const float area = (b->pos.x - a->pos.x) * (c->pos.y - a->pos.y) - (b->pos.y - a->pos.y) * (c->pos.x - a->pos.x);
    if (area == 0.0f)
        return;
    const float inv_area = 1.0f / area;

    int x0 = (int)ImFloor(ImMin(a->pos.x, ImMin(b->pos.x, c->pos.x)));
    int y0 = (int)ImFloor(ImMin(a->pos.y, ImMin(b->pos.y, c->pos.y)));
    int x1 = (int)ImCeil(ImMax(a->pos.x, ImMax(b->pos.x, c->pos.x)));
    int y1 = (int)ImCeil(ImMax(a->pos.y, ImMax(b->pos.y, c->pos.y)));
    if (x0 < clip_x0) x0 = clip_x0;
    if (y0 < clip_y0) y0 = clip_y0;
    if (x1 > clip_x1) x1 = clip_x1;
    if (y1 > clip_y1) y1 = clip_y1;

    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            const float px = (float)x + 0.5f;
            const float py = (float)y + 0.5f;
            float w0 = (b->pos.x - px) * (c->pos.y - py) - (b->pos.y - py) * (c->pos.x - px);
            float w1 = (c->pos.x - px) * (a->pos.y - py) - (c->pos.y - py) * (a->pos.x - px);
            float w2 = area - w0 - w1;
            w0 *= inv_area;
            w1 *= inv_area;
            w2 *= inv_area;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
                continue;

            const float u = w0 * a->uv.x + w1 * b->uv.x + w2 * c->uv.x;
            const float v = w0 * a->uv.y + w1 * b->uv.y + w2 * c->uv.y;
            float tr = 1.0f, tg = 1.0f, tb = 1.0f, ta = 1.0f;
            if (atlas != nullptr && aw > 0 && ah > 0)
            {
                int tx = (int)(u * (float)aw);
                int ty = (int)(v * (float)ah);
                tx = tx < 0 ? 0 : (tx >= aw ? aw - 1 : tx);
                ty = ty < 0 ? 0 : (ty >= ah ? ah - 1 : ty);
                const unsigned char* t = atlas + ((size_t)ty * aw + tx) * 4;
                tr = t[0] / 255.0f; tg = t[1] / 255.0f; tb = t[2] / 255.0f; ta = t[3] / 255.0f;
            }

            const ImU32 ca = a->col, cb = b->col, cc2 = c->col;
            const float cr = (w0 * ((ca >> IM_COL32_R_SHIFT) & 0xFF) + w1 * ((cb >> IM_COL32_R_SHIFT) & 0xFF) + w2 * ((cc2 >> IM_COL32_R_SHIFT) & 0xFF)) / 255.0f * tr;
            const float cg = (w0 * ((ca >> IM_COL32_G_SHIFT) & 0xFF) + w1 * ((cb >> IM_COL32_G_SHIFT) & 0xFF) + w2 * ((cc2 >> IM_COL32_G_SHIFT) & 0xFF)) / 255.0f * tg;
            const float cbl = (w0 * ((ca >> IM_COL32_B_SHIFT) & 0xFF) + w1 * ((cb >> IM_COL32_B_SHIFT) & 0xFF) + w2 * ((cc2 >> IM_COL32_B_SHIFT) & 0xFF)) / 255.0f * tb;
            const float src_a = (w0 * ((ca >> IM_COL32_A_SHIFT) & 0xFF) + w1 * ((cb >> IM_COL32_A_SHIFT) & 0xFF) + w2 * ((cc2 >> IM_COL32_A_SHIFT) & 0xFF)) / 255.0f * ta;
            if (src_a <= 0.0f)
                continue;

            unsigned char* d = dst + ((size_t)y * w + x) * 4;
            const float inv = 1.0f - src_a;
            d[0] = (unsigned char)(cr * 255.0f * src_a + d[0] * inv + 0.5f);
            d[1] = (unsigned char)(cg * 255.0f * src_a + d[1] * inv + 0.5f);
            d[2] = (unsigned char)(cbl * 255.0f * src_a + d[2] * inv + 0.5f);
            d[3] = (unsigned char)(src_a * 255.0f + d[3] * inv + 0.5f);
        }
}

bool AppPreviewDllRasterizeFrame(ImGuiAppPreviewDll* s, int w, int h, unsigned int clear_col, ImVector<unsigned char>* out_rgba)
{
    if (out_rgba == nullptr || w <= 0 || h <= 0)
        return false;
    out_rgba->resize(w * h * 4);
    // Fill with the clear color (channels laid out to match the RGBA32 texture upload path).
    unsigned char* dst = out_rgba->Data;
    const unsigned char c0 = (unsigned char)((clear_col >> IM_COL32_R_SHIFT) & 0xFF);
    const unsigned char c1 = (unsigned char)((clear_col >> IM_COL32_G_SHIFT) & 0xFF);
    const unsigned char c2 = (unsigned char)((clear_col >> IM_COL32_B_SHIFT) & 0xFF);
    const unsigned char c3 = (unsigned char)((clear_col >> IM_COL32_A_SHIFT) & 0xFF);
    for (int i = 0; i < w * h; i++)
    {
        dst[i * 4 + 0] = c0;
        dst[i * 4 + 1] = c1;
        dst[i * 4 + 2] = c2;
        dst[i * 4 + 3] = c3;
    }
    if (s == nullptr || s->CopyDraw == nullptr || s->CopyAtlas == nullptr || s->Handle == nullptr)
        return false;

    // Font atlas across the boundary (copied once per size; the preview keeps a stable atlas).
    int aw = 0, ah = 0;
    s->CopyAtlas(s->Handle, nullptr, 0, &aw, &ah);
    if (aw > 0 && ah > 0 && (s->AtlasW != aw || s->AtlasH != ah || s->AtlasBuf.Size < aw * ah * 4))
    {
        s->AtlasBuf.resize(aw * ah * 4);
        if (s->CopyAtlas(s->Handle, s->AtlasBuf.Data, s->AtlasBuf.Size, &aw, &ah) > 0)
        {
            s->AtlasW = aw;
            s->AtlasH = ah;
        }
    }

    // Draw-data blob across the boundary (two-pass: size, then fill).
    int need = 0;
    s->CopyDraw(s->Handle, nullptr, 0, &need);
    if (need <= 32)
        return false;   // header-only == no geometry
    if (s->DrawBuf.Size < need)
        s->DrawBuf.resize(need);
    const int wrote = s->CopyDraw(s->Handle, s->DrawBuf.Data, s->DrawBuf.Size, &need);
    if (wrote < 32)
        return false;

    const char* p = s->DrawBuf.Data;
    const char* end = p + wrote;
    int magic = 0;
    memcpy(&magic, p, 4); p += 4;
    if (magic != 0x494D4444)
        return false;
    float hdr[4];
    memcpy(hdr, p, 16); p += 16;
    const float disp_x = hdr[0];
    const float disp_y = hdr[1];
    int lists = 0, totv = 0, toti = 0;
    memcpy(&lists, p, 4); p += 4;
    memcpy(&totv, p, 4); p += 4;
    memcpy(&toti, p, 4); p += 4;

    const unsigned char* atlas = s->AtlasBuf.Size > 0 ? s->AtlasBuf.Data : nullptr;
    bool any = false;
    for (int i = 0; i < lists && p < end; i++)
    {
        int vc = 0, ic = 0, cc = 0;
        memcpy(&vc, p, 4); p += 4;
        memcpy(&ic, p, 4); p += 4;
        memcpy(&cc, p, 4); p += 4;
        // Copy into aligned scratch: the blob packs lists back-to-back, so a list's vertex block can land at a
        // 2-byte (not 4-byte) offset -- reading ImDrawVert straight from there would be misaligned.
        s->VtxScratch.resize(vc);
        if (vc > 0) memcpy(s->VtxScratch.Data, p, (size_t)vc * sizeof(ImDrawVert));
        p += (size_t)vc * sizeof(ImDrawVert);
        s->IdxScratch.resize(ic);
        if (ic > 0) memcpy(s->IdxScratch.Data, p, (size_t)ic * sizeof(ImDrawIdx));
        p += (size_t)ic * sizeof(ImDrawIdx);
        const ImDrawVert* verts = s->VtxScratch.Data;
        const ImDrawIdx* idx = s->IdxScratch.Data;
        for (int c = 0; c < cc; c++)
        {
            float cr[4];
            memcpy(cr, p, 16); p += 16;
            unsigned int vo = 0, io = 0, ec = 0;
            memcpy(&vo, p, 4); p += 4;
            memcpy(&io, p, 4); p += 4;
            memcpy(&ec, p, 4); p += 4;
            if (ec == 0)
                continue;
            int cx0 = (int)ImFloor(cr[0] - disp_x);
            int cy0 = (int)ImFloor(cr[1] - disp_y);
            int cx1 = (int)ImCeil(cr[2] - disp_x);
            int cy1 = (int)ImCeil(cr[3] - disp_y);
            if (cx0 < 0) cx0 = 0;
            if (cy0 < 0) cy0 = 0;
            if (cx1 > w) cx1 = w;
            if (cy1 > h) cy1 = h;
            if (cx1 <= cx0 || cy1 <= cy0)
                continue;
            for (unsigned int e = 0; e + 3 <= ec; e += 3)
            {
                const unsigned int i0 = vo + idx[io + e + 0];
                const unsigned int i1 = vo + idx[io + e + 1];
                const unsigned int i2 = vo + idx[io + e + 2];
                if ((int)i0 >= vc || (int)i1 >= vc || (int)i2 >= vc)
                    continue;
                ImDrawVert a = verts[i0];
                ImDrawVert b = verts[i1];
                ImDrawVert cc3 = verts[i2];
                a.pos.x -= disp_x; a.pos.y -= disp_y;
                b.pos.x -= disp_x; b.pos.y -= disp_y;
                cc3.pos.x -= disp_x; cc3.pos.y -= disp_y;
                RasterTri(dst, w, h, &a, &b, &cc3, atlas, s->AtlasW, s->AtlasH, cx0, cy0, cx1, cy1);
                any = true;
            }
        }
    }
    return any;
}

#else   // !IMGUIX_PREVIEW_DLL_ENABLED -- no toolset baked in; the composer uses the interpreter.

//-----------------------------------------------------------------------------
// [SECTION] Disabled-path stubs (no toolset baked)
//-----------------------------------------------------------------------------

bool                AppPreviewDllIsToolsetAvailable() { return false; }
ImGuiAppPreviewDll* AppPreviewDllCreate(const ImGuiAppGraph*, const char*, char* err, int err_size)
{
    if (err != nullptr && err_size > 0)
        ImFormatString(err, err_size, "DLL preview not built (no toolset) -- interpreter fallback");
    return nullptr;
}
void                AppPreviewDllDestroy(ImGuiAppPreviewDll*) {}
void                AppPreviewDllTick(ImGuiAppPreviewDll*, float) {}
int                 AppPreviewDllCopyOut(ImGuiAppPreviewDll*, const char*, bool, void*, int) { return 0; }
int                 AppPreviewDllCopyIn(ImGuiAppPreviewDll*, const char*, bool, const void*, int) { return 0; }
bool                AppPreviewDllReload(ImGuiAppPreviewDll*, const ImGuiAppGraph*, char* err, int err_size)
{
    if (err != nullptr && err_size > 0)
        ImFormatString(err, err_size, "DLL preview not built");
    return false;
}
void                AppPreviewDllSetDisplaySize(ImGuiAppPreviewDll*, int, int) {}
bool                AppPreviewDllRasterizeFrame(ImGuiAppPreviewDll*, int, int, unsigned int, ImVector<unsigned char>*) { return false; }

#endif // #if IMGUIX_PREVIEW_DLL_ENABLED
} // namespace ImGui

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#endif // #ifndef IMGUI_DISABLE
#endif // IMGUIX_DISABLE_TOOLS
