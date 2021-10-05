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

    if(error) 
    {
        return;
    }
    
    processingController->SetError(false, "");
    
    try 
    {
        torch::AutoGradMode enable_grad(false);
        // Deserialize the ScriptModule from a file using torch::jit::load().
        model = torch::jit::load(modelWeights);
        // model = torch::jit::optimize_for_inference(model);

        if(processingDevice == "GPU" && torch::cuda::is_available())
            model.to(at::kCUDA);

        model.eval();
    }

    catch (const c10::Error &e) 
    {
        processingController->SetError(true, "Error: error loading the model.");
        error = true;
        // clean GPU memory allocation

        #ifdef USE_CUDA
        if(processingDevice == "GPU" && torch::cuda::is_available())
        {
            c10::cuda::CUDACachingAllocator::emptyCache();
        }
        #endif

        return;
    } 

    // Get the num of outputs to calculate the scale factor
    auto new_frame = std::make_shared<Frame>(0, 320, 240,"#000000", 0, 0);

    // Create input frames array
    std::vector<cv::Mat> input_frames { new_frame->GetImageCV(), new_frame->GetImageCV(),  new_frame->GetImageCV(),  new_frame->GetImageCV() };

    // Interpolate Frames
    std::vector<cv::Mat> output_frames = interpolateFrames(input_frames);
    size_t frame_number;
    uint scale_factor = output_frames.size() + 1;

    if(!process_interval || end <= 1 || end-start == 0)
    {
        // Get total number of frames in video
        start = (size_t)(video.Start() * video.Reader()->info.fps.ToFloat());
        end = (size_t)(video.End() * video.Reader()->info.fps.ToFloat());
    }
    
    FFmpegWriter video_writer(savePath);
    FFmpegReader resample_video(video.path);

    // Clip resampled_video(video.path); // Create a new Clip
    const uint video_width = video.Reader()->info.width;
    const uint video_height = video.Reader()->info.height;
    size_t frame_counter = start*scale_factor + 3;

    Fraction new_fps = video.Reader()->info.fps;
    new_fps.num *= scale_factor;

    // Frame Mapper to rescale the audio data
    FrameMapper map(
        &resample_video, 
        new_fps, 
        PULLDOWN_ADVANCED, 
        video.Reader()->info.sample_rate*scale_factor, 
        video.Reader()->info.channels, 
        video.Reader()->info.channel_layout
    );

    map.info.has_audio = true;
    map.Open();
    
    video_writer.SetAudioOptions(true, "libmp3lame", video.Reader()->info.sample_rate, video.Reader()->info.channels, video.Reader()->info.channel_layout, video.Reader()->info.audio_bit_rate);
    video_writer.SetVideoOptions(true, "libx264", video.Reader()->info.fps, video_width, video_height, Fraction(1,1), false, false, 3000000);
    video_writer.Open();

    // Get the first and second frames
    if (end > 2) 
    {
        std::shared_ptr<Frame> first_frame = video.Reader()->GetFrame(1);
        std::shared_ptr<Frame> second_frame = video.Reader()->GetFrame(2);
        video_writer.WriteFrame(first_frame);
        video_writer.WriteFrame(second_frame);
    }

    for (frame_number = start+4; frame_number <= end; frame_number++)
    {
        // Stop the feature tracker process
        if (processingController->ShouldStop() || error) 
        {
            #ifdef USE_CUDA
            if(processingDevice == "GPU" && torch::cuda::is_available())
            {
                c10::cuda::CUDACachingAllocator::emptyCache();
            }
            #endif

            video_writer.Close();
            map.Close();
            map.Reader(nullptr);
            return;
        }

        std::shared_ptr<openshot::Frame> f1 = video.Reader()->GetFrame(frame_number-3);
        std::shared_ptr<openshot::Frame> f2 = video.Reader()->GetFrame(frame_number-2);
        std::shared_ptr<openshot::Frame> f3 = video.Reader()->GetFrame(frame_number-1);
        std::shared_ptr<openshot::Frame> f4 = video.Reader()->GetFrame(frame_number);

        // Create input frames array
        std::vector<cv::Mat> input_frames 
        {
            f1->GetImageCV(), 
            f2->GetImageCV(), 
            f3->GetImageCV(), 
            f4->GetImageCV()
        };

        // Interpolate Frames
        std::vector<cv::Mat> output_frames = interpolateFrames(input_frames);

        // Append all interpolated frames into the outputs
        for (size_t i = 0; i < output_frames.size(); i++) 
        {
            std::shared_ptr<openshot::Frame> frame = map.GetFrame(frame_counter);
            frame->SetImageCV(output_frames[i]);
            frame->SampleRate(frame->SampleRate()/scale_factor);
            video_writer.WriteFrame(frame);
            frame_counter++;
        }

        // Append the third input
        std::shared_ptr<openshot::Frame> middle_frame = map.GetFrame(frame_counter);
        middle_frame->SetImageCV(f3->GetImageCV());
        middle_frame->SampleRate(middle_frame->SampleRate()/scale_factor);
        video_writer.WriteFrame(middle_frame);
        frame_counter++;

        // Update progress  
        uint progress = uint(100*(frame_number-start)/(end-start));
        
        if (progress != 100)
            processingController->SetProgress(progress);
    }

    video_writer.Close();

    map.Close();
    map.Reader(nullptr);

    // clean GPU memory allocation
    #ifdef USE_CUDA
    if(processingDevice == "GPU" && torch::cuda::is_available())
    {
        c10::cuda::CUDACachingAllocator::emptyCache();
    }
    #endif

    // Complete progress after video generation
    processingController->SetProgress(uint(100));
}


std::vector<cv::Mat> CVFrameInterpolation::interpolateFrames(std::vector<cv::Mat> frames)
{
    const uint original_frame_width = frames[0].size().width;
    const uint original_frame_height = frames[0].size().height;
    at::Tensor input_tensor;

    for (size_t i = 0;  i <  frames.size(); i++) {
        cv::Mat f = frames[i];
        cv::cvtColor(f, f, cv::COLOR_BGR2RGB);

        // Resize to don't run out of memory and match the input tensor required size 
        cv::resize(f, f, cv::Size(processingWidth, processingHeight), cv::INTER_LINEAR);

        // Convert cv::Mat to at::Tensor
        at::Tensor t = torch::from_blob(f.data, {f.rows, f.cols, 3}, at::kByte);
        
        // Add axis, shape is (1,3,x,y) now
        t = t.index({torch::indexing::None, torch::indexing::Slice(0)});
        
        if(processingDevice == "GPU")
        {
            // Convert Tensor to Float and Normalize it
            t = t.to(at::kHalf) / 255.0;  
        }
        else{
            // Convert Tensor to Float and Normalize it
            t = t.to(at::kFloat) / 255.0;  
        }

        if (i == 0)
            input_tensor = t;
        else 
            input_tensor = torch::cat({input_tensor, t}, 0);
    }
    
    if(processingDevice == "GPU")
    {
        input_tensor = input_tensor.to(at::kCUDA);
    } 

    input_tensor = input_tensor.permute({3, 0, 1, 2});

    // Add batch axis, shape is (1,3,4,x,y) now
    input_tensor = input_tensor.index({torch::indexing::None, torch::indexing::Slice(0)});
    
    at::Tensor output_tensor;
    std::vector<cv::Mat> result;

    try 
    {
        output_tensor = model.forward({input_tensor}).toTensor();
    }

    catch (std::exception &e)
    {
        processingController->SetError(true, "Error: CUDA out of memory.");
        error = true;
        return frames;
    }

    output_tensor = output_tensor.permute({1, 0, 3, 4, 2});
    output_tensor = output_tensor.index({-1, torch::indexing::Slice(0)});
    // output_tensor = output_tensor.index({-1, torch::indexing::Slice(0)});
    output_tensor = output_tensor.mul(255.0).clamp(0,255).round();
    
    // Convert Tensor to uint8
    output_tensor = output_tensor.to(at::kByte); 

    if(processingDevice == "GPU")
        output_tensor = output_tensor.to(at::kCPU);



    for (size_t i = 0; i < output_tensor.sizes()[0]; i++) {
        const int width = output_tensor[i].sizes()[1];
        const int height = output_tensor[i].sizes()[0];

        // Convert tensor to cv image and save
        at::Tensor out = output_tensor[i].reshape({width * height * 3});

        cv::Mat img_out(cv::Size(width, height), CV_8UC3, out.data_ptr());
        cv::cvtColor(img_out, img_out, cv::COLOR_RGB2BGR);
        
        // Resize to original size
        cv::resize(img_out, img_out, cv::Size(original_frame_width, original_frame_height), cv::INTER_LINEAR);

        result.push_back(img_out);
    }

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