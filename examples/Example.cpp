#include "Transitions.h"
#include <chrono>
#include <iostream>

int main() {
    using namespace std::chrono;
    auto start = high_resolution_clock::now();

//    panBottomRightTransition();
//    panLeftTransition();
//    blurTransition();
//    verticalBlurTransition();
//    rotationalBlurTransition();
//    wooshTransition();
//    dissolveTransition();
//    dissolveBlurTransition();
//    circleOutTransition();
//    circleInTransition();
//    barnDoorsTransition();
//    verticalSplitTransition();
//    zoomInTransition();
//    brightnessTransition();
//    brightnessFootageTransition();

    auto end = high_resolution_clock::now();
    std::cout << "Completed: " << duration_cast<seconds>(end - start).count() << " sec." << std::endl;

    return 0;
}
