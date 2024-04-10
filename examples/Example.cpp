#include "Transitions.h"
#include <chrono>
#include <iostream>
#include "Magick++.h"
#include <opencv2/opencv.hpp>


int main() {
    using namespace std::chrono;
    auto start = high_resolution_clock::now();

    panBottomRightTransition();
//    panLeftTransition();
//    blurTransition();
//    verticalBlurTransition();
//    rotationalBlurTransition();
//    zoomInTransition();
//    dissolveTransition();
//    dissolveBlurTransition();
//    circleOutTransition();
//    circleInTransition();
//    barnDoorsTransition();
//    verticalSplitTransition();
//    wooshTransition();

    auto end = high_resolution_clock::now();
    std::cout << "Completed: " << duration_cast<seconds>(end - start).count() << " sec." << std::endl;

    return 0;
}
