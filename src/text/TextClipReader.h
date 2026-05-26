#pragma once

#include "../ReaderBase.h"
#include "TextClipTypes.h"

#include <memory>
#include <string>

class QImage;

namespace openshot {

class CacheBase;
class Frame;

/**
 * @brief Reader that renders a single text clip to a frame using Skia.
 *
 * The reader sizes the output frame to fit the rendered content (text + background +
 * shadow/stroke padding + rotation AABB), NOT the project canvas. The resulting frame's
 * CENTER lands at `transformation.positionX / positionY` on the project canvas — so the
 * caller positions the clip with `GRAVITY_CENTER` and
 *   `location_x = positionX / projectWidth  - 0.5`
 *   `location_y = positionY / projectHeight - 0.5`
 * (`scale = SCALE_NONE`).
 *
 * `projectWidth` is required because font sizing is derived from it:
 *   `fontSize = projectWidth × (1/240) × transformation.size`.
 *
 * Rendering pipeline mirrors the frontend CanvasKit reference (`text-rendering.ts`):
 *   1. Apply textTransform (uppercase / lowercase / capitalize).
 *   2. Compute pixel-space paint properties from style + transformation.size + projectWidth.
 *   3. Lay text out into wrapped lines and measure the bounding rect.
 *   4. Allocate a tight RGBA frame including padding for shadow extent, stroke width, and the
 *      rotated AABB.
 *   5. Draw background → shadow → stroke → fill, centred in the frame, rotated in place.
 */
class TextClipReader : public ReaderBase {
public:
    /// Default constructor — 1920 project width, empty text.
    TextClipReader();

    /// @param project_width Project (timeline) canvas width in pixels — drives font sizing.
    /// @param data Text clip data (value, style, transformation).
    TextClipReader(int project_width, const text::TextClipData& data);

    ~TextClipReader() override;

    void Open() override;
    void Close() override;
    bool IsOpen() override { return is_open; }

    CacheBase* GetCache() override { return nullptr; }

    std::shared_ptr<openshot::Frame> GetFrame(int64_t requested_frame) override;

    std::string Name() override { return "TextClipReader"; }

    std::string Json() const override;
    void SetJson(const std::string value) override;
    Json::Value JsonValue() const override;
    void SetJsonValue(const Json::Value root) override;

    // ---- Configuration setters ------------------------------------------------
    void SetText(const std::string& value);
    void SetStyle(const text::TextClipStyle& style);
    void SetTransformation(const text::TextTransformation& transformation);
    void SetProjectWidth(int width);

    const text::TextClipData& Data() const { return data; }
    int ProjectWidth() const { return project_width; }

    /// Frame size (matches `info.width / info.height`). Becomes valid after Open().
    int FrameWidth() const { return frame_width; }
    int FrameHeight() const { return frame_height; }

    /// Bounding rect of the rendered text including background padding, but EXCLUDING the
    /// shadow / stroke / rotation padding that the frame buffer adds around it. The bbox is
    /// centred inside the (frame_width × frame_height) buffer. Becomes valid after Open().
    /// Use these to compute alignment-offset corrections when animating size via Clip-level
    /// `scale_x` keyframes — `boundingWidth × scale` is the visible text width at that scale.
    double BoundingWidth()  const { return bounding_width; }
    double BoundingHeight() const { return bounding_height; }

    /// Position on the project canvas (in pixels) where the rendered frame's CENTRE lands.
    /// This already accounts for alignment anchoring: for `LEFT` alignment, `positionX`
    /// is treated as the left edge of the text bbox (so the centre is shifted right by
    /// boundingWidth/2); for `RIGHT`, the opposite; for `CENTER`, no shift. `positionY`
    /// is always the vertical centre.
    double FrameCenterOnProjectX() const { return frame_center_project_x; }
    double FrameCenterOnProjectY() const { return frame_center_project_y; }

private:
    void initInfo();
    void renderToImage();

    int project_width;
    int frame_width;
    int frame_height;
    double bounding_width{0.0};
    double bounding_height{0.0};
    double frame_center_project_x{0.0};
    double frame_center_project_y{0.0};
    text::TextClipData data;
    std::shared_ptr<QImage> rendered_image;
    bool is_open;
    bool dirty;            ///< True when data changed and rendered_image is stale
};

} // namespace openshot
