/**
 * @file
 * @brief Source file for CVFrameInterpolation class
 * @author Jonathan Thomas <jonathan@openshot.org>
 * @author Brenno Caldato <brenno.caldato@outlook.com>
 *
 * @ref License
 */

/* LICENSE
 *
 * Copyright (c) 2008-2019 OpenShot Studios, LLC
 * <http://www.openshotstudios.com/>. This file is part of
 * OpenShot Library (libopenshot), an open-source project dedicated to
 * delivering high quality video editing and animation solutions to the
 * world. For more information visit <http://www.openshot.org/>.
 *
 * OpenShot Library (libopenshot) is free software: you can redistribute it
 * and/or modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * OpenShot Library (libopenshot) is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with OpenShot Library. If not, see <http://www.gnu.org/licenses/>.
 */

#include <fstream>
#include <iomanip>
#include <iostream>

#include "CVFrameInterpolation.h"
#include <google/protobuf/util/time_util.h>


using namespace std;
using namespace openshot;
using google::protobuf::util::TimeUtil;

CVFrameInterpolation::CVFrameInterpolation(std::string processInfoJson, ProcessingController &processingController)
: processingController(&processingController), processingDevice("CPU"){
    SetJson(processInfoJson);
    confThreshold = 0.5;
    nmsThreshold = 0.1;
}

void CVFrameInterpolation::setProcessingDevice(){
    if(processingDevice == "GPU"){
        net.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
        net.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
    }
    else if(processingDevice == "CPU"){
        net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    }
}

void CVFrameInterpolation::interpolateClip(openshot::Clip &video, size_t _start, size_t _end, bool process_interval)
{
    start = _start; end = _end;

    video.Open();

    if(error){
        return;
    }

    processingController->SetError(false, "");

    try {
        // Deserialize the ScriptModule from a file using torch::jit::load().
        model = torch::jit::load(modelWeights);
        if(processingDevice == "GPU")
            model.to(at::kCUDA);
    }
    catch (const c10::Error& e) {
        std::cerr << "error loading the model\n";
    }
    
    auto new_frame = std::make_shared<Frame>(
		0, 1920, 1080,
		"#000000", 0, 0);

    size_t frame_number;
    if(!process_interval || end <= 1 || end-start == 0){
        // Get total number of frames in video
        start = (int)(video.Start() * video.Reader()->info.fps.ToFloat());
        end = (int)(video.End() * video.Reader()->info.fps.ToFloat());
    }

    // Get the first and second frames
    if (end > 2) {
        cv::Mat first_frame = video.GetFrame(new_frame, 1)->GetImageCV();
        cv::Mat second_frame = video.GetFrame(new_frame, 2)->GetImageCV();

        // cv::cvtColor(first_frame, first_frame, cv::COLOR_RGB2BGR);
        // cv::cvtColor(second_frame, second_frame, cv::COLOR_RGB2BGR);

        outputs.push_back(first_frame);
        outputs.push_back(second_frame);
    }


    for (frame_number = start+4; frame_number <= end; frame_number++)
    {
         // Stop the feature tracker process
        if(processingController->ShouldStop()){
            return;
        }

        std::shared_ptr<openshot::Frame> f1 = video.GetFrame(new_frame, frame_number-3);
        std::shared_ptr<openshot::Frame> f2 = video.GetFrame(new_frame, frame_number-2);
        std::shared_ptr<openshot::Frame> f3 = video.GetFrame(new_frame, frame_number-1);
        std::shared_ptr<openshot::Frame> f4 = video.GetFrame(new_frame, frame_number);

        // Grab OpenCV Mat image
        cv::Mat frame1 = f1->GetImageCV();
        cv::Mat frame2 = f2->GetImageCV();
        cv::Mat frame3 = f3->GetImageCV();
        cv::Mat frame4 = f4->GetImageCV();

        // Interpolate Frames
        std::vector<cv::Mat> output_frames = interpolateFrames(frame1, frame2, frame3, frame4);
        
        // Append all interpolated frames into the outputs
        for (cv::Mat frame : output_frames) {
            outputs.push_back(frame);
            // std::cout << "OUTPUT SIZE: " << frame.size() << std::endl;
        }

        // Append the third input
        cv::cvtColor(frame3, frame3, cv::COLOR_RGB2BGR);
        outputs.push_back(frame3);


        // Update progress
        processingController->SetProgress(uint(100*(frame_number-start)/(end-start)));
    }

    cv::VideoWriter v1("./interpolate.avi", cv::VideoWriter::fourcc('M','J','P','G'), 30, 
                cv::Size(outputs[0].size().width, outputs[0].size().height), true);

    for (cv::Mat f : outputs) {
        v1.write(f);
    }
}


std::vector<cv::Mat> CVFrameInterpolation::interpolateFrames(cv::Mat frame1, cv::Mat frame2, 
                                          cv::Mat frame3, cv::Mat frame4)
{

  const int frame_width = frame1.size().width;
  const int frame_height = frame1.size().height;
  std::cout << "HEIGHT: " << frame_height << " WIDTH: " << frame_width << std::endl; 


  cv::cvtColor(frame1, frame1, cv::COLOR_BGR2RGB);
  cv::cvtColor(frame2, frame2, cv::COLOR_BGR2RGB);
  cv::cvtColor(frame3, frame3, cv::COLOR_BGR2RGB);
  cv::cvtColor(frame4, frame4, cv::COLOR_BGR2RGB);

  cv::resize(frame1, frame1, cv::Size(1280, 720), cv::INTER_LINEAR);
  cv::resize(frame2, frame2, cv::Size(1280, 720), cv::INTER_LINEAR);
  cv::resize(frame3, frame3, cv::Size(1280, 720), cv::INTER_LINEAR);
  cv::resize(frame4, frame4, cv::Size(1280, 720), cv::INTER_LINEAR);

  // Convert cv::Mat to at::Tensor
  at::Tensor t_frame1 = torch::from_blob(frame1.data, {frame1.rows, frame1.cols, 3}, at::kByte);
  at::Tensor t_frame2 = torch::from_blob(frame2.data, {frame2.rows, frame2.cols, 3}, at::kByte);
  at::Tensor t_frame3 = torch::from_blob(frame3.data, {frame3.rows, frame3.cols, 3}, at::kByte);
  at::Tensor t_frame4 = torch::from_blob(frame4.data, {frame4.rows, frame4.cols, 3}, at::kByte);
  
  // Add axis, shape is (1,3,x,y) now
  t_frame1 = t_frame1.index({torch::indexing::None, torch::indexing::Slice(0)});
  t_frame2 = t_frame2.index({torch::indexing::None, torch::indexing::Slice(0)});
  t_frame3 = t_frame3.index({torch::indexing::None, torch::indexing::Slice(0)});
  t_frame4 = t_frame4.index({torch::indexing::None, torch::indexing::Slice(0)});

  // Convert Tensor to Float
  t_frame1 = t_frame1.to(at::kHalf) / 255.0;
  t_frame2 = t_frame2.to(at::kHalf) / 255.0;
  t_frame3 = t_frame3.to(at::kHalf) / 255.0;
  t_frame4 = t_frame4.to(at::kHalf) / 255.0;
  
  auto in = torch::cat({t_frame1, t_frame2, t_frame3, t_frame4}, 0);
  if(processingDevice == "GPU")
    in = in.to(at::kCUDA);

  in = in.permute({3, 0, 1, 2});
  
  // Add batch axis, shape is (1,3,4,x,y) now
  in = in.index({
    torch::indexing::None, torch::indexing::Slice(0)});


  auto out = model.forward({in}).toTensor();
  out = out.index({-1, torch::indexing::Slice(0)});
  out = out.index({-1, torch::indexing::Slice(0)});
  out = out.permute({1, 2, 0});

  const int width = out.sizes()[1];
  const int height = out.sizes()[0];

  // out.mul(255.0)
  out = out.mul(255.0).clamp(0,255).round();
  
//   std::cout << out << std::endl;

  // Convert Tensor to uint8
  out = out.to(at::kByte); 

  if(processingDevice == "GPU")
    out = out.to(at::kCPU);

  // Convert tensor to cv image and save
  out = out.reshape({width * height * 3});
  cv::Mat img_out(cv::Size(width, height), CV_8UC3, out.data_ptr());

  cv::cvtColor(img_out, img_out, cv::COLOR_RGB2BGR);
  cv::resize(img_out, img_out, cv::Size(frame_width, frame_height), cv::INTER_LINEAR);

  // std::cout << "HEIGHT: " << frame_height << " WIDTH: " << frame_width << std::endl; 
  // cv::imwrite("./out.png", img_out);

  std::vector<cv::Mat> result;
  result.push_back(img_out);

  return result;
}

// Load JSON string into this object
void CVFrameInterpolation::SetJson(const std::string value) {
	// Parse JSON string into JSON objects
	try
	{
		const Json::Value root = openshot::stringToJson(value);
		// Set all values that match

		SetJsonValue(root);
	}
	catch (const std::exception& e)
	{
		// Error parsing JSON (or missing keys)
		// throw InvalidJSON("JSON is invalid (missing keys or invalid data types)");
        std::cout<<"JSON is invalid (missing keys or invalid data types)"<<std::endl;
	}
}

// Load Json::Value into this object
void CVFrameInterpolation::SetJsonValue(const Json::Value root) {

	// Set data from Json (if key is found)
	if (!root["protobuf_data_path"].isNull()){
		protobuf_data_path = (root["protobuf_data_path"].asString());
	}
    if (!root["processing-device"].isNull()){
		processingDevice = (root["processing-device"].asString());
	}
    if (!root["model-weights"].isNull()){
		modelWeights= (root["model-weights"].asString());
        std::ifstream infile(modelWeights);
        if(!infile.good()){
            processingController->SetError(true, "Incorrect path to model weight file");
            error = true;
        }

	}
}