#pragma once

#include <string>

void panBottomRightTransition(const std::string& file1 = "img/Image-13.jpg", const std::string& file2 = "img/Image-12.jpg",
                              float transitionDuration = 0.5, const std::string& output = "./final/panBottomRightTransition.mp4");

void panLeftTransition(const std::string& file1 = "img/Image-13.jpg", const std::string& file2 = "img/Image-12.jpg",
                       float transitionDuration = 0.5, const std::string& output = "./final/panLeftTransition.mp4");

void blurTransition(const std::string& file1 = "img/Image-18.jpg", const std::string& file2 = "img/Image-19.jpg",
                    float transitionDuration = 0.5, const std::string& output = "./final/blurTransition.mp4");

void verticalBlurTransition(const std::string& file1 = "img/Image-18.jpg", const std::string& file2 = "img/Image-19.jpg",
                            float transitionDuration = 0.5, const std::string& output = "./final/verticalBlurTransition.mp4");

void rotationalBlurTransition(const std::string& file1 = "img/Image-17.jpg", const std::string& file2 = "img/Image-16.jpg",
                              float transitionDuration = 0.3, const std::string& output = "./final/rotationalBlurTransition.mp4");

void wooshTransition(const std::string& file1 = "img/Image-18.jpg", const std::string& file2 = "img/Image-19.jpg",
                      float transitionDuration = 1, const std::string& output = "./final/whooshTransition.mp4");

void dissolveTransition(const std::string& file1 = "img/Image-4.jpg", const std::string& file2 = "img/Image-5.jpg",
                        float transitionDuration = 0.5, const std::string& output = "./final/dissolveTransition.mp4");

void dissolveBlurTransition(const std::string& file1 = "img/Image-4.jpg", const std::string& file2 = "img/Image-5.jpg",
                            float transitionDuration = 0.5, const std::string& output = "./final/dissolveBlurTransition.mp4");

void circleOutTransition(const std::string& file1 = "img/Image-14.jpg",
                         float transitionDuration = 1, const std::string& output = "./final/circleOutTransition.mp4");

void circleInTransition(const std::string& file1 = "img/Image-14.jpg",
                        float transitionDuration = 1, const std::string& output = "./final/circleInTransition.mp4");

void barnDoorsTransition(const std::string& file1 = "img/Image-14.jpg", const std::string& file2 = "img/Image-15.jpg",
                         float transitionDuration = 0.5, const std::string& output = "./final/barnDoorsTransition.mp4");

void zoomInTransition(const std::string& file1 = "img/Image-1.jpg", const std::string& file2 = "img/Image.jpg",
                      float transitionDuration = 0.5, const std::string& output = "./final/zoomInTransition.mp4");

void verticalSplitTransition(const std::string& file1 = "img/Image-6.jpg", const std::string& file2 = "img/Image-7.jpg",
                            float transitionDuration = 0.5, const std::string& output = "./final/verticalSplitTransition.mp4");