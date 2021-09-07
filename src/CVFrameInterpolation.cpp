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
        cv::Mat first_frame = video.Reader()->GetFrame(1)->GetImageCV();
        cv::Mat second_frame = video.Reader()->GetFrame(2)->GetImageCV();

        // cv::cvtColor(first_frame, first_frame, cv::COLOR_RGB2BGR);
        // cv::cvtColor(second_frame, second_frame, cv::COLOR_RGB2BGR);

        outputs.push_back(first_frame);
        outputs.push_back(second_frame);
    }

    for (frame_number = start+4; frame_number <= end; frame_number++)
    {
         // Stop the feature tracker process
        if (processingController->ShouldStop()){
            return;
        }

        std::shared_ptr<openshot::Frame> f1 = video.Reader()->GetFrame(frame_number-3);
        std::shared_ptr<openshot::Frame> f2 = video.Reader()->GetFrame(frame_number-2);
        std::shared_ptr<openshot::Frame> f3 = video.Reader()->GetFrame(frame_number-1);
        std::shared_ptr<openshot::Frame> f4 = video.Reader()->GetFrame(frame_number);
        cv::Mat middle_frame = f3->GetImageCV();

        // Create input frames array
        std::vector<cv::Mat> input_frames{
            f1->GetImageCV(), 
            f2->GetImageCV(), 
            f3->GetImageCV(), 
            f4->GetImageCV()
        };

        // Interpolate Frames
        std::vector<cv::Mat> output_frames = interpolateFrames(input_frames);
        
        // Append all interpolated frames into the outputs
        for (cv::Mat frame : output_frames) {
            outputs.push_back(frame);
        }

        // Append the third input
        // cv::cvtColor(middle_frame, middle_frame, cv::COLOR_RGB2BGR);
        outputs.push_back(middle_frame);

        // Update progress  
        processingController->SetProgress(uint(100*(frame_number-start)/(end-start)));
    }

    cv::VideoWriter v1(savePath, cv::VideoWriter::fourcc('M','J','P','G'), 30, 
                cv::Size(outputs[0].size().width, outputs[0].size().height), true);

    for (cv::Mat f : outputs) {
        v1.write(f);
    }
}


std::vector<cv::Mat> CVFrameInterpolation::interpolateFrames(std::vector<cv::Mat> frames)
{
    const uint original_frame_width = frames[0].size().width;
    const uint original_frame_height = frames[0].size().height;
    at::Tensor input_tensor;

    for (uint i = 0;  i <  frames.size(); i++) {
        cv::Mat f = frames[i];
        cv::cvtColor(f, f, cv::COLOR_BGR2RGB);

        // Resize to don't run out of memory and match the input tensor required size 
        cv::resize(f, f, cv::Size(processingWidth, processingHeight), cv::INTER_LINEAR);

        // Convert cv::Mat to at::Tensor
        at::Tensor t = torch::from_blob(f.data, {f.rows, f.cols, 3}, at::kByte);
        
        // Add axis, shape is (1,3,x,y) now
        t = t.index({torch::indexing::None, torch::indexing::Slice(0)});

        // Convert Tensor to Float and Normalize it
        if(processingDevice == "GPU")
            t = t.to(at::kHalf).to(at::kCUDA) / 255.0;
        else
            t = t.to(at::kFloat) / 255.0;

        if (i == 0)
            input_tensor = t;
        else 
            input_tensor = torch::cat({input_tensor, t}, 0);
    }
    
    input_tensor = input_tensor.permute({3, 0, 1, 2});

    // Add batch axis, shape is (1,3,4,x,y) now
    input_tensor = input_tensor.index({torch::indexing::None, torch::indexing::Slice(0)});
    
    auto output = model.forward({input_tensor}).toTensor();
    output = output.index({-1, torch::indexing::Slice(0)});
    output = output.index({-1, torch::indexing::Slice(0)});
    output = output.permute({1, 2, 0});
    output = output.mul(255.0).clamp(0,255).round();
    
    // Convert Tensor to uint8
    output = output.to(at::kByte); 

    const int width = output.sizes()[1];
    const int height = output.sizes()[0];

    // Convert tensor to cv image and save
    output = output.reshape({width * height * 3});
    cv::Mat img_out(cv::Size(width, height), CV_8UC3, output.data_ptr());
    cv::cvtColor(img_out, img_out, cv::COLOR_RGB2BGR);
    
    // Resize to original size
    cv::resize(img_out, img_out, cv::Size(original_frame_width, original_frame_height), cv::INTER_LINEAR);

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
    if (!root["save-path"].isNull()){
		savePath = (root["save-path"].asString());
	}
    if (!root["processing-device"].isNull()){
		processingDevice = (root["processing-device"].asString());
	}
    if (!root["model-weights"].isNull()){
		modelWeights = (root["model-weights"].asString());
        std::ifstream infile(modelWeights);
        if(!infile.good()){
            processingController->SetError(true, "Incorrect path to model weight file");
            error = true;
        }

	}
    if (!root["processing-size"].isNull()){
        
        switch (root["processing-size"].asUInt())
        {
            case 1:
                processingWidth = 1920;
                processingHeight = 1080;
                break;
            
            case 2:
                processingWidth = 1280;
                processingHeight = 720;
                break;

            case 3:
                processingWidth = 800;
                processingHeight = 600;
                break;

            case 4:
                processingWidth = 640;
                processingHeight = 480;
                break;

            case 5:
            default:
                processingWidth = 320;
                processingHeight = 240;
                break;
        }
		
	}
}