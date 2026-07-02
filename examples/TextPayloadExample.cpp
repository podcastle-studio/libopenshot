// Standalone render harness for tmp/payload.json: exercises the new libopenshot text features
// (CSS linear-gradient fill + static 3D tilt) end to end. Builds the payload's three TEXT clips
// on a 1280x720 timeline with a solid background and writes a PNG frame (and a short mp4).
//
// Fonts are the payload's woff files, downloaded to FONTS_DIR (override at compile time with
// -DFONTS_DIR="..."). Positioning mirrors video-rendering-service: the reader renders a centred
// frame and the Clip's GRAVITY_CENTER + location curve places it (location = position - 0.5 for
// CENTER-aligned text).

#include <iostream>
#include <string>

#include "Timeline.h"
#include "Clip.h"
#include "Color.h"
#include "Frame.h"
#include "KeyFrame.h"
#include "FFmpegWriter.h"
#include "text/TextClipReader.h"
#include "text/TextClipTypes.h"

#ifndef FONTS_DIR
#define FONTS_DIR "/tmp/claude-1000/-home-seno-Desktop-projects-libopenshot/2cc099d5-1e7f-4060-8363-3a92bd367d5b/scratchpad/fonts/"
#endif

using namespace openshot;

namespace {

// Build a positioned text Clip. Mirrors video-rendering-service addTextClip: the reader is placed
// at canvas centre (position/rotation zeroed) while keeping size + tiltX/tiltY; the Clip carries
// the on-canvas position via its location curve.
Clip* makeTextClip(int canvasW, int canvasH,
                   const std::string& fontPath,
                   const std::string& value,
                   text::TextClipStyle style,
                   text::TextTransformation t,
                   double posX, double posY) {
    style.fontFamily = fontPath;

    text::TextClipData data;
    data.value = value;
    data.style = style;
    data.transformation = t;
    data.transformation.positionX = 0.0;
    data.transformation.positionY = 0.0;
    data.transformation.rotation  = 0.0;   // 2D rotation would move to the Clip; tiltX/Y stay.

    auto* reader = new TextClipReader(canvasW, canvasH, data);
    auto* clip = new Clip(reader);
    clip->Position(0.0);
    clip->Start(0.0);
    clip->End(5.0);
    clip->gravity = GRAVITY_CENTER;
    clip->scale   = SCALE_NONE;
    clip->location_x = Keyframe(posX - 0.5);   // CENTER alignment → no bbox-anchor shift
    clip->location_y = Keyframe(posY - 0.5);
    return clip;
}

// ── Payload 2 (gradient fill + gradient STROKE + shadow + 3D tilt) ──────────
void buildPayload2(Timeline& timeline, int W, int H, const std::string& fontsDir) {
    // Clip A: gold→red gradient fill, 3-stop horizontal gradient stroke, yellow drop shadow,
    // static 3D tilt.
    {
        text::TextClipStyle s;
        s.textAlign = text::TextAlignment::CENTER;
        s.textTransform = text::TextTransform::NONE;
        s.color = "linear-gradient(180deg, #FAC40A 0%, #C8271B 100%)";
        s.strokeColor = "linear-gradient(90deg, #DAF2E1 0%, #98C7C5 50%, #72888B 100%)";
        s.strokeWidthRatio = 0.21;
        s.shadowColor = "#FDE047";
        s.shadowBlurRatio = 0.56;
        s.shadowDistanceRatio = 0.2;
        s.shadowAngle = 40.0;
        s.lineHeight = 0.86;
        s.letterSpacing = -0.03;
        s.fontWeight = 900;

        text::TextTransformation t;
        t.size = 25.72415852566017;
        t.maxWidth = 5.86025;
        t.tiltX = 38;
        t.tiltY = 50;

        timeline.AddClip(makeTextClip(W, H,fontsDir + "poppins-900.woff",
                                      "THIS\nCONVERTS", s, t,
                                      0.46458333333333335, 0.3824074074074074));
    }

    // Clip B: solid grey label.
    {
        text::TextClipStyle s;
        s.textAlign = text::TextAlignment::CENTER;
        s.textTransform = text::TextTransform::UPPERCASE;
        s.color = "#444444";
        s.lineHeight = 1.2;
        s.letterSpacing = 0.42;
        s.fontWeight = 400;

        text::TextTransformation t;
        t.size = 2.41980670942495;
        t.maxWidth = 12.036;

        timeline.AddClip(makeTextClip(W, H,fontsDir + "roboto-400.woff",
                                      "B2B GROWTH", s, t,
                                      0.44531249999999994, 0.8105186616865886));
    }
}

// ── Payload 3 (payload 2 + a text blurRatio on the tilted clip) ─────────────
void buildPayload3(Timeline& timeline, int W, int H, const std::string& fontsDir) {
    {
        text::TextClipStyle s;
        s.textAlign = text::TextAlignment::CENTER;
        s.textTransform = text::TextTransform::NONE;
        s.color = "linear-gradient(180deg, #FAC40A 0%, #C8271B 100%)";
        s.strokeColor = "linear-gradient(90deg, #DAF2E1 0%, #98C7C5 50%, #72888B 100%)";
        s.strokeWidthRatio = 0.21;
        s.shadowColor = "#FDE047";
        s.shadowBlurRatio = 0.56;
        s.shadowDistanceRatio = 0.2;
        s.shadowAngle = 40.0;
        s.blurRatio = 0.06614999999999999;   // NEW: gaussian text blur on the tilted clip
        s.lineHeight = 0.86;
        s.letterSpacing = -0.03;
        s.fontWeight = 900;

        text::TextTransformation t;
        t.size = 25.72415852566017;
        t.maxWidth = 5.86025;
        t.tiltX = 38;
        t.tiltY = 50;

        timeline.AddClip(makeTextClip(W, H,fontsDir + "poppins-900.woff",
                                      "THIS\nCONVERTS", s, t,
                                      0.5010416666666667, 0.262037037037037));
    }
    {
        text::TextClipStyle s;
        s.textAlign = text::TextAlignment::CENTER;
        s.textTransform = text::TextTransform::UPPERCASE;
        s.color = "#444444";
        s.lineHeight = 1.2;
        s.letterSpacing = 0.42;
        s.fontWeight = 400;

        text::TextTransformation t;
        t.size = 2.41980670942495;
        t.maxWidth = 12.036;

        timeline.AddClip(makeTextClip(W, H,fontsDir + "roboto-400.woff",
                                      "B2B GROWTH", s, t,
                                      0.44531249999999994, 0.8105186616865886));
    }
}

// ── Payload 4 (payload 3 flat: gradient fill + stroke + shadow + blur, NO tilt) ─
void buildPayload4(Timeline& timeline, int W, int H, const std::string& fontsDir) {
    {
        text::TextClipStyle s;
        s.textAlign = text::TextAlignment::CENTER;
        s.textTransform = text::TextTransform::NONE;
        s.color = "linear-gradient(180deg, #FAC40A 0%, #C8271B 100%)";
        s.strokeColor = "linear-gradient(90deg, #DAF2E1 0%, #98C7C5 50%, #72888B 100%)";
        s.strokeWidthRatio = 0.21;
        s.shadowColor = "#FDE047";
        s.shadowBlurRatio = 0.56;
        s.shadowDistanceRatio = 0.2;
        s.shadowAngle = 40.0;
        s.blurRatio = 0.06614999999999999;
        s.lineHeight = 0.86;
        s.letterSpacing = -0.03;
        s.fontWeight = 900;

        text::TextTransformation t;
        t.size = 25.72415852566017;
        t.maxWidth = 5.86025;
        t.tiltX = 0;
        t.tiltY = 0;

        timeline.AddClip(makeTextClip(W, H,fontsDir + "poppins-900.woff",
                                      "THIS\nCONVERTS", s, t,
                                      0.5010416666666667, 0.262037037037037));
    }
    {
        text::TextClipStyle s;
        s.textAlign = text::TextAlignment::CENTER;
        s.textTransform = text::TextTransform::UPPERCASE;
        s.color = "#444444";
        s.lineHeight = 1.2;
        s.letterSpacing = 0.42;
        s.fontWeight = 400;

        text::TextTransformation t;
        t.size = 2.41980670942495;
        t.maxWidth = 12.036;

        timeline.AddClip(makeTextClip(W, H,fontsDir + "roboto-400.woff",
                                      "B2B GROWTH", s, t,
                                      0.44531249999999994, 0.8105186616865886));
    }
}

// ── Payload 5 (real video-rendering-service export: portrait, two TEXT tracks) ──
// From an exportId a2ba2448 project (settings 2160x3840). Reproduces addTextClip preprocessing:
// project_width = settings.width (the portrait W passed in), size/maxWidth passed through, position
// carried by the Clip (GRAVITY_CENTER, location = pos-0.5 for CENTER text), fontId -> local woff.
// The headline's in/out char animations (Zoom Shake / Fly Away) are OMITTED: we render the resting
// frame, and wrapping/layout (the thing under test across resolutions) is animation-independent.
void buildPayload5(Timeline& timeline, int W, int H, const std::string& /*fontsDir*/) {
    const std::string kRoboto =
        "/tmp/claude-1000/-home-seno-Desktop-projects-libopenshot/2eab89e1-cae0-4bfa-825b-15d9e5e1ac19/scratchpad/fonts/roboto-400.woff";
    const std::string kPlayfair =
        "/tmp/claude-1000/-home-seno-Desktop-projects-libopenshot/2eab89e1-cae0-4bfa-825b-15d9e5e1ac19/scratchpad/fonts/playfair-400-italic.woff";

    // Track zindex 1 (below): big italic Playfair headline with 3D tilt.
    {
        text::TextClipStyle s;
        s.italic = true;
        s.textAlign = text::TextAlignment::CENTER;
        s.textTransform = text::TextTransform::NONE;
        s.color = "#f0ead8";
        s.lineHeight = 0.8;
        s.letterSpacing = -0.02;
        s.fontWeight = 900;

        text::TextTransformation t;
        t.size = 26.285062962373054;
        t.maxWidth = 6.940993244171142;
        t.tiltX = -40;
        t.tiltY = 18;

        Clip* c = makeTextClip(W, H,kPlayfair, "TO DO\nGREAT WORK", s, t,
                               0.4027777777777778, 0.0765625);
        c->Layer(1);
        timeline.AddClip(c);
    }

    // Track zindex 2 (on top): small uppercase Roboto label with a faint background.
    {
        text::TextClipStyle s;
        s.textAlign = text::TextAlignment::CENTER;
        s.textTransform = text::TextTransform::UPPERCASE;
        s.color = "#4a4038";
        s.lineHeight = 1.2;
        s.letterSpacing = 0.5;
        s.fontWeight = 400;
        s.backgroundColor = "rgba(0, 0, 0, 0.01)";
        s.backgroundRadiusRatio = 0.3;
        s.backgroundPaddingXRatio = 0.1;
        s.backgroundPaddingYRatio = 1.0;

        text::TextTransformation t;
        t.size = 2.7574316377769614;
        t.maxWidth = 26.97;

        Clip* c = makeTextClip(W, H,kRoboto, "IS TO LOVE WHAT YOU DO", s, t,
                               0.4796296296296295, 0.2948591540510747);
        c->Layer(2);
        timeline.AddClip(c);
    }
}

} // namespace

int main(int argc, char** argv) {
    constexpr int fps = 30;
    const std::string fontsDir = FONTS_DIR;
    const std::string which = argc > 1 ? argv[1] : "1";
    const std::string res   = argc > 2 ? argv[2] : "720";

    // Resolution presets. Payloads 1-4 are 16:9 landscape; payload 5 is a 9:16 portrait project
    // (settings 2160x3840), so HD/FHD/4K there mean 720x1280 / 1080x1920 / 2160x3840.
    int W = 1280, H = 720;
    const bool portrait = (which == "5");
    if (res == "1080" || res == "fhd" || res == "fullhd") { W = portrait ? 1080 : 1920; H = portrait ? 1920 : 1080; }
    else if (res == "4k" || res == "2160")                { W = portrait ? 2160 : 3840; H = portrait ? 3840 : 2160; }
    else                                                  { W = portrait ? 720  : 1280; H = portrait ? 1280 : 720;  }

    const std::string suffix = (which == "1") ? "" : which;
    const std::string base    = "text-payload" + suffix + "-" + res;
    const std::string pngPath = base + ".png";
    const std::string mp4Path = base + ".mp4";

    Timeline timeline(W, H, Fraction(fps, 1), 48000, 2, ChannelLayout::LAYOUT_STEREO);
    timeline.color = Color(std::string("#222326"));   // payload background (solid COLOR)
    timeline.Open();

    if (which == "5") {
        buildPayload5(timeline, W, H, fontsDir);
    } else if (which == "4") {
        buildPayload4(timeline, W, H, fontsDir);
    } else if (which == "3") {
        buildPayload3(timeline, W, H, fontsDir);
    } else if (which == "2") {
        buildPayload2(timeline, W, H, fontsDir);
    } else {

    // ── Clip 1: gradient fill + static 3D tilt ──────────────────────────────
    {
        text::TextClipStyle s;
        s.textAlign = text::TextAlignment::CENTER;
        s.textTransform = text::TextTransform::NONE;
        s.color = "linear-gradient(180deg, #FAC40A 0%, #C8271B 100%)";
        s.lineHeight = 0.86;
        s.letterSpacing = -0.03;
        s.fontWeight = 900;

        text::TextTransformation t;
        t.size = 25.70282837762894;
        t.maxWidth = 5.86025;
        t.tiltX = 38;
        t.tiltY = 50;

        timeline.AddClip(makeTextClip(W, H,fontsDir + "poppins-900.woff",
                                      "THIS\nCONVERTS", s, t,
                                      0.4322916666666667, 0.6703703703703704));
    }

    // ── Clip 2: solid yellow rule ────────────────────────────────────────────
    {
        text::TextClipStyle s;
        s.textAlign = text::TextAlignment::CENTER;
        s.textTransform = text::TextTransform::NONE;
        s.color = "#f5d000";
        s.lineHeight = 1.2;
        s.letterSpacing = -0.04;
        s.fontWeight = 400;
        s.backgroundColor = "rgba(0, 0, 0, 0.001)";
        s.backgroundPaddingXRatio = 0.5;
        s.backgroundPaddingYRatio = 0.5;

        text::TextTransformation t;
        t.size = 3.0778597006824575;
        t.maxWidth = 6.680701754385964;

        timeline.AddClip(makeTextClip(W, H,fontsDir + "ibmplexmono-400.woff",
                                      "\xE2\x94\x81\xE2\x94\x81\xE2\x94\x81\xE2\x94\x81\xE2\x94\x81\xE2\x94\x81\xE2\x94\x81",
                                      s, t, 0.4322916666666667, 0.768320641777177));
    }

    // ── Clip 3: solid grey label ─────────────────────────────────────────────
    {
        text::TextClipStyle s;
        s.textAlign = text::TextAlignment::CENTER;
        s.textTransform = text::TextTransform::UPPERCASE;
        s.color = "#444444";
        s.lineHeight = 1.2;
        s.letterSpacing = 0.42;
        s.fontWeight = 400;

        text::TextTransformation t;
        t.size = 2.59188185320628;
        t.maxWidth = 12.036;

        timeline.AddClip(makeTextClip(W, H,fontsDir + "roboto-400.woff",
                                      "B2B GROWTH", s, t,
                                      0.4322916666666667, 0.7822089979841851));
    }

    } // end payload 1

    // Single-frame PNG (no animation → every frame identical).
    auto frame = timeline.GetFrame(1);
    frame->Save(pngPath, 1.0, "PNG", 100);
    std::cout << "Wrote " << pngPath << " (" << frame->GetWidth() << "x" << frame->GetHeight() << ")\n";

    // Short mp4 (video-only) proving the full encode pipeline.
    FFmpegWriter w(mp4Path);
    w.SetAudioOptions(false, "aac", 48000, 2, ChannelLayout::LAYOUT_STEREO, 128000);
    w.SetVideoOptions(true, "libx264", Fraction(fps, 1), W, H, Fraction(1, 1), false, false, 4000000);
    w.PrepareStreams();
    w.SetOption(VIDEO_STREAM, "crf", "18");
    w.SetOption(VIDEO_STREAM, "preset", "fast");
    w.Open();
    w.WriteFrame(&timeline, 1, 1);   // single static frame
    w.Close();
    std::cout << "Wrote " << mp4Path << "\n";

    timeline.Close();
    return 0;
}
