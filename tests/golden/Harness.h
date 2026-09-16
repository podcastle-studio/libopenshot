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
