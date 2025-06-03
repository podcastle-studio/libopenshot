/**
 * @file
 * @brief Header file for VideoCacheThread class
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * @ref License
 */

// Copyright (c) 2008-2025 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef OPENSHOT_VIDEO_CACHE_THREAD_H
#define OPENSHOT_VIDEO_CACHE_THREAD_H

#include "ReaderBase.h"

#include <AppConfig.h>
#include <juce_audio_basics/juce_audio_basics.h>

namespace openshot
{
    using juce::Thread;
    using juce::WaitableEvent;

    /**
     * @brief Handles prefetching and caching of video/audio frames for smooth playback.
     *
     * This thread maintains a rolling cache window in the current playback direction (forward or backward).
     * When paused, it continues to build the cache in the last known direction. On seek, it resets the window.
     */
    class VideoCacheThread : public Thread
    {
    public:
        /// Constructor: initializes member variables and assumes forward direction on first launch.
        VideoCacheThread();

        /// Destructor.
        ~VideoCacheThread() override;

        /// Returns true if enough frames are cached for playback (pre-roll completed).
        bool isReady();

        /// Starts playback by setting is_playing to true.
        void Play();

        /// Stops playback by setting is_playing to false.
        void Stop();

        /**
         * @brief Set the playback speed and direction.
         * @param new_speed Positive values for forward play, negative for rewind, zero to pause.
         *
         * When new_speed != 0, last_speed and last_dir are updated. If new_speed is zero,
         * last_dir remains unchanged so that pausing does not flip the cache direction.
         */
        void setSpeed(int new_speed);

        /// Returns the current speed setting (1=normal, 2=fast, -1=rewind, etc.).
        int getSpeed() const { return speed; }

        /**
         * @brief Seek to a specific frame without pre-roll.
         * @param new_position Frame index to seek to.
         */
        void Seek(int64_t new_position);

        /**
         * @brief Seek to a specific frame and optionally start pre-roll.
         * @param new_position Frame index to seek to.
         * @param start_preroll If true, signals the thread to rebuild cache from new_position.
         */
        void Seek(int64_t new_position, bool start_preroll);

        /**
         * @brief Assigns the ReaderBase source and begins caching.
         * @param new_reader Pointer to the ReaderBase instance to cache from.
         */
        void Reader(ReaderBase* new_reader) { reader = new_reader; Play(); }

        // Friend classes that may access protected members directly.
        friend class PlayerPrivate;
        friend class QtPlayer;

    protected:
        /**
         * @brief Thread entry point: maintains and updates the cache window.
         *
         * This method runs continuously until threadShouldExit() returns true. It:
         * 1. Computes effective playback direction (dir) based on speed or last_dir.
         * 2. On seek or direction change, resets cache_start and last_cached.
         * 3. When paused (speed == 0), continues caching in dir without advancing playhead.
         * 4. When playing, caches in the direction of playback around the current playhead.
         * 5. Sleeps for short intervals to throttle CPU usage.
         */
        void run() override;

        /**
         * @brief Estimate memory footprint of a single frame (video + audio).
         * @param width      Frame width in pixels.
         * @param height     Frame height in pixels.
         * @param sample_rate Audio sample rate (e.g., 48000).
         * @param channels    Number of audio channels.
         * @param fps         Frames per second.
         * @return Approximate size in bytes for storing one frame.
         */
        int64_t getBytes(int width, int height, int sample_rate, int channels, float fps);

        //------------------------------------------------------------------------
        // Member variables
        //------------------------------------------------------------------------

        std::shared_ptr<Frame> last_cached_frame; ///< Last frame pointer added to cache.

        int speed;            ///< Current playback speed (0=paused, >0=forward, <0=backward).
        int last_speed;       ///< Last non-zero speed value (used to compute direction).
        int last_dir;         ///< Last playback direction: +1 for forward, -1 for backward.

        bool is_playing;      ///< True when playback is running, false when stopped.
        bool userSeeked;      ///< True if a seek was requested (triggers cache reset).

        int64_t requested_display_frame; ///< Frame index requested by the user.
        int64_t current_display_frame;   ///< Currently displaying frame index (not used in caching).
        int64_t cached_frame_count;      ///< Number of frames currently in cache.

        int64_t min_frames_ahead;        ///< Minimum number of frames to keep ahead in cache.
        int64_t max_frames_ahead;        ///< Maximum number of frames to keep ahead in cache.
        int64_t timeline_max_frame;      ///< Highest valid frame index in the timeline.

        bool should_pause_cache;         ///< Flag to pause cache updates (not currently used).
        bool should_break;               ///< Internal flag to break out of loops (not currently used).

        ReaderBase* reader;              ///< Pointer to the video/audio source (ReaderBase).

        /// Forces caching in a fixed direction when seeking into an uncached frame.
        bool force_directional_cache;
    };

} // namespace openshot

#endif // OPENSHOT_VIDEO_CACHE_THREAD_H
