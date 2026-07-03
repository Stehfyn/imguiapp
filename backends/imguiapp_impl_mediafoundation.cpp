// ImGuiAppAV encoder backend: Windows Media Foundation mp4 (imguiapp_impl_mediafoundation.h).
// IMFSinkWriter with an H.264 output stream and an RGB32 input type; the writer loads
// the color-converter/encoder MFTs itself. Realtime output is duration-honest resampled
// CFR -- true VFR is unreachable through the sink writer (see MfBeginWriting).
// The only TU linking mfplat/mfreadwrite.

#include "imguiapp_impl_mediafoundation.h"

#ifdef _WIN32

#include <cstdio>
#include <cstring>
#include <cmath>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

// MFStartup/MFShutdown pair once per process across all live MF encoders.
static int g_mf_startup_refs = 0;

struct ImGuiAppMfEncoderData
{
  char                 OutputPath[512];
  float                Fps;
  ImGuiAppAVTimingMode Timing;
  int                  Width;        // 0 until fixed (config, else first frame)
  int                  Height;
  UINT32               BitrateBps;
  bool                 OpenCalled;
  bool                 CoInited;     // this instance holds one CoInitializeEx(MTA) ref
  bool                 MfStarted;    // this instance holds one MFStartup ref
  bool                 Writing;      // BeginWriting succeeded; Finalize pending
  bool                 Dead;         // unrecoverable HRESULT: later WriteFrames no-op false
  IMFSinkWriter*       Writer;
  DWORD                Stream;
  ImU64                FramesWritten;
  bool                 HaveFirstTime;
  double               FirstTimeSec; // Realtime PTS rebase
  LONGLONG             PrevSampleTicks;

  ImGuiAppMfEncoderData()
  {
    OutputPath[0]   = 0;
    Fps             = 60.0f;
    Timing          = ImGuiAppAVTimingMode_Constant;
    Width           = 0;
    Height          = 0;
    BitrateBps      = 0;
    OpenCalled      = false;
    CoInited        = false;
    MfStarted       = false;
    Writing         = false;
    Dead            = false;
    Writer          = nullptr;
    Stream          = 0;
    FramesWritten   = 0;
    HaveFirstTime   = false;
    FirstTimeSec    = 0.0;
    PrevSampleTicks = 0;
  }
};

// Sink writer creation waits until the frame size is known (config, else first frame):
// both media types bake the size.
static bool MfBeginWriting(ImGuiAppMfEncoderData* bd)
{
  wchar_t wpath[512];
  if (::MultiByteToWideChar(CP_UTF8, 0, bd->OutputPath, -1, wpath, IM_ARRAYSIZE(wpath)) == 0)
    return false;

  const UINT32 fps_num = (UINT32)llround((double)bd->Fps * 1000.0);
  const UINT32 fps_den = 1000;

  IMFSinkWriter* writer = nullptr;
  IMFMediaType*  out_type = nullptr;
  IMFMediaType*  in_type = nullptr;
  DWORD          stream = 0;
  HRESULT hr = MFCreateSinkWriterFromURL(wpath, nullptr, nullptr, &writer);

  // True VFR through IMFSinkWriter is NOT achievable (measured): the H.264 path
  // requires MF_MT_FRAME_RATE on the output type (BeginWriting fails without it), and
  // with any declared rate the writer resamples per-sample timestamps to CFR (90 VFR
  // samples -> 208 CFR frames even with the INPUT rate omitted). Realtime here is
  // therefore duration-honest resampled CFR; the ffmpeg pipe backend is the VFR one.
  if (SUCCEEDED(hr)) hr = MFCreateMediaType(&out_type);
  if (SUCCEEDED(hr)) hr = out_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  if (SUCCEEDED(hr)) hr = out_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
  if (SUCCEEDED(hr)) hr = out_type->SetUINT32(MF_MT_AVG_BITRATE, bd->BitrateBps);
  if (SUCCEEDED(hr)) hr = out_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  if (SUCCEEDED(hr)) hr = MFSetAttributeSize(out_type, MF_MT_FRAME_SIZE, (UINT32)bd->Width, (UINT32)bd->Height);
  if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(out_type, MF_MT_FRAME_RATE, fps_num, fps_den);
  if (SUCCEEDED(hr)) hr = writer->AddStream(out_type, &stream);

  if (SUCCEEDED(hr)) hr = MFCreateMediaType(&in_type);
  if (SUCCEEDED(hr)) hr = in_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  if (SUCCEEDED(hr)) hr = in_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
  if (SUCCEEDED(hr)) hr = in_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  // Positive default stride = top-down rows in the buffer (MF's unset default for RGB
  // is bottom-up, which would flip the video).
  if (SUCCEEDED(hr)) hr = in_type->SetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32)(bd->Width * 4));
  if (SUCCEEDED(hr)) hr = MFSetAttributeSize(in_type, MF_MT_FRAME_SIZE, (UINT32)bd->Width, (UINT32)bd->Height);
  if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(in_type, MF_MT_FRAME_RATE, fps_num, fps_den);
  if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(stream, in_type, nullptr);

  if (SUCCEEDED(hr)) hr = writer->BeginWriting();

  if (out_type != nullptr)
    out_type->Release();
  if (in_type != nullptr)
    in_type->Release();

  if (FAILED(hr))
  {
    if (writer != nullptr)
      writer->Release();
    return false;
  }

  bd->Writer = writer;
  bd->Stream = stream;
  bd->Writing = true;
  return true;
}

static bool ImGuiAppMf_Open(ImGuiAppAVEncoder* self, const ImGuiAppAVEncodeConfig* config)
{
  ImGuiAppMfEncoderData* bd = (ImGuiAppMfEncoderData*)self->UserData;
  if (bd == nullptr || config == nullptr || config->OutputPath == nullptr || bd->OpenCalled)
    return false;

  if (!bd->MfStarted)
  {
    // Self-contained COM/MF bring-up: callers (recorder threads included) never
    // CoInitialize. RPC_E_CHANGED_MODE (caller owns an STA) needs no matching
    // CoUninitialize; S_OK/S_FALSE do.
    const HRESULT co = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bd->CoInited = SUCCEEDED(co);
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE)))
    {
      if (bd->CoInited)
        ::CoUninitialize();
      bd->CoInited = false;
      return false;
    }
    bd->MfStarted = true;
    g_mf_startup_refs++;
  }

  snprintf(bd->OutputPath, sizeof(bd->OutputPath), "%s", config->OutputPath);
  bd->Fps = config->Fps > 0.0f ? config->Fps : 60.0f;
  // Auto is resolved by the recorder before Open; unresolved means no pacer context and
  // the synthetic CFR timeline is the only honest claim left.
  bd->Timing = config->Timing == ImGuiAppAVTimingMode_Realtime ? ImGuiAppAVTimingMode_Realtime : ImGuiAppAVTimingMode_Constant;
  bd->Width = config->Width;
  bd->Height = config->Height;
  bd->BitrateBps = config->BitrateKbps > 0 ? (UINT32)config->BitrateKbps * 1000u : 8000000u;
  bd->OpenCalled = true;
  // A previous take's failure must not latch this one dead: the flight recorder
  // re-opens the same encoder for every ring dump.
  bd->Dead = false;
  bd->FramesWritten = 0;
  bd->HaveFirstTime = false;
  bd->PrevSampleTicks = 0;

  // Size 0 = first frame's size: sink writer creation waits for WriteFrame.
  if (bd->Width > 0 && bd->Height > 0 && !MfBeginWriting(bd))
  {
    bd->Dead = true;
    return false;
  }
  return true;
}

static bool ImGuiAppMf_WriteFrame(ImGuiAppAVEncoder* self, const ImGuiAppAVFrame* frame)
{
  ImGuiAppMfEncoderData* bd = (ImGuiAppMfEncoderData*)self->UserData;
  if (bd == nullptr || frame == nullptr || frame->Pixels == nullptr || !bd->OpenCalled || bd->Dead)
    return false;

  if (!bd->Writing)
  {
    bd->Width = frame->Width;
    bd->Height = frame->Height;
    if (bd->Width <= 0 || bd->Height <= 0 || !MfBeginWriting(bd))
    {
      bd->Dead = true;
      return false;
    }
  }
  if (frame->Width != bd->Width || frame->Height != bd->Height)
  {
    bd->Dead = true;   // both media types bake the size; a resize aborts the recording
    return false;
  }

  const DWORD buf_size = (DWORD)(bd->Width * 4) * (DWORD)bd->Height;
  IMFMediaBuffer* buffer = nullptr;
  IMFSample*      sample = nullptr;
  BYTE*           dst = nullptr;
  HRESULT hr = MFCreateMemoryBuffer(buf_size, &buffer);
  if (SUCCEEDED(hr)) hr = buffer->Lock(&dst, nullptr, nullptr);
  if (SUCCEEDED(hr))
  {
    // RGBA -> RGB32 (BGRA in memory): swizzle R<->B per pixel, rows top-down, pitch honored.
    const int src_pitch = frame->PitchBytes > 0 ? frame->PitchBytes : bd->Width * 4;
    for (int y = 0; y < bd->Height; y++)
    {
      const unsigned char* s = (const unsigned char*)frame->Pixels + (size_t)y * (size_t)src_pitch;
      unsigned char* d = dst + (size_t)y * (size_t)(bd->Width * 4);
      for (int x = 0; x < bd->Width; x++)
      {
        d[x * 4 + 0] = s[x * 4 + 2];
        d[x * 4 + 1] = s[x * 4 + 1];
        d[x * 4 + 2] = s[x * 4 + 0];
        d[x * 4 + 3] = s[x * 4 + 3];
      }
    }
    hr = buffer->Unlock();
  }
  if (SUCCEEDED(hr)) hr = buffer->SetCurrentLength(buf_size);

  // Sample time in 100ns ticks. Constant: N/Fps positions (per-index rounding, no
  // accumulated drift). Realtime: wall-clock PTS rebased to the take's first frame;
  // duration approximated by the delta to the PREVIOUS sample (min 1 tick).
  const double period_ticks = 10000000.0 / (double)bd->Fps;
  LONGLONG t = 0;
  LONGLONG dur = (LONGLONG)llround(period_ticks);
  if (bd->Timing == ImGuiAppAVTimingMode_Realtime)
  {
    if (!bd->HaveFirstTime)
    {
      bd->FirstTimeSec = frame->FrameID.TimeSec;
      bd->HaveFirstTime = true;
    }
    t = (LONGLONG)llround((frame->FrameID.TimeSec - bd->FirstTimeSec) * 10000000.0);
    if (bd->FramesWritten > 0)
      dur = t - bd->PrevSampleTicks < 1 ? 1 : t - bd->PrevSampleTicks;
  }
  else
  {
    t = (LONGLONG)llround((double)bd->FramesWritten * period_ticks);
    dur = (LONGLONG)llround((double)(bd->FramesWritten + 1) * period_ticks) - t;
  }

  if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
  if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer);
  if (SUCCEEDED(hr)) hr = sample->SetSampleTime(t);
  if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(dur);
  if (SUCCEEDED(hr)) hr = bd->Writer->WriteSample(bd->Stream, sample);

  if (buffer != nullptr)
    buffer->Release();
  if (sample != nullptr)
    sample->Release();

  if (FAILED(hr))
  {
    bd->Dead = true;
    return false;
  }
  bd->PrevSampleTicks = t;
  bd->FramesWritten++;
  return true;
}

static void ImGuiAppMf_Close(ImGuiAppAVEncoder* self)
{
  ImGuiAppMfEncoderData* bd = (ImGuiAppMfEncoderData*)self->UserData;
  if (bd == nullptr)
    return;

  if (bd->Writer != nullptr)
  {
    if (bd->Writing && bd->FramesWritten > 0)
      bd->Writer->Finalize();   // a zero-sample Finalize fails; the empty file stays
    bd->Writer->Release();
    bd->Writer = nullptr;
  }
  bd->Writing = false;
  bd->OpenCalled = false;
  bd->HaveFirstTime = false;
}

static void ImGuiAppMf_Destroy(ImGuiAppAVEncoder* self)
{
  if (self == nullptr)
    return;
  ImGuiAppMf_Close(self);
  ImGuiAppMfEncoderData* bd = (ImGuiAppMfEncoderData*)self->UserData;
  if (bd != nullptr && bd->MfStarted)
  {
    g_mf_startup_refs--;
    if (g_mf_startup_refs == 0)
      MFShutdown();
    if (bd->CoInited)
      ::CoUninitialize();
  }
  IM_DELETE(bd);
  IM_DELETE(self);
}

IMGUI_API ImGuiAppAVEncoder* ImGuiApp_ImplMediaFoundation_CreateEncoder()
{
  ImGuiAppAVEncoder* enc = IM_NEW(ImGuiAppAVEncoder)();
  enc->Name = "mediafoundation";
  enc->SupportsRealtimePts = false;   // sink writer resamples to CFR (see MfBeginWriting); duration honest, PTS quantized
  enc->Open = ImGuiAppMf_Open;
  enc->WriteFrame = ImGuiAppMf_WriteFrame;
  enc->Close = ImGuiAppMf_Close;
  enc->Destroy = ImGuiAppMf_Destroy;
  enc->UserData = IM_NEW(ImGuiAppMfEncoderData)();
  return enc;
}

#else // !_WIN32

IMGUI_API ImGuiAppAVEncoder* ImGuiApp_ImplMediaFoundation_CreateEncoder()
{
  return nullptr;
}

#endif // _WIN32
