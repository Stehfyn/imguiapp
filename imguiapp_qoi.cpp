// Internal QOI codec (imguiapp_qoi.h), implemented from the QOI specification
// Version 1.0, 2022.01.05 (qoiformat.org). Encode always writes channels=4,
// colorspace=0 (sRGB with linear alpha); decode accepts channels 3 or 4 and always
// outputs tightly packed RGBA.

#include "imguiapp_qoi.h"

struct ImQoiPx
{
  unsigned char r;
  unsigned char g;
  unsigned char b;
  unsigned char a;
};

// Spec hash: index_position = (r*3 + g*5 + b*7 + a*11) % 64.
static inline int ImQoiHash(const ImQoiPx* p)
{
  return (p->r * 3 + p->g * 5 + p->b * 7 + p->a * 11) % 64;
}

static inline bool ImQoiEq(const ImQoiPx* x, const ImQoiPx* y)
{
  return x->r == y->r && x->g == y->g && x->b == y->b && x->a == y->a;
}

IMGUI_API bool ImGuiAppAV_QoiEncode(const void* rgba, int width, int height, int pitch_bytes, ImVector<char>* out)
{
  if (rgba == nullptr || out == nullptr || width <= 0 || height <= 0)
    return false;
  if (pitch_bytes <= 0)
    pitch_bytes = width * 4;
  if (pitch_bytes < width * 4)
    return false;

  // Worst case is one QOI_OP_RGBA (5 bytes) per pixel, plus 14-byte header + 8-byte end marker.
  const long long max_size = 14LL + 5LL * width * height + 8LL;
  if (max_size > 0x7FFFFFF0LL)
    return false;
  out->resize((int)max_size);
  unsigned char* dst = (unsigned char*)out->Data;
  int p = 0;

  dst[p++] = 'q';
  dst[p++] = 'o';
  dst[p++] = 'i';
  dst[p++] = 'f';
  dst[p++] = (unsigned char)((width >> 24) & 0xFF);    // u32 BE width
  dst[p++] = (unsigned char)((width >> 16) & 0xFF);
  dst[p++] = (unsigned char)((width >> 8) & 0xFF);
  dst[p++] = (unsigned char)(width & 0xFF);
  dst[p++] = (unsigned char)((height >> 24) & 0xFF);   // u32 BE height
  dst[p++] = (unsigned char)((height >> 16) & 0xFF);
  dst[p++] = (unsigned char)((height >> 8) & 0xFF);
  dst[p++] = (unsigned char)(height & 0xFF);
  dst[p++] = 4;                                        // channels (informative)
  dst[p++] = 0;                                        // colorspace: sRGB with linear alpha

  ImQoiPx index[64] = {};                              // zero-initialized per spec
  ImQoiPx prev = { 0, 0, 0, 255 };                     // spec-defined starting previous pixel
  int run = 0;
  const long long total = (long long)width * height;
  long long seen = 0;

  for (int y = 0; y < height; y++)
  {
    const unsigned char* row = (const unsigned char*)rgba + (size_t)y * (size_t)pitch_bytes;
    for (int x = 0; x < width; x++)
    {
      ImQoiPx px;
      px.r = row[x * 4 + 0];
      px.g = row[x * 4 + 1];
      px.b = row[x * 4 + 2];
      px.a = row[x * 4 + 3];
      seen++;

      if (ImQoiEq(&px, &prev))
      {
        run++;
        // Run lengths 63/64 are illegal (their encodings are the RGB/RGBA tags); flush at 62.
        if (run == 62 || seen == total)
        {
          dst[p++] = (unsigned char)(0xC0 | (run - 1));   // QOI_OP_RUN, bias -1
          run = 0;
        }
      }
      else
      {
        if (run > 0)
        {
          dst[p++] = (unsigned char)(0xC0 | (run - 1));
          run = 0;
        }
        const int h = ImQoiHash(&px);
        if (ImQoiEq(&index[h], &px))
        {
          dst[p++] = (unsigned char)h;                    // QOI_OP_INDEX (tag b00)
        }
        else
        {
          index[h] = px;
          if (px.a == prev.a)
          {
            // Diffs are wraparound (mod 256); signed char casts give the spec's -128..127 view.
            const signed char vr = (signed char)(unsigned char)(px.r - prev.r);
            const signed char vg = (signed char)(unsigned char)(px.g - prev.g);
            const signed char vb = (signed char)(unsigned char)(px.b - prev.b);
            const signed char vg_r = (signed char)(vr - vg);
            const signed char vg_b = (signed char)(vb - vg);
            if (vr > -3 && vr < 2 && vg > -3 && vg < 2 && vb > -3 && vb < 2)
            {
              dst[p++] = (unsigned char)(0x40 | ((vr + 2) << 4) | ((vg + 2) << 2) | (vb + 2));   // QOI_OP_DIFF, bias 2
            }
            else if (vg > -33 && vg < 32 && vg_r > -9 && vg_r < 8 && vg_b > -9 && vg_b < 8)
            {
              dst[p++] = (unsigned char)(0x80 | (vg + 32));                                      // QOI_OP_LUMA, green bias 32
              dst[p++] = (unsigned char)(((vg_r + 8) << 4) | (vg_b + 8));                        // dr-dg | db-dg, bias 8
            }
            else
            {
              dst[p++] = 0xFE;                            // QOI_OP_RGB (alpha unchanged)
              dst[p++] = px.r;
              dst[p++] = px.g;
              dst[p++] = px.b;
            }
          }
          else
          {
            dst[p++] = 0xFF;                              // QOI_OP_RGBA
            dst[p++] = px.r;
            dst[p++] = px.g;
            dst[p++] = px.b;
            dst[p++] = px.a;
          }
        }
      }
      prev = px;
    }
  }

  dst[p++] = 0x00;   // end marker: 7 x 0x00 then 0x01
  dst[p++] = 0x00;
  dst[p++] = 0x00;
  dst[p++] = 0x00;
  dst[p++] = 0x00;
  dst[p++] = 0x00;
  dst[p++] = 0x00;
  dst[p++] = 0x01;

  out->resize(p);
  return true;
}

IMGUI_API bool ImGuiAppAV_QoiDecode(const void* qoi_bytes, int qoi_size, ImVector<char>* out_rgba, int* out_width, int* out_height)
{
  if (qoi_bytes == nullptr || out_rgba == nullptr || out_width == nullptr || out_height == nullptr)
    return false;
  if (qoi_size < 14 + 8)
    return false;

  const unsigned char* src = (const unsigned char*)qoi_bytes;
  if (src[0] != 'q' || src[1] != 'o' || src[2] != 'i' || src[3] != 'f')
    return false;

  const unsigned int uw = ((unsigned int)src[4] << 24) | ((unsigned int)src[5] << 16) | ((unsigned int)src[6] << 8) | src[7];
  const unsigned int uh = ((unsigned int)src[8] << 24) | ((unsigned int)src[9] << 16) | ((unsigned int)src[10] << 8) | src[11];
  const unsigned char channels = src[12];
  if (uw == 0 || uh == 0 || (channels != 3 && channels != 4))
    return false;
  const long long total = (long long)uw * (long long)uh;
  if (total <= 0 || total * 4 > 0x7FFFFFF0LL)
    return false;

  out_rgba->resize((int)(total * 4));
  unsigned char* dst = (unsigned char*)out_rgba->Data;

  ImQoiPx index[64] = {};
  ImQoiPx px = { 0, 0, 0, 255 };
  int run = 0;
  int p = 14;
  const int chunks_end = qoi_size - 8;   // the 8-byte end marker is not chunk data

  for (long long i = 0; i < total; i++)
  {
    if (run > 0)
    {
      run--;
    }
    else if (p < chunks_end)
    {
      const unsigned char b1 = src[p++];
      if (b1 == 0xFE)                    // QOI_OP_RGB (8-bit tags checked before 2-bit tags)
      {
        if (p + 3 > chunks_end)
          return false;
        px.r = src[p++];
        px.g = src[p++];
        px.b = src[p++];
      }
      else if (b1 == 0xFF)               // QOI_OP_RGBA
      {
        if (p + 4 > chunks_end)
          return false;
        px.r = src[p++];
        px.g = src[p++];
        px.b = src[p++];
        px.a = src[p++];
      }
      else if ((b1 & 0xC0) == 0x00)      // QOI_OP_INDEX
      {
        px = index[b1 & 0x3F];
      }
      else if ((b1 & 0xC0) == 0x40)      // QOI_OP_DIFF: 2-bit diffs, bias 2, wraparound
      {
        px.r = (unsigned char)(px.r + (int)((b1 >> 4) & 0x03) - 2);
        px.g = (unsigned char)(px.g + (int)((b1 >> 2) & 0x03) - 2);
        px.b = (unsigned char)(px.b + (int)(b1 & 0x03) - 2);
      }
      else if ((b1 & 0xC0) == 0x80)      // QOI_OP_LUMA: 6-bit dg bias 32; nibbles dr-dg/db-dg bias 8
      {
        if (p + 1 > chunks_end)
          return false;
        const unsigned char b2 = src[p++];
        const int vg = (int)(b1 & 0x3F) - 32;
        px.r = (unsigned char)(px.r + vg - 8 + (int)((b2 >> 4) & 0x0F));
        px.g = (unsigned char)(px.g + vg);
        px.b = (unsigned char)(px.b + vg - 8 + (int)(b2 & 0x0F));
      }
      else                               // QOI_OP_RUN: bias -1; this pixel plus `run` more
      {
        run = (int)(b1 & 0x3F);
      }
      index[ImQoiHash(&px)] = px;
    }
    else
    {
      return false;                      // stream ended before width*height pixels were covered
    }

    dst[i * 4 + 0] = px.r;
    dst[i * 4 + 1] = px.g;
    dst[i * 4 + 2] = px.b;
    dst[i * 4 + 3] = px.a;
  }

  *out_width = (int)uw;
  *out_height = (int)uh;
  return true;
}
