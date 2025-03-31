#pragma once

#include <string>
#include <chrono>
#include <iostream>

void panDiagonalTransition(const std::string& file1 = "1.mp4", const std::string& file2 = "2.mp4",
    float transitionDuration = 1, const std::string& output = "./out.mp4");

void panHorizontal(const std::string& file1 = "1.mp4", const std::string& file2 = "2.mp4",
    float transitionDuration = 1, const std::string& output = "./out.mp4");


void blurTransition(const std::string& file1 = "1.mp4", const std::string& file2 = "2.mp4",
    float transitionDuration = 1, const std::string& output = "./out.mp4");


void blurVerticalTransition(const std::string& file1 = "1.mp4", const std::string& file2 = "2.mp4",
    float transitionDuration = 1, const std::string& output = "./out.mp4");

void rotateTransition(const std::string& file1 = "1.mp4", const std::string& file2 = "2.mp4",
    float transitionDuration = 1, const std::string& output = "./out.mp4");

void wooshTransition(const std::string& file1 = "1.mp4", const std::string& file2 = "2.mp4",
    float transitionDuration = 1, const std::string& output = "./out.mp4");


void dissolveTransition(const std::string& file1 = "1.mp4", const std::string& file2 = "2.mp4",
    float transitionDuration = 1, const std::string& output = "./out.mp4");

void dissolveBlurTransition(const std::string& file1 = "1.mp4", const std::string& file2 = "2.mp4",
    float transitionDuration = 1, const std::string& output = "./out.mp4");

void circleTransition(const std::string& file1 = "1.mp4", const std::string& file2 = "2.mp4",
    float transitionDuration = 1, const std::string& output = "./out.mp4");

void fadeOutInTransition(const std::string& file1 = "1.mp4", const std::string& file2 = "2.mp4",
    float transitionDuration = 1, const std::string& output = "./out.mp4");

void barnDoorsTransition(const std::string& file1 = "1.mp4", const std::string& file2 = "2.mp4",
    float transitionDuration = 1, const std::string& output = "./out.mp4");

void verticalSplitTransition(const std::string& file1 = "1.mp4", const std::string& file2 = "2.mp4",
    float transitionDuration = 1, const std::string& output = "./out.mp4");

void horizontalSplitTransition(const std::string& file1 = "1.mp4", const std::string& file2 = "2.mp4",
    float transitionDuration = 1, const std::string& output = "./out.mp4");

void zoomInTransition(const std::string& file1 = "1.mp4", const std::string& file2 = "2.mp4",
    float transitionDuration = 1, const std::string& output = "./out.mp4");

void contrastTransition(const std::string& file1 = "1.mp4", const std::string& file2 = "2.mp4",
    float transitionDuration = 1, const std::string& output = "./out.mp4");

void lightLeakTransition(const std::string& file1 = "1.mp4", const std::string& file2 = "2.mp4",
    float transitionDuration = 1, const std::string& output = "./out.mp4");

void lightFootageTransition(const std::string& file1 = "1.mp4", const std::string& file2 = "2.mp4",
    float transitionDuration = 1, const std::string& output = "./out.mp4");

void glitchTransition(const std::string& file1 = "1.mp4", const std::string& file2 = "2.mp4",
    float transitionDuration = 1, const std::string& output = "./out.mp4");

inline void runTransitions() {
    const auto start = std::chrono::high_resolution_clock::now();

    // panDiagonalTransition();
    // panHorizontal();
    // blurTransition();
    // blurVerticalTransition();
    rotateTransition();
    // wooshTransition();
    // dissolveTransition();
    // dissolveBlurTransition();
    // circleTransition();
    // barnDoorsTransition();
    // contrastTransition();
    // verticalSplitTransition();
    // zoomInTransition();
    // lightLeakTransition();
    // lightFootageTransition();
    // glitchTransition();

    std::cout << "Completed: " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start).count() << " sec." << std::endl;
}