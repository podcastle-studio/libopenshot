// unit.gpu_resident: the acceptance gate for "the frame never leaves the GPU".
//
// Everything the service constructs, in one timeline, rendered with every GPU switch the service
// turns on by default (GPU decode + NVDEC, GPU crop): a still background, H.264 and VP9 clips
// through NVDEC, the COLOR / LIGHT / ADJUSTMENT (with grain) filters and a LUT, crop with rounded
// corners on a video and on an image, drop shadow, clip blur, flip, a blend mode, chroma key, image
// and video masks, a camera movement, a WHOOSH-shaped transition (zoom, blur, zoom blur, alpha)
// with a light-leak overlay of another size, a SPLIT (rests at 0 outside its window), a CIRCLE_MASK
// and a zoom-out, a shape, static and animated text with glow, and subtitles.
//
// Held to: no readback but the one Timeline::GetFrame makes at the end of every frame when there
// is no encoder to hand the frame to (a GPU_ENCODE export does not make it); no GPU effect or
// overlay declining to the CPU; nothing decoded on the CPU; no crop reading its frame back; and,
// once every still and table has been uploaded, no uploads. The service's ProRes watermark is left
// out: NVDEC does not decode ProRes, so its 144 frames are the one CPU decode an export has
// (read ahead on its own thread), and readers.watermark_prores4444 covers its compositing.
#include "Recipes.h"

#include "FFmpegReader.h"
#include "GpuEffect.h"
#include "Settings.h"
#include "Timeline.h"
#include "effects/ChromaKey.h"
#include "effects/ColorAdjustment.h"
#include "effects/ColorMap.h"
#include "effects/Enhancement.h"
#include "effects/LightAdjustment.h"
#include "gpu/CudaInterop.h"
#include "gpu/GpuDevice.h"
#include "gpu/GpuOverlay.h"
#include "gpu/GpuTelemetry.h"

#include <cstdio>

using namespace golden;
using namespace golden::recipes;

namespace {

struct Saved { bool decode; int hardware; bool crop; };
Saved g_saved;

} // namespace

void golden::registerResidentScenarios() {
    addCustom("unit.gpu_resident", {"unit", "gpu"},
        [](Scene& s) {
            openshot::Settings* settings = openshot::Settings::Instance();
            g_saved = {settings->GPU_DECODE, settings->HARDWARE_DECODER, settings->GPU_CROP};
            // The service's defaults on a GPU node, before any reader opens.
            settings->GPU_DECODE = true;
            settings->HARDWARE_DECODER = 2;
            settings->GPU_CROP = true;

            auto& tl = s.makeTimeline();
            const double fps = s.fps.ToDouble();
            tl.AddClip(backgroundClip(s, s.media("background_960x540.png")));

            // Two video clips with an overlapping WHOOSH-shaped transition and a light leak.
            MediaSpec a; a.path = s.media("clip_a_640x360_30.mp4"); a.start = 0.0; a.end = 2.0; a.track = 1;
            a.transform = Transform{BBox{0.5f, 0.5f, 0.9f, 0.9f}};
            auto* out = mediaClip(s, a);
            out->AddEffect(new openshot::ColorAdjustment(openshot::Keyframe(0.08), openshot::Keyframe(0),
                                                         openshot::Keyframe(0), openshot::Keyframe(0)));
            out->AddEffect(new openshot::LightAdjustment(openshot::Keyframe(0.36), openshot::Keyframe(-0.88),
                                                         0, 0, 0, 0));
            out->AddEffect(new openshot::Enhancement(openshot::Keyframe(0.67), openshot::Keyframe(0.68),
                                                     openshot::Keyframe(-0.58)));
            out->AddEffect(new openshot::ColorMap(s.media("lut_example.cube")));
            out->AddEffect(cropEffect(0.05, 0.05, 0.05, 0.05, 0.3));
            out->Shadow(true);
            out->shadow_color = openshot::Color(0, 0, 0, 200);
            out->shadow_blur = openshot::Keyframe(12.0);
            out->shadow_distance = openshot::Keyframe(20.0);
            out->shadow_angle = openshot::Keyframe(45.0);
            addCameraMovement(*out, 0.0, 2.0, fps, {{0.0, 100}, {1.0, 130, kEaseInOut}}, {{0.0, 0}, {1.0, 8}},
                              {{0.0, 0.5}, {1.0, 0.6}}, {{0.0, 0.5}, {1.0, 0.45}});

            MediaSpec b; b.path = s.media("clip_b_854x480_24.mp4"); b.start = 1.5; b.end = 3.5; b.track = 1;
            b.priority = 1; b.transform = Transform{BBox{0.5f, 0.5f, 0.9f, 0.9f}};
            auto* in = mediaClip(s, b);
            in->AddEffect(videoMask(s, s.media("matte_wipe_640x360_30.mp4"), *in, fps));
            in->FlipHorizontal(true);
            applyOverlappingTransition(*out, *in, 1.0, fps,
                                       {TransitionEffect::Zoom, TransitionEffect::Blur, TransitionEffect::ZoomBlur,
                                        TransitionEffect::Alpha});
            addOverlayClip(s, s.media("clip_c_1280x720_25.mp4"), 1.0, openshot::Clip::ADDITIVE_BLEND, out, in);

            // A VP9 PiP (NVDEC, webm) with chroma key, a clip blur, a blend mode, a split and a circle.
            MediaSpec v; v.path = s.media("clip_vp9_640x360_30.webm"); v.start = 0.0; v.end = 1.0; v.track = 3;
            v.transform = Transform{BBox{0.8f, 0.25f, 0.3f, 0.3f}};
            auto* pip = mediaClip(s, v);
            pip->AddEffect(new openshot::ChromaKey(openshot::Color(0, 255, 0, 0), 70, 20, openshot::CHROMAKEY_YCBCR));
            pip->Blur(true);
            pip->blur_amount = openshot::Keyframe(4.0);
            pip->Blend(openshot::BLEND_SCREEN);
            RampWindow window{10, 25};
            addTransitionEffect(*pip, TransitionEffect::SplitShift, window, true);
            addTransitionEffect(*pip, TransitionEffect::CircleMask, window, false);

            // A zoomed-out VP8 clip later on.
            MediaSpec z; z.path = s.media("clip_vp8_640x360_30.webm"); z.start = 2.5; z.end = 3.5; z.track = 3;
            z.transform = Transform{BBox{0.2f, 0.75f, 0.3f, 0.3f}};
            auto* zoomed = mediaClip(s, z);
            addTransitionEffect(*zoomed, TransitionEffect::Zoom, RampWindow{76, 100}, true);

            // An image clip with a crop, rounded corners, a fade and an image mask; a shape.
            MediaSpec img; img.path = s.media("image_rgb_400x300.jpg"); img.isImage = true; img.start = 0.0;
            img.end = 3.5; img.track = 4; img.transform = Transform{BBox{0.2f, 0.25f, 0.25f, 0.25f}};
            img.fade = Fade{0.5, 0.5};
            auto* still = mediaClip(s, img);
            still->AddEffect(cropEffect(0.1, 0.0, 0.1, 0.0, 0.4));
            still->AddEffect(imageMask(s, s.media("matte_radial_640x360.png")));
            MediaSpec shape; shape.path = s.media("shape_arrow.svg"); shape.isImage = true; shape.start = 0.0;
            shape.end = 3.5; shape.track = 5; shape.transform = Transform{BBox{0.5f, 0.85f, 0.2f, 0.1f}};
            auto* arrow = mediaClip(s, shape);

            // Text: animated with glow, and static.
            TextSpec glow;
            glow.value = "GLOW";
            glow.style = baseStyle(s.font("NotoSans-Bold.ttf"));
            glow.style.glowColor = "#40C0FF";
            glow.style.glowIntensityRatio = 0.9;
            glow.style.glowRangeRatio = 0.6;
            glow.transformation.size = 20.0;
            glow.posX = 0.5f; glow.posY = 0.2f; glow.track = 6; glow.start = 0.0; glow.end = 3.5;
            openshot::text::TextAnimations animations;
            animations.inAnimationId = "rise-chars";
            animations.inAnimationDuration = 1.0;
            glow.animations = animations;
            TextSpec caption = glow;
            caption.value = "caption";
            caption.style = baseStyle(s.font("NotoSans-Regular.ttf"));
            caption.animations.reset();
            caption.posY = 0.6f; caption.track = 7;

            tl.AddClip(out);
            tl.AddClip(in);
            tl.AddClip(pip);
            tl.AddClip(zoomed);
            tl.AddClip(still);
            tl.AddClip(arrow);
            tl.AddClip(textClip(s, glow, fps));
            tl.AddClip(textClip(s, caption, fps));
            tl.LoadSubtitlesFromJsonString(subtitlesJson(s.font("NotoSans-Bold.ttf"), s.width,
                                                         SubtitleVariant::AnimatedInOut));
            tl.Open();
        },
        [](Scene& s, std::vector<Captured>&, std::vector<Check>& checks) {
            using openshot::GpuCounters;
            openshot::Settings* settings = openshot::Settings::Instance();
            const auto restore = [&] {
                settings->GPU_DECODE = g_saved.decode;
                settings->HARDWARE_DECODER = g_saved.hardware;
                settings->GPU_CROP = g_saved.crop;
            };
            if (!openshot::GpuDevice::Instance().available()) {
                restore();
                checks.push_back({"no_gpu", true, "nothing to hold without a GPU"});
                return;
            }
            const int frames = 105;
            const int warm = 5;   // readers open, stills and tables upload
            for (int f = 1; f <= warm; ++f) s.timeline->GetFrame(f);
            const auto get = [](GpuCounters::Counter c) { return GpuCounters::Get(c); };
            const auto rb0 = get(GpuCounters::Readback), cpu0 = get(GpuCounters::DecodedOnCpu),
                       crop0 = get(GpuCounters::CropReadback), up0 = get(GpuCounters::Upload),
                       dev0 = get(GpuCounters::DecodedOnDevice), fb0 = get(GpuCounters::HardwareDecodeFallback);
            openshot::GpuEffect::ResetCounters();
            openshot::GpuOverlay::ResetCounters();
            for (int f = warm + 1; f <= frames; ++f) s.timeline->GetFrame(f);
            const int n = frames - warm;
            const auto readbacks = get(GpuCounters::Readback) - rb0;
            const auto on_cpu = get(GpuCounters::DecodedOnCpu) - cpu0;
            const auto crop_rb = get(GpuCounters::CropReadback) - crop0;
            const auto uploads = get(GpuCounters::Upload) - up0;
            const auto on_device = get(GpuCounters::DecodedOnDevice) - dev0;
            const auto nvdec_fallbacks = get(GpuCounters::HardwareDecodeFallback) - fb0;
            const long long effect_fallbacks = openshot::GpuEffect::CpuFallbacks();
            const long long overlay_fallbacks = openshot::GpuOverlay::CpuFallbacks();
            restore();

            char buf[320];
            std::snprintf(buf, sizeof buf,
                          "%d frames: readbacks %llu (1 a frame is the harness's), decoded on device %llu, on CPU %llu, "
                          "nvdec fallbacks %llu, crop readbacks %llu, uploads %llu, effect fallbacks %lld (passes %lld), "
                          "overlay fallbacks %lld (passes %lld)",
                          n, readbacks, on_device, on_cpu, nvdec_fallbacks, crop_rb, uploads, effect_fallbacks,
                          openshot::GpuEffect::GpuPasses(), overlay_fallbacks, openshot::GpuOverlay::GpuPasses());
            // Without the CUDA interop (lavapipe, or no NVIDIA driver) NVDEC's frames cannot stay on
            // the device: video decodes in software and GpuYuv uploads its planes every frame.
            const bool interop = openshot::CudaInterop::Instance().available();
            checks.push_back({"no_readback_but_the_last", readbacks == static_cast<unsigned long long>(n), buf});
            checks.push_back({"no_effect_or_overlay_on_the_cpu", effect_fallbacks == 0 && overlay_fallbacks == 0, buf});
            checks.push_back({"no_crop_readback", crop_rb == 0, buf});
            // Lavapipe has no CUDA: NVDEC decodes in software there and GpuYuv converts on the GPU.
            checks.push_back({"nothing_decoded_on_the_cpu", on_cpu == 0 && (!interop || nvdec_fallbacks == 0), buf});
            checks.push_back({"no_uploads_after_warm_up", !interop || uploads <= 2, buf});
        });
}
