/**
 * Sharpen.cpp
 * Unsharp-Mask / High-Pass effect for libopenshot
 */

#include "Sharpen.h"
#include "Exceptions.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <omp.h>

using namespace openshot;

// Constructor with default keyframes
Sharpen::Sharpen()
  : amount(10.0)
  , radius(3.0)
  , threshold(0.0)
  , mode(0)
  , channel(0)
{
  init_effect_details();
}

// Constructor from keyframes
Sharpen::Sharpen(Keyframe a, Keyframe r, Keyframe t)
  : amount(a)
  , radius(r)
  , threshold(t)
  , mode(0)
  , channel(0)
{
  init_effect_details();
}

// Initialize effect metadata
void Sharpen::init_effect_details()
{
  InitEffectInfo();
  info.class_name = "Sharpen";
  info.name        = "Sharpen";
  info.description = "Edge-enhancing sharpen filter";
  info.has_audio   = false;
  info.has_video   = true;
}

// Compute three box sizes to approximate a Gaussian of sigma
static void boxes_for_gauss(double sigma, int b[3])
{
  const int n = 3;
  double wi = std::sqrt((12.0 * sigma * sigma / n) + 1.0);
  int wl = int(std::floor(wi));
  if (!(wl & 1)) --wl;
  int wu = wl + 2;
  double mi = (12.0 * sigma * sigma - n*wl*wl - 4.0*n*wl - 3.0*n)
              / (-4.0*wl - 4.0);
  int m = int(std::round(mi));
  for (int i = 0; i < n; ++i)
    b[i] = i < m ? wl : wu;
}

// Blur one axis with an edge-replicate sliding window
static void blur_axis(const QImage& src, QImage& dst, int r, bool vertical)
{
  if (r <= 0) {
    dst = src.copy();
    return;
  }

  int W   = src.width();
  int H   = src.height();
  int bpl = src.bytesPerLine();
  const uchar* in  = src.bits();
  uchar*       out = dst.bits();
  int window = 2*r + 1;

  if (!vertical) {
    #pragma omp parallel for
    for (int y = 0; y < H; ++y) {
      const uchar* rowIn  = in  + y*bpl;
      uchar*       rowOut = out + y*bpl;
      double sumB = rowIn[0]*(r+1), sumG = rowIn[1]*(r+1),
             sumR = rowIn[2]*(r+1), sumA = rowIn[3]*(r+1);
      for (int x = 1; x <= r; ++x) {
        const uchar* p = rowIn + std::min(x, W-1)*4;
        sumB += p[0]; sumG += p[1]; sumR += p[2]; sumA += p[3];
      }
      for (int x = 0; x < W; ++x) {
        uchar* o = rowOut + x*4;
        o[0] = uchar(sumB / window + 0.5);
        o[1] = uchar(sumG / window + 0.5);
        o[2] = uchar(sumR / window + 0.5);
        o[3] = uchar(sumA / window + 0.5);

        const uchar* addP = rowIn + std::min(x+r+1, W-1)*4;
        const uchar* subP = rowIn + std::max(x-r,     0)*4;
        sumB += addP[0] - subP[0];
        sumG += addP[1] - subP[1];
        sumR += addP[2] - subP[2];
        sumA += addP[3] - subP[3];
      }
    }
  }
  else {
    #pragma omp parallel for
    for (int x = 0; x < W; ++x) {
      double sumB = 0, sumG = 0, sumR = 0, sumA = 0;
      const uchar* p0 = in + x*4;
      sumB = p0[0]*(r+1); sumG = p0[1]*(r+1);
      sumR = p0[2]*(r+1); sumA = p0[3]*(r+1);
      for (int y = 1; y <= r; ++y) {
        const uchar* p = in + std::min(y, H-1)*bpl + x*4;
        sumB += p[0]; sumG += p[1]; sumR += p[2]; sumA += p[3];
      }
      for (int y = 0; y < H; ++y) {
        uchar* o = out + y*bpl + x*4;
        o[0] = uchar(sumB / window + 0.5);
        o[1] = uchar(sumG / window + 0.5);
        o[2] = uchar(sumR / window + 0.5);
        o[3] = uchar(sumA / window + 0.5);

        const uchar* addP = in + std::min(y+r+1, H-1)*bpl + x*4;
        const uchar* subP = in + std::max(y-r,     0)*bpl + x*4;
        sumB += addP[0] - subP[0];
        sumG += addP[1] - subP[1];
        sumR += addP[2] - subP[2];
        sumA += addP[3] - subP[3];
      }
    }
  }
}

// Wrapper to handle fractional radius by blending two integer passes
static void box_blur(const QImage& src, QImage& dst, double rf, bool vertical)
{
  int r0 = int(std::floor(rf));
  int r1 = r0 + 1;
  double f = rf - r0;
  if (f < 1e-4) {
    blur_axis(src, dst, r0, vertical);
  }
  else {
    QImage a(src.size(), QImage::Format_ARGB32);
    QImage b(src.size(), QImage::Format_ARGB32);
    blur_axis(src, a, r0, vertical);
    blur_axis(src, b, r1, vertical);

    int pixels = src.width() * src.height();
    const uchar* pa = a.bits();
    const uchar* pb = b.bits();
    uchar*       pd = dst.bits();
    #pragma omp parallel for
    for (int i = 0; i < pixels; ++i) {
      for (int c = 0; c < 4; ++c) {
        pd[i*4+c] = uchar((1.0 - f) * pa[i*4+c]
                        + f         * pb[i*4+c]
                        + 0.5);
      }
    }
  }
}

// Apply three sequential box blurs to approximate Gaussian
static void gauss_blur(const QImage& src, QImage& dst, double sigma)
{
  int b[3];
  boxes_for_gauss(sigma, b);
  QImage t1(src.size(), QImage::Format_ARGB32);
  QImage t2(src.size(), QImage::Format_ARGB32);

  double r = 0.5 * (b[0] - 1);
  box_blur(src , t1, r, false);
  box_blur(t1, t2, r, true);

  r = 0.5 * (b[1] - 1);
  box_blur(t2, t1, r, false);
  box_blur(t1, t2, r, true);

  r = 0.5 * (b[2] - 1);
  box_blur(t2, t1, r, false);
  box_blur(t1, dst, r, true);
}

// Main frame processing
std::shared_ptr<Frame> Sharpen::GetFrame(
  std::shared_ptr<Frame> frame, int64_t frame_number)
{
  auto img = frame->GetImage();
  if (!img || img->isNull())
    return frame;
  if (img->format() != QImage::Format_ARGB32)
    *img = img->convertToFormat(QImage::Format_ARGB32);

  int W = img->width();
  int H = img->height();
  if (W <= 0 || H <= 0)
    return frame;

  // Retrieve keyframe values
  double amt   = amount.GetValue(frame_number);    // 0–40
  double rpx   = radius.GetValue(frame_number);    // px
  double thrUI = threshold.GetValue(frame_number); // 0–1

  // Sigma scaled against 720p reference
  double sigma = std::max(0.1, rpx * H / 720.0);

  // Generate blurred image
  QImage blur(W, H, QImage::Format_ARGB32);
  gauss_blur(*img, blur, sigma);

  int bplS = img->bytesPerLine();
  int bplB = blur.bytesPerLine();
  uchar* sBits = img->bits();
  uchar* bBits = blur.bits();

  #pragma omp parallel for
  for (int y = 0; y < H; ++y) {
    uchar* sRow = sBits + y * bplS;
    uchar* bRow = bBits + y * bplB;
    for (int x = 0; x < W; ++x) {
      uchar* sp = sRow + x*4;
      uchar* bp = bRow + x*4;

      // Compute detail
      double dB  = double(sp[0]) - double(bp[0]);
      double dG  = double(sp[1]) - double(bp[1]);
      double dR  = double(sp[2]) - double(bp[2]);
      double dY  = 0.114*dB + 0.587*dG + 0.299*dR;
      double dYn = std::abs(dY) / 255.0;

      // Skip below threshold
      if (dYn < thrUI)
        continue;

      // Halo limiter for contrast
      auto halo = [](double d){ return (255.0 - std::abs(d)) / 255.0; };

      double outC[3];

      if (mode == 1) {
        // High-Pass Blend: base = blurred + amt * detail
        if (channel == 1) {
          // Luma only
          double inc = amt * dY * halo(dY);
          for (int c = 0; c < 3; ++c)
            outC[c] = bp[c] + inc;
        }
        else if (channel == 2) {
          // Chroma only
          double chroma[3] = { dB - dY, dG - dY, dR - dY };
          for (int c = 0; c < 3; ++c)
            outC[c] = bp[c] + amt * chroma[c] * halo(chroma[c]);
        }
        else {
          // All channels
          double diff[3] = { dB, dG, dR };
          for (int c = 0; c < 3; ++c)
            outC[c] = bp[c] + amt * diff[c] * halo(diff[c]);
        }
      }
      else {
        // Unsharp-Mask: base = original + amt * detail
        if (channel == 1) {
          // Luma only
          double inc = amt * dY * halo(dY);
          for (int c = 0; c < 3; ++c)
            outC[c] = sp[c] + inc;
        }
        else if (channel == 2) {
          // Chroma only
          double chroma[3] = { dB - dY, dG - dY, dR - dY };
          for (int c = 0; c < 3; ++c)
            outC[c] = sp[c] + amt * chroma[c] * halo(chroma[c]);
        }
        else {
          // All channels
          double diff[3] = { dB, dG, dR };
          for (int c = 0; c < 3; ++c)
            outC[c] = sp[c] + amt * diff[c] * halo(diff[c]);
        }
      }

      // Write back clamped
      for (int c = 0; c < 3; ++c)
        sp[c] = uchar(std::clamp(outC[c], 0.0, 255.0) + 0.5);
    }
  }

  return frame;
}

// JSON serialization
std::string Sharpen::Json() const
{
  return JsonValue().toStyledString();
}

Json::Value Sharpen::JsonValue() const
{
  Json::Value root = EffectBase::JsonValue();
  root["type"]      = info.class_name;
  root["amount"]    = amount.JsonValue();
  root["radius"]    = radius.JsonValue();
  root["threshold"] = threshold.JsonValue();
  root["mode"]      = mode;
  root["channel"]   = channel;
  return root;
}

// JSON deserialization
void Sharpen::SetJson(std::string value)
{
  auto root = openshot::stringToJson(value);
  SetJsonValue(root);
}

void Sharpen::SetJsonValue(Json::Value root)
{
  EffectBase::SetJsonValue(root);
  if (!root["amount"].isNull())
    amount.SetJsonValue(root["amount"]);
  if (!root["radius"].isNull())
    radius.SetJsonValue(root["radius"]);
  if (!root["threshold"].isNull())
    threshold.SetJsonValue(root["threshold"]);
  if (!root["mode"].isNull())
    mode = root["mode"].asInt();
  if (!root["channel"].isNull())
    channel = root["channel"].asInt();
}

// Properties for UI sliders
std::string Sharpen::PropertiesJSON(int64_t t) const
{
  Json::Value root = BasePropertiesJSON(t);
  root["amount"] = add_property_json(
    "Amount", amount.GetValue(t), "float", "", &amount, 0, 40, false, t);
  root["radius"] = add_property_json(
    "Radius", radius.GetValue(t), "float", "pixels", &radius, 0, 10, false, t);
  root["threshold"] = add_property_json(
    "Threshold", threshold.GetValue(t), "float", "ratio", &threshold, 0, 1, false, t);
  root["mode"] = add_property_json(
    "Mode", mode, "int", "", nullptr, 0, 1, false, t);
  root["mode"]["choices"].append(add_property_choice_json("UnsharpMask",   0, mode));
  root["mode"]["choices"].append(add_property_choice_json("HighPassBlend", 1, mode));
  root["channel"] = add_property_json(
    "Channel", channel, "int", "", nullptr, 0, 2, false, t);
  root["channel"]["choices"].append(add_property_choice_json("All",    0, channel));
  root["channel"]["choices"].append(add_property_choice_json("Luma",   1, channel));
  root["channel"]["choices"].append(add_property_choice_json("Chroma", 2, channel));
  return root.toStyledString();
}
