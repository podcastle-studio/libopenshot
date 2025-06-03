/**
 * @file
 * @brief Source file for VideoCacheThread class
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * @ref License
 */

// Copyright (c) 2008-2025 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "VideoCacheThread.h"
#include "CacheBase.h"
#include "Exceptions.h"
#include "Frame.h"
#include "Settings.h"
#include "Timeline.h"
#include <thread>
#include <chrono>

namespace openshot
{
    // Constructor
    VideoCacheThread::VideoCacheThread()
        : Thread("video-cache")
        , speed(0)
        , last_speed(1)
        , last_dir(1)                  // Assume forward (+1) on first launch
        , is_playing(false)
        , userSeeked(false)
        , requested_display_frame(1)
        , current_display_frame(1)
        , cached_frame_count(0)
        , min_frames_ahead(4)
        , max_frames_ahead(8)
        , timeline_max_frame(0)
        , should_pause_cache(false)
        , should_break(false)
        , reader(nullptr)
        , force_directional_cache(false)
    {
    }

    // Destructor
    VideoCacheThread::~VideoCacheThread()
    {
    }

    // Seek the reader to a particular frame number
    void VideoCacheThread::setSpeed(int new_speed)
    {
        // Only update last_speed and last_dir when new_speed is non-zero.
        if (new_speed != 0) {
            last_speed = new_speed;
            last_dir   = (new_speed > 0 ? 1 : -1);
        }
        speed = new_speed;
    }

    // Get the size in bytes of a frame (rough estimate)
    int64_t VideoCacheThread::getBytes(int width, int height, int sample_rate, int channels, float fps)
    {
        // Estimate memory for RGBA video frame
        int64_t bytes = static_cast<int64_t>(width) * height * sizeof(char) * 4;

        // Approximate audio memory: (sample_rate * channels) samples per second,
        // divided across fps frames, each sample is sizeof(float).
        bytes += ((sample_rate * channels) / fps) * sizeof(float);

        return bytes;
    }

    // Play the video
    void VideoCacheThread::Play()
    {
        is_playing = true;
    }

    // Stop the audio
    void VideoCacheThread::Stop()
    {
        is_playing = false;
    }

    // Is cache ready for playback (pre-roll)
    bool VideoCacheThread::isReady()
    {
        // Return true when pre-roll has cached at least min_frames_ahead frames.
        return (cached_frame_count > min_frames_ahead);
    }

    // Start the thread
    void VideoCacheThread::run()
    {
        using micro_sec        = std::chrono::microseconds;
        using double_micro_sec = std::chrono::duration<double, micro_sec::period>;

        // last_cached_index: Index of the most recently cached frame.
        // cache_start_index: Base index from which we build the window.
        int64_t last_cached_index = 0;
        int64_t cache_start_index = 0;
        bool    last_paused       = true;

        while (!threadShouldExit()) {
            Settings* settings = Settings::Instance();
            CacheBase* cache   = reader ? reader->GetCache() : nullptr;

            // If caching is disabled or no reader is assigned, wait briefly and retry
            if (!settings->ENABLE_PLAYBACK_CACHING || !cache) {
                std::this_thread::sleep_for(double_micro_sec(50000));
                continue;
            }

            Timeline* timeline    = static_cast<Timeline*>(reader);
            int64_t  timeline_end = timeline->GetMaxFrame();
            int64_t  playhead     = requested_display_frame;
            bool     paused       = (speed == 0);

            // Determine the effective direction:
            //   If speed != 0, dir = sign(speed).
            //   Otherwise (speed == 0), use last_dir to continue caching in the same direction.
            int dir = (speed != 0 ? (speed > 0 ? 1 : -1) : last_dir);

            // On transition from paused (speed == 0) to playing (speed != 0), reset window base.
            if (!paused && last_paused) {
                cache_start_index = playhead;
                last_cached_index = playhead - dir;
            }
            last_paused = paused;

            // Calculate bytes needed for one frame in cache
            int64_t bytes_per_frame = getBytes(
                (timeline->preview_width ? timeline->preview_width : reader->info.width),
                (timeline->preview_height ? timeline->preview_height : reader->info.height),
                reader->info.sample_rate,
                reader->info.channels,
                reader->info.fps.ToFloat()
            );

            int64_t max_bytes = cache->GetMaxBytes();
            if (max_bytes <= 0 || bytes_per_frame <= 0) {
                std::this_thread::sleep_for(double_micro_sec(50000));
                continue;
            }

            int64_t capacity = max_bytes / bytes_per_frame;
            if (capacity < 1) {
                std::this_thread::sleep_for(double_micro_sec(50000));
                continue;
            }

            // Number of frames to keep ahead (or behind) based on settings
            int64_t ahead_count = static_cast<int64_t>(capacity * settings->VIDEO_CACHE_PERCENT_AHEAD);

            // Handle user-initiated seek: always reset window base
            bool user_seek = userSeeked;
            if (user_seek) {
                cache_start_index = playhead;
                last_cached_index = playhead - dir;
                userSeeked        = false;
            }
            else if (!paused) {
                // In playing mode, if playhead moves beyond last_cached, reset window
                if ((dir > 0 && playhead > last_cached_index) || (dir < 0 && playhead < last_cached_index)) {
                    cache_start_index = playhead;
                    last_cached_index = playhead - dir;
                }
            }

            // ----------------------------------------
            // PAUSED MODE: Continue caching in 'dir' without advancing playhead
            // ----------------------------------------
            if (paused) {
                // If the playhead is not in cache, clear and restart from playhead
                if (!cache->Contains(playhead)) {
                    timeline->ClearAllCache();
                    cache_start_index = playhead;
                    last_cached_index = playhead - dir;
                }
            }

            // Compute window bounds based on dir
            int64_t window_begin, window_end;
            if (dir > 0) {
                // Forward: [cache_start_index ... cache_start_index + ahead_count]
                window_begin = cache_start_index;
                window_end   = cache_start_index + ahead_count;
            } else {
                // Backward: [cache_start_index - ahead_count ... cache_start_index]
                window_begin = cache_start_index - ahead_count;
                window_end   = cache_start_index;
            }

            // Clamp to valid timeline range
            window_begin = std::max<int64_t>(window_begin, 1);
            window_end   = std::min<int64_t>(window_end, timeline_end);

            // Prefetch loop: start from just beyond last_cached_index toward window_end
            int64_t next_frame = last_cached_index + dir;
            bool window_full = true;

            while ((dir > 0 && next_frame <= window_end) || (dir < 0 && next_frame >= window_begin)) {
                if (threadShouldExit()) {
                    break;
                }

                // Interrupt if a new seek happened
                if (userSeeked) {
                    break;
                }

                if (!cache->Contains(next_frame)) {
                    // Missing frame: fetch and add to cache
                    try {
                        auto framePtr = reader->GetFrame(next_frame);
                        cache->Add(framePtr);
                        ++cached_frame_count;
                    }
                    catch (const OutOfBoundsFrame&) {
                        break;
                    }
                    window_full = false;  // We had to fetch at least one frame
                }
                else {
                    cache->Touch(next_frame);
                }

                last_cached_index = next_frame;
                next_frame += dir;
            }

            // In paused mode, if the entire window was already filled, touch playhead
            if (paused && window_full) {
                cache->Touch(playhead);
            }

            // Sleep a short fraction of a frame interval to throttle CPU usage
            int64_t sleep_us = static_cast<int64_t>(1000000.0 / reader->info.fps.ToFloat() / 4.0);
            std::this_thread::sleep_for(double_micro_sec(sleep_us));
        }
    }

    void VideoCacheThread::Seek(int64_t new_position, bool start_preroll)
    {
        if (start_preroll) {
            userSeeked = true;
        }
        requested_display_frame = new_position;
    }

    void VideoCacheThread::Seek(int64_t new_position)
    {
        Seek(new_position, false);
    }

} // namespace openshot
