#include "Harness.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace golden {

namespace {

std::string rel(const std::string& path, const std::string& dir) {
    std::error_code ec;
    auto r = std::filesystem::relative(path, dir, ec);
    return ec ? path : r.generic_string();
}

std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '&': o += "&amp;"; break;
            default: o += c;
        }
    }
    return o;
}

std::string num(double v, int prec) {
    if (std::isinf(v)) return "inf";
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.*f", prec, v);
    return buf;
}

} // namespace

void writeHtmlReport(const std::string& dir, const RunSummary& summary, const Options& opts) {
    std::filesystem::create_directories(dir);
    std::vector<const FrameResult*> rows;
    for (const auto& f : summary.frames) rows.push_back(&f);
    std::stable_sort(rows.begin(), rows.end(), [](const FrameResult* a, const FrameResult* b) {
        return !a->pass && b->pass;
    });

    std::ofstream html(dir + "/index.html");
    html << "<!doctype html><meta charset=utf-8><title>libopenshot golden frames</title>\n"
            "<style>body{font:14px system-ui,sans-serif;margin:24px;background:#f5f6f8;color:#222}"
            "table{border-collapse:collapse;width:100%}th,td{padding:6px 10px;border-bottom:1px solid #ddd;text-align:left;vertical-align:top}"
            "th{font-size:12px;text-transform:uppercase;letter-spacing:.05em;color:#666}"
            ".pass{color:#1a7f37;font-weight:600}.fail{color:#b42318;font-weight:600}"
            "tr.img td{background:#fff}img{max-width:100%;border:1px solid #ccc}"
            ".num{font-variant-numeric:tabular-nums;text-align:right}"
            ".cap{display:flex;gap:8px;font-size:12px;color:#666;margin-bottom:4px}.cap span{flex:1;text-align:center}"
            "</style>\n";
    html << "<h1>libopenshot golden frames</h1>";
    html << "<p>" << summary.scenariosRun << " scenarios, " << summary.frames.size() << " frames, "
         << summary.checks.size() << " checks, <b class='" << (summary.failures ? "fail" : "pass") << "'>"
         << summary.failures << " failure(s)</b>, " << num(summary.seconds, 1) << " s"
         << (opts.filter.empty() ? "" : ", filter: <code>" + esc(opts.filter) + "</code>")
         << (opts.update ? ", <b>goldens updated</b>" : "") << "</p>\n";

    if (!summary.checks.empty()) {
        html << "<h2>Checks</h2><table><tr><th>scenario</th><th>check</th><th>status</th><th>message</th></tr>\n";
        for (const auto& [scn, c] : summary.checks)
            html << "<tr><td>" << esc(scn) << "</td><td>" << esc(c.name) << "</td><td class='" << (c.ok ? "pass'>PASS" : "fail'>FAIL")
                 << "</td><td>" << esc(c.message) << "</td></tr>\n";
        html << "</table>\n";
    }

    html << "<h2>Frames</h2><table><tr><th>scenario</th><th>frame</th><th class=num>PSNR dB</th><th class=num>SSIM</th>"
            "<th class=num>max |d|</th><th class=num>% px &gt; 2</th><th>status</th><th>note</th></tr>\n";
    for (const FrameResult* r : rows) {
        html << "<tr><td>" << esc(r->scenario) << "</td><td>" << esc(r->label) << "</td>";
        if (r->message.empty() || r->pass) {
            html << "<td class=num>" << num(r->metrics.psnr, 2) << "</td><td class=num>" << num(r->metrics.ssim, 4)
                 << "</td><td class=num>" << r->metrics.maxAbs << "</td><td class=num>" << num(r->metrics.pctOver2, 2) << "</td>";
        } else {
            html << "<td colspan=4></td>";
        }
        html << "<td class='" << (r->pass ? "pass'>PASS" : "fail'>FAIL") << "</td><td>" << esc(r->message) << "</td></tr>\n";
        if (!r->triptychPath.empty()) {
            html << "<tr class=img><td colspan=8><div class=cap><span>golden: " << esc(rel(r->goldenPath, dir))
                 << "</span><span>actual: " << esc(rel(r->actualPath, dir)) << "</span><span>diff (x8)</span></div>"
                 << "<a href='" << esc(rel(r->triptychPath, dir)) << "'><img loading=lazy src='" << esc(rel(r->triptychPath, dir))
                 << "'></a></td></tr>\n";
        }
    }
    html << "</table>\n";
}

} // namespace golden
