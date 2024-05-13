#include "Transitions.h"
#include <chrono>
#include <iostream>

int main() {
    using namespace std::chrono;
    auto start = high_resolution_clock::now();
//    panBottomRightTransition("input/1.mp4", "input/2.mp4", 0.8);
//    panLeftTransition("input/1.mp4", "input/2.mp4", 0.8);
//    blurTransition("input/1.mp4", "input/2.mp4", 0.8);
//    verticalBlurTransition("input/1.mp4", "input/2.mp4", 0.8);
//    rotationalBlurTransition("input/1.mp4", "input/2.mp4", 0.8);
//    wooshTransition("input/1.mp4", "input/2.mp4", 0.8);
//    dissolveTransition("input/1.mp4", "input/2.mp4", 0.8);
//    dissolveBlurTransition("input/1.mp4", "input/2.mp4", 0.8);
//    circleOutTransition("input/1.mp4", 0.8);
//    circleInTransition("input/1.mp4", 0.8);
//    barnDoorsTransition("input/1.mp4", "input/2.mp4", 0.8);
//    contrastTransition("input/1.mp4", "input/2.mp4", 0.8);
//    verticalSplitTransition("input/1.mp4", "input/2.mp4", 0.8);
//    zoomInTransition("input/1.mp4", "input/2.mp4", 0.8);
//    brightnessTransition("input/1.mp4", "input/2.mp4", 0.8);
//    brightnessFootageTransition("input/1.mp4", "input/2.mp4", 0.8);
//    glitchTransition("input/1.mp4", "input/2.mp4", 0.8);
    auto end = high_resolution_clock::now();
    std::cout << "Completed: " << duration_cast<seconds>(end - start).count() << " sec." << std::endl;

    return 0;
}
