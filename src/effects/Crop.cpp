/**
 * @file
 * @brief Source file for Crop effect class (cropping any side, with x/y offsets)
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * @ref License
 */

// Copyright (c) 2008-2019 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "Crop.h"
#include "Exceptions.h"
#include "KeyFrame.h"
#include "Settings.h"
#include "gpu/GpuDevice.h"
#include "gpu/GpuFrame.h"
#include "skia/include/core/SkImageInfo.h"
#include "skia/include/core/SkPixmap.h"
#include "gpu/GpuTelemetry.h"
#include "skia/include/core/SkCanvas.h"
#include "skia/include/core/SkImage.h"
#include "skia/include/core/SkPaint.h"
#include "skia/include/core/SkRRect.h"
#include "skia/include/core/SkRect.h"
#include "skia/include/core/SkSamplingOptions.h"

#include <QImage>
#include <QPainter>
#include <QRectF>
#include <QRect>
#include <QSize>

using namespace openshot;

/// Default constructor, useful when using Json to load the effect properties
Crop::Crop() : Crop::Crop(0.0, 0.0, 0.0, 0.0, 0.0, 0.0) {}

Crop::Crop(
	Keyframe left, Keyframe top,
	Keyframe right, Keyframe bottom, Keyframe radius,
	Keyframe x, Keyframe y) :
		left(left), top(top), right(right), bottom(bottom), radius(radius), x(x), y(y), resize(true)
{
	// Init effect properties
	init_effect_details();
}

// Init effect settings
void Crop::init_effect_details()
{
	/// Initialize the values of the EffectInfo struct.
	InitEffectInfo();

	/// Set the effect info
	info.class_name = "Crop";
	info.name = "Crop";
	info.description = "Crop out any part of your video.";
	info.has_audio = false;
	info.has_video = true;
}

// This method is required for all derived classes of EffectBase, and returns a
// modified openshot::Frame object
#include <QPainter>
#include <QPainterPath>

#include <QPainter>
#include <QPainterPath>
#include <algorithm> // std::clamp, std::min

std::shared_ptr<openshot::Frame> Crop::GetFrame(std::shared_ptr<openshot::Frame> frame, int64_t frame_number)
{
    // A frame already on the GPU is cropped there (Settings::GPU_CROP, W29), with the same
    // geometry as below; GetImage() would read it back, and did, once per clip per frame.
    if (!resize && Settings::Instance()->GPU_CROP && frame->IsGpuBacked() &&
        frame->GpuBacking()->ownedByThisThread()) {
        if (std::shared_ptr<openshot::Frame> done = GetFrameOnGpu(frame, frame->GpuBacking(), frame_number)) {
            openshot::GpuCounters::Add(openshot::GpuCounters::CropOnGpu);
            return done;
        }
    }
    // A host-memory frame -- an image clip or a shape with a crop or rounded corners -- goes onto
    // the GPU here rather than through QPainter (2026-09-24), so it composites without leaving it.
    // A still hands over the same QImage every frame, and is uploaded once.
    if (!resize && Settings::Instance()->GPU_CROP && !frame->IsGpuBacked() &&
        openshot::GpuDevice::Instance().available()) {
        if (std::shared_ptr<openshot::GpuFrame> uploaded = HostSource(frame)) {
            if (std::shared_ptr<openshot::Frame> done = GetFrameOnGpu(frame, uploaded, frame_number)) {
                openshot::GpuCounters::Add(openshot::GpuCounters::CropOnGpu);
                return done;
            }
        }
    }
    if (frame->IsGpuBacked())
        openshot::GpuCounters::Add(openshot::GpuCounters::CropReadback);   // GetImage() below

    // Get the frame's image
    std::shared_ptr<QImage> frame_image = frame->GetImage();

    // Get current keyframe values
    double left_value   = left.GetValue(frame_number);
    double top_value    = top.GetValue(frame_number);
    double right_value  = right.GetValue(frame_number);
    double bottom_value = bottom.GetValue(frame_number);
    double radius_value = radius.GetValue(frame_number);   // 0..1 normalized

    // Get the current shift amount
    double x_shift = x.GetValue(frame_number);
    double y_shift = y.GetValue(frame_number);

    QSize sz = frame_image->size();

    // Compute destination rectangle to paint into
    QRectF paint_r(
        left_value * sz.width(), top_value * sz.height(),
        std::max(0.0, 1.0 - left_value - right_value) * sz.width(),
        std::max(0.0, 1.0 - top_value - bottom_value) * sz.height());

    if (paint_r.width() <= 0.0 || paint_r.height() <= 0.0) {
    	// Fully cropped: emit a transparent frame instead of leaving the source
    	// unmodified, so an h=0/w=0 keyframe renders as invisible.
    	QImage empty(sz, QImage::Format_RGBA8888_Premultiplied);
    	empty.fill(Qt::transparent);
    	frame->AddImage(std::make_shared<QImage>(empty));
    	return frame;
    }

    // Copy rectangle is destination translated by offsets
    QRectF copy_r = paint_r;
    copy_r.translate(x_shift * sz.width(), y_shift * sz.height());

    // Constrain offset copy rect to stay within image borders
    if (copy_r.left() < 0) {
        paint_r.setLeft(paint_r.left() - copy_r.left());
        copy_r.setLeft(0);
    }
    if (copy_r.right() > sz.width()) {
        paint_r.setRight(paint_r.right() - (copy_r.right() - sz.width()));
        copy_r.setRight(sz.width());
    }
    if (copy_r.top() < 0) {
        paint_r.setTop(paint_r.top() - copy_r.top());
        copy_r.setTop(0);
    }
    if (copy_r.bottom() > sz.height()) {
        paint_r.setBottom(paint_r.bottom() - (copy_r.bottom() - sz.height()));
        copy_r.setBottom(sz.height());
    }

    QImage cropped(sz, QImage::Format_RGBA8888_Premultiplied);
    cropped.fill(Qt::transparent);

    QPainter p(&cropped);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // ---- Rounded-rect clip with uniform radius from min(width, height) ----
    const qreal min_dim = std::min(paint_r.width(), paint_r.height());
    const qreal r_norm  = std::clamp(static_cast<qreal>(radius_value), static_cast<qreal>(0.0), static_cast<qreal>(1.0));
    const qreal r_px    = r_norm * (min_dim * 0.5); // max corner radius = min_dim/2

    if (r_px > 0.0) {
        QPainterPath clip;
        clip.addRoundedRect(paint_r, r_px, r_px); // rx == ry
        p.setClipPath(clip);
    }

    // Draw the source image portion into the (possibly rounded) destination rect
    p.drawImage(paint_r, *frame_image, copy_r);
    p.end();

    if (resize) {
        // Resize image to match cropped QRect (transparent rounded corners)
        frame->AddImage(std::make_shared<QImage>(cropped.copy(paint_r.toRect())));
    } else {
        // Maintain frame size; rounded crop sits on transparent background
        frame->AddImage(std::make_shared<QImage>(cropped.copy()));
    }

    return frame;
}

// The QPainter path above, in Skia, on the frame's own GPU surface. Same rects, same clamping,
// same radius; an antialiased rounded-rect clip and a bilinear draw between fractional rects,
// which is what QPainter's Antialiasing + SmoothPixmapTransform do. Only the rasteriser differs.
// Null means "use the QPainter path".
struct Crop::HostSourceCache
{
    qint64 key = 0;
    unsigned long long generation = 0;
    std::shared_ptr<openshot::GpuFrame> surface;
};

std::shared_ptr<openshot::GpuFrame> Crop::HostSource(const std::shared_ptr<openshot::Frame>& frame)
{
    std::shared_ptr<QImage> image = frame->GetImage();
    if (!image || image->isNull() || image->format() != QImage::Format_RGBA8888_Premultiplied)
        return nullptr;
    const unsigned long long generation = openshot::GpuDevice::Generation();
    if (host_source && host_source->surface && host_source->key == image->cacheKey() &&
        host_source->generation == generation && host_source->surface->ownedByThisThread()) {
        openshot::GpuCounters::Add(openshot::GpuCounters::UploadCached);
        return host_source->surface;
    }
    auto cache = std::make_shared<HostSourceCache>();
    cache->key = image->cacheKey();
    cache->generation = generation;
    cache->surface = openshot::GpuFrame::Create(image->width(), image->height(), kRGBA_8888_SkColorType);
    const SkPixmap pixels(SkImageInfo::Make(image->width(), image->height(), kRGBA_8888_SkColorType,
                                            kPremul_SkAlphaType),
                          image->constBits(), image->bytesPerLine());
    if (!cache->surface || !cache->surface->upload(pixels))
        return nullptr;
    host_source = cache;
    return cache->surface;
}

std::shared_ptr<openshot::Frame> Crop::GetFrameOnGpu(std::shared_ptr<openshot::Frame> frame,
                                                     const std::shared_ptr<openshot::GpuFrame>& source,
                                                     int64_t frame_number)
{
    const int w = source->width();
    const int h = source->height();

    const double left_value   = left.GetValue(frame_number);
    const double top_value    = top.GetValue(frame_number);
    const double right_value  = right.GetValue(frame_number);
    const double bottom_value = bottom.GetValue(frame_number);
    const double radius_value = radius.GetValue(frame_number);
    const double x_shift = x.GetValue(frame_number);
    const double y_shift = y.GetValue(frame_number);

    double pl = left_value * w, pt = top_value * h;
    double pr = pl + std::max(0.0, 1.0 - left_value - right_value) * w;
    double pb = pt + std::max(0.0, 1.0 - top_value - bottom_value) * h;

    std::shared_ptr<openshot::GpuFrame> out = openshot::GpuFrame::Create(w, h);
    if (!out)
        return nullptr;
    SkCanvas* canvas = out->canvas();
    canvas->clear(SK_ColorTRANSPARENT);   // a pooled surface still holds its last frame

    if (pr - pl > 0.0 && pb - pt > 0.0) {
        double cl = pl + x_shift * w, ct = pt + y_shift * h;
        double cr = pr + x_shift * w, cb = pb + y_shift * h;
        if (cl < 0) { pl -= cl; cl = 0; }
        if (cr > w) { pr -= cr - w; cr = w; }
        if (ct < 0) { pt -= ct; ct = 0; }
        if (cb > h) { pb -= cb - h; cb = h; }

        const SkRect paint_r = SkRect::MakeLTRB(float(pl), float(pt), float(pr), float(pb));
        const SkRect copy_r = SkRect::MakeLTRB(float(cl), float(ct), float(cr), float(cb));
        const double min_dim = std::min(pr - pl, pb - pt);
        const double r_px = std::clamp(radius_value, 0.0, 1.0) * (min_dim * 0.5);

        sk_sp<SkImage> image = source->snapshot();
        if (!image)
            return nullptr;
        canvas->save();
        if (r_px > 0.0)
            canvas->clipRRect(SkRRect::MakeRectXY(paint_r, float(r_px), float(r_px)), true);
        SkPaint paint;
        paint.setAntiAlias(true);
        canvas->drawImageRect(image, copy_r, paint_r,
                              SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kNone),
                              &paint, SkCanvas::kStrict_SrcRectConstraint);
        canvas->restore();
    }
    // else fully cropped: the transparent frame, as the QPainter path emits.

    frame->AttachGpuFrame(out);
    return frame;
}

// Generate JSON string of this object
std::string Crop::Json() const {

	// Return formatted string
	return JsonValue().toStyledString();
}

// Generate Json::Value for this object
Json::Value Crop::JsonValue() const {

	// Create root json object
	Json::Value root = EffectBase::JsonValue(); // get parent properties
	root["type"] = info.class_name;
	root["left"] = left.JsonValue();
	root["top"] = top.JsonValue();
	root["right"] = right.JsonValue();
	root["bottom"] = bottom.JsonValue();
	root["x"] = x.JsonValue();
	root["y"] = y.JsonValue();
	root["resize"] = resize;

	// return JsonValue
	return root;
}

// Load JSON string into this object
void Crop::SetJson(const std::string value) {

	// Parse JSON string into JSON objects
	try
	{
		const Json::Value root = openshot::stringToJson(value);
		// Set all values that match
		SetJsonValue(root);
	}
	catch (const std::exception& e)
	{
		// Error parsing JSON (or missing keys)
		throw InvalidJSON("JSON is invalid (missing keys or invalid data types)");
	}
}

// Load Json::Value into this object
void Crop::SetJsonValue(const Json::Value root) {

	// Set parent data
	EffectBase::SetJsonValue(root);

	// Set data from Json (if key is found)
	if (!root["left"].isNull())
		left.SetJsonValue(root["left"]);
	if (!root["top"].isNull())
		top.SetJsonValue(root["top"]);
	if (!root["right"].isNull())
		right.SetJsonValue(root["right"]);
	if (!root["bottom"].isNull())
		bottom.SetJsonValue(root["bottom"]);
	if (!root["x"].isNull())
		x.SetJsonValue(root["x"]);
	if (!root["y"].isNull())
		y.SetJsonValue(root["y"]);
	if (!root["resize"].isNull())
		resize = root["resize"].asBool();
}

// Get all properties for a specific frame
std::string Crop::PropertiesJSON(int64_t requested_frame) const {

	// Generate JSON properties list
	Json::Value root = BasePropertiesJSON(requested_frame);

	// Keyframes
	root["left"] = add_property_json("Margin: Left", left.GetValue(requested_frame), "float", "", &left, 0.0, 1.0, false, requested_frame);
	root["top"] = add_property_json("Margin: Top", top.GetValue(requested_frame), "float", "", &top, 0.0, 1.0, false, requested_frame);
	root["right"] = add_property_json("Margin: Right", right.GetValue(requested_frame), "float", "", &right, 0.0, 1.0, false, requested_frame);
	root["bottom"] = add_property_json("Margin: Bottom", bottom.GetValue(requested_frame), "float", "", &bottom, 0.0, 1.0, false, requested_frame);
	root["x"] = add_property_json("X Offset", x.GetValue(requested_frame), "float", "", &x, -1.0, 1.0, false, requested_frame);
	root["y"] = add_property_json("Y Offset", y.GetValue(requested_frame), "float", "", &y, -1.0, 1.0, false, requested_frame);

	// Add replace_image choices (dropdown style)
	root["resize"] = add_property_json("Resize Image", resize, "int", "", NULL, 0, 1, false, requested_frame);
	root["resize"]["choices"].append(add_property_choice_json("Yes", true, resize));
	root["resize"]["choices"].append(add_property_choice_json("No", false, resize));

	// Return formatted string
	return root.toStyledString();
}
