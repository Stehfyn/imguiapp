// ImGuiAppAV encoder backend: QOI image sequence (imguiapp_impl_qoi.h).
// OutputPath is a directory: frames land as <dir>/NNNNNN.qoi with index.tsv carrying
// each frame's FrameID (file, frame_index, time_sec, tsc) -- realtime PTS are exact
// by construction, so SupportsRealtimePts is true for every timing mode.

#include "imguiapp_impl_qoi.h"
#include "imguiapp_qoi.h"
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

struct ImGuiAppQoiSeqEncoderData
{
  char           Dir[512];
  ImFileHandle   Index;
  int            FrameCounter;
  int            Width;          // 0 until fixed (config, else first frame)
  int            Height;
  ImVector<char> Encoded;        // reused per frame

  ImGuiAppQoiSeqEncoderData()
  {
    Dir[0] = 0;
    Index = nullptr;
    FrameCounter = 0;
    Width = 0;
    Height = 0;
  }
};

static bool QoiSeqMkdirOne(const char* path)
{
#ifdef _WIN32
  const int rc = _mkdir(path);
#else
  const int rc = mkdir(path, 0755);
#endif
  return rc == 0 || errno == EEXIST;
}

// Create every missing directory on the path (separators '/' or '\').
static bool QoiSeqMkdirRecursive(const char* path)
{
  char buf[512];
  snprintf(buf, sizeof(buf), "%s", path);
  for (char* c = buf + 1; *c; c++)
  {
    if (*c != '/' && *c != '\\')
      continue;
    const char sep = *c;
    *c = 0;
    if (buf[0] != 0 && buf[strlen(buf) - 1] != ':' && !QoiSeqMkdirOne(buf))
      return false;
    *c = sep;
  }
  return QoiSeqMkdirOne(buf);
}

static bool ImGuiAppQoiSeq_Open(ImGuiAppAVEncoder* self, const ImGuiAppAVEncodeConfig* config)
{
  ImGuiAppQoiSeqEncoderData* bd = (ImGuiAppQoiSeqEncoderData*)self->UserData;
  if (bd == nullptr || config == nullptr || config->OutputPath == nullptr || bd->Index != nullptr)
    return false;

  if (!QoiSeqMkdirRecursive(config->OutputPath))
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

static bool ImGuiAppQoiSeq_WriteFrame(ImGuiAppAVEncoder* self, const ImGuiAppAVFrame* frame)
{
  ImGuiAppQoiSeqEncoderData* bd = (ImGuiAppQoiSeqEncoderData*)self->UserData;
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

  if (!ImGuiAppAV_QoiEncode(frame->Pixels, frame->Width, frame->Height, frame->PitchBytes, &bd->Encoded))
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

static void ImGuiAppQoiSeq_Close(ImGuiAppAVEncoder* self)
{
  ImGuiAppQoiSeqEncoderData* bd = (ImGuiAppQoiSeqEncoderData*)self->UserData;
  if (bd == nullptr || bd->Index == nullptr)
    return;
  ImFileClose(bd->Index);
  bd->Index = nullptr;
}

static void ImGuiAppQoiSeq_Destroy(ImGuiAppAVEncoder* self)
{
  ImGuiAppQoiSeqEncoderData* bd = (ImGuiAppQoiSeqEncoderData*)self->UserData;
  if (bd != nullptr)
  {
    if (bd->Index != nullptr)
      ImFileClose(bd->Index);
    IM_DELETE(bd);
  }
  self->UserData = nullptr;
  IM_DELETE(self);
}

IMGUI_API ImGuiAppAVEncoder* ImGuiAppAV_CreateQoiSequenceEncoder()
{
  ImGuiAppAVEncoder* enc = IM_NEW(ImGuiAppAVEncoder)();
  enc->Name = "qoi-sequence";
  enc->SupportsRealtimePts = true;
  enc->Open = ImGuiAppQoiSeq_Open;
  enc->WriteFrame = ImGuiAppQoiSeq_WriteFrame;
  enc->Close = ImGuiAppQoiSeq_Close;
  enc->Destroy = ImGuiAppQoiSeq_Destroy;
  enc->UserData = IM_NEW(ImGuiAppQoiSeqEncoderData)();
  return enc;
}
