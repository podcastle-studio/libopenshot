// openshot-golden: renders every feature video-rendering-service uses and compares the frames
// against committed golden PNGs. See tests/golden/README.md.
#include "Harness.h"

#include <clocale>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

#ifndef GOLDEN_MEDIA_DIR
#define GOLDEN_MEDIA_DIR "tests/golden/media/"
#endif
#ifndef GOLDEN_EXPECTED_DIR
#define GOLDEN_EXPECTED_DIR "tests/golden/expected"
#endif

namespace {

void usage() {
    std::cout <<
        "openshot-golden [options]\n"
        "  (default)          render and compare against goldens; exit 1 on any failure\n"
        "  --update           write goldens from this run (use --filter to re-baseline a subset)\n"
        "  --list             list scenarios and exit\n"
        "  --filter <s>       only scenarios whose name contains <s> or whose tags include <s>\n"
        "  --out <dir>        where actual frames are written (default ./golden-out)\n"
        "  --expected <dir>   goldens directory (default " GOLDEN_EXPECTED_DIR ")\n"
        "  --media <dir>      media directory (default " GOLDEN_MEDIA_DIR ")\n"
        "  --report <dir>     write an HTML report (index.html + triptychs) to <dir>\n"
        "  --all-images       write triptychs for passing frames too\n"
        "  --threads N        OpenMP/FFmpeg threads (default 4)\n";
}

} // namespace

int main(int argc, char** argv) {
    setenv("TZ", "UTC", 1);
    setenv("QT_QPA_PLATFORM", "offscreen", 0);
    std::setlocale(LC_NUMERIC, "C");

    golden::Options opts;
    opts.outDir = "golden-out";
    opts.expectedDir = GOLDEN_EXPECTED_DIR;
    opts.mediaDir = GOLDEN_MEDIA_DIR;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* what) -> std::string {
            if (i + 1 >= argc) { std::cerr << what << " needs a value\n"; std::exit(2); }
            return argv[++i];
        };
        if (a == "--update") opts.update = true;
        else if (a == "--list") opts.list = true;
        else if (a == "--all-images") opts.allImages = true;
        else if (a == "--filter") opts.filter = next("--filter");
        else if (a == "--out") opts.outDir = next("--out");
        else if (a == "--expected") opts.expectedDir = next("--expected");
        else if (a == "--media") opts.mediaDir = next("--media");
        else if (a == "--report") opts.reportDir = next("--report");
        else if (a == "--threads") opts.threads = std::stoi(next("--threads"));
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else { std::cerr << "unknown option " << a << "\n"; usage(); return 2; }
    }
    if (!opts.mediaDir.empty() && opts.mediaDir.back() != '/') opts.mediaDir += '/';

    golden::registerReaderScenarios();
    golden::registerTransformScenarios();
    golden::registerCompositingScenarios();
    golden::registerClipFxScenarios();
    golden::registerEffectScenarios();
    golden::registerTransitionScenarios();
    golden::registerTextScenarios();
    golden::registerSubtitleScenarios();
    golden::registerTimeScenarios();
    golden::registerExportScenarios();
    golden::registerUnitScenarios();

    if (opts.list) {
        for (const auto& s : golden::registry()) {
            std::cout << s.name << "  [";
            for (size_t k = 0; k < s.tags.size(); ++k) std::cout << (k ? "," : "") << s.tags[k];
            std::cout << "]  frames=" << (s.capture ? std::string("custom") : std::to_string(s.frames.size())) << "\n";
        }
        return 0;
    }

    if (!std::filesystem::exists(opts.mediaDir + "clip_a_640x360_30.mp4")) {
        std::cerr << "media not found in " << opts.mediaDir << " (run tests/golden/media/generate.sh or pass --media)\n";
        return 2;
    }

    const golden::RunSummary summary = golden::runAll(opts);
    if (!opts.reportDir.empty()) {
        golden::writeHtmlReport(opts.reportDir, summary, opts);
        std::cout << "report: " << opts.reportDir << "/index.html\n";
    }
    std::cout << summary.scenariosRun << " scenarios, " << summary.frames.size() << " frames, "
              << summary.checks.size() << " checks, " << summary.failures << " failure(s), "
              << summary.seconds << " s\n";
    return summary.failures ? 1 : 0;
}
