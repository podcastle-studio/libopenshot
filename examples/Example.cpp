
#include "Timeline.h"
#include "Clip.h"
#include "FFmpegReader.h"
#include "FFmpegWriter.h"
#include "effects/ColorAdjustment.h"

int main() {

    openshot::Timeline timeLine(1920, 1080, openshot::Fraction(30, 1), 48000, 2, openshot::ChannelLayout::LAYOUT_STEREO);

    timeLine.Open();


    const auto clip = new openshot::Clip("img.jpeg");
    timeLine.AddClip(clip);

    // Load from JSON file (alternative to programmatic creation)
    timeLine.LoadSubtitlesFromJsonFile("./subtitles.json");

    openshot::FFmpegWriter w("out.mp4");
    w.SetAudioOptions(true, "aac", 48000, 2, openshot::ChannelLayout::LAYOUT_STEREO, 128000);
    w.SetVideoOptions(true, "libx264" , openshot::Fraction(30, 1),  1280, 720, openshot::Fraction(1,1), false, false, 4000000);

    w.PrepareStreams();
    w.SetOption(openshot::VIDEO_STREAM, "crf", "18");
    w.SetOption(openshot::VIDEO_STREAM, "preset", "medium");
    w.SetOption(openshot::VIDEO_STREAM, "x264-params", "colorprim=bt709:transfer=bt709:colormatrix=bt709");
    w.Open();

    w.WriteFrame(&timeLine, 1, 15*30);
    timeLine.Close();
    w.Close();
    return 0;
}
