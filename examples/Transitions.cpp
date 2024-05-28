#include "Transitions.h"

#include <utility>

#include "Timeline.h"
#include "FFmpegWriter.h"
#include "KeyFrame.h"
#include "FFmpegReader.h"
#include "ImageReader.h"
#include "effects/Blur.h"
#include "effects/BorderReflectedMove.h"
#include "effects/BorderReflectedRotation.h"
#include "effects/Mask.h"
#include "effects/Bars.h"
#include "effects/Zoom.h"
#include "effects/Wipe.h"
#include "effects/Exposure.h"
#include "effects/Brightness.h"
#include "effects/VerticalSplitShift.h"
#include "effects/ColorShift.h"
#include "Color.h"

////////////////// Helper functions //////////////////////////////////////////////////////////////////////////////////////
namespace {

int timeToFrame(float duration, float fps = 30) {
    return std::max((round)(fps * duration), 1.F);
}

float frameToTime(long frame, int fps = 30) {
    return frame / float(fps);
}

struct BezierValue {
    BezierValue(float v1_, float v2_, float v3_, float v4_) : v1(v1_), v2(v2_), v3(v3_), v4(v4_) {}
    float v1, v2, v3, v4;
};

struct PointsData {
    PointsData() = default;
    explicit PointsData(std::vector<std::pair<float, float>> timeValues_, float defaultValue_ = 0.0, std::vector<BezierValue> bezierValues_ = {})
            : timeValues(std::move(timeValues_)), defaultValue(defaultValue_), bezierValues(std::move(bezierValues_)) {}
    std::vector<std::pair<float, float>> timeValues;
    float defaultValue{};
    std::vector<BezierValue> bezierValues;
};

openshot::Keyframe createTransitionKeyframe(PointsData pointsData, float transitionDuration, bool isFirstClip = true, openshot::Clip* clip = nullptr,
                                            openshot::InterpolationType interpolation = openshot::BEZIER, float fps = 30) {
    std::vector<openshot::Point> points;
    const auto& timeValues = pointsData.timeValues;

    points.emplace_back(timeToFrame(0), pointsData.defaultValue, interpolation);

    for (int i = 0; i < timeValues.size(); i++) {
        int frameNum = isFirstClip
                       ? timeToFrame(clip->End() - (1 - timeValues[i].first) * transitionDuration, fps)
                       : timeToFrame(clip->Start() + timeValues[i].first * transitionDuration, fps);
        if (i == 0) {
            points.emplace_back(std::max(1, frameNum - 1), pointsData.defaultValue, interpolation);
        }
        points.emplace_back(frameNum, timeValues[i].second, interpolation);
    }

    if (interpolation == openshot::BEZIER) {
        for (int i = 0; i < pointsData.bezierValues.size(); ++i) {
            points[i + 2].handle_right = openshot::Coordinate(pointsData.bezierValues[i].v1, pointsData.bezierValues[i].v2);
            points[i + 3].handle_left = openshot::Coordinate(pointsData.bezierValues[i].v3, pointsData.bezierValues[i].v4);
        }
    }
    return {points};
}

std::pair<openshot::Clip*, openshot::Clip*> createTransitionClips(const std::string& file1, const std::string& file2, float transitionDuration, const int fps = 30) {
    const float perClipTransitionDuration = transitionDuration / 2.f;
    /// Open clips
    auto* clip1 = new openshot::Clip(file1);
    auto* clip2 = new openshot::Clip(file2);

    /// Init clip 1 properties
    float clip1Position = 0;
    float clip1Start = 0;
    float clip1End = clip1->info.duration + perClipTransitionDuration;

    /// Init clip 2 properties
    float clip2Position = clip1Position + clip1->End() - perClipTransitionDuration;
    float clip2Start = 0;
    float clip2End = clip2->info.duration + perClipTransitionDuration;
    int   freezeClip2FramesCountAtBeginning = timeToFrame(perClipTransitionDuration, clip2->info.fps.ToFloat());

    /// Set clip 1 properties
    clip1->Position(clip1Position);
    clip1->Start(clip1Start);
    clip1->End(clip1End);
    clip1->Layer(2);

    /// Set clip 2 properties
    clip2->Position(clip2Position);
    clip2->Start(clip2Start);
    clip2->End(clip2End);
    clip2->Layer(1);
    clip2->mFreezeFramesCountAtBeginning = freezeClip2FramesCountAtBeginning;

    return std::make_pair(clip1, clip2);
}

void createTimelineAndWriteClips(std::vector<openshot::Clip*> clips, const std::string& outFile) {
    openshot::Timeline timeLine(1920, 1080, openshot::Fraction(30, 1),48000, 2, openshot::ChannelLayout::LAYOUT_STEREO);
    timeLine.Open();

    for (const auto clip : clips) {
        timeLine.AddClip(clip);
    }
    openshot::FFmpegWriter w(outFile);
    w.SetAudioOptions(true, "libvorbis", 48000, 2, openshot::ChannelLayout::LAYOUT_STEREO, 128000);
    w.SetVideoOptions(true, "libx264" , openshot::Fraction(30, 1),  1920, 1080, openshot::Fraction(1,1), false, false, 4000000);
    w.Open();

    openshot::Clip* maxPositionClip = clips[0];
    float maxPosition = 0;
    for (const auto clip : clips) {
        if (clip->Position() > maxPosition) {
            maxPosition = clip->Position();
            maxPositionClip = clip;
        }
    }

    w.WriteFrame(&timeLine, 1, (maxPositionClip->Position() + maxPositionClip->End()) * 30);
    timeLine.Close();
    w.Close();
}

}
////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////// Transitions /////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////

void panBottomRightTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {
    auto transitionClips = createTransitionClips(file1, file2, transitionDuration);

    ////////// Clip 1 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Diagonal Border Reflected Move Effect | Clip 1
    {
        const auto xPointsData = PointsData({{0, 0}, {1, 1}}, 0, {{0.88, 0.00, 0.12, 1.00}});
        openshot::Keyframe dx = createTransitionKeyframe(xPointsData, transitionDuration, true, transitionClips.first);

        const auto yPointsData = PointsData({{0, 0}, {1, 1}}, 0, {{0.88, 0.00, 0.12, 1.00}});
        openshot::Keyframe dy = createTransitionKeyframe(yPointsData, transitionDuration, true, transitionClips.first);

        auto pBorderReflectedMove = new openshot::BorderReflectedMove(dx, dy);
        transitionClips.first->AddEffect(pBorderReflectedMove);
    }

    /// Diagonal Blur Effect | Clip 1
    {
        PointsData pointsData({{0, 0}, {0.5, 250}}, 0, {{0.67, 0.00, 0.83, 0.83}});
        openshot::Keyframe keyframe = createTransitionKeyframe(pointsData, transitionDuration, true, transitionClips.first);

        auto blurEffect = new openshot::Blur(0, 0, keyframe, 0, 0, 0, 0, 0, 1);
        transitionClips.first->AddEffect(blurEffect);
    }

    /// Alpha Effect | Clip 1
    {
        PointsData pointsData({{0.2, 1}, {0.6, 0}}, 1);
        transitionClips.first->alpha = createTransitionKeyframe(pointsData, transitionDuration, true, transitionClips.first, openshot::LINEAR);
    }

    ////////// Clip 2 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Diagonal Border Reflected Move Effect | Clip 2
    {
        // Define the keyframes for x axis
        PointsData pointsDataX({{0, -1}, {1, 0}}, -1, {{0.88, 0.00, 0.12, 1.00}});
        openshot::Keyframe keyframeDx = createTransitionKeyframe(pointsDataX, transitionDuration, false, transitionClips.second);

        // Define the keyframes for y axis
        PointsData pointsDataY({{0, -1}, {1, 0}}, -1, {{0.88, 0.00, 0.12, 1.00}});
        openshot::Keyframe keyframeDy = createTransitionKeyframe(pointsDataY, transitionDuration, false, transitionClips.second);

        auto pBorderReflectedMove = new openshot::BorderReflectedMove(keyframeDx, keyframeDy);
        transitionClips.second->AddEffect(pBorderReflectedMove);
    }

    /// Diagonal Blur Effect | Clip 2
    {
        PointsData pointsData({{0.5, 250}, {1, 0}}, 250, {{0.17, 0.17, 0.33, 1.00}});
        openshot::Keyframe keyframe = createTransitionKeyframe(pointsData, transitionDuration, false, transitionClips.second);

        auto blur = new openshot::Blur(0, 0, keyframe, 0, 0, 0, 0, 0, 1);
        transitionClips.second->AddEffect(blur);
    }

    createTimelineAndWriteClips({ transitionClips.first, transitionClips.second }, output);
}

void panLeftTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output)
{
    auto transitionClips = createTransitionClips(file1, file2, transitionDuration);

    ////////// Clip 1 ////////////////////////////////////////////////////////////////////////////////////////////

    /// X axis Border Reflected Move Effect | Clip 1
    {
        const auto xPointsData = PointsData({{0, 0}, {1, 1}}, 0, {{0.88, 0.00, 0.12, 1.00}});
        auto dxKeyframe = createTransitionKeyframe(xPointsData, transitionDuration, true, transitionClips.first);

        auto pBorderReflectedMove = new openshot::BorderReflectedMove(dxKeyframe, 0);
        transitionClips.first->AddEffect(pBorderReflectedMove);
    }

    /// Horizontal Blur Effect | Clip 1
    {
        PointsData pointsData({{0, 0}, {0.5, 250}}, 0, {{0.67, 0.00, 0.83, 0.83}});
        openshot::Keyframe keyframe = createTransitionKeyframe(pointsData, transitionDuration, true, transitionClips.first);

        auto blurEffect = new openshot::Blur(keyframe);
        transitionClips.first->AddEffect(blurEffect);
    }

    /// Alpha Effect | Clip 1
    {
        PointsData pointsData({{0.2, 1}, {0.6, 0}}, 1);
        transitionClips.first->alpha = createTransitionKeyframe(pointsData, transitionDuration, true, transitionClips.first, openshot::LINEAR);
    }

    ////////// Clip 2 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Border Reflected Move with X axis Effect | Clip 2
    {
        const auto xPointsData = PointsData({{ 0, -1 }, {1, 0}}, -1, {{0.88, 0.00, 0.12, 1.00}});
        auto dxKeyframe = createTransitionKeyframe(xPointsData, transitionDuration, false, transitionClips.second);

        auto pBorderReflectedMove = new openshot::BorderReflectedMove(dxKeyframe, 0);
        transitionClips.second->AddEffect(pBorderReflectedMove);
    }

    /// Horizontal Blur Effect | Clip 2
    {
        PointsData pointsData({{0.5, 250}, {1, 0}}, 250, {{0.17, 0.17, 0.33, 1.00}});
        openshot::Keyframe keyframe = createTransitionKeyframe(pointsData, transitionDuration, false, transitionClips.second);
        auto blurEffect = new openshot::Blur(keyframe);
        transitionClips.second->AddEffect(blurEffect);
    }

    createTimelineAndWriteClips({ transitionClips.first, transitionClips.second }, output);
}

void blurTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {
    auto transitionClips = createTransitionClips(file1, file2, transitionDuration);

    ////////// Clip 1 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Blur Effect | Clip 1
    {
        PointsData pointsData({{0, 0}, {0.5, 100}}, 0, {{0.33, 0.00, 0.00, 1.00}});
        openshot::Keyframe keyframe = createTransitionKeyframe(pointsData, transitionDuration, true, transitionClips.first);

        auto blurEffect = new openshot::Blur(keyframe, keyframe);
        transitionClips.first->AddEffect(blurEffect);
    }

    /// Alpha Effect | Clip 1
    {
        PointsData pointsData({{0, 1}, {1, 0}}, 1);
        transitionClips.first->alpha = createTransitionKeyframe(pointsData, transitionDuration, true, transitionClips.first, openshot::LINEAR);
    }

    ////////// Clip 2 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Blur Effect | Clip 2
    {
        PointsData pointsData({{0.5, 100}, {1, 0}}, 100, {{0.17, 0.18, 0.67, 1.00}});
        openshot::Keyframe keyframe = createTransitionKeyframe(pointsData, transitionDuration, false, transitionClips.second);
        auto blurEffect = new openshot::Blur(keyframe, keyframe);
        transitionClips.second->AddEffect(blurEffect);
    }

    createTimelineAndWriteClips({ transitionClips.first, transitionClips.second }, output);
}

void verticalBlurTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {

    auto transitionClips = createTransitionClips(file1, file2, transitionDuration);

    ////////// Clip 1 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Blur Effect | Clip 1
    {
        PointsData pointsData({{0, 0}, {0.5, 100}}, 0, {{0.80, 0.00, 0.34, 1.00}});
        openshot::Keyframe keyframe = createTransitionKeyframe(pointsData, transitionDuration, true,transitionClips.first);

        auto blurEffect = new openshot::Blur(0, keyframe);
        transitionClips.first->AddEffect(blurEffect);
    }

    /// Alpha Effect | Clip 1
    {
        PointsData pointsData({{0, 1}, {1, 0}}, 1, {{0.89, 0.00, 0.11, 1.00}});
        transitionClips.first->alpha = createTransitionKeyframe(pointsData, transitionDuration, true, transitionClips.first);
    }

    ////////// Clip 2 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Blur Effect | Clip 2
    {
        PointsData pointsData({{0.5, 100}, {1, 0}}, 100, {{0.64, 0.00, 0.26, 1.00}});
        openshot::Keyframe keyframe = createTransitionKeyframe(pointsData, transitionDuration, false, transitionClips.second);

        auto blurEffect = new openshot::Blur(0, keyframe);
        transitionClips.second->AddEffect(blurEffect);
    }

    createTimelineAndWriteClips({ transitionClips.first, transitionClips.second }, output);
}

void rotationalBlurTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {
    auto transitionClips = createTransitionClips(file1, file2, transitionDuration);

    ////////// Clip 1 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Alpha Effect | Clip 1
    {
        PointsData pointsData({{0.3499, 1}, {0.35, 0}}, 1);
        transitionClips.first->alpha = createTransitionKeyframe(pointsData, transitionDuration, true, transitionClips.first, openshot::LINEAR);
    }

    /// Border Reflected Rotation Effect | Clip 1
    {
        const auto pointsData = PointsData({{0, 0}, {1, 180}}, 0, {{0.68, 0.00, 0.00, 1.00}});
        auto keyframe = createTransitionKeyframe(pointsData, transitionDuration, true, transitionClips.first);

        auto borderReflectedRotationEffect = new openshot::BorderReflectedRotation(keyframe);
        transitionClips.first->AddEffect(borderReflectedRotationEffect);
    }

    /// Rotational Blur Effect | Clip 1
    {
        PointsData pointsData({{0, 0}, {1, 80}}, 0, {{0.68, 0.00, 0.00, 1.00}});
        openshot::Keyframe keyframe = createTransitionKeyframe(pointsData, transitionDuration, true, transitionClips.first);

        auto blurEffect = new openshot::Blur(0, 0, 0, keyframe);
        transitionClips.first->AddEffect(blurEffect);
    }

    ////////// Clip 2 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Border Reflected Rotation Effect | Clip 2
    {
        const auto pointsData = PointsData({{0, -180}, {1, 0}}, -180, {{0.68, 0.00, 0.00, 1.00}});
        auto keyframe = createTransitionKeyframe(pointsData, transitionDuration, false, transitionClips.second);

        auto borderReflectedRotationEffect = new openshot::BorderReflectedRotation(keyframe);
        transitionClips.second->AddEffect(borderReflectedRotationEffect);
    }

    /// Rotational Blur Effect | Clip 2
    {
        PointsData pointsData({{0, 80}, {1, 0}}, 80, {{0.68, 0.00, 0.00, 1.00}});
        openshot::Keyframe keyframe = createTransitionKeyframe(pointsData, transitionDuration, false, transitionClips.second);

        auto blurEffect = new openshot::Blur(0, 0, 0, keyframe);
        transitionClips.second->AddEffect(blurEffect);
    }

    createTimelineAndWriteClips({ transitionClips.first, transitionClips.second }, output);
}

void wooshTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {
    auto transitionClips = createTransitionClips(file1, file2, transitionDuration);

    ////////// Clip 1 ////////////////////////////////////////////////////////////////////////////////////////////
    openshot::Clip clip1(file1);

    /// Alpha Effect | Clip 1
    {
        PointsData pointsData({{0.33, 1}, {0.66, 0}}, 1, {{0.420, 0.000, 0.580, 1.000}});
        transitionClips.first->alpha = createTransitionKeyframe(pointsData, transitionDuration, true, transitionClips.first, openshot::BEZIER);
    }

    /// Zoom Effect | Clip 1
    {
        const auto zoomPointsData = PointsData(
                {{0, 100}, {0.5, 120}, {1, 100}}, 100,
                {{0.71, 0.00, 0.83, 0.83}, {0.17, 0.17, 0.29, 1}});
        openshot::Keyframe zoomKeyframe = createTransitionKeyframe(zoomPointsData, transitionDuration, true, transitionClips.first);

        const auto anchorXPointsData = PointsData({{0, 0.18}, {0.5, 0.82}}, 0.5, {{0.71, 0.00, 0.29, 1.00}});
        openshot::Keyframe anchorXKeyframe = createTransitionKeyframe(anchorXPointsData, transitionDuration, true, transitionClips.first);
        auto zoomEffect = new openshot::Zoom(zoomKeyframe, anchorXKeyframe, 0.5);
        transitionClips.first->AddEffect(zoomEffect);

    }

    /// Blur Effect | Clip 1
    {
        const auto blurPointsData = PointsData(
                {{0, 0}, {0.5, 20}, {1, 0}}, 0,
                {{0.71, 0.00, 0.83, 0.83}, {0.17, 0.17, 0.19, 1}});
        openshot::Keyframe blurKeyframe = createTransitionKeyframe(blurPointsData, transitionDuration, true, transitionClips.first);

        auto blurEffect = new openshot::Blur(blurKeyframe, blurKeyframe, 0, 0,  0, 0, 0, 0, 2);
        transitionClips.first->AddEffect(blurEffect);
    }

    /// Zoom Blur Effect | Clip 1
    {
        const auto zoomBlurPointsData = PointsData(
                {{0, 0}, {0.5, 140}, {1, 0}}, 0,
                {{0.71, 0.00, 0.83, 0.83}, {0.17, 0.17, 0.19, 1}});
        openshot::Keyframe zoomBlurKeyframe = createTransitionKeyframe(zoomBlurPointsData, transitionDuration, true, transitionClips.first);

        const auto anchorXPointsData = PointsData({{0, 0.3}, {1, 0.6}}, 0.3, {{0.71, 0.00, 0.29, 1.00}});
        openshot::Keyframe anchorXKeyframe = createTransitionKeyframe(anchorXPointsData, transitionDuration, true, transitionClips.first);

        auto blur = new openshot::Blur(0, 0, 0, 0, zoomBlurKeyframe, anchorXKeyframe, 0.5);
        transitionClips.first->AddEffect(blur);

    }

    ////////// Clip 2 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Zoom Effect | Clip 2
    {
        const auto zoomPointsData = PointsData(
                {{0, 100}, {0.5, 120}, {1, 100}}, 100,
                {{0.71, 0.00, 0.83, 0.83}, {0.17, 0.17, 0.29, 1}});
        openshot::Keyframe zoomKeyframe = createTransitionKeyframe(zoomPointsData, transitionDuration, false, transitionClips.second);

        const auto anchorXPointsData = PointsData({{0, 0.18}, {0.5, 0.82}}, 0.18, {{0.71, 0.00, 0.29, 1.00}});
        openshot::Keyframe anchorXKeyframe = createTransitionKeyframe(anchorXPointsData, transitionDuration, false, transitionClips.second);
        auto zoomEffect = new openshot::Zoom(zoomKeyframe, anchorXKeyframe, 0.5);
        transitionClips.second->AddEffect(zoomEffect);
    }

    /// Blur Effect | Clip 2
    {
        const auto blurPointsData = PointsData(
                {{0, 0}, {0.5, 20}, {1, 0}}, 0,
                {{0.71, 0.00, 0.83, 0.83}, {0.17, 0.17, 0.19, 1}});
        openshot::Keyframe blurKeyframe = createTransitionKeyframe(blurPointsData, transitionDuration, false, transitionClips.second);

        auto blurEffect = new openshot::Blur(blurKeyframe, blurKeyframe, 0, 0,  0, 0, 0, 0, 2);
        transitionClips.second->AddEffect(blurEffect);
    }

    /// Zoom Blur Effect | Clip 2
    {
        const auto zoomBlurPointsData = PointsData(
                {{0, 0}, {0.5, 140}, {1, 0}}, 0,
                {{0.71, 0.00, 0.83, 0.83}, {0.17, 0.17, 0.19, 1}});
        openshot::Keyframe zoomBlurKeyframe = createTransitionKeyframe(zoomBlurPointsData, transitionDuration, false, transitionClips.second);

        const auto anchorXPointsData = PointsData({{0, 0.3}, {1, 0.6}}, 0.3, {{0.71, 0.00, 0.29, 1.00}});
        openshot::Keyframe anchorXKeyframe = createTransitionKeyframe(anchorXPointsData, transitionDuration, false, transitionClips.second);

        auto blurEffect = new openshot::Blur(0, 0, 0, 0, zoomBlurKeyframe, anchorXKeyframe, 0.5);
        transitionClips.second->AddEffect(blurEffect);
    }

    createTimelineAndWriteClips({ transitionClips.first, transitionClips.second }, output);
}

void dissolveTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {
    auto transitionClips = createTransitionClips(file1, file2, transitionDuration);

    ////////// Clip 1 ////////////////////////////////////////////////////////////////////////////////////////////
    /// Alpha Effect | Clip
    {
        PointsData pointsData({{0.33, 1}, {0.67, 0}}, 1);
        transitionClips.first->alpha = createTransitionKeyframe(pointsData, transitionDuration, true, transitionClips.first, openshot::LINEAR);
    }

    createTimelineAndWriteClips({ transitionClips.first, transitionClips.second }, output);
}

void dissolveBlurTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {
    auto transitionClips = createTransitionClips(file1, file2, transitionDuration);

    ////////// Clip 1 ////////////////////////////////////////////////////////////////////////////////////////////
    /// Alpha Effect | Clip 1
    {
        PointsData pointsData({{0.33, 1}, {0.67, 0}}, 1);
        transitionClips.first->alpha = createTransitionKeyframe(pointsData, transitionDuration, true, transitionClips.first, openshot::LINEAR);
    }

    /// Blur Effect | Clip 1
    {
        PointsData pointsData({{0, 0}, {1, 20}}, 0, {{0.33, 0.00, 0.67, 1.00}});
        openshot::Keyframe keyframe = createTransitionKeyframe(pointsData, transitionDuration, true, transitionClips.first);
        auto blurEffect = new openshot::Blur(keyframe, keyframe, 0, 0, 0, 0, 0, 0, 1);
        transitionClips.first->AddEffect(blurEffect);
    }

    ////////// Clip 2 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Blur Effect | Clip 2
    {
        PointsData pointsData({{0, 20}, {1, 0}}, 20, {{0.33, 0.00, 0.67, 1.00}});
        openshot::Keyframe keyframe = createTransitionKeyframe(pointsData, transitionDuration, false, transitionClips.second);

        auto blurEffect = new openshot::Blur(keyframe, keyframe, 0, 0, 0, 0, 0, 0, 1);
        transitionClips.second->AddEffect(blurEffect);
    }

    createTimelineAndWriteClips({ transitionClips.first, transitionClips.second }, output);
}

void circleOutTransition(const std::string& file1, float transitionDuration, const std::string& output) {
    float clipStart = 0;
    float clipEnd = 2;
    float clipPosition = 0;

    openshot::Clip clip(file1);
    clip.Position(clipPosition);
    clip.Start(clipStart);
    clip.End(clipEnd);
    clip.scale = openshot::SCALE_FIT;

    int width = clip.Reader()->info.width;
    int height = clip.Reader()->info.height;
    float circleRadius = sqrt(width * width + height * height) / 2;

    auto mask = openshot::Mask(openshot::Mask::MaskType::CIRCLE_OUT, 0, 3);
    mask.circleRadius.AddPoint(timeToFrame(0), 0, openshot::LINEAR);
    mask.circleRadius.AddPoint(timeToFrame(clipEnd - transitionDuration) - 1, 0, openshot::LINEAR);
    mask.circleRadius.AddPoint(timeToFrame(clipEnd - transitionDuration), circleRadius, openshot::LINEAR);
    mask.circleRadius.AddPoint(timeToFrame(clipEnd), 0.0000001, openshot::LINEAR);
    clip.AddEffect(&mask);

    openshot::Timeline timeLine(1920, 1080, openshot::Fraction(30, 1),
                                48000, 2, openshot::ChannelLayout::LAYOUT_STEREO);
    timeLine.Open();
    timeLine.AddClip(&clip);
    openshot::FFmpegWriter w(output);
    w.SetAudioOptions(true, "libvorbis", 48000, 2, openshot::ChannelLayout::LAYOUT_STEREO, 128000);
    w.SetVideoOptions(true, "libx264" , openshot::Fraction(30, 1),  1920, 1080, openshot::Fraction(1,1), false, false, 4000000);
    w.Open();
    w.WriteFrame(&timeLine, 0, 70);

    timeLine.Close();
    w.Close();
}

void circleInTransition(const std::string& file1, float transitionDuration, const std::string& output) {
    float clipStart = 0;
    float clipEnd = 2;
    float clipPosition = 0;

    openshot::Clip clip(file1);
    clip.Position(clipPosition);
    clip.Start(clipStart);
    clip.End(clipEnd);
    clip.scale = openshot::SCALE_FIT;

    int width = clip.Reader()->info.width;
    int height = clip.Reader()->info.height;
    float circleRadius = sqrt(width * width + height * height) / 2;

    auto mask = openshot::Mask(openshot::Mask::MaskType::CIRCLE_OUT, 0, 3);
    mask.circleRadius.AddPoint(timeToFrame(0), 0.001, openshot::BEZIER);
    mask.circleRadius.AddPoint(timeToFrame(clipStart + transitionDuration), circleRadius, openshot::LINEAR);
    mask.circleRadius.AddPoint(timeToFrame(clipEnd), circleRadius, openshot::LINEAR);
    clip.AddEffect(&mask);

    openshot::Timeline timeLine(1920, 1080, openshot::Fraction(30, 1),
                                48000, 2, openshot::ChannelLayout::LAYOUT_STEREO);
    timeLine.Open();
    timeLine.AddClip(&clip);
    openshot::FFmpegWriter w(output);
    w.SetAudioOptions(true, "libvorbis", 48000, 2, openshot::ChannelLayout::LAYOUT_STEREO, 128000);
    w.SetVideoOptions(true, "libx264" , openshot::Fraction(30, 1),  1920, 1080, openshot::Fraction(1,1), false, false, 4000000);
    w.Open();
    w.WriteFrame(&timeLine, 0, 70);

    timeLine.Close();
    w.Close();
}

void barnDoorsTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {
    float clip1Start = 0;
    float clip1End = 2;
    float clip1Position = 0;
    float clip2Start = 0;
    float clip2End = 2;
    float clip2Position = clip1End;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////// Clip 1
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    openshot::Clip clip1(file1);
    clip1.Position(clip1Position);
    clip1.Start(clip1Start);
    clip1.End(clip1End);
    clip1.scale = openshot::SCALE_FIT;

    /// Bars Effect | Clip 1
    {
        float startValue = 0;
        float endValue = 0.5;
        float startTime = 0;
        float endTime = 1;

        auto point0 = openshot::Point(timeToFrame(clip1Start),                                      startValue);
        auto point1 = openshot::Point(timeToFrame(clip1End - (1 - startTime) * transitionDuration), startValue);
        auto point2 = openshot::Point(timeToFrame(clip1End - (1 - endTime)   * transitionDuration), endValue);

        openshot::Keyframe keyframe({point0, point1, point2});
        auto bars = new openshot::Bars(openshot::Color("#000000"), 0, keyframe, 0, keyframe);
        clip1.AddEffect(bars);
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////// Clip 2
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    openshot::Clip clip2(file2);
    clip2.Position(clip2Position);
    clip2.Start(clip2Start);
    clip2.End(clip2End);
    clip2.scale = openshot::SCALE_FIT;

    /// Bars Effect | Clip 2
    {
        float startValue = 0.5;
        float endValue = 0;
        float startTime = 0;
        float endTime = 1;

        openshot::Point point0(timeToFrame(clip2Start),                               startValue);
        openshot::Point point1(timeToFrame(clip2Start + startTime * transitionDuration),           startValue);
        openshot::Point point2(timeToFrame(clip2Start + endTime * transitionDuration),endValue);

        openshot::Keyframe keyframe({point0, point1, point2});
        auto bars = new openshot::Bars(openshot::Color("#000000"), keyframe, 0, keyframe, 0);
        clip2.AddEffect(bars);
    }

    openshot::Timeline timeLine(1920, 1080, openshot::Fraction(30, 1),
                                48000, 2, openshot::ChannelLayout::LAYOUT_STEREO);
    timeLine.Open();
    timeLine.AddClip(&clip1);
    timeLine.AddClip(&clip2);
    openshot::FFmpegWriter w(output);
    w.SetAudioOptions(true, "libvorbis", 48000, 2, openshot::ChannelLayout::LAYOUT_STEREO, 128000);
    w.SetVideoOptions(true, "libx264" , openshot::Fraction(30, 1),  1920, 1080, openshot::Fraction(1,1), false, false, 4000000);
    w.Open();
    w.WriteFrame(&timeLine, 0, 120);

    timeLine.Close();
    w.Close();
}

void verticalSplitTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {
    auto transitionClips = createTransitionClips(file1, file2, transitionDuration);

    ////////// Clip 1 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Vertical Split Shift Effect | Clip 1
    {
        PointsData pointsData({{0, 0}, {1, 1}}, 0, {{0.86, 0.00, 0.14, 1.00}});
        openshot::Keyframe keyframe = createTransitionKeyframe(pointsData, transitionDuration, true, transitionClips.first, openshot::BEZIER);

        auto verticalSplitShiftEffect = new openshot::VerticalSplitShift(keyframe);
        transitionClips.first->AddEffect(verticalSplitShiftEffect);
    }

    ////////// Clip 2 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Vertical Split Shift Effect | Clip 2
    {
        PointsData pointsData({{0, -1}, {1, 0}}, -1, {{0.86, 0.00, 0.14, 1.00}});
        openshot::Keyframe keyframe = createTransitionKeyframe(pointsData, transitionDuration, false, transitionClips.second, openshot::BEZIER);

        auto verticalSplitShiftEffect = new openshot::VerticalSplitShift(keyframe);
        transitionClips.second->AddEffect(verticalSplitShiftEffect);
    }

    createTimelineAndWriteClips({ transitionClips.first, transitionClips.second }, output);
}

void zoomInTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {
    auto transitionClips = createTransitionClips(file1, file2, transitionDuration);

    ////////// Clip 1 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Alpha Effect | Clip 1
    {
        PointsData pointsData({{0.33, 1}, {0.66, 0}}, 1, {{0.33, 0.67, 0.00, 1}});
        transitionClips.first->alpha = createTransitionKeyframe(pointsData, transitionDuration, true, transitionClips.first, openshot::LINEAR);
    }

    /// Zoom Effect | Clip 1
    {
        const auto zoomPointsData = PointsData({{0, 100}, {1, 250}}, 100, {{0.58, 0.00, 0.13, 1.00}});
        openshot::Keyframe zoomKeyframe = createTransitionKeyframe(zoomPointsData, transitionDuration, true, transitionClips.first);

        auto zoomEffect = new openshot::Zoom(zoomKeyframe, 0.5, 0.5);
        transitionClips.first->AddEffect(zoomEffect);
    }

    ////////// Clip 2 ////////////////////////////////////////////////////////////////////////////////////////////


    /// Zoom Effect | Clip 2
    {
        const auto zoomPointsData = PointsData({{0, 40}, {1, 100}}, 40, {{0.58, 0.00, 0.13, 1.00}});
        openshot::Keyframe zoomKeyframe = createTransitionKeyframe(zoomPointsData, transitionDuration, false, transitionClips.second);

        auto zoomEffect = new openshot::Zoom(zoomKeyframe, 0.5, 0.5);
        transitionClips.second->AddEffect(zoomEffect);
    }

    createTimelineAndWriteClips({ transitionClips.first, transitionClips.second }, output);
}

void contrastTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {
    auto transitionClips = createTransitionClips(file1, file2, transitionDuration);

    ////////// Clip 1 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Mask Effect | Clip 1
    {
        const auto levelPercentage = PointsData({{0, 100}, {1, 0}}, 100, {{0.33, 0.00, 0.67, 1.00}});
        openshot::Keyframe levelPercentageKeyframe = createTransitionKeyframe(levelPercentage, transitionDuration, true, transitionClips.first);

        auto wipeEffect = new openshot::Wipe(levelPercentageKeyframe,levelPercentageKeyframe);
        transitionClips.first->AddEffect(wipeEffect);
    }

    createTimelineAndWriteClips({ transitionClips.first, transitionClips.second }, output);
}

void brightnessTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {
    auto transitionClips = createTransitionClips(file1, file2, transitionDuration);

    ////////// Clip 1 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Alpha Effect | Clip 1
    {
        PointsData pointsData({{0.499999, 1}, {0.5, 0}}, 1);
        transitionClips.first->alpha = createTransitionKeyframe(pointsData, transitionDuration, true, transitionClips.first, openshot::LINEAR);
    }

    /// Brightness Effect | Clip 1
    {
        const auto exposurePoints = PointsData({{0.2, 1}, {0.5, 4}, {0.8, 0}}, 1, {{0.33, 0.00, 0.67, 1.00}});
        openshot::Keyframe exposureKeyframe = createTransitionKeyframe(exposurePoints, transitionDuration, true, transitionClips.first);

        auto brightnessEffect = new openshot::Exposure(exposureKeyframe);
        transitionClips.first->AddEffect(brightnessEffect);
    }

    ////////// Clip 2 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Brightness Effect | Clip 2
    {
        const auto exposurePoints = PointsData({{0.2, 1}, {0.5, 4}, {0.8, 0}}, 1, {{0.33, 0.00, 0.67, 1.00}});
        openshot::Keyframe brightnessKeyframe = createTransitionKeyframe(exposurePoints, transitionDuration, false, transitionClips.second);

        auto brightnessEffect = new openshot::Exposure(brightnessKeyframe);
        transitionClips.second->AddEffect(brightnessEffect);
    }

    createTimelineAndWriteClips({ transitionClips.first, transitionClips.second }, output);
}

void brightnessFootageTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {
    auto transitionClips = createTransitionClips(file1, file2, transitionDuration);

    ////////// Clip 1 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Alpha Effect | Clip 1
    {
        PointsData pointsData({{0.499999, 1}, {0.5, 0}}, 1);
        transitionClips.first->alpha = createTransitionKeyframe(pointsData, transitionDuration, true, transitionClips.first, openshot::LINEAR);
    }

    /// Brightness Effect | Clip 1
    {
        const auto exposurePoints = PointsData({{0.2, 0}, {0.5, 0.3}, {0.8, 0}}, 0, {{0.33, 0.00, 0.67, 1.00}});
        openshot::Keyframe exposureKeyframe = createTransitionKeyframe(exposurePoints, transitionDuration, true, transitionClips.first);

        auto brightnessEffect = new openshot::Brightness(exposureKeyframe, 3);
        transitionClips.first->AddEffect(brightnessEffect);
    }

    ////////// Clip 2 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Brightness Effect | Clip 2
    {
        const auto exposurePoints = PointsData({{0.2, 0}, {0.5, 0.3}, {0.8, 0}}, 0, {{0.33, 0.00, 0.67, 1.00}});
        openshot::Keyframe brightnessKeyframe = createTransitionKeyframe(exposurePoints, transitionDuration, false, transitionClips.second);

        auto brightnessEffect = new openshot::Brightness(brightnessKeyframe, 3);
        transitionClips.second->AddEffect(brightnessEffect);
    }

    ///////// Light footage clip ////////////////////////////////////////////////////////////////////////////////////
    auto lightFootageClip = new openshot::Clip("input/light_leaks.mp4");
    if (transitionDuration > lightFootageClip->info.duration) {
        throw std::runtime_error("Transition duration is longer than light footage clip duration");
    }
    lightFootageClip->Position(transitionClips.second->Position());
    lightFootageClip->Start(lightFootageClip->info.duration/2.f - transitionDuration/2.f);
    lightFootageClip->End(lightFootageClip->info.duration/2.f + transitionDuration/2.f);
    lightFootageClip->Layer(std::max(transitionClips.first->Layer(), transitionClips.second->Layer()) + 1);
    lightFootageClip->composition_mode = QPainter::CompositionMode_Plus;
    {
        openshot::Point point0(timeToFrame(lightFootageClip->Start()), 0, openshot::LINEAR);
        openshot::Point point1(timeToFrame(lightFootageClip->Start() + 0.2 * transitionDuration), 1, openshot::LINEAR);
        openshot::Point point2(timeToFrame(lightFootageClip->Start() + 0.8 * transitionDuration), 1, openshot::LINEAR);
        openshot::Point point3(timeToFrame(lightFootageClip->Start() + transitionDuration), 0, openshot::LINEAR);
        lightFootageClip->alpha = openshot::Keyframe({point0, point1, point2, point3});
    }

    createTimelineAndWriteClips({ transitionClips.first, transitionClips.second, lightFootageClip }, output);
}

void glitchTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {
    auto transitionClips = createTransitionClips(file1, file2, transitionDuration);

    ////////// Clip 1 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Alpha Effect | Clip 1
    {
        PointsData pointsData({{0.899999, 1}, {0.9, 0}}, 1);
        transitionClips.first->alpha = createTransitionKeyframe(pointsData, transitionDuration, true, transitionClips.first, openshot::LINEAR);
    }

    /// Color Shift Effect | Clip 1 and Clip 2
    {
        // Creating Red color X and Y keyframes for Clip 1
        const auto redXPoints = PointsData({{0, 1}, {0.1, 1  }, {0.2, 1.0114}, {0.35, 1}, {0.9, 1.0114}, {1, 1}}, 0);
        const auto redYPoints = PointsData({{0, 1}, {0.1, 1.012}, {0.2, 0.96  }, {0.35, 1}, {0.9, 0.96  }, {1, 1}}, 0);
        openshot::Keyframe redXKeyframe = createTransitionKeyframe(redXPoints, transitionDuration, true, transitionClips.first, openshot::CONSTANT);
        openshot::Keyframe redYKeyframe = createTransitionKeyframe(redYPoints, transitionDuration, true, transitionClips.first, openshot::CONSTANT);

        // Creating Green color X and Y keyframes for Clip 1
        const auto greenXPoints = PointsData({{0, 1}, {0.1, 1  }, {0.2, 0.971}, {0.35, 1.007}, {0.9, 0.971}, {1, 1}}, 0);
        const auto greenYPoints = PointsData({{0, 1}, {0.1, 1.018}, {0.2, 1.018}, {0.35, 0.997}, {0.9, 1.018}, {1, 1}}, 0);
        openshot::Keyframe greenXKeyframe = createTransitionKeyframe(greenXPoints, transitionDuration, true, transitionClips.first, openshot::CONSTANT);
        openshot::Keyframe greenYKeyframe = createTransitionKeyframe(greenYPoints, transitionDuration, true, transitionClips.first, openshot::CONSTANT);

        auto colorShiftEffect = new openshot::ColorShift(redXKeyframe, redYKeyframe, greenXKeyframe, greenYKeyframe, 0, 0, 0, 0);
        transitionClips.first->AddEffect(colorShiftEffect);

        redXKeyframe = createTransitionKeyframe(redXPoints, transitionDuration, false, transitionClips.second, openshot::CONSTANT);
        redYKeyframe = createTransitionKeyframe(redYPoints, transitionDuration, false, transitionClips.second, openshot::CONSTANT);

        greenXKeyframe = createTransitionKeyframe(greenXPoints, transitionDuration, false, transitionClips.second, openshot::CONSTANT);
        greenYKeyframe = createTransitionKeyframe(greenYPoints, transitionDuration, false, transitionClips.second, openshot::CONSTANT);

        colorShiftEffect = new openshot::ColorShift(redXKeyframe, redYKeyframe, greenXKeyframe, greenYKeyframe, 0, 0, 0, 0);
        transitionClips.second->AddEffect(colorShiftEffect);
    }

    ///////// footage clip ////////////////////////////////////////////////////////////////////////////////////
    auto glitchMapClip = new openshot::Clip("input/glitch_map.mp4");
    if (transitionDuration > glitchMapClip->info.duration) {
        throw std::runtime_error("Transition duration is longer than light footage clip duration");
    }
    glitchMapClip->isDisplacementMap = true;
    glitchMapClip->Position(transitionClips.second->Position());
    glitchMapClip->Start(glitchMapClip->info.duration/2.f - transitionDuration/2.f);
    glitchMapClip->End(glitchMapClip->info.duration/2.f + transitionDuration/2.f);
    glitchMapClip->Layer(std::max(transitionClips.first->Layer(), transitionClips.second->Layer()) + 1);
    {
        openshot::Point hDisplacementPoint0(timeToFrame(glitchMapClip->Start(), glitchMapClip->info.fps.ToFloat()), 0, openshot::CONSTANT);
        openshot::Point hDisplacementPoint1(timeToFrame(glitchMapClip->Start() + 0.1 * transitionDuration, glitchMapClip->info.fps.ToFloat()), 0.2697, openshot::CONSTANT);
        openshot::Point hDisplacementPoint2(timeToFrame(glitchMapClip->Start() + 0.2 * transitionDuration, glitchMapClip->info.fps.ToFloat()), 0.0104, openshot::CONSTANT);
        openshot::Point hDisplacementPoint3(timeToFrame(glitchMapClip->Start() + 0.35 * transitionDuration, glitchMapClip->info.fps.ToFloat()), 0.2697, openshot::CONSTANT);
        openshot::Point hDisplacementPoint4(timeToFrame(glitchMapClip->Start() + 0.9 * transitionDuration, glitchMapClip->info.fps.ToFloat()), -0.07135, openshot::CONSTANT);
        openshot::Point hDisplacementPoint5(timeToFrame(glitchMapClip->Start() + 1 * transitionDuration, glitchMapClip->info.fps.ToFloat()), 0, openshot::CONSTANT);
        glitchMapClip->horizontal_displacement = openshot::Keyframe({hDisplacementPoint0, hDisplacementPoint1, hDisplacementPoint2, hDisplacementPoint3, hDisplacementPoint4, hDisplacementPoint5});

        openshot::Point vDisplacementPoint0(timeToFrame(glitchMapClip->Start()), 0, openshot::CONSTANT);
        openshot::Point vDisplacementPoint1(timeToFrame(glitchMapClip->Start() + 0.1 * transitionDuration, glitchMapClip->info.fps.ToFloat()), 0.0185, openshot::CONSTANT);
        openshot::Point vDisplacementPoint2(timeToFrame(glitchMapClip->Start() + 0.2 * transitionDuration, glitchMapClip->info.fps.ToFloat()), 0.23, openshot::CONSTANT);
        openshot::Point vDisplacementPoint3(timeToFrame(glitchMapClip->Start() + 0.35 * transitionDuration, glitchMapClip->info.fps.ToFloat()), 0.0185, openshot::CONSTANT);
        openshot::Point vDisplacementPoint4(timeToFrame(glitchMapClip->Start() + 0.9 * transitionDuration, glitchMapClip->info.fps.ToFloat()), 0.37, openshot::CONSTANT);
        openshot::Point vDisplacementPoint5(timeToFrame(glitchMapClip->Start() + transitionDuration, glitchMapClip->info.fps.ToFloat()), 0, openshot::CONSTANT);
        glitchMapClip->vertical_displacement = openshot::Keyframe({vDisplacementPoint0, vDisplacementPoint1, vDisplacementPoint2, vDisplacementPoint3, vDisplacementPoint4, vDisplacementPoint5});
    }

    createTimelineAndWriteClips({ transitionClips.first, transitionClips.second, glitchMapClip }, output);
}