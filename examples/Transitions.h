#pragma once

#include <string>
#include <chrono>
#include <iostream>

void panDiagonalTransition(const std::string& file1 = "clips/clip-13.mp4", const std::string& file2 = "clips/clip-12.mp4",
                              float transitionDuration = 0.5, const std::string& output = "./final/panDiagonalTransition.mp4");

void panHorizontal(const std::string& file1 = "clips/clip-13.mp4", const std::string& file2 = "clips/clip-12.mp4",
                       float transitionDuration = 0.5, const std::string& output = "./final/panHorizontal.mp4");

void blurTransition(const std::string& file1 = "clips/clip-18.mp4", const std::string& file2 = "clips/clip-19.mp4",
                    float transitionDuration = 0.5 , const std::string& output = "./final/blurTransition.mp4");

void blurVerticalTransition(const std::string& file1 = "clips/clip-18.mp4", const std::string& file2 = "clips/clip-19.mp4",
                            float transitionDuration = 0.5, const std::string& output = "./final/blurVerticalTransition.mp4");

void rotateTransition(const std::string& file1 = "clips/clip-17.mp4", const std::string& file2 = "clips/clip-16.mp4",
                              float transitionDuration = 0.5, const std::string& output = "./final/rotateTransition.mp4");

void wooshTransition(const std::string& file1 = "clips/clip-18.mp4", const std::string& file2 = "clips/clip-19.mp4",
                     float transitionDuration = 0.5, const std::string& output = "./final/whooshTransition.mp4");

void dissolveTransition(const std::string& file1 = "clips/clip-4.mp4", const std::string& file2 = "clips/clip-5.mp4",
                        float transitionDuration = 0.5, const std::string& output = "./final/dissolveTransition.mp4");

void dissolveBlurTransition(const std::string& file1 = "clips/clip-4.mp4", const std::string& file2 = "clips/clip-5.mp4",
                            float transitionDuration = 0.5, const std::string& output = "./final/dissolveBlurTransition.mp4");

void circleTransition(const std::string& file1 = "clips/clip-14.mp4", const std::string& file2 = "clips/clip-14.mp4",
                         float transitionDuration = 0.5, const std::string& output = "./circleTransition.mp4");

void fadeOutInTransition(const std::string& file1 = "clips/clip-14.mp4", const std::string& file2 = "clips/clip-14.mp4",
                         float transitionDuration = 0.5, const std::string& output = "./final/circleOutTransition.mp4");

void barnDoorsTransition(const std::string& file1 = "clips/clip-14.mp4", const std::string& file2 = "clips/clip-15.mp4",
                         float transitionDuration = 0.5, const std::string& output = "./final/barnDoorsTransition.mp4");

void verticalSplitTransition(const std::string& file1 = "clips/clip-6.mp4", const std::string& file2 = "clips/clip-7.mp4",
                            float transitionDuration = 0.5, const std::string& output = "./final/splitTransition.mp4");

void horizontalSplitTransition(const std::string& file1 = "clips/clip-6.mp4", const std::string& file2 = "clips/clip-7.mp4",
                            float transitionDuration = 0.5, const std::string& output = "./final/splitTransition.mp4");

void zoomInTransition(const std::string& file1 = "clips/clip-1.mp4", const std::string& file2 = "clips/clip-0.mp4",
                      float transitionDuration = 0.5, const std::string& output = "./final/zoomInTransition.mp4");

void contrastTransition(const std::string& file1 = "clips/clip-14.mp4", const std::string& file2 = "clips/clip-15.mp4",
                        float transitionDuration = 0.5, const std::string& output = "./final/contrastTransition.mp4");

void lightLeakTransition(const std::string& file1 = "clips/clip-2.mp4", const std::string& file2 = "clips/clip-3.mp4",
                          float transitionDuration = 0.5, const std::string& output = "./final/lightLeakTransition.mp4");

void lightFootageTransition(const std::string& file1 = "clips/clip-2.mp4", const std::string& file2 = "clips/clip-3.mp4",
                          float transitionDuration = 0.5, const std::string& output = "./final/lightFootageTransition.mp4");

void glitchTransition(const std::string& file1 = "clips/glitch1.mp4", const std::string& file2 = "clips/glitch2.mp4",
                      float transitionDuration = 0.5, const std::string& output = "./final/glitchTransition.mp4");

inline void runTransitions() {
    using namespace std::chrono;
    const auto start = high_resolution_clock::now();
       // panDiagonalTransition("input/1.mp4", "input/2.mp4", 0.8);
       // panHorizontal("input/1.mp4", "input/2.mp4", 0.8);
       // blurTransition("input/1.mp4", "input/2.mp4", 0.8);
       // blurVerticalTransition("input/1.mp4", "input/2.mp4", 0.8);
       // rotationalBlurTransition("./ForRender/Rotate/1.mp4", "./ForRender/Rotate/2.mp4", 2, "./ForRender/rotateTransition.mp4");
       // wooshTransition("input/1.mp4", "input/2.mp4", 0.8);
       // dissolveTransition("input/1.mp4", "input/2.mp4", 0.8);
       // dissolveBlurTransition("input/1.mp4", "input/2.mp4", 0.8);
       circleTransition("1.mp4", "2.mp4", 1);
       // barnDoorsTransition("input/1.mp4", "input/2.mp4", 0.8);
       // contrastTransition("input/1.mp4", "input/2.mp4", 0.8);
       // verticalSplitTransition();
       // zoomInTransition("input/1.mp4", "input/2.mp4", 0.8);
       // brightnessTransition("./ForRender/Flash(Light)/1.mp4", "./ForRender/Flash(Light)/2.mp4", 1, "./ForRender/Flash(Light)/out.mp4");
       // brightnessFootageTransition("./ForRender/Flash(Light)/1.mp4", "./ForRender/Flash(Light)/2.mp4", 0.8);
       // glitchTransition("input/1.mp4", "input/2.mp4", 0.8);
    auto end = high_resolution_clock::now();
    std::cout << "Completed: " << duration_cast<seconds>(end - start).count() << " sec." << std::endl;
}