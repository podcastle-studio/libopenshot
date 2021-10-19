/**
 * @file
 * @brief Header file for CVFrameInterpolation class
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

#pragma once


#define int64 opencv_broken_int
#define uint64 opencv_broken_uint

#include <opencv2/dnn.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/core.hpp>
#undef uint64
#undef int64
#include "Json.h"
#include "ProcessingController.h"
#include "Frame.h"
#include "Clip.h"
#include "FFmpegReader.h"
#include "FFmpegWriter.h"
#include "FrameMapper.h"
#include "Fraction.h"
#include "AudioResampler.h"
#include "protobuf_messages/objdetectdata.pb.h"

// including torch and opencv 
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc.hpp>

#undef slots // workaround to run torch with QT 
#include <torch/torch.h>
#include <torch/script.h> 
#include <caffe2/core/macros.h>

#ifdef CAFFE2_USE_CUDNN
#include <c10/cuda/CUDACachingAllocator.h>
#endif

#define slots Q_SLOTS

#include "sort_filter/sort.hpp"

namespace openshot
{
   
    /**
     * @brief This class runs trought a clip and performs frame interpolation to increase the clip FPS
     * or to create a Slow Motion effect.
     * The implementation is based on the model FLAVR: Flow-Agnostic Video Representations for Fast Frame Interpolation
     * https://github.com/tarun005/FLAVR
     */
    class CVFrameInterpolation 
    {
        private:

        cv::dnn::Net net;
        std::vector<std::string> classNames;
        float confThreshold, nmsThreshold;

        std::string classesFile;
        std::string savePath;
        std::string modelConfiguration;
        std::string modelWeights;
        std::string processingDevice;
        std::string protobuf_data_path;
        uint processingWidth;
        uint processingHeight;

        uint progress;

        size_t start;
        size_t end;

        bool error = false;

        torch::jit::script::Module model;
        std::vector<cv::Mat> outputs;

        /// Will handle a Thread safely comutication between ClipProcessingJobs and the processing effect classes
        ProcessingController *processingController;

        std::vector<cv::Mat> interpolateFrames(std::vector<cv::Mat> frames);

        public:

        CVFrameInterpolation(std::string processInfoJson, ProcessingController &processingController);

        // Iterate over a clip object and run inference for each video frame
        void interpolateClip(openshot::Clip &video, size_t start=0, size_t end=0, bool process_interval=false);

        // Get and Set JSON methods
        void SetJson(const std::string value); ///< Load JSON string into this object
        void SetJsonValue(const Json::Value root); ///< Load Json::Value into this object
        
    };

}
