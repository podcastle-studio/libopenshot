#include "Timeline.h"
#include "Clip.h"
#include "FFmpegReader.h"
#include "FFmpegWriter.h"
#include "effects/ColorAdjustment.h"

#include "subtitle/SubtitleTypes.h"

int main() {

    openshot::Timeline timeLine(1920, 1080, openshot::Fraction(30, 1), 48000, 2, openshot::ChannelLayout::LAYOUT_STEREO);
    timeLine.Open();

    auto clip = new openshot::Clip("img.jpeg");
    timeLine.AddClip(clip);

    // ===========================================================================
    // SUBTITLE SETUP - Add this section
    // ===========================================================================
    // Get subtitle manager
    auto* subtitleManager = timeLine.GetSubtitleManager();
    if (subtitleManager) {
        // Enable subtitles
        timeLine.EnableSubtitles(true);

        // // Option 1: Configure style and add subtitles programmatically
        // auto& style = subtitleManager->getDefaultStyle();
        // style.fontFamily = "Arial";
        // style.fontSize = 64;
        // style.color = "#FFFFFF";
        // style.strokeWidth = 3;
        // style.strokeColor = "#000000";
        // style.strokeOpacity = 1.0f;
        //
        // // Add shadow for better readability
        // style.shadowColor = "#000000";
        // style.shadowOpacity = 0.8f;
        // style.shadowBlur = 4;
        // style.shadowDistance = 2;
        // style.shadowAngle = 45;
        //
        // // Optional: Add background
        // style.backgroundColor = "#000000";
        // style.backgroundOpacity = 0.5f;
        // style.backgroundPaddingX = 12;
        // style.backgroundPaddingY = 8;
        // style.backgroundRadius = 8;
        // style.bubble = false;  // Set to true for speech bubble style
        //
        // // Create a subtitle segment that displays for 1 second (30 frames)
        // openshot::subtitle::SubtitleSegment segment;
        // segment.id = "test-subtitle";
        // segment.visible = true;
        // segment.attached = true;
        // segment.startTimeMs = 0;     // Start at beginning
        // segment.endTimeMs = 1000;    // End at 1 second (30 frames at 30fps)
        //
        // // Add some words
        // segment.wordDetails = {
        //     {"HELLO", 0, 300, 1.0f},
        //     {"OPENSHOT", 300, 700, 1.0f},
        //     {"WORLD", 700, 1000, 1.0f}
        // };
        //
        // subtitleManager->addSegment(segment);

        // Option 2: Load from JSON file (alternative to programmatic creation)
        timeLine.LoadSubtitles("./subtitles.json");

        // Option 3: Use example subtitles for testing
        // subtitleManager->createExampleSubtitles();
    }
    // ===========================================================================

    openshot::FFmpegWriter w("out.mp4");
    w.SetAudioOptions(true, "aac", 48000, 2, openshot::ChannelLayout::LAYOUT_STEREO, 128000);
    w.SetVideoOptions(true, "libx264" , openshot::Fraction(30, 1),  1920, 1080, openshot::Fraction(1,1), false, false, 4000000);
    w.Open();

    w.WriteFrame(&timeLine, 1, 30);  // This will now include subtitles!
    timeLine.Close();
    w.Close();
    return 0;
}