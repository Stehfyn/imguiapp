// ImGuiAppAV encoder backend: libav* (imguiapp_impl_libav.h). Exact per-frame PTS:
// AVFrame->pts carries FrameID.TimeSec in a 1/1000000 timebase, rebased to the first
// frame -- no wallclock residual. Also hosts the embedded-input-log reader (the decode
// half of the embedded meta stream; format spec lives on ImGuiAppAVEncodeConfig::EmbedRows).

#include "imguiapp_impl_libav.h"
#include "imgui_internal.h"   // ImHashData (embed checksum)

#include <cstdio>
#include <cstring>
#include <cmath>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"
}

//-----------------------------------------------------------------------------
// [SECTION] Encoder
//-----------------------------------------------------------------------------

struct ImGuiAppLibavEncoderData
{
  char                 OutputPath[512];
  float                Fps;
  ImGuiAppAVTimingMode Timing;
  int                  Width;
  int                  Height;
  int                  BitrateKbps;
  bool                 OpenCalled;
  bool                 PipelineOpen;
  bool                 TrailerWritten;
  bool                 Dead;            // open/encode failure: every later WriteFrame no-ops false
  bool                 HaveFirstTime;
  double               FirstTimeSec;    // Realtime PTS rebase epoch (first frame -> pts 0)
  ImU64                FrameCounter;    // Constant-mode pts index
  ImS64                LastPtsUs;       // duplicate-PTS guard (muxers reject non-monotonic)
  AVFormatContext*     Format;
  AVCodecContext*      Codec;
  AVStream*            Stream;
  AVFrame*             Frame;
  AVPacket*            Packet;
  SwsContext*          Sws;

  ImGuiAppLibavEncoderData()
  {
    OutputPath[0] = 0;
    Fps = 60.0f;
    Timing = ImGuiAppAVTimingMode_Constant;
    Width = 0;
    Height = 0;
    BitrateKbps = 0;
    OpenCalled = false;
    PipelineOpen = false;
    TrailerWritten = false;
    Dead = false;
    HaveFirstTime = false;
    FirstTimeSec = 0.0;
    FrameCounter = 0;
    LastPtsUs = -1;
    Format = nullptr;
    Codec = nullptr;
    Stream = nullptr;
    Frame = nullptr;
    Packet = nullptr;
    Sws = nullptr;
  }
};

static void LibavFreePipeline(ImGuiAppLibavEncoderData* bd)
{
  if (bd->Sws != nullptr)
  {
    sws_freeContext(bd->Sws);
    bd->Sws = nullptr;
  }
  if (bd->Frame != nullptr)
    av_frame_free(&bd->Frame);
  if (bd->Packet != nullptr)
    av_packet_free(&bd->Packet);
  if (bd->Codec != nullptr)
    avcodec_free_context(&bd->Codec);
  if (bd->Format != nullptr)
  {
    if (bd->Format->pb != nullptr)
      avio_closep(&bd->Format->pb);
    avformat_free_context(bd->Format);
    bd->Format = nullptr;
  }
  bd->Stream = nullptr;
  bd->PipelineOpen = false;
}

// Drain accepted packets to the muxer; a NULL frame was already sent by the caller
// when flushing. False on mux/encode error.
static bool LibavPumpPackets(ImGuiAppLibavEncoderData* bd)
{
  for (;;)
  {
    const int r = avcodec_receive_packet(bd->Codec, bd->Packet);
    if (r == AVERROR(EAGAIN) || r == AVERROR_EOF)
      return true;
    if (r < 0)
      return false;
    av_packet_rescale_ts(bd->Packet, bd->Codec->time_base, bd->Stream->time_base);
    // The encoder does not propagate frame durations to reordered/flushed packets; the
    // mov muxer then leaves the FINAL sample zero-length, its pts falls outside the
    // track's end_pts, and demuxers flag it DISCARD (last frame of every take
    // silently failed to decode). Nominal duration keeps the edit list covering it.
    if (bd->Packet->duration <= 0)
      bd->Packet->duration = (ImS64)llround(1e6 / (double)bd->Fps);
    bd->Packet->stream_index = bd->Stream->index;
    const int w = av_interleaved_write_frame(bd->Format, bd->Packet);
    av_packet_unref(bd->Packet);
    if (w < 0)
      return false;
    // Crash-honesty: the fragmented container only protects bytes that reached disk.
    // Without this, a short take sits entirely in the avio buffer (init segment
    // included) and a killed process leaves an unreadable file.
    if (bd->Format->pb != nullptr)
      avio_flush(bd->Format->pb);
  }
}

static bool LibavOpenPipeline(ImGuiAppLibavEncoderData* bd)
{
  const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
  if (codec == nullptr)
    codec = avcodec_find_encoder(AV_CODEC_ID_H264);
  if (codec == nullptr)
    return false;

  if (avformat_alloc_output_context2(&bd->Format, nullptr, "mp4", bd->OutputPath) < 0 || bd->Format == nullptr)
    return false;

  bd->Codec = avcodec_alloc_context3(codec);
  if (bd->Codec == nullptr)
    return false;
  bd->Codec->width = bd->Width;
  bd->Codec->height = bd->Height;
  bd->Codec->pix_fmt = AV_PIX_FMT_YUV420P;
  bd->Codec->time_base = AVRational{ 1, 1000000 };
  // framerate is informative for VFR; Constant mode muxes CFR-spaced pts anyway.
  bd->Codec->framerate = AVRational{ (int)llround((double)bd->Fps * 1000.0), 1000 };
  if (bd->BitrateKbps > 0)
    bd->Codec->bit_rate = (int64_t)bd->BitrateKbps * 1000;
  else
    av_opt_set(bd->Codec->priv_data, "crf", "18", 0);
  av_opt_set(bd->Codec->priv_data, "preset", "veryfast", 0);
  // Crash-honesty lives in the container: fragments close at keyframes, so a killed
  // process still yields every completed fragment. Bounded gop keeps fragments short.
  bd->Codec->gop_size = 30;
  if ((bd->Format->oformat->flags & AVFMT_GLOBALHEADER) != 0)
    bd->Codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

  if (avcodec_open2(bd->Codec, codec, nullptr) < 0)
    return false;

  bd->Stream = avformat_new_stream(bd->Format, nullptr);
  if (bd->Stream == nullptr)
    return false;
  if (avcodec_parameters_from_context(bd->Stream->codecpar, bd->Codec) < 0)
    return false;
  bd->Stream->time_base = AVRational{ 1, 1000000 };   // microsecond track: exact VFR pts survive the muxer

  bd->Frame = av_frame_alloc();
  bd->Packet = av_packet_alloc();
  if (bd->Frame == nullptr || bd->Packet == nullptr)
    return false;
  bd->Frame->format = AV_PIX_FMT_YUV420P;
  bd->Frame->width = bd->Width;
  bd->Frame->height = bd->Height;
  if (av_frame_get_buffer(bd->Frame, 0) < 0)
    return false;

  bd->Sws = sws_getContext(bd->Width, bd->Height, AV_PIX_FMT_RGBA,
                           bd->Width, bd->Height, AV_PIX_FMT_YUV420P,
                           SWS_BILINEAR, nullptr, nullptr, nullptr);
  if (bd->Sws == nullptr)
    return false;

  if (avio_open(&bd->Format->pb, bd->OutputPath, AVIO_FLAG_WRITE) < 0)
    return false;
  AVDictionary* mux_opts = nullptr;
  av_dict_set(&mux_opts, "movflags", "+frag_keyframe+empty_moov+default_base_moof", 0);
  const int header_rc = avformat_write_header(bd->Format, &mux_opts);
  av_dict_free(&mux_opts);
  if (header_rc < 0)
    return false;

  bd->PipelineOpen = true;
  return true;
}

static bool ImGuiAppLibav_Open(ImGuiAppAVEncoder* self, const ImGuiAppAVEncodeConfig* config)
{
  ImGuiAppLibavEncoderData* bd = (ImGuiAppLibavEncoderData*)self->UserData;
  if (bd == nullptr || config == nullptr || config->OutputPath == nullptr || bd->OpenCalled)
    return false;

  snprintf(bd->OutputPath, sizeof(bd->OutputPath), "%s", config->OutputPath);
  bd->Fps = config->Fps > 0.0f ? config->Fps : 60.0f;
  bd->Timing = config->Timing == ImGuiAppAVTimingMode_Realtime ? ImGuiAppAVTimingMode_Realtime : ImGuiAppAVTimingMode_Constant;
  bd->Width = config->Width;
  bd->Height = config->Height;
  bd->BitrateKbps = config->BitrateKbps;
  bd->OpenCalled = true;
  // A previous take's failure must not latch this one dead: the flight recorder
  // re-opens the same encoder for every ring dump.
  bd->Dead = false;
  bd->TrailerWritten = false;
  bd->HaveFirstTime = false;
  bd->FirstTimeSec = 0.0;
  bd->FrameCounter = 0;
  bd->LastPtsUs = -1;

  // Size 0 = first frame's size: the pipeline (which bakes WxH) waits for WriteFrame.
  if (bd->Width > 0 && bd->Height > 0)
  {
    if (!LibavOpenPipeline(bd))
    {
      LibavFreePipeline(bd);
      bd->Dead = true;
      return false;
    }
  }
  return true;
}

static bool ImGuiAppLibav_WriteFrame(ImGuiAppAVEncoder* self, const ImGuiAppAVFrame* frame)
{
  ImGuiAppLibavEncoderData* bd = (ImGuiAppLibavEncoderData*)self->UserData;
  if (bd == nullptr || frame == nullptr || frame->Pixels == nullptr || !bd->OpenCalled || bd->Dead)
    return false;

  if (!bd->PipelineOpen)
  {
    bd->Width = frame->Width;
    bd->Height = frame->Height;
    if (bd->Width <= 0 || bd->Height <= 0 || !LibavOpenPipeline(bd))
    {
      LibavFreePipeline(bd);
      bd->Dead = true;
      return false;
    }
  }
  if (frame->Width != bd->Width || frame->Height != bd->Height)
  {
    bd->Dead = true;   // WxH is baked into the open pipeline; a resize aborts the recording
    return false;
  }

  ImS64 pts_us = 0;
  if (bd->Timing == ImGuiAppAVTimingMode_Realtime)
  {
    if (!bd->HaveFirstTime)
    {
      bd->FirstTimeSec = frame->FrameID.TimeSec;
      bd->HaveFirstTime = true;
    }
    pts_us = (ImS64)llround((frame->FrameID.TimeSec - bd->FirstTimeSec) * 1e6);
  }
  else
  {
    pts_us = (ImS64)llround((double)bd->FrameCounter * 1e6 / (double)bd->Fps);
  }
  if (pts_us <= bd->LastPtsUs)
    pts_us = bd->LastPtsUs + 1;   // muxers reject non-monotonic pts; sub-us captures nudge forward
  bd->LastPtsUs = pts_us;
  bd->FrameCounter++;

  if (av_frame_make_writable(bd->Frame) < 0)
  {
    bd->Dead = true;
    return false;
  }
  const uint8_t* src_data[4] = { (const uint8_t*)frame->Pixels, nullptr, nullptr, nullptr };
  const int src_stride[4] = { frame->PitchBytes > 0 ? frame->PitchBytes : frame->Width * 4, 0, 0, 0 };
  sws_scale(bd->Sws, src_data, src_stride, 0, bd->Height, bd->Frame->data, bd->Frame->linesize);
  bd->Frame->pts = pts_us;
  // Nominal duration keeps the mov edit list covering the FINAL sample: with zero
  // durations its pts falls outside the track's end_pts and demuxers flag it DISCARD
  // (the last frame of every take silently failed to decode).
  bd->Frame->duration = (ImS64)llround(1e6 / (double)bd->Fps);

  if (avcodec_send_frame(bd->Codec, bd->Frame) < 0 || !LibavPumpPackets(bd))
  {
    bd->Dead = true;
    return false;
  }
  return true;
}

static void ImGuiAppLibav_Close(ImGuiAppAVEncoder* self)
{
  ImGuiAppLibavEncoderData* bd = (ImGuiAppLibavEncoderData*)self->UserData;
  if (bd == nullptr)
    return;
  if (bd->PipelineOpen && !bd->TrailerWritten)
  {
    avcodec_send_frame(bd->Codec, nullptr);   // flush
    LibavPumpPackets(bd);
    av_write_trailer(bd->Format);
    bd->TrailerWritten = true;
  }
  LibavFreePipeline(bd);
  bd->OpenCalled = false;   // reopenable for the next ring dump
}

static void ImGuiAppLibav_Destroy(ImGuiAppAVEncoder* self)
{
  ImGuiAppLibavEncoderData* bd = (ImGuiAppLibavEncoderData*)self->UserData;
  if (bd != nullptr)
  {
    LibavFreePipeline(bd);
    IM_DELETE(bd);
  }
  self->UserData = nullptr;
  IM_DELETE(self);
}

IMGUI_API ImGuiAppAVEncoder* ImGuiApp_ImplLibav_CreateEncoder()
{
  ImGuiAppAVEncoder* enc = IM_NEW(ImGuiAppAVEncoder)();
  enc->Name = "libav";
  enc->SupportsRealtimePts = true;
  enc->Open = ImGuiAppLibav_Open;
  enc->WriteFrame = ImGuiAppLibav_WriteFrame;
  enc->Close = ImGuiAppLibav_Close;
  enc->Destroy = ImGuiAppLibav_Destroy;
  enc->UserData = IM_NEW(ImGuiAppLibavEncoderData)();
  return enc;
}

//-----------------------------------------------------------------------------
// [SECTION] Embedded input log reader
//-----------------------------------------------------------------------------

// Extract the embedded byte stream from one decoded frame's GRAY8 luma. Block value =
// mean of the 16 pixels; bit = mean >= 128; blocks row-major topmost-first within the
// bottom embed_rows; bits fill bytes MSB-first (spec: ImGuiAppAVEncodeConfig::EmbedRows).
static void LibavExtractStripBytes(const unsigned char* luma, int stride, int width, int height, int embed_rows, ImVector<char>* out)
{
  const int base_row = height - embed_rows;
  const int blocks_x = width / 4;
  const int blocks_y = embed_rows / 4;
  const int total_bits = blocks_x * blocks_y;
  out->resize(total_bits / 8);
  memset(out->Data, 0, (size_t)out->Size);
  for (int b = 0; b < (out->Size * 8); b++)
  {
    const int bx = b % blocks_x;
    const int by = b / blocks_x;
    const int px = bx * 4;
    const int py = base_row + by * 4;
    int sum = 0;
    for (int y = 0; y < 4; y++)
      for (int x = 0; x < 4; x++)
        sum += luma[(size_t)(py + y) * (size_t)stride + (size_t)(px + x)];
    if (sum >= 128 * 16)
      out->Data[b / 8] |= (char)(0x80 >> (b % 8));
  }
}

// Parse one frame's stream: "IMIL" | u32 size | payload | u32 checksum. Returns the
// payload span inside bytes, or false (bad magic / bounds / checksum).
static bool LibavParseStrip(const ImVector<char>* bytes, const char** out_payload, ImU32* out_size)
{
  if (bytes->Size < 12)
    return false;
  const char* d = bytes->Data;
  if (d[0] != 'I' || d[1] != 'M' || d[2] != 'I' || d[3] != 'L')
    return false;
  ImU32 size = 0;
  memcpy(&size, d + 4, 4);
  if ((ImS64)size > (ImS64)bytes->Size - 12)
    return false;
  ImU32 check = 0;
  memcpy(&check, d + 8 + size, 4);
  if (check != (ImU32)ImHashData(d + 8, (size_t)size, 0))
    return false;
  *out_payload = d + 8;
  *out_size = size;
  return true;
}

// Decode every frame, parse its strip chunk, concatenate: the reconstructed meta
// stream. A corrupt frame truncates the stream at that point (chunks are positional);
// frame 1 without the magic = the file carries no embedded stream.
static bool LibavExtractMetaInternal(const char* video_path, int embed_rows, ImVector<char>* out_meta, int* out_corrupt_frames)
{
  IM_ASSERT(video_path != nullptr && out_meta != nullptr && embed_rows >= 4);
  if (out_corrupt_frames != nullptr)
    *out_corrupt_frames = 0;
  if (video_path == nullptr || out_meta == nullptr || embed_rows < 4)
    return false;
  out_meta->clear();

  AVFormatContext* fmt = nullptr;
  if (avformat_open_input(&fmt, video_path, nullptr, nullptr) < 0)
    return false;

  bool ok = false;
  AVCodecContext* dec = nullptr;
  SwsContext* to_gray = nullptr;
  AVFrame* frame = av_frame_alloc();
  AVFrame* gray = av_frame_alloc();
  AVPacket* pkt = av_packet_alloc();
  int stream_index = -1;
  bool first_frame = true;
  ImVector<char> strip;

  do
  {
    if (frame == nullptr || gray == nullptr || pkt == nullptr)
      break;
    if (avformat_find_stream_info(fmt, nullptr) < 0)
      break;
    const AVCodec* codec = nullptr;
    stream_index = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (stream_index < 0 || codec == nullptr)
      break;
    dec = avcodec_alloc_context3(codec);
    if (dec == nullptr)
      break;
    if (avcodec_parameters_to_context(dec, fmt->streams[stream_index]->codecpar) < 0)
      break;
    if (avcodec_open2(dec, codec, nullptr) < 0)
      break;

    bool draining = false;
    bool failed = false;
    while (!failed)
    {
      if (!draining)
      {
        const int r = av_read_frame(fmt, pkt);
        if (r < 0)
        {
          draining = true;
          avcodec_send_packet(dec, nullptr);
        }
        else
        {
          if (pkt->stream_index == stream_index)
            avcodec_send_packet(dec, pkt);
          av_packet_unref(pkt);
          if (pkt->stream_index != stream_index && !draining)
            continue;
        }
      }

      for (;;)
      {
        const int r = avcodec_receive_frame(dec, frame);
        if (r == AVERROR(EAGAIN))
          break;
        if (r == AVERROR_EOF)
        {
          failed = false;
          goto decoded_all;
        }
        if (r < 0)
        {
          failed = true;
          break;
        }

        // GRAY8 conversion isolates the luma regardless of the decoder's pixel format.
        if (to_gray == nullptr)
        {
          to_gray = sws_getContext(frame->width, frame->height, (AVPixelFormat)frame->format,
                                   frame->width, frame->height, AV_PIX_FMT_GRAY8,
                                   SWS_POINT, nullptr, nullptr, nullptr);
          gray->format = AV_PIX_FMT_GRAY8;
          gray->width = frame->width;
          gray->height = frame->height;
          if (to_gray == nullptr || av_frame_get_buffer(gray, 0) < 0)
          {
            failed = true;
            break;
          }
        }
        sws_scale(to_gray, frame->data, frame->linesize, 0, frame->height, gray->data, gray->linesize);

        if (embed_rows > frame->height)
        {
          failed = true;
          break;
        }
        LibavExtractStripBytes(gray->data[0], gray->linesize[0], frame->width, frame->height, embed_rows, &strip);
        const char* payload = nullptr;
        ImU32 size = 0;
        if (LibavParseStrip(&strip, &payload, &size))
        {
          if (size > 0)
          {
            const int base = out_meta->Size;
            out_meta->resize(base + (int)size);
            memcpy(out_meta->Data + base, payload, (size_t)size);
          }
        }
        else
        {
          if (first_frame)
          {
            failed = true;   // frame 1 without magic = no embedded stream in this file
            break;
          }
          // Chunks are positional: a corrupt frame ends the reconstructable stream.
          if (out_corrupt_frames != nullptr)
            (*out_corrupt_frames)++;
          av_frame_unref(frame);
          goto decoded_all;
        }
        first_frame = false;
        av_frame_unref(frame);
      }
    }
decoded_all:
    ok = !failed && out_meta->Size > 0;
  } while (false);

  if (to_gray != nullptr)
    sws_freeContext(to_gray);
  av_frame_free(&frame);
  av_frame_free(&gray);
  av_packet_free(&pkt);
  if (dec != nullptr)
    avcodec_free_context(&dec);
  avformat_close_input(&fmt);
  return ok;
}

IMGUI_API bool ImGuiApp_ImplLibav_ExtractEmbeddedMeta(const char* video_path, int embed_rows, ImVector<char>* out_meta)
{
  return LibavExtractMetaInternal(video_path, embed_rows, out_meta, nullptr);
}

IMGUI_API bool ImGuiApp_ImplLibav_ReadEmbeddedInputLog(const char* video_path, int embed_rows, ImGuiAppInputLog* out_log, int* out_corrupt_frames)
{
  IM_ASSERT(out_log != nullptr);
  if (out_log == nullptr)
    return false;
  ImVector<char> meta;
  if (!LibavExtractMetaInternal(video_path, embed_rows, &meta, out_corrupt_frames))
    return false;
  return ImGui::AppAVMetaReadInputLog(meta.Data, meta.Size, out_log);
}
