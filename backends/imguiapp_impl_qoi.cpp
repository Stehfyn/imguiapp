// ImGuiAppAV encoder backend: QOI image sequence (imguiapp_impl_qoi.h).
// OutputPath is a directory: frames land as <dir>/NNNNNN.qoi with index.tsv carrying
// each frame's FrameID (file, frame_index, time_sec, tsc) -- realtime PTS are exact
// by construction, so SupportsRealtimePts is true for every timing mode.

// CHANGELOG
//  2026-07-08: Backend-internal symbols prefixed ImGuiApp_ImplQoi_* (house backend grammar); IMGUI_DISABLE guards added.

#include "imguiapp_impl_qoi.h"
#ifndef IMGUI_DISABLE
#include "imguiapp_internal.h"
#include "imgui_internal.h"   // ImFileOpen/ImFileWrite/ImFileClose, ImFormatString

#include <cstdio>
#include <cstring>
#include <cerrno>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

struct ImGuiApp_ImplQoi_Data
{
    char           Dir[512];
    ImFileHandle   Index;
    int            FrameCounter;
    int            Width;   // 0 until fixed (config, else first frame)
    int            Height;
    ImVector<char> Encoded; // reused per frame

    ImGuiApp_ImplQoi_Data()
    {
        Dir[0]       = 0;
        Index        = nullptr;
        FrameCounter = 0;
        Width        = 0;
        Height       = 0;
    }
};

static bool ImGuiApp_ImplQoi_MkdirOne(const char* path)
{
#ifdef _WIN32
    const int rc = _mkdir(path);
#else
    const int rc = mkdir(path, 0755);
#endif
    return rc == 0 || errno == EEXIST;
}

// Create every missing directory on the path (separators '/' or '\').
static bool ImGuiApp_ImplQoi_MkdirRecursive(const char* path)
{
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", path);
    for (char* c = buf + 1; *c; c++)
    {
        if (*c != '/' && *c != '\\')
            continue;
        const char sep = *c;
        *c = 0;
        if (buf[0] != 0 && buf[strlen(buf) - 1] != ':' && !ImGuiApp_ImplQoi_MkdirOne(buf))
            return false;
        *c = sep;
    }
    return ImGuiApp_ImplQoi_MkdirOne(buf);
}

static bool ImGuiApp_ImplQoi_Open(ImGuiAppAVEncoder* self, const ImGuiAppAVEncodeConfig* config)
{
    ImGuiApp_ImplQoi_Data* bd = (ImGuiApp_ImplQoi_Data*)self->UserData;
    if (bd == nullptr || config == nullptr || config->OutputPath == nullptr || bd->Index != nullptr)
        return false;

    if (!ImGuiApp_ImplQoi_MkdirRecursive(config->OutputPath))
        return false;

    snprintf(bd->Dir, sizeof(bd->Dir), "%s", config->OutputPath);
    bd->Width = config->Width;
    bd->Height = config->Height;
    bd->FrameCounter = 0;

    char index_path[560];
    snprintf(index_path, sizeof(index_path), "%s/index.tsv", bd->Dir);
    bd->Index = fopen(index_path, "w");
    if (bd->Index == nullptr)
        return false;
    fprintf(bd->Index, "file\tframe_index\ttime_sec\ttsc\n");
    return true;
}

static bool ImGuiApp_ImplQoi_WriteFrame(ImGuiAppAVEncoder* self, const ImGuiAppAVFrame* frame)
{
    ImGuiApp_ImplQoi_Data* bd = (ImGuiApp_ImplQoi_Data*)self->UserData;
    if (bd == nullptr || frame == nullptr || frame->Pixels == nullptr || bd->Index == nullptr)
        return false;

    // First frame fixes the size (when the config left it 0); any later mismatch aborts.
    if (bd->Width == 0 && bd->Height == 0)
    {
        bd->Width = frame->Width;
        bd->Height = frame->Height;
    }
    if (frame->Width != bd->Width || frame->Height != bd->Height || bd->Width <= 0 || bd->Height <= 0)
        return false;

    if (!ImGui::AppAVImageEncode(frame->Pixels, frame->Width, frame->Height, frame->PitchBytes, &bd->Encoded))
        return false;

    char frame_path[560];
    snprintf(frame_path, sizeof(frame_path), "%s/%06d.qoi", bd->Dir, bd->FrameCounter);
    FILE* f = fopen(frame_path, "wb");
    if (f == nullptr)
        return false;
    const bool wrote = fwrite(bd->Encoded.Data, 1, (size_t)bd->Encoded.Size, f) == (size_t)bd->Encoded.Size;
    fclose(f);
    if (!wrote)
        return false;

    fprintf(bd->Index, "%06d.qoi\t%llu\t%.9f\t%llu\n",
            bd->FrameCounter,
            (unsigned long long)frame->FrameID.FrameIndex,
            frame->FrameID.TimeSec,
            (unsigned long long)frame->FrameID.Tsc);
    bd->FrameCounter++;
    return true;
}

static void ImGuiApp_ImplQoi_Close(ImGuiAppAVEncoder* self)
{
    ImGuiApp_ImplQoi_Data* bd = (ImGuiApp_ImplQoi_Data*)self->UserData;
    if (bd == nullptr || bd->Index == nullptr)
        return;
    ImFileClose(bd->Index);
    bd->Index = nullptr;
}

static void ImGuiApp_ImplQoi_Destroy(ImGuiAppAVEncoder* self)
{
    ImGuiApp_ImplQoi_Data* bd = (ImGuiApp_ImplQoi_Data*)self->UserData;
    if (bd != nullptr)
    {
        if (bd->Index != nullptr)
            ImFileClose(bd->Index);
        IM_DELETE(bd);
    }
    self->UserData = nullptr;
    IM_DELETE(self);
}

ImGuiAppAVEncoder* ImGuiApp_ImplQoi_CreateEncoder()
{
    ImGuiAppAVEncoder* enc = IM_NEW(ImGuiAppAVEncoder)();
    enc->Name = "qoi-sequence";
    enc->SupportsRealtimePts = true;
    enc->OpenFn = ImGuiApp_ImplQoi_Open;
    enc->WriteFrameFn = ImGuiApp_ImplQoi_WriteFrame;
    enc->CloseFn = ImGuiApp_ImplQoi_Close;
    enc->DestroyFn = ImGuiApp_ImplQoi_Destroy;
    enc->UserData = IM_NEW(ImGuiApp_ImplQoi_Data)();
    return enc;
}

// Strip chunk reader over decoded RGBA (the stamp writes R=G=B, so R is the luma).
// Block grid per the frozen contract in imguiapp_av.h; per-frame framing
// 'IMIL' | u32 chunk_size | chunk | u32 ImHashData(chunk).
static bool ImGuiApp_ImplQoi_ReadStripChunk(const unsigned char* rgba, int w, int h, int embed_rows, ImVector<char>* scratch, ImVector<char>* out_meta)
{
    if (embed_rows > h)
        return false;
    const int blocks_x = w / 4;
    const int blocks_y = embed_rows / 4;
    const int base_row = h - embed_rows;
    scratch->resize(blocks_x * blocks_y / 8);
    memset(scratch->Data, 0, (size_t)scratch->Size);
    for (int b = 0; b < scratch->Size * 8; b++)
    {
        const int bx = b % blocks_x;
        const int by = b / blocks_x;
        int sum = 0;
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++)
                sum += rgba[(((size_t)(base_row + by * 4 + y) * w) + (size_t)(bx * 4 + x)) * 4];
        if (sum >= 128 * 16)
            scratch->Data[b / 8] |= (char)(0x80 >> (b % 8));
    }

    if (scratch->Size < 12)
        return false;
    const char* d = scratch->Data;
    if (d[0] != 'I' || d[1] != 'M' || d[2] != 'I' || d[3] != 'L')
        return false;
    ImU32 size = 0;
    memcpy(&size, d + 4, 4);
    if ((ImS64)size > (ImS64)scratch->Size - 12)
        return false;
    ImU32 check = 0;
    memcpy(&check, d + 8 + size, 4);
    if (check != (ImU32)ImHashData(d + 8, (size_t)size, 0))
        return false;
    if (size > 0)
    {
        const int base = out_meta->Size;
        out_meta->resize(base + (int)size);
        memcpy(out_meta->Data + base, d + 8, (size_t)size);
    }
    return true;
}

bool ImGuiApp_ImplQoi_ExtractEmbeddedMeta(const char* dir, int embed_rows, ImVector<char>* out_meta)
{
    IM_ASSERT(dir != nullptr && out_meta != nullptr && embed_rows >= 4);
    if (dir == nullptr || out_meta == nullptr || embed_rows < 4)
        return false;
    out_meta->clear();

    ImVector<char> rgba;
    ImVector<char> scratch;
    for (int i = 0; ; i++)
    {
        char path[560];
        ImFormatString(path, IM_ARRAYSIZE(path), "%s/%06d.qoi", dir, i);
        size_t size = 0;
        void* bytes = ImFileLoadToMemory(path, "rb", &size);
        if (bytes == nullptr)
            break;   // sequence end (or a missing frame: the stream truncates here)
        int w = 0;
        int h = 0;
        const bool decoded = ImGui::AppAVImageDecode(bytes, (int)size, &rgba, &w, &h);
        IM_FREE(bytes);
        if (!decoded)
            return out_meta->Size > 0;
        if (!ImGuiApp_ImplQoi_ReadStripChunk((const unsigned char*)rgba.Data, w, h, embed_rows, &scratch, out_meta))
            return i > 0 && out_meta->Size > 0;   // frame 0 without magic = no embedded stream
    }
    return out_meta->Size > 0;
}

bool ImGuiApp_ImplQoi_DecodeFrame(const char* dir, int frame_ordinal, ImVector<char>* out_rgba, int* out_w, int* out_h)
{
    if (dir == nullptr || out_rgba == nullptr || frame_ordinal < 0)
        return false;
    char path[560];
    ImFormatString(path, IM_ARRAYSIZE(path), "%s/%06d.qoi", dir, frame_ordinal);
    size_t size = 0;
    void* bytes = ImFileLoadToMemory(path, "rb", &size);
    if (bytes == nullptr)
        return false;
    int w = 0;
    int h = 0;
    const bool decoded = ImGui::AppAVImageDecode(bytes, (int)size, out_rgba, &w, &h);
    IM_FREE(bytes);
    if (!decoded)
        return false;
    if (out_w != nullptr)
        *out_w = w;
    if (out_h != nullptr)
        *out_h = h;
    return true;
}

#endif // #ifndef IMGUI_DISABLE
