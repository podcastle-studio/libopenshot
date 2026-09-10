// Helpers that build openshot objects exactly the way video-rendering-service does.
//
// Every formula and constructor call here mirrors a specific place in the service
// (VideoRenderingImpl.cpp, KeyframeApplier.cpp, AlphaKeyframe.cpp, Transition.cpp, Animation.cpp,
// Subtitles.cpp). Keep them in sync: a scenario is only meaningful if it drives the library the
// way production does.
#pragma once

#include "Harness.h"

#include "Clip.h"
#include "Color.h"
#include "Enums.h"
#include "FFmpegWriter.h"
#include "FrameMapper.h"
#include "KeyFrame.h"
#include "Point.h"
#include "ReaderBase.h"
#include "effects/Crop.h"
#include "effects/Mask.h"
#include "text/TextAnimationEngine.h"
#include "text/TextClipReader.h"
#include "text/TextClipTypes.h"
#include "text/TextStyleKeyframes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace golden::recipes {

// ── time → frame conventions (three of them coexist in the service) ──────────────────────────
inline int timeToClipFrame(double t, double fps) { return static_cast<int>(std::lround(fps * t)) + 1; }   // clip curves
inline int timeToFrame(double t, double fps)     { return std::max(static_cast<int>(std::lround(fps * t)), 1); } // fades, camera

// ── keyframes ────────────────────────────────────────────────────────────────────────────────
struct Interp {
    openshot::InterpolationType type = openshot::BEZIER;
    float v1 = 0, v2 = 0, v3 = 0, v4 = 0;   // payload bezier values
};
inline const Interp kLinear{openshot::LINEAR, 0, 0, 0, 0};
inline const Interp kConstant{openshot::CONSTANT, 0, 0, 0, 0};
inline const Interp kEaseInOut{openshot::BEZIER, 0.42f, 0.0f, 0.58f, 1.0f};

// KeyframeApplier convention: V3,V4 = this point's left handle; V1,V2 = previous point's right handle.
openshot::Point makePoint(int frame, float value, const Interp& interp);
void appendKeyframePoint(openshot::Keyframe& curve, int frame, float value, const Interp& interp);

// Transition.cpp / Animation.cpp convention: same point gets Left(V1,V2) + Right(V3,V4).
struct RampWindow {
    int64_t startFrame = 1;
    int64_t endFrame = 1;
    int64_t frameAt(double t01) const;
};
struct RampKf { double time; double value; Interp interp = kLinear; };
openshot::Keyframe createRampKeyframe(const std::vector<RampKf>& kfs, const RampWindow& ramp);
openshot::Keyframe mergeKeyframes(const openshot::Keyframe& a, const openshot::Keyframe& b);

// ── geometry (KeyframeApplier) ───────────────────────────────────────────────────────────────
struct Size { int width = 0; int height = 0; };
struct BBox { float cx = 0.5f, cy = 0.5f, w = 1.f, h = 1.f; };

Size predictPrescaledSize_ScaleNone(const openshot::ReaderInfo& info, int previewW, int previewH);
float calculateFitScale(Size source, Size video, float hScale, float vScale, const BBox& bbox);

struct ScaleKf    { double time; float hScale, vScale; Interp interp = Interp{}; };
struct PositionKf { double time; float cx, cy;         Interp interp = Interp{}; };
struct RotationKf { double time; float degrees;        Interp interp = Interp{}; };
struct Transform {
    BBox bbox;
    float hScale = 1.f, vScale = 1.f;
    float rotation = 0.f;
    std::vector<ScaleKf> scaleKfs;
    std::vector<PositionKf> positionKfs;
    std::vector<RotationKf> rotationKfs;
};
// applyTransformKeyframes: resets scale/location/rotation, seeds baseline at frame 1, emits kfs.
void applyTransform(openshot::Clip& clip, const Transform& t, Size source, Size video, double fps);

// buildAlphaKeyframe (AlphaKeyframe.cpp) + mergeOpacityKeyframes
struct Fade { double fadeIn = 0, fadeOut = 0; Interp easeOut = kEaseInOut; };
struct OpacityKf { double time; float value; Interp interp = Interp{}; };
openshot::Keyframe buildAlphaKeyframe(float mainOpacity, double clipDuration, double fps, double trimStart,
                                      const std::optional<Fade>& fade,
                                      const std::vector<std::pair<double, double>>& ghostIntervals = {});
void mergeOpacityKeyframes(openshot::Keyframe& alpha, const std::vector<OpacityKf>& kfs, double fps);

// ── clips (VideoRenderingImpl.cpp) ───────────────────────────────────────────────────────────
inline int layerOf(int trackIdx, int priority) { return trackIdx * 1000 + priority; }

openshot::ReaderBase* imageReader(Scene& scene, const std::string& path);        // QtImageReader, owned by scene
openshot::ReaderBase* videoReader(Scene& scene, const std::string& path);        // FFmpegReader, owned by scene

openshot::Clip* backgroundClip(Scene& scene, const std::string& path);           // addBackgroundImageClip

struct MediaSpec {
    std::string path;
    bool isImage = false;
    double start = 0, end = 3;          // timeline seconds
    double trimStart = 0, trimEnd = 0;
    double speed = 1;
    int track = 1, priority = 0;
    std::optional<Transform> transform;  // nullopt = leave library defaults (SCALE_NONE, centre)
    float opacity = 1.f;
    std::optional<Fade> fade;
    std::vector<OpacityKf> opacityKfs;
    std::vector<std::pair<double, double>> ghostIntervals;
};
// addVisualMediaClip up to (not including) effects; returns the clip so scenarios add effects.
openshot::Clip* mediaClip(Scene& scene, const MediaSpec& spec);

openshot::Crop* cropEffect(openshot::Keyframe left, openshot::Keyframe top, openshot::Keyframe right,
                           openshot::Keyframe bottom, openshot::Keyframe radius = 0);  // apply_before_clip, resize=false, Layer 999999

class ClipSyncedFrameMapper : public openshot::FrameMapper {   // verbatim copy of the service class
public:
    ClipSyncedFrameMapper(openshot::ReaderBase* reader, openshot::Fraction target_fps,
                          openshot::PulldownType pulldown, int sample_rate, int channels,
                          openshot::ChannelLayout layout, const openshot::Clip* clip)
        : openshot::FrameMapper(reader, target_fps, pulldown, sample_rate, channels, layout),
          sourceClip(clip) {}

    std::shared_ptr<openshot::Frame> GetFrame(int64_t frame_number) override {
        int64_t sourceFrame = std::max<int64_t>(1, frame_number - sourceClip->mFreezeFramesCountAtBeginning);
        if (sourceClip->time.GetLength() > 1) {
            sourceFrame = std::max<int64_t>(1, sourceClip->time.GetLong(sourceFrame));
        }
        return openshot::FrameMapper::GetFrame(sourceFrame);
    }

private:
    const openshot::Clip* sourceClip;
};

openshot::Mask* imageMask(Scene& scene, const std::string& path);                                  // Mask(reader, 0, 3), invert
openshot::Mask* videoMask(Scene& scene, const std::string& path, const openshot::Clip& parent, double fps);

// ── text (addTextClip) ───────────────────────────────────────────────────────────────────────
openshot::text::TextClipStyle baseStyle(const std::string& fontPath);
const openshot::text::AnimationPresetMap& presets();   // "fade-in", "rise-chars", "pulse", "fade-out"

struct TextSpec {
    std::string value;
    openshot::text::TextClipStyle style;
    openshot::text::TextTransformation transformation;   // size, maxWidth, tiltX/Y; position/rotation ignored
    float posX = 0.5f, posY = 0.5f;                       // canvas fractions of the bbox centre
    float rotation = 0.f;                                 // 2D rotation (moved to the Clip)
    double start = 0, end = 3;
    int track = 2, priority = 0;
    std::optional<openshot::text::TextAnimations> animations;
    std::optional<openshot::text::TextStyleKeyframes> styleKeyframes;
    float opacity = 1.f;
    std::optional<Fade> fade;
};
openshot::Clip* textClip(Scene& scene, const TextSpec& spec, double fps);

// ── transitions (Transition.cpp) ─────────────────────────────────────────────────────────────
enum class TransitionEffect {
    Alpha, Blur, DiagonalBlur, RotationalBlur, ZoomBlur, Zoom, BorderReflectedMove, BorderReflectedRotation,
    Bars, ThresholdWipeMask, SplitShift, CircleMask, Exposure, Brightness, ColorShift
};
const char* name(TransitionEffect e);
// Adds the effect with a representative parameter ramp over `ramp` (clip-local frames).
// `outgoing` selects the "out" ramp (towards fully transitioned) vs the "in" ramp (recovering).
void addTransitionEffect(openshot::Clip& clip, TransitionEffect e, const RampWindow& ramp, bool outgoing);

int64_t clipFirstTimelineFrame(const openshot::Clip& clip, double fps);
int64_t clipLastTimelineFrame(const openshot::Clip& clip, double fps);
RampWindow toLocalWindow(const openshot::Clip& clip, int64_t firstTimelineFrame, int64_t lastTimelineFrame, double fps);

// Overlapping transition between two clips: holds (freeze frames), layer bump, shared ramp window,
// then the effect on both sides. Mirrors applyTransitions passes 2–4 for one transition.
void applyOverlappingTransition(openshot::Clip& outClip, openshot::Clip& inClip, double durationSec, double fps,
                                const std::vector<TransitionEffect>& effects);

// Overlay clip (light leak style). Returns the overlay (owned by scene, not added to the timeline).
openshot::Clip* addOverlayClip(Scene& scene, const std::string& path, double durationSec,
                               openshot::Clip::OverlayType type, openshot::Clip* outClip, openshot::Clip* inClip,
                               double hDisp = 0.0, double vDisp = 0.0);

// ── camera movement (Animation.cpp) ──────────────────────────────────────────────────────────
void addCameraMovement(openshot::Clip& clip, double startSec, double endSec, double fps,
                       const std::vector<RampKf>& zoom, const std::vector<RampKf>& rotate,
                       const std::vector<RampKf>& moveX, const std::vector<RampKf>& moveY);

// ── subtitles (Subtitles.cpp output shape) ───────────────────────────────────────────────────
enum class SubtitleVariant { PerTime, OneWordContainer, AnimatedInOut };
std::string subtitlesJson(const std::string& fontPath, int exportWidth, SubtitleVariant v);

// ── writer (renderVideo) ─────────────────────────────────────────────────────────────────────
void configureWriter(openshot::FFmpegWriter& w, int width, int height, openshot::Fraction fps, int bitrate = 5000000);

} // namespace golden::recipes
