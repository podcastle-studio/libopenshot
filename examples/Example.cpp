#include <chrono>
#include <iostream>

#include "Timeline.h"
#include "Clip.h"
#include "KeyFrame.h"
#include "FFmpegWriter.h"

int main() {
    const auto start = std::chrono::steady_clock::now();

    constexpr int renderWidth = 1920;
    constexpr int renderHeight = 1080;
    constexpr int fps = 30;
    constexpr double durationSeconds = 2.0;

    openshot::Timeline timeLine(renderWidth, renderHeight, openshot::Fraction(fps, 1), 48000, 2, openshot::ChannelLayout::LAYOUT_STEREO);
    timeLine.Open();

    // --- Blur sample: an image clip (./in.jpeg) blurred for 2 seconds ---
    {
        // Clip(path) auto-detects the image reader for ./in.jpeg
        const auto clip = new openshot::Clip("in.jpeg");
        clip->Position(0.0);          // start at the beginning of the timeline
        clip->Start(0.0);             // first frame of the source
        clip->End(durationSeconds);   // show for 2 seconds
        clip->Layer(1);

        // Enable the clip blur. blur_amount is the box-blur kernel size in pixels
        // (same units as boxShadow.blur). A constant 25 here; swap the Keyframe below
        // for an animated ramp, e.g. Keyframe with points (1, 0) -> (60, 50).
        clip->Blur(true);
        clip->blur_amount = openshot::Keyframe(25.0);

        timeLine.AddClip(clip);
    }

    openshot::FFmpegWriter w("out.mp4");
    w.SetAudioOptions(true, "aac", 48000, 2, openshot::ChannelLayout::LAYOUT_STEREO, 128000);
    w.SetVideoOptions(true, "libx264", openshot::Fraction(fps, 1), renderWidth, renderHeight, openshot::Fraction(1, 1), false, false, 4000000);

    w.PrepareStreams();
    w.SetOption(openshot::VIDEO_STREAM, "crf", "18");
    w.SetOption(openshot::VIDEO_STREAM, "preset", "fast");
    w.SetOption(openshot::VIDEO_STREAM, "x264-params", "colorprim=bt709:transfer=bt709:colormatrix=bt709");
    w.Open();

    // Render 2 seconds worth of frames
    w.WriteFrame(&timeLine, 1, static_cast<int>(durationSeconds * fps));
    timeLine.Close();
    w.Close();

    const auto end = std::chrono::steady_clock::now();
    std::cout << std::chrono::duration<double, std::milli>(end - start).count() / 1000.f << " s" << std::endl;

    return 0;
}
