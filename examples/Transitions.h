#pragma once

#include <string>

void panBottomRightTransition(const std::string& file1 = "clips/clip-13.mp4", const std::string& file2 = "clips/clip-12.mp4",
                              float transitionDuration = 0.5, const std::string& output = "./final/panBottomRightTransition.mp4");

void panLeftTransition(const std::string& file1 = "clips/clip-13.mp4", const std::string& file2 = "clips/clip-12.mp4",
                       float transitionDuration = 0.5, const std::string& output = "./final/panLeftTransition.mp4");

void blurTransition(const std::string& file1 = "clips/clip-18.mp4", const std::string& file2 = "clips/clip-19.mp4",
                    float transitionDuration = 0.5 , const std::string& output = "./final/blurTransition.mp4");

void verticalBlurTransition(const std::string& file1 = "clips/clip-18.mp4", const std::string& file2 = "clips/clip-19.mp4",
                            float transitionDuration = 0.5, const std::string& output = "./final/verticalBlurTransition.mp4");

void rotationalBlurTransition(const std::string& file1 = "clips/clip-17.mp4", const std::string& file2 = "clips/clip-16.mp4",
                              float transitionDuration = 0.3, const std::string& output = "./final/rotationalBlurTransition.mp4");

void wooshTransition(const std::string& file1 = "clips/clip-18.mp4", const std::string& file2 = "clips/clip-19.mp4",
                     float transitionDuration = 0.6, const std::string& output = "./final/whooshTransition.mp4");

void dissolveTransition(const std::string& file1 = "clips/clip-4.mp4", const std::string& file2 = "clips/clip-5.mp4",
                        float transitionDuration = 0.5, const std::string& output = "./final/dissolveTransition.mp4");

void dissolveBlurTransition(const std::string& file1 = "clips/clip-4.mp4", const std::string& file2 = "clips/clip-5.mp4",
                            float transitionDuration = 0.5, const std::string& output = "./final/dissolveBlurTransition.mp4");

void circleOutTransition(const std::string& file1 = "clips/clip-14.mp4",
                         float transitionDuration = 1, const std::string& output = "./final/circleOutTransition.mp4");

void circleInTransition(const std::string& file1 = "clips/clip-14.mp4",
                        float transitionDuration = 1, const std::string& output = "./final/circleInTransition.mp4");

void barnDoorsTransition(const std::string& file1 = "clips/clip-14.mp4", const std::string& file2 = "clips/clip-15.mp4",
                         float transitionDuration = 0.5, const std::string& output = "./final/barnDoorsTransition.mp4");

void verticalSplitTransition(const std::string& file1 = "clips/clip-6.mp4", const std::string& file2 = "clips/clip-7.mp4",
                            float transitionDuration = 0.5, const std::string& output = "./final/verticalSplitTransition.mp4");

void zoomInTransition(const std::string& file1 = "clips/clip-1.mp4", const std::string& file2 = "clips/clip-0.mp4",
                      float transitionDuration = 0.5, const std::string& output = "./final/zoomInTransition.mp4");

void contrastTransition(const std::string& file1 = "clips/clip-14.mp4", const std::string& file2 = "clips/clip-15.mp4",
                        float transitionDuration = 0.5, const std::string& output = "./final/contrastTransition.mp4");

void brightnessTransition(const std::string& file1 = "clips/clip-2.mp4", const std::string& file2 = "clips/clip-3.mp4",
                          float transitionDuration = 0.5, const std::string& output = "./final/brightnessTransition.mp4");

void brightnessFootageTransition(const std::string& file1 = "clips/clip-2.mp4", const std::string& file2 = "clips/clip-3.mp4",
                          float transitionDuration = 0.5, const std::string& output = "./final/brightnessFootageTransition.mp4");

void glitchTransition(const std::string& file1 = "clips/glitch1.mp4", const std::string& file2 = "clips/glitch2.mp4",
                      float transitionDuration = 1, const std::string& output = "./final/glitchTransition.mp4");