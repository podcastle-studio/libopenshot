// Golden-frame suite: scenario registry and runner.
#pragma once

#include "Image.h"

#include "Fraction.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace openshot { class Timeline; class Clip; class ReaderBase; class Frame; }

namespace golden {

struct Tolerance {
    double psnrMin = 45.0;
    double ssimMin = 0.98;
    int maxAbs = 255;          // exact scenarios set 1
    double pctOver2Max = 100.0;
    static Tolerance Default() { return {}; }
    static Tolerance Exact()   { return {60.0, 0.995, 1, 0.5}; }
    static Tolerance Codec()   { return {35.0, 0.92, 255, 100.0}; }   // lossy round trips
    static Tolerance Loose()   { return {38.0, 0.95, 255, 100.0}; }   // text AA / large blurs

    /// What a GPU compositor is allowed to differ from the CPU goldens by.
    ///
    /// The goldens are CPU-rendered, and the GPU composite resamples with Skia's
    /// bilinear where QPainter uses its own smooth transform. The two do not agree
    /// bit-for-bit and never will, so a scenario the compositor touches is held to the
    /// parity policy's "close" class (GPU-RENDER-PLAN.md §5) rather than to "exact"
    /// when a GPU is actually in use. On CPU the same scenario keeps its strict
    /// tolerance -- the CPU path ships, and nothing may move it.
    static Tolerance GpuClose() { return {45.0, 0.98, 255, 100.0}; }

    /// For a clip whose blur the GPU applies as an SkImageFilter rather than an in-place
    /// cv::GaussianBlur on the source. Both use the same box->sigma mapping, but Skia has
    /// no mirror tile mode, so the edge band within ~3 sigma is clamped where OpenCV
    /// reflects, and the blur is applied after the transform rather than before it.
    /// W14's gate for blur, measured 43.2 dB / SSIM 0.9986 at the widest.
    static Tolerance GpuBlur() { return {40.0, 0.995, 255, 100.0}; }

    /// For the few blend modes whose formula is mathematically steep enough to turn a
    /// sub-LSB difference in the source into a large one in the result.
    ///
    /// Colour-burn divides by the source channel, hue and saturation renormalise chroma.
    /// The GPU resamples the clip onto the canvas with Skia's bilinear rather than
    /// QPainter's smooth transform, and these three magnify that: measured 26.5 dB,
    /// 39.6 dB and 44.6 dB against CPU goldens, from inputs differing by ~1 LSB.
    ///
    /// The band needed to cover colour-burn is so wide that this tolerance cannot detect
    /// a genuine regression in these modes. It is not the real gate:
    /// `openshot-gpu-blend-parity` is, and it is far sharper -- it blends identical pixels
    /// with no resampling and holds every mode to 2 LSB. Run it when touching blend code.
    static Tolerance GpuAmplified() { return {25.0, 0.96, 255, 100.0}; }

    /// For a scenario with film grain, when a GPU is in use. The GPU grain is the same noise as
    /// the CPU's at a different random phase (Enhancement's float hash, owner decision
    /// 2026-09-24), so the frame differs from its CPU golden by roughly the grain's own amplitude
    /// wherever there is grain. This band only proves the frame is the right picture with grain on
    /// it; it is **not** a regression gate for the grain -- unit.gpu_grain is.
    static Tolerance GpuGrain() { return {20.0, 0.60, 255, 100.0}; }

    /// For a scenario whose video frames the GPU decoded: the reader ran the YUV->RGBA
    /// conversion as an SkSL pass instead of handing it to swscale.
    ///
    /// The conversion itself is faithful -- measured **46.2 dB, SSIM 0.9997, 3 LSB at worst**
    /// on an unscaled 640x360 clip, which is rounding plus swscale's own fixed-point maths.
    /// This band is a little wider than that measurement and no wider.
    static Tolerance GpuDecode() { return {44.0, 0.999, 4, 3.0}; }

    /// For a scenario where the reader ALSO pre-scaled on the GPU, which is a different
    /// story: swscale's SWS_FAST_BILINEAR is fast because it carries a half-pixel phase,
    /// and on colour bars that puts a whole column of wrong pixels at every bar edge.
    /// Measured 28.9-30.7 dB, SSIM 0.9912-0.9941, up to 211 LSB -- all of it at edges the
    /// two filters place differently, with the flat areas matching.
    ///
    /// The band is too wide to catch a regression, exactly as GpuAmplified is. It is not
    /// the real gate on this path: unit.gpu_decode is, and it compares the two conversions
    /// directly on an unscaled clip, with nothing else in the frame. **Whether the reader should pre-scale at all once
    /// the frame stays on the GPU is an open question** -- the compositor already scales,
    /// with the same sampler, in its own transformed draw. See GPU-WORKLIST W23.
    static Tolerance GpuDecodeScaled() { return {28.0, 0.99, 255, 100.0}; }
};

// Everything a scenario builds lives here so teardown order is fixed:
// timeline first (deletes its FrameMappers), then clips (delete their effects), then readers.
struct Scene {
    int width = 640;
    int height = 360;
    openshot::Fraction fps{30, 1};
    std::string mediaDir;      // tests/golden/media/
    std::string workDir;       // scratch for export outputs

    std::unique_ptr<openshot::Timeline> timeline;
    std::vector<openshot::Clip*> clips;
    std::vector<openshot::ReaderBase*> readers;

    openshot::Timeline& makeTimeline();                      // Timeline(w,h,fps,48000,2,LAYOUT_STEREO)
    openshot::Clip* own(openshot::Clip* c)             { clips.push_back(c); return c; }
    openshot::ReaderBase* own(openshot::ReaderBase* r) { readers.push_back(r); return r; }
    std::string media(const std::string& file) const { return mediaDir + file; }
    std::string font(const std::string& file) const  { return mediaDir + "fonts/" + file; }
    ~Scene();
};

struct Captured {
    std::string label;   // "f000030" or "f000030_decoded"
    Image image;
};

struct Check {           // non-image assertion (export metadata, audio silence, …)
    std::string name;
    bool ok;
    std::string message;
};

struct Scenario {
    std::string name;                       // "effects.chromakey"
    std::vector<std::string> tags;          // {"effects"}; "exact" switches to Tolerance::Exact()
    std::vector<int64_t> frames;            // 1-based timeline frames captured by the default capture
    Tolerance tol;
    /// Tolerance used instead of @c tol when the run has a GPU active. Same as @c tol
    /// unless the scenario is tagged "gpu-composite"; see Tolerance::GpuClose().
    Tolerance gpuTol;
    std::function<void(Scene&)> build;      // adds clips to scene.makeTimeline(), must call Open()
    // Optional custom capture (export scenarios). Default: GetFrame(n) for each frame.
    std::function<void(Scene&, std::vector<Captured>&, std::vector<Check>&)> capture;
};

// Registration helper used by the scenario files.
void add(const std::string& name, std::vector<std::string> tags, std::vector<int64_t> frames,
         std::function<void(Scene&)> build, Tolerance tol = Tolerance::Default());
void addCustom(const std::string& name, std::vector<std::string> tags,
               std::function<void(Scene&)> build,
               std::function<void(Scene&, std::vector<Captured>&, std::vector<Check>&)> capture,
               Tolerance tol = Tolerance::Default());
std::vector<Scenario>& registry();

// Group registration (explicit order, called from main).
void registerReaderScenarios();
void registerTransformScenarios();
void registerCompositingScenarios();
void registerClipFxScenarios();
void registerEffectScenarios();
void registerTransitionScenarios();
void registerTextScenarios();
void registerSubtitleScenarios();
void registerTimeScenarios();
void registerExportScenarios();
void registerUnitScenarios();

struct Options {
    bool update = false;
    bool list = false;
    bool allImages = false;    // write triptychs for passing frames too
    std::string filter;        // substring of name, or exact tag
    std::string outDir;        // actual frames
    std::string expectedDir;   // goldens
    std::string reportDir;     // HTML report (empty = none)
    std::string mediaDir;
    int threads = 4;
};

struct FrameResult {
    std::string scenario;
    std::string label;
    Metrics metrics;
    bool pass = false;
    std::string message;       // error text or "missing golden"
    std::string goldenPath, actualPath, diffPath, triptychPath;
};

struct RunSummary {
    std::vector<FrameResult> frames;
    std::vector<std::pair<std::string, Check>> checks;   // scenario, check
    int scenariosRun = 0;
    int failures = 0;
    double seconds = 0.0;
};

RunSummary runAll(const Options& opts);
void writeHtmlReport(const std::string& dir, const RunSummary& summary, const Options& opts);

} // namespace golden
