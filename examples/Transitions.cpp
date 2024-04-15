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
#include "effects/VerticalSplitShift.h"
#include "Color.h"

namespace {

void saveVideoFrame(const std::string& file, int frameNum, std::string& frameFilePath) {
    auto* clip = new openshot::Clip(file);
    clip->Open();
    clip->scale = openshot::SCALE_NONE;

    clip->GetFrame(frameNum)->GetImage()->save(frameFilePath.c_str());
}

int timeToFrame(float duration, int fps = 30) {
    return std::max((int)(fps * duration), 1);
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
    explicit PointsData(std::vector<std::pair<float, float>> timeValues_, std::vector<BezierValue> bezierValues_ = {})
            : timeValues(std::move(timeValues_)), bezierValues(std::move(bezierValues_)) {}
    std::vector<std::pair<float, float>> timeValues;
    std::vector<BezierValue> bezierValues;
};

openshot::Keyframe createKeyframe(PointsData pointsData, float transitionDuration, openshot::InterpolationType interpolation = openshot::BEZIER, int fps = 30) {
    std::vector<openshot::Point> points;
    const auto& timeValues = pointsData.timeValues;
    for (int i = 0; i < timeValues.size(); i++) {
        if (i == 0) {
            points.emplace_back(timeToFrame(0), timeValues[i].second, interpolation);
        }
        points.emplace_back(timeToFrame(transitionDuration - (1 - timeValues[i].first) * transitionDuration), timeValues[i].second, interpolation);
    }

    if (interpolation == openshot::BEZIER) {
        for (int i = 0; i < pointsData.bezierValues.size(); ++i) {
            points[i + 1].handle_right = openshot::Coordinate(pointsData.bezierValues[i].v1, pointsData.bezierValues[i].v2);
            points[i + 2].handle_left = openshot::Coordinate(pointsData.bezierValues[i].v3, pointsData.bezierValues[i].v4);
        }
    }

    return {points};
}

std::pair<std::pair<openshot::Clip*, openshot::Clip*>, std::pair<openshot::Clip*, openshot::Clip*>>
createTransitionClips(const std::string& file1, const std::string& file2, float transitionDuration, const int fps = 30) {
    /// Create clips
    auto* clip1 = new openshot::Clip(file1);
    auto* clip2 = new openshot::Clip(file2);

    /// Define clips properties
    float clip1Position = 0;
    float clip1Start = 0;
    float frameNum = (clip1->info.video_length - timeToFrame(transitionDuration));
    float clip1End = frameNum / (float)fps;

    int   transitionFrame1 = frameNum;
    float transitionPosition1 = clip1End;
    float transitionStart1 = 0;
    float transitionEnd1 = transitionDuration;

    int   transitionFrame2 = timeToFrame(transitionDuration, fps);
    float transitionPosition2 = transitionPosition1;
    float transitionStart2 = 0;
    float transitionEnd2 = transitionDuration;

    float clip2Position = transitionPosition2 + transitionDuration;
    float clip2Start = frameToTime(transitionFrame2);
    float clip2End = clip2->info.duration;

    /// Initialize clips properties
    clip1->Position(clip1Position);
    clip1->Start(clip1Start);
    clip1->End(clip1End);
    clip1->Layer(1);
    clip1->scale = openshot::SCALE_NONE;

    std::string frame1 = "frame1.jpg";
    saveVideoFrame(file1, transitionFrame1, frame1);
    auto* transitionClip1 = new openshot::Clip(frame1);
    transitionClip1->scale = openshot::SCALE_NONE;
    transitionClip1->Layer(2);
    transitionClip1->Position(transitionPosition1);
    transitionClip1->Start(transitionStart1);
    transitionClip1->End(transitionEnd1);

    std::string frame2 = "frame2.jpg";
    saveVideoFrame(file2, transitionFrame2, frame2);
    auto* transitionClip2 = new openshot::Clip(frame2);
    transitionClip2->scale = openshot::SCALE_NONE;
    transitionClip2->Layer(1);
    transitionClip2->Position(transitionPosition2);
    transitionClip2->Start(transitionStart2);
    transitionClip2->End(transitionEnd2);

    clip2->scale = openshot::SCALE_NONE;
    clip2->Layer(2);
    clip2->Position(clip2Position);
    clip2->Start(clip2Start);
    clip2->End(clip2End);

    return std::make_pair(std::make_pair(transitionClip1, transitionClip2), std::make_pair(clip1, clip2));
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
    openshot::Clip* maxPositionClip;
    float maxPosition = 0;
    float duration = 0;
    for (const auto clip : clips) {
        if (clip->Position() > maxPosition) {
            maxPosition = clip->Position();
            maxPositionClip = clip;
        }
    }

    w.WriteFrame(&timeLine, 0, (maxPositionClip->Position() + maxPositionClip->End()) * 30);
    timeLine.Close();
    w.Close();
}

}

void panBottomRightTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {
    auto clips = createTransitionClips(file1, file2, transitionDuration);

    auto transitionClips = clips.first;
    auto mainClips = clips.second;

    ////////// Clip 1 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Diagonal Border Reflected Move Effect | Clip 1
    {
        const auto xPointsData = PointsData({{0, 0}, {1, transitionClips.first->Reader()->info.width}}, {{0.88, 0.00, 0.12, 1.00}});
        openshot::Keyframe dx = createKeyframe(xPointsData, transitionDuration);

        const auto yPointsData = PointsData({{0, 0}, {1, transitionClips.first->Reader()->info.height}}, {{0.88, 0.00, 0.12, 1.00}});
        openshot::Keyframe dy = createKeyframe(yPointsData, transitionDuration);

        auto pBorderReflectedMove = new openshot::BorderReflectedMove(dx, dy);
        transitionClips.first->AddEffect(pBorderReflectedMove);
    }

    /// Diagonal Blur Effect | Clip 1
    {
        PointsData pointsData({{0, 0}, {0.5, 250}}, {{0.67, 0.00, 0.83, 0.83}});
        openshot::Keyframe keyframe = createKeyframe(pointsData, transitionDuration);
        auto blurEffect = new openshot::Blur(0, 0, keyframe, 0, 0, 0, 0, 0, 1);
        transitionClips.first->AddEffect(blurEffect);
    }

    /// Alpha Effect | Clip 2
    {
        PointsData pointsData({{0.2, 1}, {0.6, 0}});
        transitionClips.first->alpha = createKeyframe(pointsData, transitionDuration, openshot::LINEAR);
    }

    ////////// Clip 2 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Diagonal Border Reflected Move Effect | Clip 2
    {
        // Define the keyframes for x axis
        PointsData pointsDataX({{0, -transitionClips.second->Reader()->info.width}, {1, 0}}, {{0.88, 0.00, 0.12, 1.00}});
        openshot::Keyframe keyframeDx = createKeyframe(pointsDataX, transitionDuration);

        // Define the keyframes for y axis
        PointsData pointsDataY({{0, -transitionClips.second->Reader()->info.height}, {1, 0}}, {{0.88, 0.00, 0.12, 1.00}});
        openshot::Keyframe keyframeDy = createKeyframe(pointsDataY, transitionDuration);

        auto pBorderReflectedMove = new openshot::BorderReflectedMove(keyframeDx, keyframeDy);
        transitionClips.second->AddEffect(pBorderReflectedMove);
    }

    /// Diagonal Blur Effect | Clip 1
    {
        PointsData pointsData({{0.5, 250}, {1, 0}}, {{0.17, 0.17, 0.33, 1.00}});
        openshot::Keyframe keyframe = createKeyframe(pointsData, transitionDuration);
        auto blur = new openshot::Blur(0, 0, keyframe, 0, 0, 0, 0, 0, 1);
        transitionClips.second->AddEffect(blur);
    }

    createTimelineAndWriteClips({transitionClips.first, transitionClips.second, mainClips.first, mainClips.second}, output);
}

void panLeftTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output)
{
    auto clips = createTransitionClips(file1, file2, transitionDuration);

    auto transitionClips = clips.first;
    auto mainClips = clips.second;

    ////////// Clip 1 ////////////////////////////////////////////////////////////////////////////////////////////

    /// X axis Border Reflected Move Effect | Clip 1
    {
        const auto xPointsData = PointsData({{0, 0}, {1, transitionClips.first->Reader()->info.width}}, {{0.88, 0.00, 0.12, 1.00}});
        auto dxKeyframe = createKeyframe(xPointsData, transitionDuration);

        auto pBorderReflectedMove = new openshot::BorderReflectedMove(dxKeyframe, 0);
        transitionClips.first->AddEffect(pBorderReflectedMove);
    }

    /// Horizontal Blur Effect | Clip 1
    {
        PointsData pointsData({{0, 0}, {0.5, 250}}, {{0.67, 0.00, 0.83, 0.83}});
        openshot::Keyframe keyframe = createKeyframe(pointsData, transitionDuration);
        auto blurEffect = new openshot::Blur(keyframe);
        transitionClips.first->AddEffect(blurEffect);
    }

    /// Alpha Effect | Clip 1
    {
        PointsData pointsData({{0.2, 1}, {0.6, 0}});
        transitionClips.first->alpha = createKeyframe(pointsData, transitionDuration, openshot::LINEAR);
    }

    ////////// Clip 2 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Border Reflected Move with X axis Effect | Clip 2
    {
        const auto xPointsData = PointsData({{0, -transitionClips.second->Reader()->info.width}, {1, 0}}, {{0.88, 0.00, 0.12, 1.00}});
        auto dxKeyframe = createKeyframe(xPointsData, transitionDuration);

        auto pBorderReflectedMove = new openshot::BorderReflectedMove(dxKeyframe, 0);
        transitionClips.second->AddEffect(pBorderReflectedMove);
    }

    /// Horizontal Blur Effect | Clip 2
    {
        PointsData pointsData({{0.5, 250}, {1, 0}}, {{0.17, 0.17, 0.33, 1.00}});
        openshot::Keyframe keyframe = createKeyframe(pointsData, transitionDuration);
        auto blurEffect = new openshot::Blur(keyframe);
        transitionClips.second->AddEffect(blurEffect);
    }

    createTimelineAndWriteClips({ mainClips.first, transitionClips.first, transitionClips.second, mainClips.second}, output);
}

void blurTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {
    auto clips = createTransitionClips(file1, file2, transitionDuration);

    auto transitionClips = clips.first;
    auto mainClips = clips.second;

    ////////// Clip 1 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Blur Effect | Clip 1
    {
        PointsData pointsData({{0, 0}, {0.5, 100}}, {{0.33, 0.00, 0.00, 1.00}});
        openshot::Keyframe keyframe = createKeyframe(pointsData, transitionDuration);
        auto blurEffect = new openshot::Blur(keyframe, keyframe);
        transitionClips.first->AddEffect(blurEffect);
    }

    /// Alpha Effect | Clip 1
    {
        PointsData pointsData({{0, 1}, {1, 0}});
        transitionClips.first->alpha = createKeyframe(pointsData, transitionDuration, openshot::LINEAR);
    }

    ////////// Clip 2 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Blur Effect | Clip 2
    {
        PointsData pointsData({{0.5, 100}, {1, 0}}, {{0.17, 0.18, 0.67, 1.00}});
        openshot::Keyframe keyframe = createKeyframe(pointsData, transitionDuration);
        auto blurEffect = new openshot::Blur(keyframe, keyframe);
        transitionClips.second->AddEffect(blurEffect);
    }

    createTimelineAndWriteClips({transitionClips.first, transitionClips.second, mainClips.first, mainClips.second}, output);
}

void verticalBlurTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {

    auto clips = createTransitionClips(file1, file2, transitionDuration);

    auto transitionClips = clips.first;
    auto mainClips = clips.second;

    ////////// Clip 1 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Blur Effect | Clip 1
    {
        PointsData pointsData({{0, 0}, {0.5, 100}}, {{0.80, 0.00, 0.34, 1.00}});
        openshot::Keyframe keyframe = createKeyframe(pointsData, transitionDuration);
        auto blurEffect = new openshot::Blur(0, keyframe);
        transitionClips.first->AddEffect(blurEffect);
    }

    /// Alpha Effect | Clip 1
    {
        PointsData pointsData({{0, 1}, {1, 0}}, {{0.89, 0.00, 0.11, 1.00}});
        transitionClips.first->alpha = createKeyframe(pointsData, transitionDuration);
    }

    ////////// Clip 2 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Blur Effect | Clip 2
    {
        PointsData pointsData({{0.5, 100}, {1, 0}}, {{0.64, 0.00, 0.26, 1.00}});
        openshot::Keyframe keyframe = createKeyframe(pointsData, transitionDuration);
        auto blurEffect = new openshot::Blur(0, keyframe);
        transitionClips.second->AddEffect(blurEffect);
    }

    createTimelineAndWriteClips({transitionClips.first, transitionClips.second, mainClips.first, mainClips.second}, output);
}

void rotationalBlurTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {
    auto clips = createTransitionClips(file1, file2, transitionDuration);

    auto transitionClips = clips.first;
    auto mainClips = clips.second;

    ////////// Clip 1 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Alpha Effect | Clip 1
    {
        PointsData pointsData({{0.3499, 1}, {0.35, 0}});
        transitionClips.first->alpha = createKeyframe(pointsData, transitionDuration, openshot::LINEAR);
    }

    /// Border Reflected Rotation Effect | Clip 1
    {
        const auto pointsData = PointsData({{0, 0}, {1, 180}}, {{0.68, 0.00, 0.00, 1.00}});
        auto keyframe = createKeyframe(pointsData, transitionDuration);

        auto borderReflectedRotationEffect = new openshot::BorderReflectedRotation(keyframe);
        transitionClips.first->AddEffect(borderReflectedRotationEffect);
    }

    /// Rotational Blur Effect | Clip 1
    {
        PointsData pointsData({{0, 0}, {1, 80}}, {{0.68, 0.00, 0.00, 1.00}});
        openshot::Keyframe keyframe = createKeyframe(pointsData, transitionDuration);
        auto blurEffect = new openshot::Blur(0, 0, 0, keyframe);
        transitionClips.first->AddEffect(blurEffect);
    }

    ////////// Clip 1 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Border Reflected Rotation Effect | Clip 2
    {
        const auto pointsData = PointsData({{0, -180}, {1, 0}}, {{0.68, 0.00, 0.00, 1.00}});
        auto keyframe = createKeyframe(pointsData, transitionDuration);

        auto borderReflectedRotationEffect = new openshot::BorderReflectedRotation(keyframe);
        transitionClips.second->AddEffect(borderReflectedRotationEffect);
    }


    /// Rotational Blur Effect | Clip 2
    {
        PointsData pointsData({{0, 80}, {1, 0}}, {{0.68, 0.00, 0.00, 1.00}});
        openshot::Keyframe keyframe = createKeyframe(pointsData, transitionDuration);
        auto blurEffect = new openshot::Blur(0, 0, 0, keyframe);
        transitionClips.second->AddEffect(blurEffect);
    }

    createTimelineAndWriteClips({transitionClips.first, transitionClips.second, mainClips.first, mainClips.second}, output);
}

void wooshTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {
    auto clips = createTransitionClips(file1, file2, transitionDuration);

    auto transitionClips = clips.first;
    auto mainClips = clips.second;

    ////////// Clip 1 ////////////////////////////////////////////////////////////////////////////////////////////
    openshot::Clip clip1(file1);

    /// Alpha Effect | Clip 1
    {
        PointsData pointsData({{0.33, 1}, {0.66, 0}}, {{0.420, 0.000, 0.580, 1.000}});
        transitionClips.first->alpha = createKeyframe(pointsData, transitionDuration, openshot::BEZIER);
    }

    /// Zoom Effect | Clip 1
    {
        const auto zoomPointsData = PointsData(
                {{0, 100}, {0.5, 120}, {1, 100}},
                {{0.71, 0.00, 0.83, 0.83}, {0.17, 0.17, 0.29, 1}});
        openshot::Keyframe zoomKeyframe = createKeyframe(zoomPointsData, transitionDuration);

        const auto anchorXPointsData = PointsData({{0, 0.18}, {0.5, 0.82}},{{0.71, 0.00, 0.29, 1.00}});
        openshot::Keyframe anchorXKeyframe = createKeyframe(anchorXPointsData, transitionDuration);
        auto zoomEffect = new openshot::Zoom(zoomKeyframe, anchorXKeyframe, 0.5);
        transitionClips.first->AddEffect(zoomEffect);

    }

    /// Blur Effect | Clip 1
    {
        const auto blurPointsData = PointsData(
                {{0, 0}, {0.5, 20}, {1, 0}},
                {{0.71, 0.00, 0.83, 0.83}, {0.17, 0.17, 0.19, 1}});
        openshot::Keyframe blurKeyframe = createKeyframe(blurPointsData, transitionDuration);

        auto blurEffect = new openshot::Blur(blurKeyframe, blurKeyframe, 0, 0,  0, 0, 0, 0, 2);
        transitionClips.first->AddEffect(blurEffect);
    }

    /// Zoom Blur Effect | Clip 1
    {
        const auto zoomBlurPointsData = PointsData(
                {{0, 0}, {0.5, 140}, {1, 0}},
                {{0.71, 0.00, 0.83, 0.83}, {0.17, 0.17, 0.19, 1}});
        openshot::Keyframe zoomBlurKeyframe = createKeyframe(zoomBlurPointsData, transitionDuration);

        const auto anchorXPointsData = PointsData({{0, 0.3}, {1, 0.6}},{{0.71, 0.00, 0.29, 1.00}});
        openshot::Keyframe anchorXKeyframe = createKeyframe(anchorXPointsData, transitionDuration);

        auto blur = new openshot::Blur(0, 0, 0, 0, zoomBlurKeyframe, anchorXKeyframe, 0.5);
        transitionClips.first->AddEffect(blur);

    }

    ////////// Clip 2 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Zoom Effect | Clip 2
    {
        const auto zoomPointsData = PointsData(
                {{0, 100}, {0.5, 120}, {1, 100}},
                {{0.71, 0.00, 0.83, 0.83}, {0.17, 0.17, 0.29, 1}});
        openshot::Keyframe zoomKeyframe = createKeyframe(zoomPointsData, transitionDuration);

        const auto anchorXPointsData = PointsData({{0, 0.18}, {0.5, 0.82}},{{0.71, 0.00, 0.29, 1.00}});
        openshot::Keyframe anchorXKeyframe = createKeyframe(anchorXPointsData, transitionDuration);
        auto zoomEffect = new openshot::Zoom(zoomKeyframe, anchorXKeyframe, 0.5);
        transitionClips.second->AddEffect(zoomEffect);
    }

    /// Blur Effect | Clip 2
    {
        const auto blurPointsData = PointsData(
                {{0, 0}, {0.5, 20}, {1, 0}},
                {{0.71, 0.00, 0.83, 0.83}, {0.17, 0.17, 0.19, 1}});
        openshot::Keyframe blurKeyframe = createKeyframe(blurPointsData, transitionDuration);

        auto blurEffect = new openshot::Blur(blurKeyframe, blurKeyframe, 0, 0,  0, 0, 0, 0, 2);

        transitionClips.second->AddEffect(blurEffect);
    }

    /// Zoom Blur Effect | Clip 2
    {
        const auto zoomBlurPointsData = PointsData(
                {{0, 0}, {0.5, 140}, {1, 0}},
                {{0.71, 0.00, 0.83, 0.83}, {0.17, 0.17, 0.19, 1}});
        openshot::Keyframe zoomBlurKeyframe = createKeyframe(zoomBlurPointsData, transitionDuration);

        const auto anchorXPointsData = PointsData({{0, 0.3}, {1, 0.6}},{{0.71, 0.00, 0.29, 1.00}});
        openshot::Keyframe anchorXKeyframe = createKeyframe(anchorXPointsData, transitionDuration);

        auto blurEffect = new openshot::Blur(0, 0, 0, 0, zoomBlurKeyframe, anchorXKeyframe, 0.5);
        transitionClips.second->AddEffect(blurEffect);
    }

    createTimelineAndWriteClips({ mainClips.first, transitionClips.first , transitionClips.second, mainClips.second}, output);
}

void dissolveTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {
    auto clips = createTransitionClips(file1, file2, transitionDuration);

    auto transitionClips = clips.first;
    auto mainClips = clips.second;

    ////////// Clip 1 ////////////////////////////////////////////////////////////////////////////////////////////
    /// Alpha Effect | Clip 2
    {
        PointsData pointsData({{0.33, 1}, {0.67, 0}});
        transitionClips.first->alpha = createKeyframe(pointsData, transitionDuration, openshot::LINEAR);
    }

    createTimelineAndWriteClips({transitionClips.first, transitionClips.second, mainClips.first, mainClips.second}, output);
}

void dissolveBlurTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {
    auto clips = createTransitionClips(file1, file2, transitionDuration);

    auto transitionClips = clips.first;
    auto mainClips = clips.second;

    ////////// Clip 1 ////////////////////////////////////////////////////////////////////////////////////////////
    /// Alpha Effect | Clip 2
    {
        PointsData pointsData({{0.33, 1}, {0.67, 0}});
        transitionClips.first->alpha = createKeyframe(pointsData, transitionDuration, openshot::LINEAR);
    }

    /// Blur Effect | Clip 1
    {
        PointsData pointsData({{0, 0}, {1, 20}}, {{0.33, 0.00, 0.67, 1.00}});
        openshot::Keyframe keyframe = createKeyframe(pointsData, transitionDuration);
        auto blurEffect = new openshot::Blur(keyframe, keyframe, 0, 0, 0, 0, 0, 0, 1);
        transitionClips.first->AddEffect(blurEffect);
    }

    ////////// Clip 2 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Blur Effect | Clip 2
    {
        PointsData pointsData({{0, 20}, {1, 0}}, {{0.33, 0.00, 0.67, 1.00}});
        openshot::Keyframe keyframe = createKeyframe(pointsData, transitionDuration);
        auto blurEffect = new openshot::Blur(keyframe, keyframe, 0, 0, 0, 0, 0, 0, 1);
        transitionClips.second->AddEffect(blurEffect);
    }
    createTimelineAndWriteClips({transitionClips.first, transitionClips.second, mainClips.first, mainClips.second}, output);
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

    /// Alpha Effect | Clip 2
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
    auto clips = createTransitionClips(file1, file2, transitionDuration);

    auto transitionClips = clips.first;
    auto mainClips = clips.second;

    ////////// Clip 1 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Vertical Split Shift Effect | Clip 1
    {
        PointsData pointsData({{0, 0}, {1, transitionClips.first->Reader()->info.height}},
                              {{0.86, 0.00, 0.14, 1.00}});
        openshot::Keyframe keyframe = createKeyframe(pointsData, transitionDuration);
        auto verticalSplitShiftEffect = new openshot::VerticalSplitShift(keyframe);
        transitionClips.first->AddEffect(verticalSplitShiftEffect);
    }

    ////////// Clip 2 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Vertical Split Shift Effect | Clip 2
    {
        PointsData pointsData({{0, -transitionClips.second->Reader()->info.height}, {1, 0}},
                              {{0.86, 0.00, 0.14, 1.00}});
        openshot::Keyframe keyframe = createKeyframe(pointsData, transitionDuration);
        auto verticalSplitShiftEffect = new openshot::VerticalSplitShift(keyframe);
        transitionClips.second->AddEffect(verticalSplitShiftEffect);
    }

    createTimelineAndWriteClips({transitionClips.first, transitionClips.second, mainClips.first, mainClips.second}, output);
}

void zoomInTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {
    auto clips = createTransitionClips(file1, file2, transitionDuration);

    auto transitionClips = clips.first;
    auto mainClips = clips.second;
    ////////// Clip 1 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Alpha Effect | Clip 1
    {
        PointsData pointsData({{0.33, 1}, {0.66, 0}}, {{0.33, 0.67, 0.00, 1}});
        transitionClips.first->alpha = createKeyframe(pointsData, transitionDuration, openshot::LINEAR);
    }

    /// Zoom Effect | Clip 1
    {
        const auto zoomPointsData = PointsData({{0, 100}, {1, 250}},{{0.58, 0.00, 0.13, 1.00}});
        openshot::Keyframe zoomKeyframe = createKeyframe(zoomPointsData, transitionDuration);
        auto zoomEffect = new openshot::Zoom(zoomKeyframe);
        transitionClips.first->AddEffect(zoomEffect);
    }

    ////////// Clip 2 ////////////////////////////////////////////////////////////////////////////////////////////


    /// Zoom Effect | Clip 2
    {
        const auto zoomPointsData = PointsData({{0, 40}, {1, 100}},{{0.58, 0.00, 0.13, 1.00}});
        openshot::Keyframe zoomKeyframe = createKeyframe(zoomPointsData, transitionDuration);
        auto zoomEffect = new openshot::Zoom(zoomKeyframe);
        transitionClips.second->AddEffect(zoomEffect);
    }

    createTimelineAndWriteClips({transitionClips.first, transitionClips.second, mainClips.first, mainClips.second}, output);
}

void contrastTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {
    auto clips = createTransitionClips(file1, file2, transitionDuration);

    auto transitionClips = clips.first;
    auto mainClips = clips.second;
    ////////// Clip 1 ////////////////////////////////////////////////////////////////////////////////////////////

    /// Mask Effect | Clip 1
    {
        const auto levelPercentage = PointsData({{0, 100}, {1, 0}},{{0.33, 0.00, 0.67, 1.00}});
        openshot::Keyframe levelPercentageKeyframe = createKeyframe(levelPercentage, transitionDuration);

        auto wipeEffect = new openshot::Wipe(levelPercentageKeyframe,levelPercentageKeyframe);
        transitionClips.first->AddEffect(wipeEffect);
    }

    createTimelineAndWriteClips({transitionClips.first, transitionClips.second, mainClips.first, mainClips.second}, output);
}