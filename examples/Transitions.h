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
    auto start = std::chrono::high_resolution_clock::now();
    panDiagonalTransition();
    auto end = std::chrono::high_resolution_clock::now();
    std::cout << "panDiagonalTransition completed: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms." << std::endl;

    start = std::chrono::high_resolution_clock::now();
    panHorizontal();
    end = std::chrono::high_resolution_clock::now();
    std::cout << "panHorizontal completed: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms." << std::endl;

    start = std::chrono::high_resolution_clock::now();
    blurTransition();
    end = std::chrono::high_resolution_clock::now();
    std::cout << "blurTransition completed: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms." << std::endl;

    start = std::chrono::high_resolution_clock::now();
    blurVerticalTransition();
    end = std::chrono::high_resolution_clock::now();
    std::cout << "blurVerticalTransition completed: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms." << std::endl;

    start = std::chrono::high_resolution_clock::now();
    rotateTransition();
    end = std::chrono::high_resolution_clock::now();
    std::cout << "rotateTransition completed: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms." << std::endl;

    start = std::chrono::high_resolution_clock::now();
    wooshTransition();
    end = std::chrono::high_resolution_clock::now();
    std::cout << "wooshTransition completed: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms." << std::endl;

    start = std::chrono::high_resolution_clock::now();
    dissolveTransition();
    end = std::chrono::high_resolution_clock::now();
    std::cout << "dissolveTransition completed: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms." << std::endl;

    start = std::chrono::high_resolution_clock::now();
    dissolveBlurTransition();
    end = std::chrono::high_resolution_clock::now();
    std::cout << "dissolveBlurTransition completed: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms." << std::endl;

    start = std::chrono::high_resolution_clock::now();
    circleTransition();
    end = std::chrono::high_resolution_clock::now();
    std::cout << "circleTransition completed: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms." << std::endl;

    start = std::chrono::high_resolution_clock::now();
    barnDoorsTransition();
    end = std::chrono::high_resolution_clock::now();
    std::cout << "barnDoorsTransition completed: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms." << std::endl;

    start = std::chrono::high_resolution_clock::now();
    contrastTransition();
    end = std::chrono::high_resolution_clock::now();
    std::cout << "contrastTransition completed: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms." << std::endl;

    start = std::chrono::high_resolution_clock::now();
    verticalSplitTransition();
    end = std::chrono::high_resolution_clock::now();
    std::cout << "verticalSplitTransition completed: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms." << std::endl;

    start = std::chrono::high_resolution_clock::now();
    zoomInTransition();
    end = std::chrono::high_resolution_clock::now();
    std::cout << "zoomInTransition completed: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms." << std::endl;

    start = std::chrono::high_resolution_clock::now();
    lightLeakTransition();
    end = std::chrono::high_resolution_clock::now();
    std::cout << "lightLeakTransition completed: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms." << std::endl;

    start = std::chrono::high_resolution_clock::now();
    lightFootageTransition();
    end = std::chrono::high_resolution_clock::now();
    std::cout << "lightFootageTransition completed: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms." << std::endl;

    start = std::chrono::high_resolution_clock::now();
    glitchTransition();
    end = std::chrono::high_resolution_clock::now();
    std::cout << "glitchTransition completed: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms." << std::endl;
}