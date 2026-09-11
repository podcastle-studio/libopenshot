// openshot-bench: performance baseline / regression benchmark for the render pipeline.
//
// Each (scenario, resolution, mode) case runs in a child process (fork + exec of this binary with
// --case) so CPU time and peak RSS are measured per case with wait4(). Results are written as JSON
// (one file per run) and optionally as a Markdown table; `compare` prints the delta between two runs.
//
//   openshot-bench [--label L] [--frames N] [--res 540,720,1080,1440,2160] [--modes render,x264,nvenc]
//                  [--scenario substr] [--threads N] [--quick] [--json out.json] [--md out.md]
//   openshot-bench compare old.json new.json [--md out.md]
//   openshot-bench --list
#include "Harness.h"
#include "Recipes.h"
#include "Scenarios.h"

#include "FFmpegWriter.h"
#include "Settings.h"
#include "Timeline.h"

extern "C" {
#include <libavutil/avutil.h>
}
#include <json/json.h>

#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Res { std::string name; int w, h; };
const std::vector<Res> kAllRes = {{"540p", 960, 540}, {"720p", 1280, 720}, {"1080p", 1920, 1080}, {"1440p", 2560, 1440}, {"2160p", 3840, 2160}};

struct Opts {
    std::string label = "baseline";
    int frames = 150;
    std::vector<std::string> res = {"540p", "720p", "1080p", "1440p", "2160p"};
    std::vector<std::string> modes = {"render", "x264"};
    std::string scenarioFilter;
    int threads = 0;              // 0 = library defaults (what the service runs with)
    int parallel = 1;             // N identical processes per case, started together
    std::string jsonOut, mdOut;
    std::string resumeFrom;       // JSON of an interrupted run: cases present there are skipped and kept
    std::string goldenMedia = BENCH_GOLDEN_MEDIA_DIR;
    std::string benchMedia = BENCH_MEDIA_DIR;
    std::string outDir = "/tmp/openshot-bench";
    // child mode
    std::string caseSpec;         // scenario:WxH:mode
};

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out; std::string cur; std::istringstream in(s);
    while (std::getline(in, cur, sep)) if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string shell(const std::string& cmd) {
    std::string out; char buf[256];
    FILE* p = popen((cmd + " 2>/dev/null").c_str(), "r");
    if (!p) return out;
    while (fgets(buf, sizeof buf, p)) out += buf;
    pclose(p);
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
    return out;
}

std::string procField(const char* file, const char* key) {
    std::ifstream in(file); std::string line;
    while (std::getline(in, line)) if (line.rfind(key, 0) == 0) { auto p = line.find(':'); return p == std::string::npos ? "" : line.substr(p + 1); }
    return "";
}
std::string trim(std::string s) { while (!s.empty() && isspace(s.front())) s.erase(0, 1); while (!s.empty() && isspace(s.back())) s.pop_back(); return s; }

// ── child: run one case in-process ─────────────────────────────────────────────────────────────
int runCase(const Opts& o) {
#ifdef PR_SET_PTRACER
    prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);   // allow the poor-man's profiler to attach
#endif
    const auto parts = split(o.caseSpec, ':');
    if (parts.size() != 3) { std::cerr << "bad --case\n"; return 2; }
    const std::string scenarioName = parts[0], mode = parts[2];
    const auto wh = split(parts[1], 'x');
    const int W = std::stoi(wh[0]), H = std::stoi(wh[1]);

    const bench::BenchScenario* sc = nullptr;
    for (const auto& s : bench::scenarios()) if (s.name == scenarioName) sc = &s;
    if (!sc) { std::cerr << "unknown scenario " << scenarioName << "\n"; return 2; }

    auto* settings = openshot::Settings::Instance();
    if (o.threads > 0) { settings->OMP_THREADS = o.threads; settings->FF_THREADS = o.threads; }
    settings->DEBUG_TO_STDERR = false;
    bench::setBenchMediaDir(o.benchMedia);

    golden::Scene scene;
    scene.width = W; scene.height = H;
    scene.mediaDir = o.goldenMedia;
    scene.workDir = o.outDir + "/";
    std::filesystem::create_directories(scene.workDir);

    const auto tBuild0 = std::chrono::steady_clock::now();
    sc->build(scene);
    const double buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tBuild0).count();

    std::vector<double> perFrameMs;
    double wall = 0.0;
    const auto t0 = std::chrono::steady_clock::now();
    if (mode == "render") {
        perFrameMs.reserve(o.frames);
        for (int64_t n = 1; n <= o.frames; ++n) {
            const auto f0 = std::chrono::steady_clock::now();
            auto frame = scene.timeline->GetFrame(n);
            (void)frame->GetPixels();
            perFrameMs.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - f0).count());
        }
    } else {
        const std::string codec = mode == "nvenc" ? "h264_nvenc" : "libx264";
        openshot::FFmpegWriter w(scene.workDir + scenarioName + "_" + parts[1] + "_" + mode + ".mp4");
        golden::recipes::configureWriter(w, W, H, scene.fps, W >= 3840 ? 20000000 : (W >= 1920 ? 10000000 : 5000000), codec);
        w.Open();
        w.WriteFrame(scene.timeline.get(), 1, o.frames);
        w.Close();
    }
    wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    double p50 = 0, p95 = 0, mx = 0, avg = 0;
    if (!perFrameMs.empty()) {
        std::vector<double> s = perFrameMs; std::sort(s.begin(), s.end());
        p50 = s[s.size() / 2]; p95 = s[std::min(s.size() - 1, static_cast<size_t>(s.size() * 0.95))]; mx = s.back();
        for (double v : perFrameMs) avg += v; avg /= perFrameMs.size();
    } else { avg = 1000.0 * wall / o.frames; }
    std::printf("RESULT frames=%d wall=%.4f fps=%.3f avg_ms=%.3f p50_ms=%.3f p95_ms=%.3f max_ms=%.3f build_ms=%.1f\n",
                o.frames, wall, o.frames / wall, avg, p50, p95, mx, buildMs);
    std::fflush(stdout);
    return 0;
}

// ── parent: spawn children, collect rusage ────────────────────────────────────────────────────
struct CaseResult {
    std::string scenario, res, mode; int w = 0, h = 0;
    bool ok = false; std::string error;
    int frames = 0; double wall = 0, fps = 0, avgMs = 0, p50Ms = 0, p95Ms = 0, maxMs = 0, buildMs = 0;
    double cpuSeconds = 0, cpuCores = 0; long maxRssKb = 0;
    int parallel = 1; double aggFps = 0;
};

struct Child { pid_t pid = -1; int fd = -1; std::string out; };

Child startChild(const Opts& o, const std::string& spec, const char* self, int index) {
    Child c;
    int pipefd[2];
    if (pipe(pipefd) != 0) return c;
    const std::string frames = std::to_string(o.frames), threads = std::to_string(o.threads);
    const std::string outDir = o.outDir + "/p" + std::to_string(index);
    const pid_t pid = fork();
    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO); close(pipefd[0]); close(pipefd[1]);
        int devnull = open("/dev/null", O_WRONLY); if (devnull >= 0) dup2(devnull, STDERR_FILENO);
        execl(self, self, "--case", spec.c_str(), "--frames", frames.c_str(), "--threads", threads.c_str(),
              "--media", o.goldenMedia.c_str(), "--bench-media", o.benchMedia.c_str(), "--out", outDir.c_str(), (char*)nullptr);
        _exit(127);
    }
    close(pipefd[1]);
    c.pid = pid; c.fd = pipefd[0];
    return c;
}

// Runs o.parallel identical processes for one case at the same time. Per-process metrics are
// averaged; aggregate fps = total frames / wall time of the slowest process.
CaseResult spawnCase(const Opts& o, const std::string& scenario, const Res& r, const std::string& mode, const char* self) {
    CaseResult cr; cr.scenario = scenario; cr.res = r.name; cr.mode = mode; cr.w = r.w; cr.h = r.h; cr.parallel = o.parallel;
    const std::string spec = scenario + ":" + std::to_string(r.w) + "x" + std::to_string(r.h) + ":" + mode;
    std::vector<Child> kids;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < o.parallel; ++i) kids.push_back(startChild(o, spec, self, i));
    // read all pipes to EOF (children block on a full pipe otherwise)
    for (auto& k : kids) {
        char buf[512]; ssize_t n;
        while (k.fd >= 0 && (n = read(k.fd, buf, sizeof buf)) > 0) k.out.append(buf, n);
        if (k.fd >= 0) close(k.fd);
    }
    double cpuTotal = 0; long rssTotal = 0; int okCount = 0;
    double fpsSum = 0, avgSum = 0, p50Sum = 0, p95Sum = 0, maxSum = 0, buildSum = 0, wallMax = 0;
    for (auto& k : kids) {
        int status = 0; struct rusage ru{};
        wait4(k.pid, &status, 0, &ru);
        cpuTotal += ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6 + ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6;
        rssTotal += ru.ru_maxrss;
        const auto pos = k.out.find("RESULT ");
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || pos == std::string::npos) {
            cr.error = WIFSIGNALED(status) ? "signal " + std::to_string(WTERMSIG(status)) : "exit " + std::to_string(WEXITSTATUS(status));
            continue;
        }
        std::istringstream in(k.out.substr(pos + 7)); std::string kv;
        double wall = 0;
        while (in >> kv) {
            const auto eq = kv.find('='); if (eq == std::string::npos) continue;
            const std::string key = kv.substr(0, eq); const double v = std::atof(kv.c_str() + eq + 1);
            if (key == "frames") cr.frames = int(v); else if (key == "wall") wall = v; else if (key == "fps") fpsSum += v;
            else if (key == "avg_ms") avgSum += v; else if (key == "p50_ms") p50Sum += v; else if (key == "p95_ms") p95Sum += v;
            else if (key == "max_ms") maxSum += v; else if (key == "build_ms") buildSum += v;
        }
        wallMax = std::max(wallMax, wall);
        ++okCount;
    }
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    cr.cpuSeconds = cpuTotal;
    cr.maxRssKb = rssTotal;                       // sum over processes = what the pod needs
    if (okCount != o.parallel) return cr;
    cr.wall = wallMax;
    cr.fps = fpsSum / okCount;                    // per-process fps
    cr.aggFps = double(cr.frames) * okCount / wallMax;
    cr.avgMs = avgSum / okCount; cr.p50Ms = p50Sum / okCount; cr.p95Ms = p95Sum / okCount; cr.maxMs = maxSum / okCount; cr.buildMs = buildSum / okCount;
    cr.cpuCores = elapsed > 0 ? cpuTotal / elapsed : 0;   // cores busy across all processes
    cr.ok = true;
    return cr;
}

Json::Value machineInfo(const Opts& o) {
    Json::Value m;
    m["cpu"] = trim(procField("/proc/cpuinfo", "model name"));
    m["hardware_threads"] = static_cast<int>(std::thread::hardware_concurrency());
    m["mem_total"] = trim(procField("/proc/meminfo", "MemTotal"));
    m["gpu"] = shell("nvidia-smi --query-gpu=name,driver_version,memory.total --format=csv,noheader");
    m["ffmpeg"] = av_version_info();
    m["git"] = shell(std::string("git -C ") + BENCH_SOURCE_DIR + " rev-parse --short HEAD");
    m["git_branch"] = shell(std::string("git -C ") + BENCH_SOURCE_DIR + " rev-parse --abbrev-ref HEAD");
    m["build_type"] = BENCH_BUILD_TYPE;
    m["threads_setting"] = o.threads == 0 ? "library defaults (FF_THREADS=16, OMP_THREADS=16)" : std::to_string(o.threads);
    m["kernel"] = shell("uname -r");
    return m;
}

std::string nowStamp() {
    char buf[32]; std::time_t t = std::time(nullptr); std::strftime(buf, sizeof buf, "%Y%m%d-%H%M", std::localtime(&t)); return buf;
}
std::string isoNow() {
    char buf[32]; std::time_t t = std::time(nullptr); std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S", std::localtime(&t)); return buf;
}

std::string fmt(double v, int prec) { char b[32]; std::snprintf(b, sizeof b, "%.*f", prec, v); return b; }

// ── markdown ──────────────────────────────────────────────────────────────────────────────────
std::string markdown(const Json::Value& run) {
    std::ostringstream md;
    const auto& m = run["machine"];
    md << "Run `" << run["label"].asString() << "` on " << run["date"].asString() << ", commit " << m["git"].asString()
       << " (" << m["git_branch"].asString() << "), " << m["build_type"].asString() << " build, " << run["frames"].asInt()
       << " frames per case at 30 fps, threads: " << m["threads_setting"].asString()
       << (run["parallel"].asInt() > 1 ? ", **" + std::to_string(run["parallel"].asInt()) + " identical processes per case run concurrently** (fps = per process, agg = all processes together; cores/RSS summed)" : "")
       << ".\n\n"
       << "Machine: " << m["cpu"].asString() << " (" << m["hardware_threads"].asInt() << " threads), "
       << m["mem_total"].asString() << " RAM, GPU " << m["gpu"].asString() << ", FFmpeg " << m["ffmpeg"].asString()
       << ", kernel " << m["kernel"].asString() << ".\n\n";

    std::vector<std::string> scen, res, modes;
    for (const auto& c : run["cases"]) {
        auto push = [](std::vector<std::string>& v, const std::string& s) { if (std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s); };
        push(scen, c["scenario"].asString()); push(res, c["res"].asString()); push(modes, c["mode"].asString());
    }
    auto find = [&](const std::string& s, const std::string& r, const std::string& mo) -> const Json::Value* {
        for (const auto& c : run["cases"]) if (c["scenario"] == s && c["res"] == r && c["mode"] == mo) return &c;
        return nullptr;
    };
    for (const auto& mode : modes) {
        const std::string title = mode == "render" ? "Render only (Timeline::GetFrame, no encoder)"
                                : mode == "x264" ? "Export with libx264 (service configuration)" : "Export with h264_nvenc";
        md << "### " << title << "\n\n";
        md << "fps, with the p95 per-frame time in ms for render mode. CPU = average cores busy; RSS = peak resident memory of the process.\n\n";
        md << "| scenario |"; for (const auto& r : res) md << " " << r << " |"; md << "\n|---|"; for (size_t i = 0; i < res.size(); ++i) md << "---:|"; md << "\n";
        for (const auto& s : scen) {
            md << "| `" << s << "` |";
            for (const auto& r : res) {
                const auto* c = find(s, r, mode);
                if (!c) { md << " – |"; continue; }
                if (!(*c)["ok"].asBool()) { md << " FAIL |"; continue; }
                md << " **" << fmt((*c)["fps"].asDouble(), 1) << "**";
                if ((*c)["parallel"].asInt() > 1) md << " (agg " << fmt((*c)["agg_fps"].asDouble(), 1) << ")";
                else if (mode == "render") md << " (p95 " << fmt((*c)["p95_ms"].asDouble(), 1) << ")";
                md << " |";
            }
            md << "\n";
        }
        md << "\nCPU cores busy / peak RSS (GB):\n\n";
        md << "| scenario |"; for (const auto& r : res) md << " " << r << " |"; md << "\n|---|"; for (size_t i = 0; i < res.size(); ++i) md << "---:|"; md << "\n";
        for (const auto& s : scen) {
            md << "| `" << s << "` |";
            for (const auto& r : res) {
                const auto* c = find(s, r, mode);
                if (!c || !(*c)["ok"].asBool()) { md << " – |"; continue; }
                md << " " << fmt((*c)["cpu_cores"].asDouble(), 1) << " / " << fmt((*c)["max_rss_kb"].asDouble() / 1048576.0, 2) << " |";
            }
            md << "\n";
        }
        md << "\n";
    }
    return md.str();
}

std::string compareMarkdown(const Json::Value& a, const Json::Value& b) {
    std::ostringstream md;
    md << "Comparing `" << a["label"].asString() << "` (" << a["machine"]["git"].asString() << ", " << a["date"].asString()
       << ") → `" << b["label"].asString() << "` (" << b["machine"]["git"].asString() << ", " << b["date"].asString() << ").\n\n"
       << "| scenario | res | mode | fps before | fps after | speedup | cores before → after | RSS before → after (GB) |\n|---|---|---|---:|---:|---:|---:|---:|\n";
    for (const auto& cb : b["cases"]) {
        const Json::Value* ca = nullptr;
        for (const auto& c : a["cases"]) if (c["scenario"] == cb["scenario"] && c["res"] == cb["res"] && c["mode"] == cb["mode"]) ca = &c;
        if (!ca || !(*ca)["ok"].asBool() || !cb["ok"].asBool()) continue;
        const double fa = (*ca)["fps"].asDouble(), fb = cb["fps"].asDouble();
        md << "| `" << cb["scenario"].asString() << "` | " << cb["res"].asString() << " | " << cb["mode"].asString() << " | "
           << fmt(fa, 1) << " | " << fmt(fb, 1) << " | **" << fmt(fb / fa, 2) << "x** | "
           << fmt((*ca)["cpu_cores"].asDouble(), 1) << " → " << fmt(cb["cpu_cores"].asDouble(), 1) << " | "
           << fmt((*ca)["max_rss_kb"].asDouble() / 1048576.0, 2) << " → " << fmt(cb["max_rss_kb"].asDouble() / 1048576.0, 2) << " |\n";
    }
    return md.str();
}

Json::Value loadJson(const std::string& path) {
    std::ifstream in(path); Json::Value v; in >> v; return v;
}

void usage() {
    std::cout << "openshot-bench [--label L] [--frames N] [--res 540p,720p,1080p,1440p,2160p] [--modes render,x264,nvenc]\n"
                 "               [--scenario substr] [--threads N] [--parallel N] [--quick] [--json out.json] [--md out.md] [--out dir]\n"
                 "               [--resume interrupted.json]   (results are written after every case; resume skips finished cases)\n"
                 "openshot-bench compare old.json new.json [--md out.md]\n"
                 "openshot-bench --list\n";
}

} // namespace

int main(int argc, char** argv) {
    setenv("TZ", "UTC", 1);
    Opts o;
    bool list = false, quick = false, compareMode = false;
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { if (i + 1 >= argc) { usage(); std::exit(2); } return argv[++i]; };
        if (a == "--label") o.label = next();
        else if (a == "--frames") o.frames = std::stoi(next());
        else if (a == "--res") o.res = split(next(), ',');
        else if (a == "--modes") o.modes = split(next(), ',');
        else if (a == "--scenario") o.scenarioFilter = next();
        else if (a == "--threads") o.threads = std::stoi(next());
        else if (a == "--parallel") o.parallel = std::max(1, std::stoi(next()));
        else if (a == "--json") o.jsonOut = next();
        else if (a == "--resume") o.resumeFrom = next();
        else if (a == "--md") o.mdOut = next();
        else if (a == "--out") o.outDir = next();
        else if (a == "--media") o.goldenMedia = next();
        else if (a == "--bench-media") o.benchMedia = next();
        else if (a == "--case") o.caseSpec = next();
        else if (a == "--quick") quick = true;
        else if (a == "--list") list = true;
        else if (a == "compare") compareMode = true;
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else positional.push_back(a);
    }
    if (!o.goldenMedia.empty() && o.goldenMedia.back() != '/') o.goldenMedia += '/';
    if (!o.benchMedia.empty() && o.benchMedia.back() != '/') o.benchMedia += '/';

    if (compareMode) {
        if (positional.size() != 2) { usage(); return 2; }
        const std::string md = compareMarkdown(loadJson(positional[0]), loadJson(positional[1]));
        std::cout << md;
        if (!o.mdOut.empty()) { std::ofstream(o.mdOut) << md; }
        return 0;
    }
    if (!o.caseSpec.empty()) return runCase(o);
    if (list) { for (const auto& s : bench::scenarios()) std::cout << s.name << "  " << s.description << "\n"; return 0; }
    if (!std::filesystem::exists(o.benchMedia + "v1080_a.mp4")) {
        std::cerr << "bench media missing; run tests/bench/media/generate.sh\n"; return 2;
    }
    if (quick) { o.res = {"1080p"}; o.frames = std::min(o.frames, 60); }

    // resolve names like "1080p" or "1080"
    std::vector<Res> resList;
    for (const auto& r : o.res) for (const auto& k : kAllRes) if (k.name == r || k.name == r + "p") resList.push_back(k);

    Json::Value run;
    run["label"] = o.label; run["date"] = isoNow(); run["frames"] = o.frames; run["parallel"] = o.parallel; run["machine"] = machineInfo(o);
    Json::Value cases(Json::arrayValue);
    if (!o.resumeFrom.empty() && std::filesystem::exists(o.resumeFrom)) {
        const Json::Value prev = loadJson(o.resumeFrom);
        for (const auto& c : prev["cases"]) if (c["ok"].asBool()) cases.append(c);
        std::cout << "resuming: " << cases.size() << " finished cases kept from " << o.resumeFrom << "\n";
    }
    auto alreadyDone = [&](const std::string& sc, const std::string& res, const std::string& mode) {
        for (const auto& c : cases) if (c["scenario"] == sc && c["res"] == res && c["mode"] == mode) return true;
        return false;
    };
    std::string jsonPath = o.jsonOut;
    if (jsonPath.empty()) { std::filesystem::create_directories(BENCH_RESULTS_DIR); jsonPath = std::string(BENCH_RESULTS_DIR) + "/" + nowStamp() + "_" + o.label + ".json"; }
    std::filesystem::create_directories(std::filesystem::path(jsonPath).parent_path());
    auto flush = [&]() {
        run["cases"] = cases;
        Json::StreamWriterBuilder wb; wb["indentation"] = "  ";
        std::ofstream(jsonPath) << Json::writeString(wb, run);
    };
    int total = 0, done = 0;
    for (const auto& s : bench::scenarios()) if (o.scenarioFilter.empty() || s.name.find(o.scenarioFilter) != std::string::npos) total += int(resList.size() * o.modes.size());
    const auto tRun0 = std::chrono::steady_clock::now();
    for (const auto& s : bench::scenarios()) {
        if (!o.scenarioFilter.empty() && s.name.find(o.scenarioFilter) == std::string::npos) continue;
        for (const auto& r : resList) {
            for (const auto& mode : o.modes) {
                ++done;
                if (alreadyDone(s.name, r.name, mode)) { std::printf("[%3d/%3d] %-22s %-6s %-7s (kept from resume)\n", done, total, s.name.c_str(), r.name.c_str(), mode.c_str()); continue; }
                const CaseResult cr = spawnCase(o, s.name, r, mode, argv[0]);
                Json::Value c;
                c["scenario"] = cr.scenario; c["description"] = s.description; c["res"] = cr.res; c["width"] = cr.w; c["height"] = cr.h; c["mode"] = cr.mode;
                c["ok"] = cr.ok; c["error"] = cr.error; c["frames"] = cr.frames; c["wall_s"] = cr.wall; c["fps"] = cr.fps;
                c["avg_ms"] = cr.avgMs; c["p50_ms"] = cr.p50Ms; c["p95_ms"] = cr.p95Ms; c["max_ms"] = cr.maxMs; c["build_ms"] = cr.buildMs;
                c["cpu_seconds"] = cr.cpuSeconds; c["cpu_cores"] = cr.cpuCores; c["max_rss_kb"] = static_cast<Json::Int64>(cr.maxRssKb);
                c["parallel"] = cr.parallel; c["agg_fps"] = cr.aggFps;
                cases.append(c);
                flush();
                std::printf("[%3d/%3d] %-22s %-6s %-7s %s\n", done, total, cr.scenario.c_str(), cr.res.c_str(), cr.mode.c_str(),
                            cr.ok ? (fmt(cr.fps, 1) + " fps" + (cr.parallel > 1 ? "/proc (agg " + fmt(cr.aggFps, 1) + ")" : "") + "  " + fmt(cr.cpuCores, 1) + " cores  " + fmt(cr.maxRssKb / 1048576.0, 2) + " GB" +
                                     (mode == "render" ? "  p95 " + fmt(cr.p95Ms, 1) + " ms" : "")).c_str()
                                  : ("FAILED " + cr.error).c_str());
                std::fflush(stdout);
            }
        }
    }
    run["total_seconds"] = std::chrono::duration<double>(std::chrono::steady_clock::now() - tRun0).count();
    flush();
    std::cout << "results: " << jsonPath << "\n";
    if (!o.mdOut.empty()) { std::ofstream(o.mdOut) << markdown(run); std::cout << "markdown: " << o.mdOut << "\n"; }
    return 0;
}
