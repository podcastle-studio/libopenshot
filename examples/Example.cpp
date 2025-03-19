#include "Transitions.h"
#include <FFmpegReader.h>
#include <FFmpegWriter.h>
#include <FFmpegWriter.h>
#include <QtImageReader.h>
#include <Timeline.h>

struct Size {
    Size(int _width, int _height) : width(_width), height(_height) {};
    int width;
    int height;
};

float calculateFitRatio(const Size& sourceSize, const Size& targetSize) {
    const float withRatio = static_cast<float>(targetSize.width) / sourceSize.width;
    const float heightRatio = static_cast<float>(targetSize.height) / sourceSize.height;
    return std::min(withRatio, heightRatio);
}

void renderingTest() {
    int width = 3840;
    int height = 2160;
    int fps = 30;
    int aSampleRate = 48000;
    int aNumChannels = 2;
    openshot::Timeline timeline(width, height, openshot::Fraction(fps, 1), aSampleRate, aNumChannels, openshot::ChannelLayout::LAYOUT_STEREO);
    timeline.Open();

    auto* reader = new openshot::FFmpegReader("in.mp4");
    auto clipPtr = new openshot::Clip(reader);
    clipPtr->Position(0);
    clipPtr->Start(0);
    clipPtr->Layer(1);
    timeline.AddClip(clipPtr);

    openshot::FFmpegWriter ffWriter("out.mp4");
    ffWriter.SetAudioOptions("aac", aSampleRate, 128000);
    ffWriter.SetVideoOptions("libx264", width, height, openshot::Fraction(fps, 1), 8000000);
    ffWriter.Open();
    ffWriter.WriteFrame(&timeline, 0, 10 * 30);

    ffWriter.Close();
    timeline.ClearAllCache(true);
    timeline.Close();
}

void renderingTest2() {
    int width = 1280;
    int height = 720;
    int fps = 30;
    int aSampleRate = 48000;
    int aNumChannels = 2;
    openshot::Timeline timeline(width, height, openshot::Fraction(fps, 1), aSampleRate, aNumChannels, openshot::ChannelLayout::LAYOUT_STEREO);
    timeline.Open();

    std::vector<openshot::ReaderBase*> readers;
    std::vector<openshot::Clip*> clipPtrs;
    openshot::FFmpegReader* reader = new openshot::FFmpegReader("lrv.mp4");
    auto clipPtr = new openshot::Clip(reader);
    clipPtr->Position(1708.15);
    clipPtr->Start(1946.049);
    clipPtr->End(2403.1333);
    clipPtr->Layer(1);
    clipPtr->origin_x = 0;
    clipPtr->alpha = 1;
    clipPtr->origin_y = 0;
    clipPtr->rotation = 0;
    clipPtr->gravity = openshot::GRAVITY_TOP_LEFT;
    clipPtr->location_x = -0.019999999;
    clipPtr->location_y = -0.019999999;
    clipPtr->scale = openshot::SCALE_NONE;
    clipPtr->scale_x = 1.03;
    clipPtr->scale_y = 1.03;
    timeline.AddClip(clipPtr, true);
    clipPtrs.emplace_back(clipPtr);
    readers.emplace_back(reader);
    timeline.sort_clips();
    openshot::FFmpegWriter ffWriter("out.mp4");
    ffWriter.SetAudioOptions("aac", aSampleRate, 128000);
    ffWriter.SetVideoOptions("libx264", width, height, openshot::Fraction(fps, 1), 8000000);
    ffWriter.Open();
    // ffWriter.WriteFrame(&timeline, 64950, 64957/* duration * fps*/);
    ffWriter.WriteFrame(&timeline, 64930, 64957/* duration * fps*/);

    ffWriter.Close();
    timeline.Close();

}

int main() {

    // using namespace std::chrono;
    // const auto start = high_resolution_clock::now();

    runTransitions();

    // const auto end = high_resolution_clock::now();
    // std::cout << "Completed: " << duration_cast<seconds>(end - start).count() << " sec." << std::endl;

    return 0;
}
