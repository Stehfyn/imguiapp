// DLL preview backend implementation (F78; docs/dll-preview-design.md). Copy-marshalling: the preview DLL
// owns its entire runtime; the host only compiles/loads it and moves bytes across the C-ABI (see the emitted
// surface in GenerateAppPreviewModuleCode). No shared context/allocator/pointer -> link-agnostic.
//
// Build-baked paths (imguix/CMakeLists.txt file(GENERATE), Windows/MSVC only):
//   IMGUIX_PREVIEW_CL_ARGS -- include-dir + define flags matching how imguix itself compiles
//   IMGUIX_PREVIEW_LIBS    -- the static libs the self-contained module links (imguix/imgui/imgui_te + deps)
// Absent (non-MSVC / web / unconfigured) -> the disabled path; the composer uses the interpreter.

#include "imguiapp_preview_dll.h"
#include "imguiapp.h"

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

struct ImGuiAppPreviewDll
{
#if IMGUIX_PREVIEW_DLL_ENABLED
  typedef unsigned int (*AbiFn)();
  typedef void*        (*CreateFn)(int, int);
  typedef void         (*DestroyFn)(void*);
  typedef void         (*TickFn)(void*, float);
  typedef int          (*CopyOutFn)(void*, const char*, int, void*, int);
  typedef int          (*CopyInFn)(void*, const char*, int, const void*, int);

  HMODULE   Dll = nullptr;
  void*     Handle = nullptr;    // opaque instance owned by the module
  AbiFn     Abi = nullptr;
  CreateFn  Create = nullptr;
  DestroyFn Destroy = nullptr;
  TickFn    Tick = nullptr;
  CopyOutFn CopyOut = nullptr;
  CopyInFn  CopyIn = nullptr;
  int       Counter = 0;         // monotonic DLL name -- never overwrite a mapped DLL
  char      ScratchDir[1024] = "";
#endif
};

namespace ImGui
{
#if IMGUIX_PREVIEW_DLL_ENABLED

  // vcvars64.bat located once via vswhere, cached. Empty = search failed (no toolset).
  static const char* AppPreviewDllVcvars()
  {
    static char s_vcvars[1024] = { 1 };   // [0]==1 => not yet searched
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
    GenerateAppPreviewModuleCode(graph, &module);
    FILE* f = fopen(cpp_path, "wb");
    if (f == nullptr)
    {
      ImFormatString(err, err_size, "cannot write %s", cpp_path);
      return false;
    }
    fwrite(module.c_str(), 1, (size_t)module.size(), f);
    fclose(f);

    // Enter the toolset env, then a self-contained /LD compile. /MD keeps a normal dynamic CRT; the module
    // has its OWN imgui context, so nothing crosses the boundary except copied bytes.
    FILE* b = fopen(bat_path, "wb");
    if (b == nullptr)
    {
      ImFormatString(err, err_size, "cannot write %s", bat_path);
      return false;
    }
    fprintf(b, "@echo off\r\n");
    fprintf(b, "call \"%s\" >nul\r\n", vcvars);
    fprintf(b, "cl /nologo /LD /std:c++20 /O2 /MD /EHsc %s \"%s\" /Fe:\"%s\" /link /OPT:REF %s > \"%s\" 2>&1\r\n",
            IMGUIX_PREVIEW_CL_ARGS, cpp_path, dll_path, IMGUIX_PREVIEW_LIBS, log_path);
    fclose(b);

    // The batch redirects cl's output to the log itself, so system() just runs it (absolute paths inside).
    char run[1400];
    ImFormatString(run, IM_ARRAYSIZE(run), "\"%s\"", bat_path);
    const int rc = system(run);

    if (rc != 0 || GetFileAttributesA(dll_path) == INVALID_FILE_ATTRIBUTES)
    {
      err[0] = 0;
      FILE* lg = fopen(log_path, "rb");
      if (lg != nullptr)
      {
        fseek(lg, 0, SEEK_END);
        long sz = ftell(lg);
        long from = sz > (long)err_size - 1 ? sz - (err_size - 1) : 0;
        fseek(lg, from, SEEK_SET);
        size_t rd = fread(err, 1, (size_t)(err_size - 1), lg);
        err[rd] = 0;
        fclose(lg);
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
    if (abi == nullptr || create == nullptr || destroy == nullptr || tick == nullptr || cout == nullptr || cin == nullptr)
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

  bool AppPreviewDllToolsetAvailable()
  {
    return AppPreviewDllVcvars()[0] != 0;
  }

  ImGuiAppPreviewDll* AppPreviewDllCreate(const ImGuiAppGraph* graph, const char* scratch_dir, char* err, int err_size)
  {
    IM_ASSERT(graph != nullptr);
    if (err != nullptr && err_size > 0)
      err[0] = 0;
    if (!AppPreviewDllToolsetAvailable())
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

    // Preserve every control's Persist bytes by copying them OUT of the old instance, keyed by label, then
    // back IN after the swap. SavedControl is POD (a fixed buffer, not a nested ImVector) because
    // ImVector::push_back memcpys its element -- a heap-owning member would alias then double-free / dangle
    // when the local dies (the F58 nested-ImVector use-after-free). New/removed controls have no counterpart.
    struct SavedControl
    {
      char Label[IM_LABEL_SIZE];
      char Bytes[1024];
      int  Len;
    };
    ImVector<SavedControl> saved;
    for (int i = 0; i < graph->Nodes.Size; i++)
    {
      const ImGuiAppNode* nd = &graph->Nodes.Data[i];
      if (nd->Kind != ImGuiAppNodeKind_Control || nd->IsLive)
        continue;
      SavedControl sc;
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

#else   // !IMGUIX_PREVIEW_DLL_ENABLED -- no toolset baked in; the composer uses the interpreter.

  bool                AppPreviewDllToolsetAvailable() { return false; }
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

#endif
}
