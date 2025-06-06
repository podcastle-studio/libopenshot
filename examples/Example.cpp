/**
 * @file
 * @brief Source file for Example Executable (example app for libopenshot)
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * @ref License
 */

// Copyright (c) 2008-2019 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <chrono>
#include <iostream>
#include <memory>
#include "Frame.h"
#include "FFmpegReader.h"
#include "FFmpegWriter.h"

using namespace openshot;

int main(int argc, char* argv[]) {
    // Path to your video
    const char* path = "/home/jonathan/Downloads/openshot-testing/sintel_trailer-720p.mp4";

    FFmpegReader r(path);
    r.Open();

    const long int total_frames = r.info.video_length;


    // Reader
    std::stringstream writer_path;
    writer_path << TEST_MEDIA_PATH << "sintel_trailer-720p.mp4";

    /* WRITER ---------------- */
    FFmpegWriter w("/home/jonathan/Downloads/performance-test.mp4");

    // Set options
    w.SetAudioOptions("aac", 48000, 192000);
    w.SetVideoOptions("libx264", 1280, 720, Fraction(30,1), 5000000);

    // Open writer
    w.Open();


    // --- Measure forward pass ---
    auto t0 = std::chrono::high_resolution_clock::now();
    for (long int frame = 1; frame <= total_frames; frame++) {
        float percent = (float(frame) / total_frames) * 100.0f;
        std::cout << "Forward: Requesting Frame #: " << frame
                  << " (" << percent << "%)\n";
        std::shared_ptr<Frame> f = r.GetFrame(frame);
        w.WriteFrame(f);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    auto forward_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    // --- Measure backward pass ---
    auto t2 = std::chrono::high_resolution_clock::now();
    for (long int frame = total_frames; frame >= 1; frame--) {
        float percent = (float(total_frames - frame + 1) / total_frames) * 100.0f;
        std::cout << "Backward: Requesting Frame #: " << frame
                  << " (" << percent << "%)\n";
        std::shared_ptr<Frame> f = r.GetFrame(frame);
        w.WriteFrame(f);
    }
    auto t3 = std::chrono::high_resolution_clock::now();
    auto backward_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();

    std::cout << "\nForward pass elapsed: " << forward_ms << " ms\n";
    std::cout << "Backward pass elapsed: " << backward_ms << " ms\n";

    r.Close();
    w.Close();
    return 0;
}
