/**
 * @file
 * @brief Source file for SphericalProjection effect class
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * @ref License
 */

// Copyright (c) 2008-2025 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "SphericalProjection.h"
#include "Exceptions.h"

#include <cmath>
#include <algorithm>

#ifdef _OPENMP
#include <omp.h>
#endif

using namespace openshot;

SphericalProjection::SphericalProjection()
  : yaw(0.0), pitch(0.0), roll(0.0), fov(90.0),
    projection_mode(0), interpolation(0)
{
    init_effect_details();
}

SphericalProjection::SphericalProjection(Keyframe new_yaw,
                                         Keyframe new_pitch,
                                         Keyframe new_roll,
                                         Keyframe new_fov)
  : yaw(new_yaw), pitch(new_pitch), roll(new_roll), fov(new_fov),
    projection_mode(0), interpolation(0)
{
    init_effect_details();
}

void SphericalProjection::init_effect_details()
{
    InitEffectInfo();
    info.class_name = "SphericalProjection";
    info.name        = "Spherical Projection";
    info.description = "Flatten 360° video with yaw/pitch/roll, sphere or hemisphere mode, nearest or bilinear";
    info.has_audio   = false;
    info.has_video   = true;

    // Keyframes auto-registered
}

std::shared_ptr<openshot::Frame> SphericalProjection::GetFrame(
    std::shared_ptr<openshot::Frame> frame,
    int64_t frame_number)
{
    auto img = frame->GetImage();
    if (img->format() != QImage::Format_ARGB32)
        *img = img->convertToFormat(QImage::Format_ARGB32);

    int W = img->width(), H = img->height();
    int bpl = img->bytesPerLine();
    uchar *src = img->bits();

    QImage output(W, H, QImage::Format_ARGB32);
    output.fill(Qt::black);
    uchar *dst = output.bits();
    int dst_bpl = output.bytesPerLine();

    // read keyframes & convert
    double yaw_r   = yaw.GetValue(frame_number)   * M_PI/180.0;
    double pitch_r = pitch.GetValue(frame_number) * M_PI/180.0;
    double roll_r  = roll.GetValue(frame_number)  * M_PI/180.0;
    double fov_r   = fov.GetValue(frame_number)   * M_PI/180.0;

    // rotation matrix R = Ry * Rx * Rz
    double sy=sin(yaw_r),  cy=cos(yaw_r),
           sp=sin(pitch_r),cp=cos(pitch_r),
           sr=sin(roll_r), cr=cos(roll_r);
    double r00=cy*cr+sy*sp*sr, r01=-cy*sr+sy*sp*cr, r02=sy*cp;
    double r10=cp*sr,          r11=cp*cr,          r12=-sp;
    double r20=-sy*cr+cy*sp*sr, r21=sy*sr+cy*sp*cr, r22=cy*cp;

    // fov scalars
    double hx = tan(fov_r/2.0);
    double vy = hx * (double(H)/W);

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for(int yy=0; yy<H; yy++) {
        uchar *dst_row = dst + yy*dst_bpl;
        double ndc_y = (2.0*(yy+0.5)/H - 1.0) * vy;
        for(int xx=0; xx<W; xx++) {
            double ndc_x = (2.0*(xx+0.5)/W - 1.0) * hx;
            // camera ray
            double vx=ndc_x, vy2=-ndc_y, vz=-1.0;
            double inv = 1.0/sqrt(vx*vx+vy2*vy2+vz*vz);
            vx*=inv; vy2*=inv; vz*=inv;
            // rotate
            double dx = r00*vx + r01*vy2 + r02*vz;
            double dy = r10*vx + r11*vy2 + r12*vz;
            double dz = r20*vx + r21*vy2 + r22*vz;
            // spherical coords
            double lon = atan2(dx, dz);
            double lat = asin(dy);

            // adjust for hemisphere
            if (projection_mode == 1) {
                lon = std::clamp(lon, -M_PI/2.0, M_PI/2.0);
            }

            // UV mapping
            double uf = ((lon + (projection_mode?M_PI/2.0:M_PI))
                         / (projection_mode?M_PI:2.0*M_PI)) * W;
            double vf = (lat + M_PI/2.0) / M_PI * H;

            // sampling
            if (interpolation == 0) {
                int sx = std::clamp(int(std::floor(uf)),0,W-1);
                int sy2= std::clamp(int(std::floor(vf)),0,H-1);
                uchar *s = src + sy2*bpl + sx*4;
                uchar *d = dst_row + xx*4;
                d[0]=s[0]; d[1]=s[1]; d[2]=s[2]; d[3]=s[3];
            } else {
                // bilinear
                double x0 = std::floor(uf), y0 = std::floor(vf);
                double dxr = uf - x0, dyr = vf - y0;
                int x1 = std::clamp(int(x0)+1, 0, W-1);
                int y1 = std::clamp(int(y0)+1, 0, H-1);
                int x0i = std::clamp(int(x0),0,W-1), y0i = std::clamp(int(y0),0,H-1);
                uchar *p00 = src + y0i*bpl + x0i*4;
                uchar *p10 = src + y0i*bpl + x1*4;
                uchar *p01 = src + y1*bpl + x0i*4;
                uchar *p11 = src + y1*bpl + x1*4;
                uchar *d   = dst_row + xx*4;
                for(int c=0;c<4;c++) {
                    double v0 = p00[c] * (1-dxr) + p10[c] * dxr;
                    double v1 = p01[c] * (1-dxr) + p11[c] * dxr;
                    double v2 = v0 * (1-dyr) + v1 * dyr;
                    d[c] = uchar(v2 + 0.5);
                }
            }
        }
    }

    *img = output;
    return frame;
}

std::string SphericalProjection::Json() const
{
    return JsonValue().toStyledString();
}

Json::Value SphericalProjection::JsonValue() const
{
    Json::Value root = EffectBase::JsonValue();
    root["type"]            = info.class_name;
    root["yaw"]             = yaw.JsonValue();
    root["pitch"]           = pitch.JsonValue();
    root["roll"]            = roll.JsonValue();
    root["fov"]             = fov.JsonValue();
    root["projection_mode"] = projection_mode;
    root["interpolation"]   = interpolation;
    return root;
}

void SphericalProjection::SetJson(const std::string value)
{
    try {
        Json::Value root = openshot::stringToJson(value);
        SetJsonValue(root);
    } catch (...) {
        throw InvalidJSON("Invalid JSON for SphericalProjection");
    }
}

void SphericalProjection::SetJsonValue(const Json::Value root)
{
    EffectBase::SetJsonValue(root);
    if (!root["yaw"].isNull())             yaw.SetJsonValue(root["yaw"]);
    if (!root["pitch"].isNull())         pitch.SetJsonValue(root["pitch"]);
    if (!root["roll"].isNull())           roll.SetJsonValue(root["roll"]);
    if (!root["fov"].isNull())             fov.SetJsonValue(root["fov"]);
    if (!root["projection_mode"].isNull()) projection_mode = root["projection_mode"].asInt();
    if (!root["interpolation"].isNull())   interpolation   = root["interpolation"].asInt();
}

std::string SphericalProjection::PropertiesJSON(int64_t requested_frame) const
{
    Json::Value root = BasePropertiesJSON(requested_frame);

    root["yaw"]   = add_property_json("Yaw",
                                      yaw.GetValue(requested_frame),
                                      "float","degrees",
                                      &yaw,-180,180,false,requested_frame);
    root["pitch"] = add_property_json("Pitch",
                                      pitch.GetValue(requested_frame),
                                      "float","degrees",
                                      &pitch,-90,90,false,requested_frame);
    root["roll"]  = add_property_json("Roll",
                                      roll.GetValue(requested_frame),
                                      "float","degrees",
                                      &roll,-180,180,false,requested_frame);
    root["fov"]   = add_property_json("FOV",
                                      fov.GetValue(requested_frame),
                                      "float","degrees",
                                      &fov,1,179,false,requested_frame);

    root["projection_mode"] = add_property_json("Projection Mode",
                                                projection_mode,
                                                "int","",
                                                NULL,0,1,false,requested_frame);
    root["projection_mode"]["choices"].append(add_property_choice_json("Sphere",     0, projection_mode));
    root["projection_mode"]["choices"].append(add_property_choice_json("Hemisphere", 1, projection_mode));

    root["interpolation"] = add_property_json("Interpolation",
                                              interpolation,
                                              "int","",
                                              NULL,0,1,false,requested_frame);
    root["interpolation"]["choices"].append(add_property_choice_json("Nearest",   0, interpolation));
    root["interpolation"]["choices"].append(add_property_choice_json("Bilinear",  1, interpolation));

    return root.toStyledString();
}
