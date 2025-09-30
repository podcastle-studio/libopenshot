#include "Timeline.h"
#include "Clip.h"
#include "FFmpegReader.h"
#include "FFmpegWriter.h"
#include "effects/ColorAdjustment.h"

int main() {
    const auto start = std::chrono::steady_clock::now();

    // constexpr int renderWidth = 3840;
    constexpr int renderWidth = 1920;
    // constexpr int renderHeight = 2160;
    constexpr int renderHeight = 1080;
    openshot::Timeline timeLine(renderWidth, renderHeight, openshot::Fraction(30, 1), 48000, 2, openshot::ChannelLayout::LAYOUT_STEREO);

    timeLine.Open();


    {
        const auto clip = new openshot::Clip("img");
        timeLine.AddClip(clip);
    }

    {
        const auto reader = new openshot::FFmpegReader("vertical_1080.mp4");
        const auto clip = new openshot::Clip(reader);
        timeLine.AddClip(clip);
    }

    {
        const auto reader = new openshot::FFmpegReader("maxine_demo_1080.mp4");
        const auto clip = new openshot::Clip(reader);
        timeLine.AddClip(clip);
    }

    openshot::FFmpegWriter w("out.mp4");
    w.SetAudioOptions(true, "aac", 48000, 2, openshot::ChannelLayout::LAYOUT_STEREO, 128000);
    w.SetVideoOptions(true, "libx264" , openshot::Fraction(30, 1),  renderWidth, renderHeight, openshot::Fraction(1,1), false, false, 4000000);

    w.PrepareStreams();
    w.SetOption(openshot::VIDEO_STREAM, "crf", "18");
    w.SetOption(openshot::VIDEO_STREAM, "preset", "fast");
    w.SetOption(openshot::VIDEO_STREAM, "x264-params", "colorprim=bt709:transfer=bt709:colormatrix=bt709");
    w.Open();

    w.WriteFrame(&timeLine, 1, 10*30);
    timeLine.Close();
    w.Close();

    const auto end = std::chrono::steady_clock::now();

    std::cout << std::chrono::duration<double, std::milli>(end - start).count()/1000.f << " s" << std::endl;

    return 0;
}