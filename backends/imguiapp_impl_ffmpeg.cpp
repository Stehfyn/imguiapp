// ImGuiAppAV encoder backend: ffmpeg process pipe (imguiapp_impl_ffmpeg.h).
// Child ffmpeg reads rawvideo RGBA on stdin; its stdout/stderr land in
// "<OutputPath>.ffmpeg.log". SupportsRealtimePts is false: rawvideo has no PTS
// channel, so Realtime timing duplicates frames into a CFR stream (honest to within
// 1/Fps) and Close leaves the exact-VFR remux recipe next to the output.

#include "imguiapp_impl_ffmpeg.h"

#include <cstdio>
#include <cstring>
#include <cmath>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

struct ImGuiAppFfmpegEncoderData
{
  char                 Exe[512];
  char                 ExtraArgs[1024];
  char                 OutputPath[512];
  float                Fps;
  ImGuiAppAVTimingMode Timing;
  int                  Width;
  int                  Height;
  bool                 OpenCalled;
  bool                 Spawned;
  bool                 Dead;          // broken pipe / spawn failure: every later WriteFrame no-ops false
  bool                 HaveLastTime;
  double               LastTimeSec;
  ImU64                FramesEmitted; // raw frames written to the pipe, after duplication
#ifdef _WIN32
  HANDLE               ChildProcess;
  HANDLE               ChildStdinWrite;
#endif

  ImGuiAppFfmpegEncoderData()
  {
    Exe[0] = 0;
    ExtraArgs[0] = 0;
    OutputPath[0] = 0;
    Fps = 60.0f;
    Timing = ImGuiAppAVTimingMode_Constant;
    Width = 0;
    Height = 0;
    OpenCalled = false;
    Spawned = false;
    Dead = false;
    HaveLastTime = false;
    LastTimeSec = 0.0;
    FramesEmitted = 0;
#ifdef _WIN32
    ChildProcess = nullptr;
    ChildStdinWrite = nullptr;
#endif
  }
};

#ifdef _WIN32

static bool FfmpegSpawn(ImGuiAppFfmpegEncoderData* bd)
{
  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof(sa);
  sa.lpSecurityDescriptor = nullptr;
  sa.bInheritHandle = TRUE;

  HANDLE stdin_read = nullptr;
  HANDLE stdin_write = nullptr;
  if (!::CreatePipe(&stdin_read, &stdin_write, &sa, 0))
    return false;
  // Only the child's end may be inherited; a leaked write end would keep the pipe
  // open after Close and ffmpeg would never see EOF.
  ::SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0);

  char log_path[560];
  snprintf(log_path, sizeof(log_path), "%s.ffmpeg.log", bd->OutputPath);
  HANDLE log_file = ::CreateFileA(log_path, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (log_file == INVALID_HANDLE_VALUE)
    log_file = nullptr;   // encoder chatter is diagnostics, not a reason to fail the recording

  char cmd[2600];
  snprintf(cmd, sizeof(cmd), "\"%s\" -hide_banner -y -f rawvideo -pix_fmt rgba -s %dx%d -r %.6g -i - %s \"%s\"",
           bd->Exe, bd->Width, bd->Height, (double)bd->Fps, bd->ExtraArgs, bd->OutputPath);

  STARTUPINFOA si;
  memset(&si, 0, sizeof(si));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = stdin_read;
  si.hStdOutput = log_file;
  si.hStdError = log_file;

  PROCESS_INFORMATION pi;
  memset(&pi, 0, sizeof(pi));
  const BOOL ok = ::CreateProcessA(nullptr, cmd, nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

  // The child holds inherited duplicates; our copies of its ends close either way.
  ::CloseHandle(stdin_read);
  if (log_file != nullptr)
    ::CloseHandle(log_file);

  if (!ok)
  {
    ::CloseHandle(stdin_write);
    return false;
  }

  ::CloseHandle(pi.hThread);
  bd->ChildProcess = pi.hProcess;
  bd->ChildStdinWrite = stdin_write;
  bd->Spawned = true;
  return true;
}

static bool FfmpegWriteBytes(ImGuiAppFfmpegEncoderData* bd, const void* bytes, int size)
{
  const char* p = (const char*)bytes;
  int remaining = size;
  while (remaining > 0)
  {
    DWORD written = 0;
    if (!::WriteFile(bd->ChildStdinWrite, p, (DWORD)remaining, &written, nullptr))
      return false;
    p += written;
    remaining -= (int)written;
  }
  return true;
}

#endif // _WIN32

static bool ImGuiAppFfmpeg_Open(ImGuiAppAVEncoder* self, const ImGuiAppAVEncodeConfig* config)
{
  ImGuiAppFfmpegEncoderData* bd = (ImGuiAppFfmpegEncoderData*)self->UserData;
  if (bd == nullptr || config == nullptr || config->OutputPath == nullptr || bd->OpenCalled)
    return false;

  snprintf(bd->OutputPath, sizeof(bd->OutputPath), "%s", config->OutputPath);
  bd->Fps = config->Fps > 0.0f ? config->Fps : 60.0f;
  // Auto is resolved by the recorder before Open; reaching here unresolved means no
  // pacer context, and a synthetic CFR timeline is the only honest claim left.
  bd->Timing = config->Timing == ImGuiAppAVTimingMode_Realtime ? ImGuiAppAVTimingMode_Realtime : ImGuiAppAVTimingMode_Constant;
  bd->Width = config->Width;
  bd->Height = config->Height;
  bd->OpenCalled = true;
  // A previous take's failure must not latch this one dead: the flight recorder
  // re-opens the same encoder for every ring dump.
  bd->Dead = false;
  bd->HaveLastTime = false;
  bd->LastTimeSec = 0.0;
  bd->FramesEmitted = 0;

#ifdef _WIN32
  // Size 0 = first frame's size: the spawn (which bakes -s WxH) waits for WriteFrame.
  if (bd->Width > 0 && bd->Height > 0)
  {
    if (!FfmpegSpawn(bd))
    {
      bd->Dead = true;
      return false;
    }
  }
  return true;
#else
  bd->Dead = true;
  return false;
#endif
}

static bool ImGuiAppFfmpeg_WriteFrame(ImGuiAppAVEncoder* self, const ImGuiAppAVFrame* frame)
{
  ImGuiAppFfmpegEncoderData* bd = (ImGuiAppFfmpegEncoderData*)self->UserData;
  if (bd == nullptr || frame == nullptr || frame->Pixels == nullptr || !bd->OpenCalled || bd->Dead)
    return false;

#ifdef _WIN32
  if (!bd->Spawned)
  {
    bd->Width = frame->Width;
    bd->Height = frame->Height;
    if (bd->Width <= 0 || bd->Height <= 0 || !FfmpegSpawn(bd))
    {
      bd->Dead = true;
      return false;
    }
  }
  if (frame->Width != bd->Width || frame->Height != bd->Height)
  {
    bd->Dead = true;   // -s WxH is baked into the running child; a resize aborts the recording
    return false;
  }

  int repeats = 1;
  if (bd->Timing == ImGuiAppAVTimingMode_Realtime)
  {
    // Cumulative CFR emission: the stream's frame count tracks wall time drift-free.
    // Captures faster than Fps DROP (repeats 0); slower captures duplicate. A min-1
    // clamp here made faster-than-Fps apps play slower than wall clock.
    if (bd->HaveLastTime)
    {
      // Frames due through time t = output ticks 0..round(elapsed * fps), inclusive.
      const long long target = llround((frame->FrameID.TimeSec - bd->LastTimeSec) * (double)bd->Fps) + 1;
      const long long due = target - (long long)bd->FramesEmitted;
      repeats = due < 0 ? 0 : (int)due;
    }
    else
    {
      bd->LastTimeSec = frame->FrameID.TimeSec;   // take epoch: first capture emits exactly one frame
      bd->HaveLastTime = true;
    }
  }
  if (repeats == 0)
    return true;   // ahead of the output timeline: honest skip, not a failure

  const int row_bytes = bd->Width * 4;
  for (int r = 0; r < repeats; r++)
  {
    bool ok;
    if (frame->PitchBytes == row_bytes)
    {
      ok = FfmpegWriteBytes(bd, frame->Pixels, row_bytes * bd->Height);
    }
    else
    {
      ok = true;
      const char* row = (const char*)frame->Pixels;
      for (int y = 0; y < bd->Height && ok; y++, row += frame->PitchBytes)
        ok = FfmpegWriteBytes(bd, row, row_bytes);
    }
    if (!ok)
    {
      bd->Dead = true;
      ::CloseHandle(bd->ChildStdinWrite);
      bd->ChildStdinWrite = nullptr;
      return false;
    }
    bd->FramesEmitted++;
  }
  return true;
#else
  return false;
#endif
}

static void ImGuiAppFfmpeg_Close(ImGuiAppAVEncoder* self)
{
  ImGuiAppFfmpegEncoderData* bd = (ImGuiAppFfmpegEncoderData*)self->UserData;
  if (bd == nullptr)
    return;

#ifdef _WIN32
  if (bd->ChildStdinWrite != nullptr)
  {
    // EOF on stdin is ffmpeg's signal to drain and finalize the container.
    ::CloseHandle(bd->ChildStdinWrite);
    bd->ChildStdinWrite = nullptr;
  }
  if (bd->ChildProcess != nullptr)
  {
    ::WaitForSingleObject(bd->ChildProcess, 30000);
    ::CloseHandle(bd->ChildProcess);
    bd->ChildProcess = nullptr;
  }
#endif

  if (bd->OpenCalled && bd->Timing == ImGuiAppAVTimingMode_Realtime && bd->OutputPath[0])
  {
    char remux_path[560];
    snprintf(remux_path, sizeof(remux_path), "%s.remux.txt", bd->OutputPath);
    if (FILE* f = fopen(remux_path, "wb"))
    {
      fprintf(f,
        "# Exact-VFR rebuild of \"%s\".\n"
        "# This file is CFR at %.6g fps with frames duplicated to approximate wall-clock\n"
        "# timing; the true per-frame times live in the sidecar \"%s.avmeta\".\n"
        "#\n"
        "# 1) Dump unique source frames (duplicates collapse in step 3):\n"
        "#      ffmpeg -i \"%s\" -vsync vfr frames/%%06d.png\n"
        "# 2) Emit a concat list from the sidecar (ImGui::AppAVMetaDump prints TSV of\n"
        "#    frame_index + time_sec) -- one entry per captured frame:\n"
        "#      file 'frames/000001.png'\n"
        "#      duration <time_sec[n+1] - time_sec[n]>\n"
        "#    (repeat; concat demuxer syntax, last entry listed twice per ffmpeg docs)\n"
        "# 3) Encode the exact-VFR file:\n"
        "ffmpeg -f concat -safe 0 -i concat.txt -vsync vfr -c:v libx264 -crf 18 \"%s.vfr.mp4\"\n",
        bd->OutputPath, (double)bd->Fps, bd->OutputPath, bd->OutputPath, bd->OutputPath);
      fclose(f);
    }
  }

  bd->OpenCalled = false;
  bd->Spawned = false;
  bd->HaveLastTime = false;
}

static void ImGuiAppFfmpeg_Destroy(ImGuiAppAVEncoder* self)
{
  if (self == nullptr)
    return;
  ImGuiAppFfmpeg_Close(self);
  ImGuiAppFfmpegEncoderData* bd = (ImGuiAppFfmpegEncoderData*)self->UserData;
  IM_DELETE(bd);
  IM_DELETE(self);
}

IMGUI_API ImGuiAppAVEncoder* ImGuiAppAV_CreateFfmpegEncoder(const char* ffmpeg_exe, const char* extra_args)
{
  ImGuiAppFfmpegEncoderData* bd = IM_NEW(ImGuiAppFfmpegEncoderData)();
  snprintf(bd->Exe, sizeof(bd->Exe), "%s", ffmpeg_exe != nullptr ? ffmpeg_exe : "ffmpeg");
  snprintf(bd->ExtraArgs, sizeof(bd->ExtraArgs), "%s", extra_args != nullptr ? extra_args : "-c:v libx264 -preset veryfast -crf 18");

  ImGuiAppAVEncoder* enc = IM_NEW(ImGuiAppAVEncoder)();
  enc->Name = "ffmpeg-pipe";
  enc->SupportsRealtimePts = false;
  enc->Open = ImGuiAppFfmpeg_Open;
  enc->WriteFrame = ImGuiAppFfmpeg_WriteFrame;
  enc->Close = ImGuiAppFfmpeg_Close;
  enc->Destroy = ImGuiAppFfmpeg_Destroy;
  enc->UserData = bd;
  return enc;
}
