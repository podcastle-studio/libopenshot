
#include "Timeline.h"
#include "Clip.h"
#include "FFmpegReader.h"
#include "FFmpegWriter.h"
#include "effects/ColorAdjustment.h"

int main() {

    openshot::Timeline timeLine(1920, 1080, openshot::Fraction(30, 1), 48000, 2, openshot::ChannelLayout::LAYOUT_STEREO);
    timeLine.Open();

    auto reader = openshot::FFmpegReader("be2372bf-27e8-4bcd-bfd7-620a68496267");
    openshot::Clip c(&reader);
    timeLine.AddClip(&c);

    openshot::FFmpegWriter w("out.mp4");
    w.SetAudioOptions(true, "aac", 48000, 2, openshot::ChannelLayout::LAYOUT_STEREO, 128000);
    w.SetVideoOptions(true, "libx264" , openshot::Fraction(30, 1),  1920, 1080, openshot::Fraction(1,1), false, false, 4000000);
    w.Open();


    w.WriteFrame(&timeLine, 1, 29 * 30);
    timeLine.Close();
    w.Close();
    return 0;
}
