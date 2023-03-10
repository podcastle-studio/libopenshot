#include <string>
#include <Clip.h>
#include <effects/Crop.h>
#include <effects/Mask.h>
#include <OpenShot.h>
#include <FFmpegWriter.h>
#include <FFmpegReader.h>
#include <Timeline.h>
#include <cmath>
#include <Json/Json.h>
using namespace openshot;
namespace
{
    const int kNumChannels = 2;
    const int kSampleRate = 44100;
}

nlohmann::json payload = nlohmann::json::parse("{\n"
                                               "   \"version\":1,\n"
                                               "   \"height\":720.0,\n"
                                               "   \"width\":1280.0,\n"
                                               "   \"duration\":11000,\n"
                                               "   \"fps\":30,\n"
                                               "   \"audioBitRate\":320000,\n"
                                               "   \"videoBitRate\":4000000,\n"
                                               "   \"background\":\"videos/background.png\",\n"
                                               "   \"watermark\":\"videos/watermark.png\",\n"
                                               "   \"requestId\":\"fe809eac-0a52-431b-97d2-3ca2a05fcc25\",\n"
                                               "   \"interviewId\":\"97011077-a4d4-4350-9ce1-8416ce9080cb\",\n"
                                               "   \"recordingId\":\"1cd28de5-f521-40dc-b0d7-a60b4fe9cf34\",\n"
                                               "   \"includeWatermark\":false,\n"
                                               "   \"clips\":[\n"
                                               "      {\n"
                                               "         \"recordingId\":\"1cd28de5-f521-40dc-b0d7-a60b4fe9cf34\",\n"
                                               "         \"file\":\"aac811d4-6135-453e-8d3d-c4604c21ac07\",\n"
                                               "         \"name\":\"videos/clip.mp4\",\n"
                                               "         \"type\":\"PARTICIPANT\",\n"
                                               "         \"start\":0,\n"
                                               "         \"end\":11000,\n"
                                               "         \"frames\":[\n"
                                               "            {\n"
                                               "               \"start\":0,\n"
                                               "               \"position\":{\n"
                                               "                  \"top\":4.472396925200002,\n"
                                               "                  \"left\":4.472396925200009,\n"
                                               "                  \"height\":91.0552061496,\n"
                                               "                  \"width\":91.05520614959998,\n"
                                               "                  \"zindex\":2\n"
                                               "               }\n"
                                               "            }\n"
                                               "         ]\n"
                                               "      }\n"
                                               "   ],\n"
                                               "   \"start\":0\n"
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

void initClip(openshot::Clip* clipPtr, int layer, float position, float start, float end)
{
    clipPtr->Position(position);
    clipPtr->Start(start);
    clipPtr->End(end);
    clipPtr->Layer(layer);
    clipPtr->gravity = openshot::GRAVITY_TOP_LEFT;
    clipPtr->scale = openshot::SCALE_FIT;
    clipPtr->scale_x = openshot::Keyframe();
    clipPtr->scale_y = openshot::Keyframe();

    clipPtr->location_x = openshot::Keyframe();
    clipPtr->location_y = openshot::Keyframe();

    clipPtr->alpha = openshot::Keyframe();
    clipPtr->alpha.AddPoint(1, 0);
    clipPtr->alpha.AddPoint(8, 0.99);
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

    const int frameWidth = clipMetaInfo["position"]["width"];
    const int frameHeight = clipMetaInfo["position"]["height"];
    const int left = clipMetaInfo["position"]["left"];
    const int top = clipMetaInfo["position"]["top"];
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

int main_advanced()
{
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

    const auto& payloadClips = payload["clips"];
    for (const auto& payloadClip : payloadClips) {
        /// Downloading clip
        const std::string videoFilePath = payloadClip["name"];
        auto *clipReader = new FFmpegReader(videoFilePath);
        auto *clip = new openshot::Clip(clipReader);

        /// Initializing clip with default values
        if ((float) payloadClip["start"] < 0) {
            initClip(clip, 1, 0, -(float) payloadClip["start"], (float) payloadClip["end"]);
        } else {
            initClip(clip, 1, (float) payloadClip["start"], 0, (float) payloadClip["end"]);
        }

        /// Applying clips param from payload
        if (payloadClip["type"] == "PRESENTATION") {
            const auto &keyFramesParams = payloadClip["frames"];
            for (const auto &keyFrameParams: keyFramesParams) {
                // applyPresentationClipParams(clip, keyFrameParams, videoWidth, videoHeight, videoFps);
            }
        } else {
            openshot::Keyframe cropLeftKeyframe;
            openshot::Keyframe cropRightKeyframe;
            openshot::Keyframe cropTopKeyframe;
            openshot::Keyframe cropBottomKeyframe;

            const auto &keyFramesParams = payloadClip["frames"];
            for (const auto &keyFrameParams: keyFramesParams) {
                applyClipParams(*clip, keyFrameParams,
                                cropLeftKeyframe, cropRightKeyframe, cropTopKeyframe, cropBottomKeyframe,
                                videoWidth, videoHeight, videoFps);
            }

            // Creating crop effect for the clip
            auto *cropEffect = new openshot::Crop(cropLeftKeyframe, cropTopKeyframe, cropRightKeyframe,
                                                  cropBottomKeyframe);
            clip->AddEffect(cropEffect);
        }
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

#include "../src/effects/Caption.h"
#include "OpenShot.h"
#include <QFontDatabase>
#include "boost/shared_ptr.hpp"
int main()
{

//    QFont f("Ubuntu", 12);
//    std::cout << f.family().toStdString() << std::endl;
//    QFontMetricsF metrics = QFontMetricsF(f);
//    metrics.
//    double metrics_line_spacing = metrics.lineSpacing();

    auto *clipReader = new FFmpegReader("./v.mp4");
    auto *clip = new openshot::Clip(clipReader);
    openshot::Timeline timeLine(clipReader->info.width, clipReader->info.height, openshot::Fraction(30,1), kSampleRate, kNumChannels, openshot::ChannelLayout::LAYOUT_STEREO);
    timeLine.Open();

    timeLine.AddClip(clip);

    auto caption = new Caption();
    caption->SetPosition(100, 100);
    caption->SetPadding(10);
    caption->SetColor(255, 255, 255);
    caption->SetOutlineColor(0, 0, 0);
    caption->SetOutlineSize(2);
    clip->AddCaption(caption);

    clip->AddEffect(captions);

    /// Create a writer
    openshot::FFmpegWriter w("./output.mp4");
    w.SetCacheSize(30);
    w.SetAudioOptions(true, "libvorbis", kSampleRate, kNumChannels, openshot::ChannelLayout::LAYOUT_STEREO, 128000);
    w.SetVideoOptions(true, "libx264" , openshot::Fraction(30, 1), clipReader->info.width, clipReader->info.height, openshot::Fraction(1,1), false, false, 1600000);



    w.Open();
    w.WriteFrame(&timeLine, 0, 2 * 30);


    // Close the reader & writer
    timeLine.Close();
    w.Close();
}