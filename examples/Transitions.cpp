#include "Transitions.h"

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
#include "effects/VerticalSplitShift.h"
#include "Color.h"

long timeToFrame(float duration, int fps = 30) {
    return std::max(long(fps * duration), 1L);
}

struct BezierValue {
    BezierValue(float v1_, float v2_, float v3_, float v4_)
        : v1(v1_), v2(v2_), v3(v3_), v4(v4_) {}
    float v1;
    float v2;
    float v3;
    float v4;
};


struct PointsData {
    std::vector<std::pair<float, float>> timeValues;
    std::vector<openshot::Point> points;
    std::vector<BezierValue> bezierValues;
};

void foo() {
//    float clip1Position = 0;
//    float clip2Position = clip1End - transitionDuration;


    float transitionDuration = 0.5;

    float clip1Start = 0;
    float clip1End = transitionDuration;
    float clip2Start = 0;
    float clip2End = transitionDuration;

    std::vector<openshot::Point> points;
    PointsData pointsData;
    const auto& timeValues = pointsData.timeValues;
    for (int i = 0; i < timeValues.size(); i++) {
        if (i == 0) {
            points.emplace_back(timeToFrame(0), timeValues[i].second);
            continue;
        }
        points.emplace_back(timeToFrame(transitionDuration - (1-timeValues[i].first)), timeValues[i].second);
    }
    float startValue = 0;
    float endValue = 1;
    float startTime = 0;
    float endTime = 1;
    auto startHandle = std::make_pair(0.88, 0.00);
    auto endHandle = std::make_pair(0.12, 1.00);

    auto point0 = openshot::Point(timeToFrame(clip1Start),                                      startValue);
    auto point1 = openshot::Point(timeToFrame(clip1End - (1 - startTime) * transitionDuration), startValue);
    auto point2 = openshot::Point(timeToFrame(clip1End - (1 - endTime)   * transitionDuration), endValue);

    point1.handle_right = openshot::Coordinate(startHandle.first, startHandle.second);
    point2.handle_left = openshot::Coordinate(endHandle.first, endHandle.second);
}

void panBottomRightTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {
    float clip1Start = 0;
    float clip1End = transitionDuration;
    float clip1Position = 0;
    float clip2Start = 0;
    float clip2End = transitionDuration;
    float clip2Position = clip1End - transitionDuration;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////// Clip 1
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    openshot::Clip clip1(file1);
    clip1.Layer(1);
    clip1.Position(clip1Position);
    clip1.Start(clip1Start);
    clip1.End(clip1End);
    clip1.scale = openshot::SCALE_FIT;

    /// Border Reflected Move with X axis Effect | Clip 1
    {
        // Define the keyframes for x axis
        float startValue = 0;
        float endValue = clip1.Reader()->info.width;
        float startTime = 0;
        float endTime = 1;
        auto startHandle = std::make_pair(0.88, 0.00);
        auto endHandle = std::make_pair(0.12, 1.00);

        auto point0 = openshot::Point(timeToFrame(clip1Start),                                      startValue);
        auto point1 = openshot::Point(timeToFrame(clip1End - (1 - startTime) * transitionDuration), startValue);
        auto point2 = openshot::Point(timeToFrame(clip1End - (1 - endTime)   * transitionDuration), endValue);

        point1.handle_right = openshot::Coordinate(startHandle.first, startHandle.second);
        point2.handle_left = openshot::Coordinate(endHandle.first, endHandle.second);

        openshot::Keyframe keyframeDx({point0, point1, point2});

        // Define the keyframes for y axis
        endValue = clip1.Reader()->info.height;

        point0 = openshot::Point(timeToFrame(clip1Start),                                      startValue);
        point1 = openshot::Point(timeToFrame(clip1End - (1 - startTime) * transitionDuration), startValue);
        point2 = openshot::Point(timeToFrame(clip1End - (1 - endTime)   * transitionDuration), endValue);

        point1.handle_right = openshot::Coordinate(startHandle.first, startHandle.second);
        point2.handle_left = openshot::Coordinate(endHandle.first, endHandle.second);

        openshot::Keyframe keyframeDy({point0, point1, point2});

        auto pBorderReflectedMove = new openshot::BorderReflectedMove(keyframeDx, keyframeDy);
        clip1.AddEffect(pBorderReflectedMove);
    }

    /// Diagonal Blur Effect | Clip 1
    {
        float startValue = 0;
        float endValue = 250;
        float startTime = 0;
        float endTime = 0.5;
        auto startHandle = std::make_pair(0.67, 0.00);
        auto endHandle = std::make_pair(0.83, 0.83);

        auto point0 = openshot::Point(timeToFrame(clip1Start),                                      startValue);
        auto point1 = openshot::Point(timeToFrame(clip1End - (1 - startTime) * transitionDuration), startValue);
        auto point2 = openshot::Point(timeToFrame(clip1End - (1 - endTime)   * transitionDuration), endValue);

        point1.handle_right = openshot::Coordinate(startHandle.first, startHandle.second);
        point2.handle_left = openshot::Coordinate(endHandle.first, endHandle.second);

        openshot::Keyframe keyframe({point0, point1, point2});
        auto blur = new openshot::Blur(0, 0, keyframe, 0, 0, 0, 0, 0, 1);
        clip1.AddEffect(blur);
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////// Clip 2
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    openshot::Clip clip2(file2);
    clip2.Layer(2);
    clip2.Position(clip2Position);
    clip2.Start(clip2Start);
    clip2.End(clip2End);
    clip2.scale = openshot::SCALE_FIT;

    /// Border Reflected Move with X axis Effect | Clip 2
    {
        // Define keyframe for x axis
        float startValue = -clip2.Reader()->info.width;
        float endValue = 0;
        float startTime = 0;
        float endTime = 1;
        auto startHandle = std::make_pair(0.88, 0.00);
        auto endHandle = std::make_pair(0.12, 1.00);

        auto point0 = openshot::Point(timeToFrame(clip2Start),                                startValue);
        auto point1 = openshot::Point(timeToFrame(clip2Start + startTime * transitionDuration), startValue);
        auto point2 = openshot::Point(timeToFrame(clip2Start + endTime * transitionDuration),   endValue);

        point1.handle_right = openshot::Coordinate(startHandle.first, startHandle.second);
        point2.handle_left = openshot::Coordinate(endHandle.first, endHandle.second);

        openshot::Keyframe keyframeDx({point0, point1, point2});

        // Define keyframe for y axis
        startValue = -clip2.Reader()->info.height;

        point0 = openshot::Point(timeToFrame(clip2Start),                                  startValue);
        point1 = openshot::Point(timeToFrame(clip2Start + startTime * transitionDuration), startValue);
        point2 = openshot::Point(timeToFrame(clip2Start + endTime * transitionDuration),   endValue);

        point1.handle_right = openshot::Coordinate(startHandle.first, startHandle.second);
        point2.handle_left = openshot::Coordinate(endHandle.first, endHandle.second);

        openshot::Keyframe keyframeDy({point0, point1, point2});

        auto pBorderReflectedMove = new openshot::BorderReflectedMove(keyframeDx, keyframeDy);
        clip2.AddEffect(pBorderReflectedMove);
    }

    /// Diagonal Blur Effect | Clip 2
    {
        float startValue = 250;
        float endValue = 0;
        float startTime = 0.5;
        float endTime = 1;
        auto startHandle = std::make_pair(0.17, 0.17);
        auto endHandle = std::make_pair(0.33, 1.00);

        auto point0 = openshot::Point(timeToFrame(clip2Start),                               startValue);
        auto point1 = openshot::Point(timeToFrame(clip2Start + startTime * transitionDuration),startValue);
        auto point2 = openshot::Point(timeToFrame(clip2Start + endTime * transitionDuration),  endValue);

        point1.handle_right = openshot::Coordinate(startHandle.first, startHandle.second);
        point2.handle_left = openshot::Coordinate(endHandle.first, endHandle.second);

        openshot::Keyframe keyframe({point0, point1, point2});
        auto blur = new openshot::Blur(0, 0, keyframe, 0, 0, 0, 0, 0, 1);
        clip2.AddEffect(blur);
    }

    /// Alpha Effect | Clip 2
    {
        float startValue = 0;
        float endValue = 0.99;
        float startTime = 0.2;
        float endTime = 0.6;

        openshot::Point point0(timeToFrame(clip2Start),                                startValue, openshot::LINEAR);
        openshot::Point point1(timeToFrame(clip2Start + startTime * transitionDuration), startValue, openshot::LINEAR);
        openshot::Point point2(timeToFrame(clip2Start + endTime * transitionDuration),   endValue, openshot::LINEAR);

        clip2.alpha = openshot::Keyframe({point0, point1, point2});
    }

    openshot::Timeline timeLine(1920, 1080, openshot::Fraction(30, 1),
                                48000, 2, openshot::ChannelLayout::LAYOUT_STEREO);
    timeLine.Open();
    timeLine.AddClip(&clip2);
    timeLine.AddClip(&clip1);
    openshot::FFmpegWriter w(output);
    w.SetAudioOptions(true, "libvorbis", 48000, 2, openshot::ChannelLayout::LAYOUT_STEREO, 128000);
    w.SetVideoOptions(true, "libx264" , openshot::Fraction(30, 1),  1920, 1080, openshot::Fraction(1,1), false, false, 4000000);
    w.Open();
    w.WriteFrame(&timeLine, 0, transitionDuration * 30);

    timeLine.Close();
    w.Close();
}

void panLeftTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output)
{
    float clip1Start = 0;
    float clip1End = 2;
    float clip1Position = 0;
    float clip2Start = 0;
    float clip2End = 2;
    float clip2Position = clip1End - transitionDuration;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////// Clip 1
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    openshot::Clip clip1(file1);
    clip1.Layer(1);
    clip1.Position(clip1Position);
    clip1.Start(clip1Start);
    clip1.End(clip1End);
    clip1.scale = openshot::SCALE_FIT;

    /// Border Reflected Move with X axis Effect | Clip 1
    {
        float startValue = 0;
        float endValue = clip1.Reader()->info.width;
        float startTime = 0;
        float endTime = 1;
        auto startHandle = std::make_pair(0.88, 0.00);
        auto endHandle = std::make_pair(0.12, 1.00);

        auto point0 = openshot::Point(timeToFrame(clip1Start),                                      startValue);
        auto point1 = openshot::Point(timeToFrame(clip1End - (1 - startTime) * transitionDuration), startValue);
        auto point2 = openshot::Point(timeToFrame(clip1End - (1 - endTime)   * transitionDuration), endValue);

        point1.handle_right = openshot::Coordinate(startHandle.first, startHandle.second);
        point2.handle_left = openshot::Coordinate(endHandle.first, endHandle.second);

        openshot::Keyframe keyframe({point0, point1, point2});
        auto pBorderReflectedMove = new openshot::BorderReflectedMove(keyframe, 0);
        clip1.AddEffect(pBorderReflectedMove);
    }

    /// Horizontal Blur Effect | Clip 1
    {
        float startValue = 0;
        float endValue = 250;
        float startTime = 0;
        float endTime = 0.5;
        auto startHandle = std::make_pair(0.67, 0.00);
        auto endHandle = std::make_pair(0.83, 0.83);

        auto point0 = openshot::Point(timeToFrame(clip1Start),                                      startValue);
        auto point1 = openshot::Point(timeToFrame(clip1End - (1 - startTime) * transitionDuration), startValue);
        auto point2 = openshot::Point(timeToFrame(clip1End - (1 - endTime)   * transitionDuration), endValue);

        point1.handle_right = openshot::Coordinate(startHandle.first, startHandle.second);
        point2.handle_left = openshot::Coordinate(endHandle.first, endHandle.second);

        openshot::Keyframe keyframe({point0, point1, point2});
        auto blur = new openshot::Blur(keyframe, 0, 0, 0);
        clip1.AddEffect(blur);
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////// Clip 2
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    openshot::Clip clip2(file2);
    clip2.Layer(2);
    clip2.Position(clip2Position);
    clip2.Start(clip2Start);
    clip2.End(clip2End);
    clip2.scale = openshot::SCALE_FIT;

    /// Border Reflected Move with X axis Effect | Clip 2
    {
        float startValue = -clip2.Reader()->info.width;
        float endValue = 0;
        float startTime = 0;
        float endTime = 1;
        auto startHandle = std::make_pair(0.88, 0.00);
        auto endHandle = std::make_pair(0.12, 1.00);

        auto point0 = openshot::Point(timeToFrame(clip2Start),                                startValue);
        auto point1 = openshot::Point(timeToFrame(clip2Start + startTime * transitionDuration), startValue);
        auto point2 = openshot::Point(timeToFrame(clip2Start + endTime * transitionDuration),   endValue);

        point1.handle_right = openshot::Coordinate(startHandle.first, startHandle.second);
        point2.handle_left = openshot::Coordinate(endHandle.first, endHandle.second);

        openshot::Keyframe keyframe({point0, point1, point2});
        auto pBorderReflectedMove = new openshot::BorderReflectedMove(keyframe, 0);
        clip2.AddEffect(pBorderReflectedMove);
    }

    /// Horizontal Blur Effect | Clip 2
    {
        float startValue = 250;
        float endValue = 0;
        float startTime = 0.5;
        float endTime = 1;
        auto startHandle = std::make_pair(0.17, 0.17);
        auto endHandle = std::make_pair(0.33, 1.00);

        auto point0 = openshot::Point(timeToFrame(clip2Start),                               startValue);
        auto point1 = openshot::Point(timeToFrame(clip2Start + startTime * transitionDuration),startValue);
        auto point2 = openshot::Point(timeToFrame(clip2Start + endTime * transitionDuration),  endValue);

        point1.handle_right = openshot::Coordinate(startHandle.first, startHandle.second);
        point2.handle_left = openshot::Coordinate(endHandle.first, endHandle.second);

        openshot::Keyframe keyframe({point0, point1, point2});
        auto blur = new openshot::Blur(keyframe, 0, 0, 0);
        clip2.AddEffect(blur);
    }

    /// Alpha Effect | Clip 2
    {
        float startValue = 0;
        float endValue = 0.99;
        float startTime = 0.2;
        float endTime = 0.6;

        openshot::Point point0(timeToFrame(clip2Start),                                startValue, openshot::LINEAR);
        openshot::Point point1(timeToFrame(clip2Start + startTime * transitionDuration), startValue, openshot::LINEAR);
        openshot::Point point2(timeToFrame(clip2Start + endTime * transitionDuration),   endValue, openshot::LINEAR);

        clip2.alpha = openshot::Keyframe({point0, point1, point2});
    }

    openshot::Timeline timeLine(1920, 1080, openshot::Fraction(30, 1),
                                48000, 2, openshot::ChannelLayout::LAYOUT_STEREO);
    timeLine.Open();
    timeLine.AddClip(&clip2);
    timeLine.AddClip(&clip1);
    openshot::FFmpegWriter w(output);
    w.SetAudioOptions(true, "libvorbis", 48000, 2, openshot::ChannelLayout::LAYOUT_STEREO, 128000);
    w.SetVideoOptions(true, "libx264" , openshot::Fraction(30, 1),  1920, 1080, openshot::Fraction(1,1), false, false, 4000000);
    w.Open();
    w.WriteFrame(&timeLine, 0, 120);

    timeLine.Close();
    w.Close();
}

void blurTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {
    float clip1Start = 0;
    float clip1End = 2;
    float clip1Position = 0;
    float clip2Start = 0;
    float clip2End = 2;
    float clip2Position = clip1End - transitionDuration;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////// Clip 1
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    openshot::Clip clip1(file1);
    clip1.Layer(1);
    clip1.Position(clip1Position);
    clip1.Start(clip1Start);
    clip1.End(clip1End);
    clip1.scale = openshot::SCALE_FIT;

    /// Blur Effect | Clip 1
    {
        float startValue = 0;
        float endValue = 100;
        float startTime = 0;
        float endTime = 0.5;
        auto startHandle = std::make_pair(0.33, 0.00);
        auto endHandle = std::make_pair(0.00, 1.00);

        auto point0 = openshot::Point(timeToFrame(clip1Start),                                      startValue);
        auto point1 = openshot::Point(timeToFrame(clip1End - (1 - startTime) * transitionDuration), startValue);
        auto point2 = openshot::Point(timeToFrame(clip1End - (1 - endTime)   * transitionDuration), endValue);

        point1.handle_right = openshot::Coordinate(startHandle.first, startHandle.second);
        point2.handle_left = openshot::Coordinate(endHandle.first, endHandle.second);

        openshot::Keyframe keyframe({point0, point1, point2});
        auto blur = new openshot::Blur(keyframe, keyframe, 0, 0);
        clip1.AddEffect(blur);
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////// Clip 2
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    openshot::Clip clip2(file2);
    clip2.Layer(2);
    clip2.Position(clip2Position);
    clip2.Start(clip2Start);
    clip2.End(clip2End);
    clip2.scale = openshot::SCALE_FIT;

    /// Alpha Effect | Clip 2
    {
        float startValue = 0;
        float endValue = 1;
        float startTime = 0;
        float endTime = 1;
        auto startHandle = std::make_pair(0.89, 0.00);
        auto endHandle = std::make_pair(0.11, 1.00);

        openshot::Point point0(timeToFrame(clip2Start),                    startValue);
        openshot::Point point1(timeToFrame(clip2Start + startTime * transitionDuration), startValue);
        openshot::Point point2(timeToFrame(clip2Start + endTime * transitionDuration),     endValue);

        point1.handle_right = openshot::Coordinate(startHandle.first, startHandle.second);
        point2.handle_left = openshot::Coordinate(endHandle.first, endHandle.second);

        clip2.alpha = openshot::Keyframe({point0, point1, point2});
    }

    /// Blur Effect | Clip 2
    {
        float startValue = 100;
        float endValue = 0;
        float startTime = 0.5;
        float endTime = 1;
        auto startHandle = std::make_pair(0.17, 0.18);
        auto endHandle = std::make_pair(0.67, 1.00);

        auto point0 = openshot::Point(timeToFrame(clip2Start),                                  startValue);
        auto point1 = openshot::Point(timeToFrame(clip2Start + startTime * transitionDuration), startValue);
        auto point2 = openshot::Point(timeToFrame(clip2Start + endTime * transitionDuration),   endValue);

        point1.handle_right = openshot::Coordinate(startHandle.first, startHandle.second);
        point2.handle_left = openshot::Coordinate(endHandle.first, endHandle.second);

        openshot::Keyframe keyframe({point0, point1, point2});
        auto blur = new openshot::Blur(keyframe, keyframe, 0, 0);
        clip2.AddEffect(blur);
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

void verticalBlurTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {
    float clip1Start = 0;
    float clip1End = 2;
    float clip1Position = 0;
    float clip2Start = 0;
    float clip2End = 2;
    float clip2Position = clip1End - transitionDuration;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////// Clip 1
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    openshot::Clip clip1(file1);
    clip1.Layer(1);
    clip1.Position(clip1Position);
    clip1.Start(clip1Start);
    clip1.End(clip1End);
    clip1.scale = openshot::SCALE_FIT;

    /// Blur Effect | Clip 1
    {
        float startValue = 0;
        float endValue = 100;
        float startTime = 0;
        float endTime = 0.5;
        auto startHandle = std::make_pair(0.80, 0.00);
        auto endHandle = std::make_pair(0.34, 1.00);

        auto point0 = openshot::Point(timeToFrame(clip1Start),                                      startValue);
        auto point1 = openshot::Point(timeToFrame(clip1End - (1 - startTime) * transitionDuration), startValue);
        auto point2 = openshot::Point(timeToFrame(clip1End - (1 - endTime)   * transitionDuration), endValue);

        point1.handle_right = openshot::Coordinate(startHandle.first, startHandle.second);
        point2.handle_left = openshot::Coordinate(endHandle.first, endHandle.second);

        openshot::Keyframe keyframe({point0, point1, point2});
        auto blur = new openshot::Blur(0, keyframe, 0, 0);
        clip1.AddEffect(blur);
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////// Clip 2
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    openshot::Clip clip2(file2);
    clip2.Layer(2);
    clip2.Position(clip2Position);
    clip2.Start(clip2Start);
    clip2.End(clip2End);
    clip2.scale = openshot::SCALE_FIT;

    /// Alpha Effect | Clip 2
    {
        float startValue = 0;
        float endValue = 1;
        float startTime = 0;
        float endTime = 1;
        auto startHandle = std::make_pair(0.89, 0.00);
        auto endHandle = std::make_pair(0.11, 1.00);

        openshot::Point point0(timeToFrame(clip2Start),                               startValue);
        openshot::Point point1(timeToFrame(clip2Start + startTime * transitionDuration),           startValue);
        openshot::Point point2(timeToFrame(clip2Start + endTime * transitionDuration),endValue);

        point1.handle_right = openshot::Coordinate(startHandle.first, startHandle.second);
        point2.handle_left = openshot::Coordinate(endHandle.first, endHandle.second);

        clip2.alpha = openshot::Keyframe({point0, point1, point2});
    }

    /// Blur Effect | Clip 2
    {
        float startValue = 100;
        float endValue = 0;
        float startTime = 0.5;
        float endTime = 1;
        auto startHandle = std::make_pair(0.64, 0.00);
        auto endHandle = std::make_pair(0.26, 1.00);

        auto point0 = openshot::Point(timeToFrame(clip2Start),                                  startValue);
        auto point1 = openshot::Point(timeToFrame(clip2Start + startTime * transitionDuration), startValue);
        auto point2 = openshot::Point(timeToFrame(clip2Start + endTime * transitionDuration),   endValue);

        point1.handle_right = openshot::Coordinate(startHandle.first, startHandle.second);
        point2.handle_left = openshot::Coordinate(endHandle.first, endHandle.second);

        openshot::Keyframe keyframe({point0, point1, point2});
        auto blur = new openshot::Blur(0, keyframe, 0, 0);
        clip2.AddEffect(blur);
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

void rotationalBlurTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {
    float clip1Start = 0;
    float clip1End = 2;
    float clip1Position = 0;
    float clip2Start = 0;
    float clip2End = 2;
    float clip2Position = clip1End - transitionDuration;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////// Clip 1
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    openshot::Clip clip1(file1);
    clip1.Layer(2);
    clip1.Position(clip1Position);
    clip1.Start(clip1Start);
    clip1.End(clip1End);
    clip1.scale = openshot::SCALE_FIT;

    /// Alpha Effect | Clip 1
    {
        float startValue = 0.999;
        float endValue = 0;
        float startTime = 0.3499;
        float endTime = 0.35;

        openshot::Point point0(timeToFrame(clip1Start),                                startValue, openshot::LINEAR);
        openshot::Point point1(timeToFrame(clip1End - (1 - startTime) * transitionDuration), startValue, openshot::LINEAR);
        openshot::Point point2(timeToFrame(clip1End - (1- endTime) * transitionDuration),   endValue, openshot::LINEAR);

        clip1.alpha = openshot::Keyframe({point0, point1, point2});
    }

    /// Border Reflected Rotation Effect | Clip 1
    {
        float rotationAngleStartValue = 0;
        float rotationAngleEndValue = 180;
        float rotationAngleStartTime = 0;
        float rotationAngleEndTime = 1;
        auto rotationAngleStartHandle = std::make_pair(0.68, 0.00);
        auto rotationAngleEndHandle = std::make_pair(0.00, 1.00);

        auto rotationAnglePoint0 = openshot::Point(timeToFrame(clip1Start),                                      rotationAngleStartValue);
        auto rotationAnglePoint1 = openshot::Point(timeToFrame(clip1End - (1 - rotationAngleStartTime) * transitionDuration), rotationAngleStartValue);
        auto rotationAnglePoint2 = openshot::Point(timeToFrame(clip1End - (1 - rotationAngleEndTime)   * transitionDuration), rotationAngleEndValue);

        rotationAnglePoint1.handle_right = openshot::Coordinate(rotationAngleStartHandle.first, rotationAngleStartHandle.second);
        rotationAnglePoint2.handle_left = openshot::Coordinate(rotationAngleEndHandle.first, rotationAngleEndHandle.second);

        openshot::Keyframe rotationAngleKeyframe({rotationAnglePoint0, rotationAnglePoint1, rotationAnglePoint2});
        auto borderReflectedRotationEffect = new openshot::BorderReflectedRotation(rotationAngleKeyframe);
        borderReflectedRotationEffect->Layer(2);
        clip1.AddEffect(borderReflectedRotationEffect);
    }

    /// Rotational Blur Effect | Clip 1
    {
        float rotationBlurStartValue = 0;
        float rotationBlurEndValue = 80;
        float rotationBlurStarTime = 0;
        float rotationBlurEndTime = 1;
        auto rotationBlurStartHandle = std::make_pair(0.68, 0.00);
        auto rotationBlurEndHandle = std::make_pair(0.00, 1.00);

        auto rotationBlurPoint0 = openshot::Point(timeToFrame(clip1Start),                                      rotationBlurStartValue);
        auto rotationBlurPoint1 = openshot::Point(timeToFrame(clip1End - (1 - rotationBlurStarTime) * transitionDuration), rotationBlurStartValue);
        auto rotationBlurPoint2 = openshot::Point(timeToFrame(clip1End - (1 - rotationBlurEndTime)   * transitionDuration), rotationBlurEndValue);

        rotationBlurPoint1.handle_right = openshot::Coordinate(rotationBlurStartHandle.first, rotationBlurStartHandle.second);
        rotationBlurPoint2.handle_left = openshot::Coordinate(rotationBlurEndHandle.first, rotationBlurEndHandle.second);

        openshot::Keyframe rotationBlurKeyframe({rotationBlurPoint0, rotationBlurPoint1, rotationBlurPoint2});

        auto blurEffect = new openshot::Blur(0, 0, 0, rotationBlurKeyframe);
        blurEffect->Layer(2);
        clip1.AddEffect(blurEffect);
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////// Clip 2
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    openshot::Clip clip2(file2);
    clip2.Layer(1);
    clip2.Position(clip2Position);
    clip2.Start(clip2Start);
    clip2.End(clip2End);
    clip2.scale = openshot::SCALE_FIT;

    /// Border Reflected Rotation Effect | Clip 2
    {
        float rotationAngleStartValue = -180;
        float rotationAngleEndValue = 0;
        float rotationAngleStartTime = 0;
        float rotationAngleEndTime = 1;
        auto rotationAngleStartHandle = std::make_pair(0.68, 0.00);
        auto rotationAngleEndHandle = std::make_pair(0.00, 1.00);

        auto rotationAnglePoint0 = openshot::Point(timeToFrame(clip2Start),                                      rotationAngleStartValue);
        auto rotationAnglePoint1 = openshot::Point(timeToFrame(clip2Start + rotationAngleStartTime * transitionDuration), rotationAngleStartValue);
        auto rotationAnglePoint2 = openshot::Point(timeToFrame(clip2Start + rotationAngleEndTime * transitionDuration), rotationAngleEndValue);

        rotationAnglePoint1.handle_right = openshot::Coordinate(rotationAngleStartHandle.first, rotationAngleStartHandle.second);
        rotationAnglePoint2.handle_left = openshot::Coordinate(rotationAngleEndHandle.first, rotationAngleEndHandle.second);

        openshot::Keyframe rotationAngleKeyframe({rotationAnglePoint0, rotationAnglePoint1, rotationAnglePoint2});
        auto borderReflectedRotation = new openshot::BorderReflectedRotation(rotationAngleKeyframe);
        borderReflectedRotation->Layer(1);
        clip2.AddEffect(borderReflectedRotation);
    }

    /// Rotational Blur Effect | Clip 2
    {
        float rotationBlurStartValue = 80;
        float rotationBlurEndValue = 0;
        float rotationBlurStarTime = 0;
        float rotationBlurEndTime = 1;
        auto rotationBlurStartHandle = std::make_pair(0.68, 0.00);
        auto rotationBlurEndHandle = std::make_pair(0.00, 1.00);

        auto rotationBlurPoint0 = openshot::Point(timeToFrame(clip2Start),                                      rotationBlurStartValue);
        auto rotationBlurPoint1 = openshot::Point(timeToFrame(clip2Start + rotationBlurStarTime * transitionDuration), rotationBlurStartValue);
        auto rotationBlurPoint2 = openshot::Point(timeToFrame(clip2Start + rotationBlurEndTime * transitionDuration), rotationBlurEndValue);

        rotationBlurPoint1.handle_right = openshot::Coordinate(rotationBlurStartHandle.first, rotationBlurStartHandle.second);
        rotationBlurPoint2.handle_left = openshot::Coordinate(rotationBlurEndHandle.first, rotationBlurEndHandle.second);

        openshot::Keyframe rotationBlurKeyframe({rotationBlurPoint0, rotationBlurPoint1, rotationBlurPoint2});

        auto blurEffect = new openshot::Blur(0, 0, 0, rotationBlurKeyframe);
        blurEffect->Layer(2);
        clip2.AddEffect(blurEffect);
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

void wooshTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {
    float clip1Start = 0;
    float clip1End = 1;
    float clip1Position = 0;
    float clip2Start = 0;
    float clip2End = 1;
    float clip2Position = clip1End - transitionDuration;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////// Clip 1
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    openshot::Clip clip1(file1);
    clip1.Layer(2);
    clip1.Position(clip1Position);
    clip1.Start(clip1Start);
    clip1.End(clip1End);
    clip1.scale = openshot::SCALE_FIT;

    /// Alpha Effect | Clip 1
    {
        float startValue = 1;
        float endValue = 0;
        float startTime = 0.33;
        float endTime = 0.66;

        openshot::Point point0(timeToFrame(clip1Start),                                startValue, openshot::LINEAR);
        openshot::Point point1(timeToFrame(clip1End - (1 - startTime) * transitionDuration), startValue, openshot::LINEAR);
        openshot::Point point2(timeToFrame(clip1End - (1- endTime) * transitionDuration),   endValue, openshot::LINEAR);

        clip1.alpha = openshot::Keyframe({point0, point1, point2});
    }

    /// Zoom Effect | Clip 1
    {
        // Zoom amount keyframe
        float zoomStartValue = 100;
        float zoomMidValue = 120;
        float zoomEndValue = 100;
        float zoomStarTime = 0;
        float zoomMidTime = 0.5;
        float zoomEndTime = 1;
        auto zoomStartHandle = std::make_pair(0.71, 0.00);
        auto zoomMidHandleLeft = std::make_pair(0.83, 0.83);
        auto zoomMidHandleRight = std::make_pair(0.17, 0.17);
        auto zoomEndHandle = std::make_pair(0.29, 1);

        auto zoomPoint0 = openshot::Point(timeToFrame(clip1Start),                                     zoomStartValue);
        auto zoomPointStart = openshot::Point(timeToFrame(clip1End - (1 - zoomStarTime) * transitionDuration), zoomStartValue);
        auto zoomPointMid = openshot::Point(timeToFrame(clip1End - (1 - zoomMidTime) * transitionDuration), zoomMidValue);
        auto zoomPointEnd = openshot::Point(timeToFrame(clip1End - (1 - zoomEndTime)  * transitionDuration), zoomEndValue);

        zoomPointStart.handle_right = openshot::Coordinate(zoomStartHandle.first, zoomStartHandle.second);
        zoomPointMid.handle_left = openshot::Coordinate(zoomMidHandleLeft.first, zoomMidHandleLeft.second);
        zoomPointMid.handle_right = openshot::Coordinate(zoomMidHandleRight.first, zoomMidHandleRight.second);
        zoomPointEnd.handle_left = openshot::Coordinate(zoomEndHandle.first, zoomEndHandle.second);

        openshot::Keyframe zoomKeyframe({zoomPoint0, zoomPointStart, zoomPointMid, zoomPointEnd});

        // Zoom anchor point keyframe
        float anchorXStartValue = 0.18;
        float anchorXEndValue = 0.82;
        float anchorStarTime = 0;
        float anchorEndTime = 0.5;
        auto anchorStartHandle = std::make_pair(0.71, 0.00);
        auto anchorEndHandle = std::make_pair(0.29, 1.00);

        auto anchorXPoint0 = openshot::Point(timeToFrame(clip1Start),                                     anchorXStartValue);
        auto anchorXPoint1 = openshot::Point(timeToFrame(clip1End - (1 - anchorStarTime) * transitionDuration), anchorXStartValue);
        auto anchorXPoint2 = openshot::Point(timeToFrame(clip1End - (1 - anchorEndTime)  * transitionDuration), anchorXEndValue);

        anchorXPoint1.handle_right = openshot::Coordinate(anchorStartHandle.first, anchorStartHandle.second);
        anchorXPoint2.handle_left = openshot::Coordinate(anchorEndHandle.first, anchorEndHandle.second);

        openshot::Keyframe anchorXKeyframe({anchorXPoint0, anchorXPoint1, anchorXPoint2});

        auto zoom = new openshot::Zoom(zoomKeyframe, anchorXKeyframe, 0);
        clip1.AddEffect(zoom);
    }

    /// Blur Effect | Clip 1
    {
        // Blur amount keyframe
        float zoomBlurStartValue = 0;
        float zoomBlurMidValue = 120;
        float zoomBlurEndValue = 0;
        float zoomBlurStartTime = 0;
        float zoomBlurMidTime = 0.5;
        float zoomBlurEndTime = 1;
        auto zoomBlurStartHandle = std::make_pair(0.71, 0.00);
        auto zoomBlurMidHandleLeft = std::make_pair(0.83, 0.83);
        auto zoomBlurMidHandleRight = std::make_pair(0.17, 0.17);
        auto zoomBlurEndHandle = std::make_pair(0.19, 1.00);

        auto zoomBlurPoint0 = openshot::Point(timeToFrame(clip1Start),                                     zoomBlurStartValue);
        auto zoomBlurPointStart = openshot::Point(timeToFrame(clip1End - (1 - zoomBlurStartTime) * transitionDuration), zoomBlurStartValue);
        auto zoomBlurPointMid = openshot::Point(timeToFrame(clip1End - (1 - zoomBlurMidTime) * transitionDuration), zoomBlurMidValue);
        auto zoomBlurPointEnd = openshot::Point(timeToFrame(clip1End - (1 - zoomBlurEndTime)  * transitionDuration), zoomBlurEndValue);

        zoomBlurPointStart.handle_right = openshot::Coordinate(zoomBlurStartHandle.first, zoomBlurStartHandle.second);
        zoomBlurPointMid.handle_left = openshot::Coordinate(zoomBlurMidHandleLeft.first, zoomBlurMidHandleLeft.second);
        zoomBlurPointMid.handle_right = openshot::Coordinate(zoomBlurMidHandleRight.first, zoomBlurMidHandleRight.second);
        zoomBlurPointEnd.handle_left = openshot::Coordinate(zoomBlurEndHandle.first, zoomBlurEndHandle.second);

        openshot::Keyframe zoomBlurKeyframe({zoomBlurPoint0, zoomBlurPointStart, zoomBlurPointMid, zoomBlurPointEnd});

        // Zoom anchor point keyframe
        float anchorXStartValue = 0.3;
        float anchorXEndValue = 0.6;
        float anchorStarTime = 0;
        float anchorEndTime = 1;
        auto anchorStartHandle = std::make_pair(0.71, 0.00);
        auto anchorEndHandle = std::make_pair(0.29, 1.00);

        auto anchorXPoint0 = openshot::Point(timeToFrame(clip1Start),                                     anchorXStartValue);
        auto anchorXPoint1 = openshot::Point(timeToFrame(clip1End - (1 - anchorStarTime) * transitionDuration), anchorXStartValue);
        auto anchorXPoint2 = openshot::Point(timeToFrame(clip1End - (1 - anchorEndTime)  * transitionDuration), anchorXEndValue);

        anchorXPoint1.handle_right = openshot::Coordinate(anchorStartHandle.first, anchorStartHandle.second);
        anchorXPoint2.handle_left = openshot::Coordinate(anchorEndHandle.first, anchorEndHandle.second);

        openshot::Keyframe anchorXKeyframe({anchorXPoint0, anchorXPoint1, anchorXPoint2});

        auto blur = new openshot::Blur(0, 0, 0, 0, zoomBlurKeyframe, anchorXKeyframe, anchorXKeyframe);
        clip1.AddEffect(blur);
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////// Clip 2
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    openshot::Clip clip2(file2);
    clip2.Layer(1);
    clip2.Position(clip2Position);
    clip2.Start(clip2Start);
    clip2.End(clip2End);
    clip2.scale = openshot::SCALE_FIT;

    /// Zoom Effect | Clip 2
    {
        // Zoom amount keyframe
        float zoomStartValue = 100;
        float zoomMidValue = 120;
        float zoomEndValue = 100;
        float zoomStarTime = 0;
        float zoomMidTime = 0.5;
        float zoomEndTime = 1;
        auto zoomStartHandle = std::make_pair(0.71, 0.00);
        auto zoomMidHandleLeft = std::make_pair(0.83, 0.83);
        auto zoomMidHandleRight = std::make_pair(0.17, 0.17);
        auto zoomEndHandle = std::make_pair(0.29, 1);

        auto zoomPoint0 = openshot::Point(timeToFrame(clip2Start),                                     zoomStartValue);
        auto zoomPointStart = openshot::Point(timeToFrame(clip2Start + zoomStarTime * transitionDuration), zoomStartValue);
        auto zoomPointMid = openshot::Point(timeToFrame(clip2Start + zoomMidTime * transitionDuration), zoomMidValue);
        auto zoomPointEnd = openshot::Point(timeToFrame(clip2Start  + zoomEndTime * transitionDuration), zoomEndValue);

        zoomPointStart.handle_right = openshot::Coordinate(zoomStartHandle.first, zoomStartHandle.second);
        zoomPointMid.handle_left = openshot::Coordinate(zoomMidHandleLeft.first, zoomMidHandleLeft.second);
        zoomPointMid.handle_right = openshot::Coordinate(zoomMidHandleRight.first, zoomMidHandleRight.second);
        zoomPointEnd.handle_left = openshot::Coordinate(zoomEndHandle.first, zoomEndHandle.second);

        openshot::Keyframe zoomKeyframe({zoomPoint0, zoomPointStart, zoomPointMid, zoomPointEnd});

        // Zoom anchor point keyframe
        float anchorXStartValue = 0.18;
        float anchorXEndValue = 0.82;
        float anchorStarTime = 0;
        float anchorEndTime = 0.5;
        auto anchorStartHandle = std::make_pair(0.71, 0.00);
        auto anchorEndHandle = std::make_pair(0.29, 1.00);

        auto anchorXPoint0 = openshot::Point(timeToFrame(clip2Start),                                     anchorXStartValue);
        auto anchorXPoint1 = openshot::Point(timeToFrame(clip2Start + anchorStarTime * transitionDuration), anchorXStartValue);
        auto anchorXPoint2 = openshot::Point(timeToFrame(clip2Start + anchorEndTime * transitionDuration), anchorXEndValue);

        anchorXPoint1.handle_right = openshot::Coordinate(anchorStartHandle.first, anchorStartHandle.second);
        anchorXPoint2.handle_left = openshot::Coordinate(anchorEndHandle.first, anchorEndHandle.second);

        openshot::Keyframe anchorXKeyframe({anchorXPoint0, anchorXPoint1, anchorXPoint2});

        auto zoom = new openshot::Zoom(zoomKeyframe, anchorXKeyframe, 0);
        clip2.AddEffect(zoom);
    }

    /// Blur Effect | Clip 2
    {
        // Blur amount keyframe
        float zoomBlurStartValue = 0;
        float zoomBlurMidValue = 120;
        float zoomBlurEndValue = 0;
        float zoomBlurStartTime = 0;
        float zoomBlurMidTime = 0.5;
        float zoomBlurEndTime = 1;
        auto zoomBlurStartHandle = std::make_pair(0.71, 0.00);
        auto zoomBlurMidHandleLeft = std::make_pair(0.83, 0.83);
        auto zoomBlurMidHandleRight = std::make_pair(0.17, 0.17);
        auto zoomBlurEndHandle = std::make_pair(0.19, 1.00);

        auto zoomBlurPoint0 = openshot::Point(timeToFrame(clip2Start),                                     zoomBlurStartValue);
        auto zoomBlurPointStart = openshot::Point(timeToFrame(clip2Start + zoomBlurStartTime * transitionDuration), zoomBlurStartValue);
        auto zoomBlurPointMid = openshot::Point(timeToFrame(clip2Start + zoomBlurMidTime * transitionDuration), zoomBlurMidValue);
        auto zoomBlurPointEnd = openshot::Point(timeToFrame(clip2Start + zoomBlurEndTime * transitionDuration), zoomBlurEndValue);

        zoomBlurPointStart.handle_right = openshot::Coordinate(zoomBlurStartHandle.first, zoomBlurStartHandle.second);
        zoomBlurPointMid.handle_left = openshot::Coordinate(zoomBlurMidHandleLeft.first, zoomBlurMidHandleLeft.second);
        zoomBlurPointMid.handle_right = openshot::Coordinate(zoomBlurMidHandleRight.first, zoomBlurMidHandleRight.second);
        zoomBlurPointEnd.handle_left = openshot::Coordinate(zoomBlurEndHandle.first, zoomBlurEndHandle.second);

        openshot::Keyframe zoomBlurKeyframe({zoomBlurPoint0, zoomBlurPointStart, zoomBlurPointMid, zoomBlurPointEnd});

        // Zoom anchor point keyframe
        float anchorXStartValue = 0.3;
        float anchorXEndValue = 0.6;
        float anchorStarTime = 0;
        float anchorEndTime = 1;
        auto anchorStartHandle = std::make_pair(0.71, 0.00);
        auto anchorEndHandle = std::make_pair(0.29, 1.00);

        auto anchorXPoint0 = openshot::Point(timeToFrame(clip2Start),                                     anchorXStartValue);
        auto anchorXPoint1 = openshot::Point(timeToFrame(clip2Start + anchorStarTime * transitionDuration), anchorXStartValue);
        auto anchorXPoint2 = openshot::Point(timeToFrame(clip2Start + anchorEndTime * transitionDuration), anchorXEndValue);

        anchorXPoint1.handle_right = openshot::Coordinate(anchorStartHandle.first, anchorStartHandle.second);
        anchorXPoint2.handle_left = openshot::Coordinate(anchorEndHandle.first, anchorEndHandle.second);

        openshot::Keyframe anchorXKeyframe({anchorXPoint0, anchorXPoint1, anchorXPoint2});

        auto blur = new openshot::Blur(0, 0, 0, 0, zoomBlurKeyframe, anchorXKeyframe, anchorXKeyframe);
        clip2.AddEffect(blur);
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

void dissolveTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {
    float clip1Start = 0;
    float clip1End = 2;
    float clip1Position = 0;
    float clip2Start = 0;
    float clip2End = 2;
    float clip2Position = clip1End - transitionDuration;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////// Clip 1
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    openshot::Clip clip1(file1);
    clip1.Position(clip1Position);
    clip1.Start(clip1Start);
    clip1.End(clip1End);
    clip1.scale = openshot::SCALE_FIT;

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
        float startValue = 0;
        float endValue = 1;
        float startTime = 0;
        float endTime = 1;
        auto startHandle = std::make_pair(0.33, 0.00);
        auto endHandle = std::make_pair(0.67, 1.00);

        openshot::Point point0(timeToFrame(clip2Start),                               startValue);
        openshot::Point point1(timeToFrame(clip2Start + startTime * transitionDuration),           startValue);
        openshot::Point point2(timeToFrame(clip2Start + endTime * transitionDuration),endValue);

        point1.handle_right = openshot::Coordinate(startHandle.first, startHandle.second);
        point2.handle_left = openshot::Coordinate(endHandle.first, endHandle.second);

        clip2.alpha = openshot::Keyframe({point0, point1, point2});
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

void dissolveBlurTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {
    float clip1Start = 0;
    float clip1End = 2;
    float clip1Position = 0;
    float clip2Start = 0;
    float clip2End = 2;
    float clip2Position = clip1End - transitionDuration;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////// Clip 1
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    openshot::Clip clip1(file1);
    clip1.Layer(1);
    clip1.Position(clip1Position);
    clip1.Start(clip1Start);
    clip1.End(clip1End);
    clip1.scale = openshot::SCALE_FIT;

    /// Blur Effect | Clip 1
    {
        float startValue = 0;
        float endValue = 20;
        float startTime = 0;
        float endTime = 1;
        auto startHandle = std::make_pair(0.33, 0.00);
        auto endHandle = std::make_pair(0.67, 1.00);

        auto point0 = openshot::Point(timeToFrame(clip1Start),                                      startValue);
        auto point1 = openshot::Point(timeToFrame(clip1End - (1 - startTime) * transitionDuration), startValue);
        auto point2 = openshot::Point(timeToFrame(clip1End - (1 - endTime)   * transitionDuration), endValue);

        point1.handle_right = openshot::Coordinate(startHandle.first, startHandle.second);
        point2.handle_left = openshot::Coordinate(endHandle.first, endHandle.second);

        openshot::Keyframe keyframe({point0, point1, point2});
        auto blur = new openshot::Blur(keyframe, keyframe, 0, 0);
        clip1.AddEffect(blur);
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////// Clip 2
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    openshot::Clip clip2(file2);
    clip2.Layer(2);
    clip2.Position(clip2Position);
    clip2.Start(clip2Start);
    clip2.End(clip2End);
    clip2.scale = openshot::SCALE_FIT;

    /// Alpha Effect | Clip 2
    {
        float startValue = 0;
        float endValue = 1;
        float startTime = 0;
        float endTime = 1;
        auto startHandle = std::make_pair(0.33, 0.00);
        auto endHandle = std::make_pair(0.67, 1.00);

        openshot::Point point0(timeToFrame(clip2Start),                               startValue);
        openshot::Point point1(timeToFrame(clip2Start + startTime * transitionDuration),           startValue);
        openshot::Point point2(timeToFrame(clip2Start + endTime * transitionDuration),endValue);

        point1.handle_right = openshot::Coordinate(startHandle.first, startHandle.second);
        point2.handle_left = openshot::Coordinate(endHandle.first, endHandle.second);

        clip2.alpha = openshot::Keyframe({point0, point1, point2});
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
    float clip1Start = 0;
    float clip1End = 0.5;
    float clip1Position = 0;
    float clip2Start = 0;
    float clip2End = 0.5;
    float clip2Position = clip1End - transitionDuration;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////// Clip 1
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    openshot::Clip clip1(file1);
    clip1.Layer(1);
    clip1.Position(clip1Position);
    clip1.Start(clip1Start);
    clip1.End(clip1End);
    clip1.scale = openshot::SCALE_FIT;

    /// Vertical Split Shift Effect | Clip 1
    {
        float startValue = 0;
        float endValue = clip1.Reader()->info.height;
        float starTime = 0;
        float endTime = 1;
        auto startHandle = std::make_pair(0.86, 0.00);
        auto endHandle = std::make_pair(0.14, 1.00);

        auto point0 = openshot::Point(timeToFrame(clip1Start),                                     startValue);
        auto point1 = openshot::Point(timeToFrame(clip1End - (1 - starTime) * transitionDuration), startValue);
        auto point2 = openshot::Point(timeToFrame(clip1End - (1 - endTime)  * transitionDuration), endValue);

        point0.handle_right = openshot::Coordinate(startHandle.first, startHandle.second);
        point1.handle_right = openshot::Coordinate(startHandle.first, startHandle.second);
        point2.handle_left = openshot::Coordinate(endHandle.first, endHandle.second);

        openshot::Keyframe keyframe({point0, point1, point2});

        auto verticalSplitShiftEffect = new openshot::VerticalSplitShift(keyframe);
        clip1.AddEffect(verticalSplitShiftEffect);
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////// Clip 2
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    openshot::Clip clip2(file2);
    clip2.Layer(2);
    clip2.Position(clip2Position);
    clip2.Start(clip2Start);
    clip2.End(clip2End);
    clip2.scale = openshot::SCALE_FIT;

    /// Vertical Split Shift Effect | Clip 2
    {
        float startValue = -clip2.Reader()->info.height;
        float endValue = 0;
        float startTime = 0;
        float endTime = 1;
        auto startHandle = std::make_pair(0.86, 0.00);
        auto endHandle = std::make_pair(0.14, 1.00);

        openshot::Point point0(timeToFrame(clip2Start),                                 startValue);
        openshot::Point point1(timeToFrame(clip2Start + startTime * transitionDuration),startValue);
        openshot::Point point2(timeToFrame(clip2Start + endTime * transitionDuration),  endValue);

        point1.handle_right = openshot::Coordinate(startHandle.first, startHandle.second);
        point2.handle_left = openshot::Coordinate(endHandle.first, endHandle.second);

        openshot::Keyframe keyframe({point0, point1, point2});

        auto verticalSplitShiftEffect = new openshot::VerticalSplitShift(keyframe, true);
        clip2.AddEffect(verticalSplitShiftEffect);
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

void zoomInTransition(const std::string& file1, const std::string& file2, float transitionDuration, const std::string& output) {
    float clip1Start = 0;
    float clip1End = 2;
    float clip1Position = 0;
    float clip2Start = 0;
    float clip2End = 2;
    float clip2Position = clip1End - transitionDuration;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////// Clip 1
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    openshot::Clip clip1(file1);
    clip1.Layer(1);
    clip1.Position(clip1Position);
    clip1.Start(clip1Start);
    clip1.End(clip1End);
    clip1.scale = openshot::SCALE_FIT;

    /// Zoom Effect | Clip 1
    {
        float startValue = 100;
        float endValue = 250;
        float starTime = 0;
        float endTime = 1;
        auto startHandle = std::make_pair(0.58, 0.00);
        auto endHandle = std::make_pair(0.13, 1.00);

        auto point0 = openshot::Point(timeToFrame(clip1Start),                                     startValue);
        auto point1 = openshot::Point(timeToFrame(clip1End - (1 - starTime) * transitionDuration), startValue);
        auto point2 = openshot::Point(timeToFrame(clip1End - (1 - endTime)  * transitionDuration), endValue);

        point1.handle_right = openshot::Coordinate(startHandle.first, startHandle.second);
        point2.handle_left = openshot::Coordinate(endHandle.first, endHandle.second);

        openshot::Keyframe keyframe({point0, point1, point2});

        auto zoom = new openshot::Zoom(keyframe);
        clip1.AddEffect(zoom);
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////// Clip 2
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    openshot::Clip clip2(file2);
    clip2.Layer(2);
    clip2.Position(clip2Position);
    clip2.Start(clip2Start);
    clip2.End(clip2End);
    clip2.scale = openshot::SCALE_FIT;

    /// Alpha Effect | Clip 2
    {
        float startValue = 0;
        float endValue = 1;
        float startTime = 0.33;
        float endTime = 0.66;
        auto startHandle = std::make_pair(0.33, 0.00);
        auto endHandle = std::make_pair(0.67, 1.00);

        openshot::Point point0(timeToFrame(clip2Start),                    startValue);
        openshot::Point point1(timeToFrame(clip2Start + startTime * transitionDuration), startValue);
        openshot::Point point2(timeToFrame(clip2Start + endTime * transitionDuration),     endValue);

        point1.handle_right = openshot::Coordinate(startHandle.first, startHandle.second);
        point2.handle_left = openshot::Coordinate(endHandle.first, endHandle.second);

        clip2.alpha = openshot::Keyframe({point0, point1, point2});
    }

    /// Zoom Effect | Clip 2
    {
        float startValue = 40;
        float endValue = 100;
        float startTime = 0;
        float endTime = 1;
        auto startHandle = std::make_pair(0.58, 0.00);
        auto endHandle = std::make_pair(0.13, 1.00);

        auto point0 = openshot::Point(timeToFrame(clip2Start),                                  startValue);
        auto point1 = openshot::Point(timeToFrame(clip2Start + startTime * transitionDuration), startValue);
        auto point2 = openshot::Point(timeToFrame(clip2Start + endTime * transitionDuration),   endValue);

        point1.handle_right = openshot::Coordinate(startHandle.first, startHandle.second);
        point2.handle_left = openshot::Coordinate(endHandle.first, endHandle.second);

        openshot::Keyframe keyframe({point0, point1, point2});
        auto zoom = new openshot::Zoom(keyframe);
        clip2.AddEffect(zoom);
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