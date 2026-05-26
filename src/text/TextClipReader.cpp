#include "TextClipReader.h"

#include "../Exceptions.h"
#include "../Frame.h"
#include "../Json.h"
#include "../subtitle/SkiaRenderer.h"
#include "TextClipRenderer.h"

#include <skia/include/core/SkBitmap.h>
#include <skia/include/core/SkCanvas.h>
#include <skia/include/core/SkImageInfo.h>

#include <QColor>
#include <QImage>

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace openshot {

namespace {

// Blur reach: cover ~3 σ of the gaussian (the rest is visually negligible).
constexpr double SHADOW_BLUR_SIGMA_MULTIPLIER = 3.0;

text::TextAlignment parseAlignment(const std::string& s) {
    if (s == "LEFT") return text::TextAlignment::LEFT;
    if (s == "RIGHT") return text::TextAlignment::RIGHT;
    return text::TextAlignment::CENTER;
}

const char* alignmentToString(text::TextAlignment a) {
    switch (a) {
    case text::TextAlignment::LEFT:  return "LEFT";
    case text::TextAlignment::RIGHT: return "RIGHT";
    default:                          return "CENTER";
    }
}

text::TextTransform parseTransform(const std::string& s) {
    if (s == "UPPERCASE")  return text::TextTransform::UPPERCASE;
    if (s == "LOWERCASE")  return text::TextTransform::LOWERCASE;
    if (s == "CAPITALIZE") return text::TextTransform::CAPITALIZE;
    return text::TextTransform::NONE;
}

const char* transformToString(text::TextTransform t) {
    switch (t) {
    case text::TextTransform::UPPERCASE:  return "UPPERCASE";
    case text::TextTransform::LOWERCASE:  return "LOWERCASE";
    case text::TextTransform::CAPITALIZE: return "CAPITALIZE";
    default:                                return "NONE";
    }
}

void styleFromJson(const Json::Value& j, text::TextClipStyle& style) {
    if (!j["fontFamily"].isNull())            style.fontFamily = j["fontFamily"].asString();
    if (!j["italic"].isNull())                style.italic = j["italic"].asBool();
    if (!j["textAlign"].isNull())             style.textAlign = parseAlignment(j["textAlign"].asString());
    if (!j["textTransform"].isNull())         style.textTransform = parseTransform(j["textTransform"].asString());
    if (!j["color"].isNull())                 style.color = j["color"].asString();
    if (!j["lineHeight"].isNull())            style.lineHeight = j["lineHeight"].asDouble();
    if (!j["letterSpacing"].isNull())         style.letterSpacing = j["letterSpacing"].asDouble();
    if (!j["fontWeight"].isNull())            style.fontWeight = j["fontWeight"].asInt();
    if (!j["strokeColor"].isNull())           style.strokeColor = j["strokeColor"].asString();
    if (!j["strokeWidthRatio"].isNull())      style.strokeWidthRatio = j["strokeWidthRatio"].asDouble();
    if (!j["shadowColor"].isNull())           style.shadowColor = j["shadowColor"].asString();
    if (!j["shadowBlurRatio"].isNull())       style.shadowBlurRatio = j["shadowBlurRatio"].asDouble();
    if (!j["shadowDistanceRatio"].isNull())   style.shadowDistanceRatio = j["shadowDistanceRatio"].asDouble();
    if (!j["shadowAngle"].isNull())           style.shadowAngle = j["shadowAngle"].asDouble();
    if (!j["backgroundColor"].isNull())       style.backgroundColor = j["backgroundColor"].asString();
    if (!j["backgroundRadiusRatio"].isNull()) style.backgroundRadiusRatio = j["backgroundRadiusRatio"].asDouble();
    if (!j["backgroundPaddingXRatio"].isNull())style.backgroundPaddingXRatio = j["backgroundPaddingXRatio"].asDouble();
    if (!j["backgroundPaddingYRatio"].isNull())style.backgroundPaddingYRatio = j["backgroundPaddingYRatio"].asDouble();
}

Json::Value styleToJson(const text::TextClipStyle& style) {
    Json::Value j(Json::objectValue);
    j["fontFamily"] = style.fontFamily;
    j["italic"] = style.italic;
    j["textAlign"] = alignmentToString(style.textAlign);
    j["textTransform"] = transformToString(style.textTransform);
    j["color"] = style.color;
    j["lineHeight"] = style.lineHeight;
    j["letterSpacing"] = style.letterSpacing;
    j["fontWeight"] = style.fontWeight;
    j["strokeWidthRatio"] = style.strokeWidthRatio;
    j["shadowBlurRatio"] = style.shadowBlurRatio;
    j["shadowDistanceRatio"] = style.shadowDistanceRatio;
    j["shadowAngle"] = style.shadowAngle;
    j["backgroundRadiusRatio"] = style.backgroundRadiusRatio;
    j["backgroundPaddingXRatio"] = style.backgroundPaddingXRatio;
    j["backgroundPaddingYRatio"] = style.backgroundPaddingYRatio;
    if (style.strokeColor.has_value())     j["strokeColor"]     = *style.strokeColor;
    if (style.shadowColor.has_value())     j["shadowColor"]     = *style.shadowColor;
    if (style.backgroundColor.has_value()) j["backgroundColor"] = *style.backgroundColor;
    return j;
}

void transformationFromJson(const Json::Value& j, text::TextTransformation& t) {
    if (!j["size"].isNull())     t.size = j["size"].asDouble();
    if (!j["rotation"].isNull()) t.rotation = j["rotation"].asDouble();
    if (!j["maxWidth"].isNull()) t.maxWidth = j["maxWidth"].asDouble();
    if (j["position"].isObject()) {
        if (!j["position"]["x"].isNull()) t.positionX = j["position"]["x"].asDouble();
        if (!j["position"]["y"].isNull()) t.positionY = j["position"]["y"].asDouble();
    }
}

Json::Value transformationToJson(const text::TextTransformation& t) {
    Json::Value j(Json::objectValue);
    j["size"] = t.size;
    j["rotation"] = t.rotation;
    j["maxWidth"] = t.maxWidth;
    Json::Value pos(Json::objectValue);
    pos["x"] = t.positionX;
    pos["y"] = t.positionY;
    j["position"] = pos;
    return j;
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TextClipReader::TextClipReader()
    : project_width(1920), frame_width(0), frame_height(0), is_open(false), dirty(true)
{
    Open();
    Close();
}

TextClipReader::TextClipReader(int project_width_, const text::TextClipData& data_)
    : project_width(project_width_), frame_width(0), frame_height(0), data(data_), is_open(false), dirty(true)
{
    Open();
    Close();
}

TextClipReader::~TextClipReader() = default;

// ---------------------------------------------------------------------------
// Open / Close
// ---------------------------------------------------------------------------

void TextClipReader::Open() {
    if (is_open) return;
    renderToImage();   // computes frame_width / frame_height first
    initInfo();
    is_open = true;
}

void TextClipReader::Close() {
    if (!is_open) return;
    is_open = false;
    rendered_image.reset();
    info.vcodec = "";
    info.acodec = "";
}

void TextClipReader::initInfo() {
    info.has_audio = false;
    info.has_video = true;
    info.has_alpha = true;
    info.has_single_image = true;
    info.file_size = 0;
    info.vcodec = "QImage";
    info.width = std::max(1, frame_width);
    info.height = std::max(1, frame_height);
    info.pixel_ratio.num = 1;
    info.pixel_ratio.den = 1;
    info.duration = 60 * 60 * 1; // 1 hour
    info.fps.num = 30;
    info.fps.den = 1;
    info.video_timebase.num = 1;
    info.video_timebase.den = 30;
    info.video_length = static_cast<int64_t>(std::round(info.duration * info.fps.ToDouble()));

    Fraction dar(info.width * info.pixel_ratio.num, info.height * info.pixel_ratio.den);
    dar.Reduce();
    info.display_ratio.num = dar.num;
    info.display_ratio.den = dar.den;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void TextClipReader::renderToImage() {
    if (project_width <= 0 || data.value.empty()) {
        // Minimal 1×1 transparent placeholder — keeps Clip/Frame happy.
        frame_width = 1;
        frame_height = 1;
        bounding_width = 0.0;
        bounding_height = 0.0;
        frame_center_project_x = data.transformation.positionX;
        frame_center_project_y = data.transformation.positionY;
        rendered_image = std::make_shared<QImage>(1, 1, QImage::Format_RGBA8888_Premultiplied);
        rendered_image->fill(QColor(0, 0, 0, 0));
        dirty = false;
        return;
    }

    // 1. Compute paint + background + layout (using only a measuring SkiaRenderer).
    const std::string transformed = text::transformTextValue(data.value, data.style.textTransform);
    const text::TextClipPaintStyle paint =
        text::convertTextStyleToPaintStyle(data.style, data.transformation, project_width);

    // `layoutPaint` drives line breaks at LAYOUT_REFERENCE_SIZE so wrap decisions are stable
    // across `transformation.size`. See BACKEND_PATCH_LINE_CALCULATION.md.
    text::TextTransformation refTransformation = data.transformation;
    refTransformation.size = text::LAYOUT_REFERENCE_SIZE;
    const text::TextClipPaintStyle layoutPaint =
        text::convertTextStyleToPaintStyle(data.style, refTransformation, project_width);

    const auto background = text::convertBackgroundStyle(data.style, paint);

    // maxWidth is a dimensionless multiplier of the canvas-and-size scale; convert to pixels.
    // See BACKEND_PATCH_MAX_WIDTH_SIZE_RELATIVE.md.
    const double maxWidthPx = data.transformation.maxWidth > 0.0
        ? project_width * text::SIZE_BASE_COEFFICIENT
              * data.transformation.size * data.transformation.maxWidth
        : 0.0;
    const double wrapWidth    = maxWidthPx > 0.0 ? maxWidthPx : 1e9;
    const double userMaxWidth = maxWidthPx;

    // Layout needs a canvas-less renderer for font measurement only. Skia requires a canvas,
    // so spin up a tiny dummy bitmap just to satisfy the API. The canvas isn't drawn to.
    SkBitmap measureBitmap;
    measureBitmap.allocPixels(SkImageInfo::MakeN32Premul(1, 1));
    SkCanvas measureCanvas(measureBitmap);
    subtitle::SkiaRenderer measureRenderer(&measureCanvas);

    const text::TextClipLayout layout = text::layoutTextAtReferenceSize(
        transformed, paint, layoutPaint, wrapWidth, userMaxWidth, &measureRenderer);

    // 2. Compute the bounding rect (text + bg padding).
    const double bgPaddingX = background.has_value() ? background->paddingX : 0.0;
    const double bgPaddingY = background.has_value() ? background->paddingY : 0.0;
    const double boundingWidth  = layout.layoutWidth + 2.0 * bgPaddingX;
    const double boundingHeight = layout.textHeight  + 2.0 * bgPaddingY;
    bounding_width  = boundingWidth;
    bounding_height = boundingHeight;

    // 2a. Apply alignment anchoring to derive the bbox CENTRE on the project canvas.
    //     position.x is interpreted as:
    //       LEFT   -> left edge of bbox    (centre = positionX + boundingWidth/2)
    //       CENTER -> centre of bbox       (centre = positionX)
    //       RIGHT  -> right edge of bbox   (centre = positionX - boundingWidth/2)
    //     position.y is always the vertical centre.
    const double alignmentOffset = text::alignmentOffsetX(data.style.textAlign, boundingWidth);
    frame_center_project_x = data.transformation.positionX + alignmentOffset;
    frame_center_project_y = data.transformation.positionY;

    // 3. Compute extra padding for shadow blur/distance and stroke.
    const double strokeHalf = paint.stroke.has_value() ? paint.stroke->width / 2.0 : 0.0;
    double shadowExtentX = 0.0;
    double shadowExtentY = 0.0;
    if (paint.dropShadow.has_value()) {
        const double angleRad = paint.dropShadow->angle * M_PI / 180.0;
        const double dx = std::abs(std::cos(angleRad) * paint.dropShadow->distance);
        const double dy = std::abs(std::sin(angleRad) * paint.dropShadow->distance);
        const double blurExtent = SHADOW_BLUR_SIGMA_MULTIPLIER * paint.dropShadow->blur;
        shadowExtentX = dx + blurExtent;
        shadowExtentY = dy + blurExtent;
    }
    const double padX = std::max(strokeHalf, shadowExtentX);
    const double padY = std::max(strokeHalf, shadowExtentY);

    // Pre-rotation frame: bounding rect centred, with symmetric padding around it.
    const double preW = boundingWidth  + 2.0 * padX;
    const double preH = boundingHeight + 2.0 * padY;

    // 4. Rotation AABB — the smallest axis-aligned rect containing the rotated preW×preH.
    double aabbW = preW;
    double aabbH = preH;
    if (data.transformation.rotation != 0.0) {
        const double th = data.transformation.rotation * M_PI / 180.0;
        const double c = std::abs(std::cos(th));
        const double s = std::abs(std::sin(th));
        aabbW = preW * c + preH * s;
        aabbH = preW * s + preH * c;
    }

    frame_width  = std::max(1, static_cast<int>(std::ceil(aabbW)));
    frame_height = std::max(1, static_cast<int>(std::ceil(aabbH)));

    // 5. Allocate the tight frame.
    rendered_image = std::make_shared<QImage>(frame_width, frame_height, QImage::Format_RGBA8888_Premultiplied);
    rendered_image->fill(QColor(0, 0, 0, 0));

    SkBitmap bitmap;
    const SkImageInfo skiaInfo = SkImageInfo::MakeN32Premul(frame_width, frame_height);
    if (!bitmap.installPixels(skiaInfo, rendered_image->bits(), rendered_image->bytesPerLine())) {
        dirty = false;
        return;
    }
    SkCanvas canvas(bitmap);
    subtitle::SkiaRenderer renderer(&canvas);

    // 6. Centre the bounding box at the frame's centre; rotate around that centre.
    canvas.save();
    canvas.translate(static_cast<float>(frame_width)  / 2.0f,
                     static_cast<float>(frame_height) / 2.0f);
    if (data.transformation.rotation != 0.0) {
        canvas.rotate(static_cast<float>(data.transformation.rotation));
    }
    // renderLayout expects origin = top-left of the text block (NOT the background).
    // The background extends paddingX/Y beyond, so to centre the BACKGROUND on (0,0):
    //   text block top-left = (-layoutWidth/2, -textHeight/2)
    const double originX = -layout.layoutWidth / 2.0;
    const double originY = -layout.textHeight  / 2.0;
    text::renderLayout(layout, paint, background, originX, originY, &renderer);
    canvas.restore();

    dirty = false;
}

// ---------------------------------------------------------------------------
// GetFrame
// ---------------------------------------------------------------------------

std::shared_ptr<Frame> TextClipReader::GetFrame(int64_t requested_frame) {
    const std::lock_guard<std::recursive_mutex> lock(getFrameMutex);

    if (!is_open) Open();
    if (dirty) {
        renderToImage();
        initInfo();
    }

    const int sample_count = Frame::GetSamplesPerFrame(requested_frame, info.fps, info.sample_rate, info.channels);

    if (!rendered_image) {
        return std::make_shared<Frame>(requested_frame, std::max(1, frame_width),
                                       std::max(1, frame_height), "#00000000",
                                       sample_count, info.channels);
    }

    auto frame = std::make_shared<Frame>(
        requested_frame, rendered_image->width(), rendered_image->height(),
        "#00000000", sample_count, info.channels);
    frame->AddImage(std::make_shared<QImage>(rendered_image->copy()));
    return frame;
}

// ---------------------------------------------------------------------------
// Setters
// ---------------------------------------------------------------------------

void TextClipReader::SetText(const std::string& value) {
    data.value = value;
    dirty = true;
}

void TextClipReader::SetStyle(const text::TextClipStyle& style) {
    data.style = style;
    dirty = true;
}

void TextClipReader::SetTransformation(const text::TextTransformation& transformation) {
    data.transformation = transformation;
    dirty = true;
}

void TextClipReader::SetProjectWidth(int width) {
    project_width = width;
    dirty = true;
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

std::string TextClipReader::Json() const {
    return JsonValue().toStyledString();
}

Json::Value TextClipReader::JsonValue() const {
    Json::Value root = ReaderBase::JsonValue();
    root["type"] = "TextClipReader";
    root["project_width"] = project_width;
    root["value"] = data.value;
    root["style"] = styleToJson(data.style);
    root["transformation"] = transformationToJson(data.transformation);
    return root;
}

void TextClipReader::SetJson(const std::string value) {
    try {
        const Json::Value root = openshot::stringToJson(value);
        SetJsonValue(root);
    } catch (const std::exception& e) {
        throw InvalidJSON("JSON is invalid (missing keys or invalid data types)");
    }
}

void TextClipReader::SetJsonValue(const Json::Value root) {
    ReaderBase::SetJsonValue(root);

    if (!root["project_width"].isNull())   project_width = root["project_width"].asInt();
    if (!root["value"].isNull())           data.value = root["value"].asString();
    if (root["style"].isObject())          styleFromJson(root["style"], data.style);
    if (root["transformation"].isObject()) transformationFromJson(root["transformation"], data.transformation);

    dirty = true;

    if (is_open) {
        renderToImage();
        initInfo();
    }
}

} // namespace openshot
