#include <QPainter>
#include "../gpu/GpuDevice.h"
#include "../gpu/GpuFrame.h"
#include "skia/include/core/SkCanvas.h"
#include "skia/include/core/SkColor.h"
#include "skia/include/core/SkImage.h"
#include "skia/include/core/SkImageInfo.h"
#include "skia/include/core/SkMatrix.h"
#include "skia/include/core/SkPaint.h"
#include "skia/include/core/SkPixmap.h"
#include "skia/include/core/SkSamplingOptions.h"

#include "CameraMovement.h"
#include "Exceptions.h"
#include "./image-processing-lib/src/Effects/effects.h"

using namespace openshot;

/// Blank constructor, useful when using Json to load the effect properties
CameraMovement::CameraMovement() : rotationAngle(0), zoomPercent(100), moveX(0), moveY(0) {
	// Init effect properties
	init_effect_details();
}

// Default constructor
CameraMovement::CameraMovement(const Keyframe& zoomPercent, const Keyframe& rotationAngle, const Keyframe& moveX, const Keyframe& moveY)
	: rotationAngle(rotationAngle), zoomPercent(zoomPercent), moveX(moveX), moveY(moveY) {
	// Init effect properties
	init_effect_details();
}

// Init effect settings
void CameraMovement::init_effect_details() {
	/// Initialize the values of the EffectInfo struct.
	InitEffectInfo();

	/// Set the effect info
	info.class_name = "CameraMovement";
	info.name = "CameraMovement";
	info.description = "";
	info.has_audio = false;
	info.has_video = true;
}

std::shared_ptr<Frame> CameraMovement::GetFrame(std::shared_ptr<Frame> frame, int64_t frame_number)
{
    //------------------------------------------------------------------
    // 1. Read the key-frames
    //------------------------------------------------------------------
    const double z  =  zoomPercent  .GetValue(frame_number) / 100.0;   // 1.0 = 100 %
    const double mx =  moveX        .GetValue(frame_number);
    const double my =  moveY        .GetValue(frame_number);
    const double a  = -rotationAngle.GetValue(frame_number);           // CW positive

    if (qFuzzyCompare(z, 1.0) && qFuzzyIsNull(mx) && qFuzzyIsNull(my) && qFuzzyIsNull(a))
        return frame;                                                  // nothing to do

    // On the GPU when there is one (2026-09-24): the same transform drawn into a pooled surface,
    // so a GPU-decoded frame is not read back and the result stays on the device. Owner's
    // decision: Skia's bilinear where QPainter smooth-transforms, as in the compositor.
    if (ApplyOnGpu(frame, z, mx, my, a))
        return frame;

    //------------------------------------------------------------------
    // 2. Borrow the source pixels (no deep copy)
    //------------------------------------------------------------------
    std::shared_ptr<QImage> src = frame->GetImage();
    if (!src || src->isNull())
        return frame;

    const int W = src->width();
    const int H = src->height();

    //------------------------------------------------------------------
    // 3. Scratch buffer (thread-local, re-used every frame)
    //------------------------------------------------------------------
    thread_local QImage scratch;
    if (scratch.size()   != src->size() || scratch.format() != src->format()) {
        scratch = QImage(src->size(), src->format());
    }
    scratch.fill(Qt::transparent);

    //------------------------------------------------------------------
    // 4. Build the combined transform  (same logic as your original)
    //------------------------------------------------------------------
    // 4-a) zoom round image-centre
    QTransform T;
    T.translate(W * 0.5, H * 0.5);
    T.scale(z, z);
    T.translate(-W * 0.5, -H * 0.5);

    // 4-b) pan — UI gives % of frame → pixels (note the sign)
    const double dx = -(mx / 100.0) * W;
    const double dy = -(my / 100.0) * H;
    T.translate(dx, dy);

    // 4-c) rotate round the *viewport centre after pan*
    const double cx = W * 0.5 - dx;
    const double cy = H * 0.5 - dy;
    T.translate(cx, cy);
    T.rotate(a);
    T.translate(-cx, -cy);

    //------------------------------------------------------------------
    // 5. Draw once
    //------------------------------------------------------------------
    {
        QPainter p(&scratch);
        if (!qFuzzyCompare(z, 1.0) || !qFuzzyIsNull(a))
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.setWorldTransform(T);
        p.drawImage(0, 0, *src);
    }

    //------------------------------------------------------------------
    // 6. Replace the frame’s image plane (cheap – implicit sharing)
    //------------------------------------------------------------------
    frame->AddImage(std::make_shared<QImage>(scratch.copy()));
    return frame;
}

bool CameraMovement::ApplyOnGpu(const std::shared_ptr<Frame>& frame, double z, double mx, double my,
								double a)
{
	if (!GpuDevice::Instance().available())
		return false;
	const int W = frame->GetWidth();
	const int H = frame->GetHeight();
	if (W <= 0 || H <= 0)
		return false;

	// The frame's pixels as a texture: its own GPU backing, or one upload of a host image.
	sk_sp<SkImage> source;
	std::shared_ptr<GpuFrame> staging;
	if (frame->IsGpuBacked()) {
		const std::shared_ptr<GpuFrame>& gpu = frame->GpuBacking();
		if (!gpu || !gpu->ownedByThisThread())
			return false;
		source = gpu->snapshot();
	} else {
		std::shared_ptr<QImage> image = frame->GetImage();
		if (!image || image->isNull() || image->format() != QImage::Format_RGBA8888_Premultiplied)
			return false;
		staging = GpuFrame::Create(image->width(), image->height(), kRGBA_8888_SkColorType);
		const SkPixmap pixels(SkImageInfo::Make(image->width(), image->height(), kRGBA_8888_SkColorType,
												kPremul_SkAlphaType),
							  image->constBits(), image->bytesPerLine());
		if (!staging || !staging->upload(pixels))
			return false;
		source = staging->snapshot();
	}
	if (!source)
		return false;

	std::shared_ptr<GpuFrame> destination = GpuFrame::Create(W, H, kRGBA_8888_SkColorType);
	SkCanvas* canvas = destination ? destination->canvas() : nullptr;
	if (!canvas)
		return false;
	canvas->clear(SK_ColorTRANSPARENT);

	// The CPU branch's transform, built the same way (see GetFrame).
	QTransform T;
	T.translate(W * 0.5, H * 0.5);
	T.scale(z, z);
	T.translate(-W * 0.5, -H * 0.5);
	const double dx = -(mx / 100.0) * W;
	const double dy = -(my / 100.0) * H;
	T.translate(dx, dy);
	const double cx = W * 0.5 - dx;
	const double cy = H * 0.5 - dy;
	T.translate(cx, cy);
	T.rotate(a);
	T.translate(-cx, -cy);

	const bool smooth = !qFuzzyCompare(z, 1.0) || !qFuzzyIsNull(a);
	SkMatrix m;
	if (smooth) {
		// Qt stores the matrix row-vector style, Skia column-vector style: a transpose.
		m.setAll((SkScalar) T.m11(), (SkScalar) T.m21(), (SkScalar) T.m31(),
				 (SkScalar) T.m12(), (SkScalar) T.m22(), (SkScalar) T.m32(),
				 (SkScalar) T.m13(), (SkScalar) T.m23(), (SkScalar) T.m33());
	} else {
		// Translate only, no smoothing: QPainter snaps the image to whole pixels, so does this.
		m.setTranslate((SkScalar) qRound(T.dx()), (SkScalar) qRound(T.dy()));
	}
	canvas->concat(m);
	SkPaint paint;
	canvas->drawImage(source, 0, 0,
					  smooth ? SkSamplingOptions(SkFilterMode::kLinear) : SkSamplingOptions(), &paint);
	frame->AttachGpuFrame(std::move(destination));
	return true;
}

// Generate JSON string of this object
std::string CameraMovement::Json() const {

	// Return formatted string
	return JsonValue().toStyledString();
}

// Generate Json::Value for this object
Json::Value CameraMovement::JsonValue() const {
	// Create root json object
	Json::Value root = EffectBase::JsonValue(); // get parent properties
	root["type"] = info.class_name;
	root["rotationAngle"] = rotationAngle.JsonValue();
	root["zoomPercent"] = zoomPercent.JsonValue();
	root["moveX"] = moveX.JsonValue();
	root["moveY"] = moveY.JsonValue();

	// return JsonValue
	return root;
}

// Load JSON string into this object
void CameraMovement::SetJson(const std::string value) {

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
void CameraMovement::SetJsonValue(const Json::Value root) {

	// Set parent data
	EffectBase::SetJsonValue(root);

	// Set data from Json (if key is found)
	if (!root["rotationAngle"].isNull())
		rotationAngle.SetJsonValue(root["rotationAngle"]);
	if (!root["zoomPercent"].isNull())
		zoomPercent.SetJsonValue(root["zoomPercent"]);
	if (!root["moveX"].isNull())
		moveX.SetJsonValue(root["moveX"]);
	if (!root["moveY"].isNull())
		moveY.SetJsonValue(root["moveY"]);
}

// Get all properties for a specific frame
std::string CameraMovement::PropertiesJSON(int64_t requested_frame) const {

	// Generate JSON properties list
	Json::Value root = BasePropertiesJSON(requested_frame);

	// Keyframes

	// Return formatted string
	return root.toStyledString();
}
