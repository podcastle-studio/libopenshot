/**
 * @file
 * @brief Source file for Example Executable (example app for libopenshot)
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * @ref License
 */

// Copyright (c) 2008-2019 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <fstream>
#include <iostream>
#include <memory>
#include "Clip.h"
#include "Frame.h"
#include "FFmpegReader.h"
#include "Timeline.h"

using namespace openshot;


int main(int argc, char* argv[]) {

    // Types for storing time durations in whole and fractional milliseconds
    using ms = std::chrono::milliseconds;
    using s = std::chrono::seconds;
    using double_ms = std::chrono::duration<double, ms::period>;

    // FFmpeg Reader performance test
    const auto total_1 = std::chrono::high_resolution_clock::now();
    FFmpegReader r9("videos/1.mp4");
    r9.Open();
    for (long int frame = 1; frame <= 1000; frame++)
    {
        const auto time1 = std::chrono::high_resolution_clock::now();
        std::shared_ptr<Frame> f = r9.GetFrame(frame);
        const auto time2 = std::chrono::high_resolution_clock::now();
        std::cout << "FFmpegReader: " << frame
                  << " (" << double_ms(time2 - time1).count() << " ms)\n";
    }
    const auto total_2 = std::chrono::high_resolution_clock::now();
    auto total_sec = std::chrono::duration_cast<ms>(total_2 - total_1);
    std::cout << "FFmpegReader TOTAL: " << total_sec.count() << " ms\n";
    r9.Close();


    // Timeline Reader performance test
    Timeline tm(r9.info);
    Clip *c = new Clip(&r9);
    tm.AddClip(c);
    tm.Open();

    const auto total_3 = std::chrono::high_resolution_clock::now();
    for (long int frame = 1; frame <= 1000; frame++)
    {
        const auto time1 = std::chrono::high_resolution_clock::now();
        std::shared_ptr<Frame> f = tm.GetFrame(frame);
        const auto time2 = std::chrono::high_resolution_clock::now();
        std::cout << "Timeline: " << frame
                  << " (" << double_ms(time2 - time1).count() << " ms)\n";
    }
    const auto total_4 = std::chrono::high_resolution_clock::now();
    total_sec = std::chrono::duration_cast<ms>(total_4 - total_3);
    std::cout << "Timeline TOTAL: " << total_sec.count() << " ms\n";
    tm.Close();

    std::cout << "Completed successfully!\n";

    while(true);
    return 0;
}


/*
#include <string>
#include <Json.h>
#include <Clip.h>
#include <Frame.h>
#include <effects/Crop.h>
#include <effects/Mask.h>
#include <OpenShot.h>
#include <FFmpegWriter.h>
#include <Timeline.h>
#include <cmath>
#include <Json/Json.h>

#include "CacheMemory.h"
namespace
{
    const int kNumChannels = 2;
    const int kSampleRate = 44100;
}

nlohmann::json payload = nlohmann::json::parse("{\n"
                                         "   \"duration\": 100,\n"
                                         "   \"hardwareDecoder\": 2,\n"
                                         "   \"encoder\": \"h264_nvenc\",\n"
                                         "   \"width\":1280,\n"
                                         "   \"height\":1024,\n"
                                         "   \"fps\":30,\n"
                                         "   \"background\":\"videos/background.jpg\",\n"
                                         "   \"clips\":[\n"
                                         "      {\n"
                                         "         \"name\":\"videos/1.mp4\",\n"
                                         "         \"position\":0,\n"
                                         "         \"keyFrames\":[\n"
                                         "            {\n"
                                         "               \"start\":0,\n"
                                         "               \"width\":600,\n"
                                         "               \"height\":462,\n"
                                         "               \"left\":24,\n"
                                         "               \"top\":92\n"
                                         "            }\n"
                                         "         ]\n"
                                         "      },\n"
                                         "      {\n"
                                       "           \"name\":\"videos/2.mp4\",\n"
                                         "         \"position\":0,\n"
                                         "         \"keyFrames\":[\n"
                                         "            {\n"
                                         "               \"start\":0,\n"
                                         "               \"width\":600,\n"
                                         "               \"height\":462,\n"
                                         "               \"left\":656,\n"
                                         "               \"top\":92\n"
                                         "            }\n"
                                         "         ]\n"
                                         "      }\n"
                                         "   ]\n"
                                         "}");

void calculateClipFitFrameSizes(openshot::Clip& clip, int outVideoWidth, int outVideoHeight, float& newWidth, float& newHeight)
{
    float clipWidth = clip.info.width;
    float clipHeight = clip.info.height;

    const float withRatio = outVideoWidth / clipWidth;
    const float heightRatio = outVideoHeight / clipHeight;
    const float bestRatio = std::min(withRatio, heightRatio);

    newWidth = clipWidth * bestRatio;
    newHeight = clipHeight * bestRatio;
}

void initClip(openshot::Clip& clip, int layer, float position)
{
    clip.Layer(layer);
    clip.Position(position - 1);
    clip.Start(0);
    clip.gravity = openshot::GRAVITY_TOP_LEFT;

    clip.scale = openshot::SCALE_FIT;
    clip.scale_y = openshot::Keyframe();
    clip.scale_y = openshot::Keyframe();

    clip.location_x = openshot::Keyframe();
    clip.location_y = openshot::Keyframe();
}

void addPointToKeyframe(openshot::Keyframe& keyframe, const int x, const float y, int animationFramesCount,
                        openshot::InterpolationType interpolationType = openshot::InterpolationType::LINEAR)
{
    const auto pointsCount = keyframe.GetCount();
    if (pointsCount > 0)
    {
        keyframe.AddPoint(x - animationFramesCount, keyframe.GetPoint(pointsCount - 1).co.Y, interpolationType);
    }
    keyframe.AddPoint(x, y, interpolationType);
}

void applyClipParams(openshot::Clip& clip, const nlohmann::json& clipMetaInfo,
                     openshot::Keyframe& cropLeftKeyframe, openshot::Keyframe& cropRightKeyframe,
                     openshot::Keyframe& cropTopKeyframe, openshot::Keyframe& cropBottomKeyframe,
                     const int outVideoWidth, const int outVideoHeight, int fps)
{
    float clipWidth, clipHeight;
    calculateClipFitFrameSizes(clip, outVideoWidth, outVideoHeight, clipWidth, clipHeight);

    bool widthIsFitEdge = (clipWidth < clipHeight);

    const int frameWidth = clipMetaInfo["width"];
    const int frameHeight = clipMetaInfo["height"];
    const int left = clipMetaInfo["left"];
    const int top = clipMetaInfo["top"];
    const int frameCount = (int)clipMetaInfo["start"] == 0 ? 1 : (int)clipMetaInfo["start"] * fps;

    const auto scalePercent = widthIsFitEdge ? ((float)frameWidth / clipWidth): ((float)frameHeight / clipHeight);

    const auto animationFramesCount = int(0.2 * fps);

    addPointToKeyframe(clip.scale_x, frameCount, scalePercent, animationFramesCount);
    addPointToKeyframe(clip.scale_y, frameCount, scalePercent, animationFramesCount);

    const float clipWidthScaled = clipWidth * scalePercent;
    const float clipHeightScaled = clipHeight * scalePercent;
    const float cropInPixelsPerEdge = (widthIsFitEdge
                                       ? abs(clipHeightScaled - frameHeight)
                                       : abs(clipWidthScaled - frameWidth)) / 2;

    const float cropPercentPerEdge = widthIsFitEdge ? cropInPixelsPerEdge / clipHeightScaled : cropInPixelsPerEdge / clipWidthScaled;

    const float cropShiftPercent = widthIsFitEdge ? cropInPixelsPerEdge / outVideoHeight : cropInPixelsPerEdge / outVideoWidth;
    const float locationXPercent = (float)left / outVideoWidth;
    const float locationYPercent = (float)top / outVideoHeight;

    if (widthIsFitEdge)
    {
        /// Handle crop keyframes
        addPointToKeyframe(cropLeftKeyframe, frameCount, 0, animationFramesCount);
        addPointToKeyframe(cropRightKeyframe, frameCount, 0, animationFramesCount);
        addPointToKeyframe(cropTopKeyframe, frameCount, cropPercentPerEdge, animationFramesCount);
        addPointToKeyframe(cropBottomKeyframe, frameCount, cropPercentPerEdge, animationFramesCount);

        /// Handle location keyframes
        addPointToKeyframe(clip.location_x, frameCount, locationXPercent, animationFramesCount);
        addPointToKeyframe(clip.location_y, frameCount, locationYPercent, animationFramesCount);
    }
    else
    {
        /// Handle crop keyframes
        addPointToKeyframe(cropLeftKeyframe, frameCount, cropPercentPerEdge, animationFramesCount);
        addPointToKeyframe(cropRightKeyframe, frameCount, cropPercentPerEdge, animationFramesCount);
        addPointToKeyframe(cropTopKeyframe, frameCount, 0, animationFramesCount);
        addPointToKeyframe(cropBottomKeyframe, frameCount, 0, animationFramesCount);

        /// Handle location keyframes
        addPointToKeyframe(clip.location_x, frameCount, locationXPercent, animationFramesCount);
        addPointToKeyframe(clip.location_y, frameCount, locationYPercent, animationFramesCount);
    }
}

void applyBackgroundParams(openshot::Clip& backgroundClip)
{
    backgroundClip.Layer(0);
    backgroundClip.Start(0);
    backgroundClip.Position(0);
    backgroundClip.gravity = openshot::GRAVITY_CENTER;
    backgroundClip.scale = openshot::SCALE_NONE;
}

int main(int argc, char* argv[])
{

//    openshot::Settings::Instance()->HARDWARE_DECODER = 0;
//    openshot::Settings::Instance()->HW_EN_DEVICE_SET = 0;
//    openshot::Settings::Instance()->HW_EN_DEVICE_SET = 0;
//    openshot::Settings::Instance()->HW_EN_DEVICE_SET = 1;
    const int videoWidth = payload["width"];
    const int videoHeight = payload["height"];
    const int videoFps = payload["fps"];

    /// Creating timeline
    openshot::Timeline timeLine(videoWidth, videoHeight, openshot::Fraction(videoFps,1), kSampleRate, kNumChannels, openshot::ChannelLayout::LAYOUT_STEREO);
    timeLine.Open();
    
    /// Creating background layer
    openshot::Clip backgroundClip(payload["background"]);
    applyBackgroundParams(backgroundClip);
    timeLine.AddClip(&backgroundClip);

    const auto clipsMetaInfo = payload["clips"];
    
    int layer = 1;
    for (const auto& clipMetaInfo : clipsMetaInfo)
    {
        auto* clip = new openshot::Clip(clipMetaInfo["name"]);
        initClip(*clip, layer++, clipMetaInfo["position"]);

        openshot::Keyframe cropLeftKeyframe;
        openshot::Keyframe cropRightKeyframe;
        openshot::Keyframe cropTopKeyframe;
        openshot::Keyframe cropBottomKeyframe;

        const auto& keyframes = clipMetaInfo["keyFrames"];
        for (const auto& keyframe : keyframes)
        {
            applyClipParams(*clip, keyframe, cropLeftKeyframe, cropRightKeyframe, cropTopKeyframe, cropBottomKeyframe, videoWidth, videoHeight, videoFps);
        }

        auto* cropEffect = new openshot::Crop(cropLeftKeyframe, cropTopKeyframe, cropRightKeyframe, cropBottomKeyframe);
        auto* mask = new openshot::Mask(openshot::Mask::MaskType::ROUNDED_CORNERS, 0, 3);
        mask->SetRoundedCornersMaskRadius(8, 8);
        clip->AddEffect(cropEffect);
        clip->AddEffect(mask);
        timeLine.AddClip(clip);
    }

    /// Create a writer
    openshot::FFmpegWriter w("videos/output.mp4");
    w.SetCacheSize(30);
    w.SetAudioOptions(true, "libvorbis", kSampleRate, kNumChannels, openshot::ChannelLayout::LAYOUT_STEREO, 128000);
    w.SetVideoOptions(true, "h264_nvenc" , openshot::Fraction(videoFps, 1),  videoWidth, videoHeight, openshot::Fraction(1,1), false, false, 1600000);

    w.Open();
    w.WriteFrame(&timeLine, 0, 6000 * videoFps);

    // Close the reader & writer
    timeLine.Close();
    w.Close();
    return 0;
}
 */