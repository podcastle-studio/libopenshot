// Standalone harness to reproduce/verify subtitle container-background vertical centring.
// Renders a subtitles JSON (settings + segments) onto a dark canvas at a given frame → PNG.
//   openshot-subtitle-payload <subs.json> <frame> <out.png>
#include "subtitle/SubtitleManager.h"

#include <QColor>
#include <QImage>

#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 4) { std::cerr << "usage: <subs.json> <frame> <out.png>\n"; return 1; }
    const std::string jsonPath = argv[1];
    const int64_t frame = std::stoll(argv[2]);
    const std::string outPng = argv[3];
    constexpr int W = 1080, H = 1920;
    constexpr float fps = 30.0f;

    std::ifstream in(jsonPath);
    std::stringstream ss; ss << in.rdbuf();

    openshot::subtitle::SubtitleManager mgr(fps);
    mgr.loadFromJSONString(ss.str());

    auto img = std::make_shared<QImage>(W, H, QImage::Format_RGBA8888_Premultiplied);
    img->fill(QColor(34, 35, 38, 255));   // #222326

    mgr.renderAtFrame(img, frame);
    img->save(QString::fromStdString(outPng), "PNG");
    std::cout << "Wrote " << outPng << " (frame " << frame << ")\n";
    return 0;
}
