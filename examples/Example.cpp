
#include "Timeline.h"
#include "Clip.h"
#include "FFmpegReader.h"
#include "FFmpegWriter.h"
#include "effects/ColorAdjustment.h"
#include "effects/LightAdjustment.h"
#include "effects/Enhancement.h"

int main() {

    openshot::Timeline timeLine(1920, 1080, openshot::Fraction(30, 1), 48000, 2, openshot::ChannelLayout::LAYOUT_STEREO);
    timeLine.Open();

    openshot::Clip img("img.png");
    // img.AddEffect(new openshot::ColorAdjustment(0, 0, 0, 0));
    // img.AddEffect(new openshot::LightAdjustment(0, 0, 1, 0, 0, 0));
    img.AddEffect(new openshot::Enhancement(1, 1, 1));
    timeLine.AddClip(&img);

    openshot::FFmpegWriter w("out.mp4");
    w.SetAudioOptions(true, "aac", 48000, 2, openshot::ChannelLayout::LAYOUT_STEREO, 128000);
    w.SetVideoOptions(true, "libx264" , openshot::Fraction(30, 1),  1920, 1080, openshot::Fraction(1,1), false, false, 4000000);
    w.Open();


    w.WriteFrame(&timeLine, 1, 5);
    timeLine.Close();
    w.Close();
    return 0;
}
