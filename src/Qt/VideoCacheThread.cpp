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
#include <algorithm>

namespace openshot
{
    // Constructor
    VideoCacheThread::VideoCacheThread()
        : Thread("video-cache")
        , speed(0)
        , last_speed(1)
        , last_dir(1)                   // assume forward (+1) on first launch
        , userSeeked(false)
        , preroll_on_next_fill(false)
        , clear_cache_on_next_fill(false)
        , scrub_active(false)
        , requested_display_frame(1)
        , current_display_frame(1)
        , cached_frame_count(0)
        , min_frames_ahead(4)
        , timeline_max_frame(0)
        , reader(nullptr)
        , force_directional_cache(false)
        , last_cached_index(0)
        , seen_timeline_cache_epoch(0)
        , timeline_cache_epoch_initialized(false)
    {
    }

    // Destructor
    VideoCacheThread::~VideoCacheThread()
    {
    }

    // Is cache ready for playback (pre-roll)
    bool VideoCacheThread::isReady()
    {
        if (!reader) {
            return false;
        }

        const int64_t ready_min = min_frames_ahead.load();
        if (ready_min < 0) {
            return true;
        }

        const int64_t cached_index = last_cached_index.load();
        int64_t playhead = requested_display_frame.load();
        int dir = computeDirection();

        // Near timeline boundaries, don't require more pre-roll than can exist.
        int64_t max_frame = reader->info.video_length;
        if (auto* timeline = dynamic_cast<Timeline*>(reader)) {
            const int64_t timeline_max = timeline->GetMaxFrame();
            if (timeline_max > 0) {
                max_frame = timeline_max;
            }
        }
        if (max_frame < 1) {
            return false;
        }
        playhead = clampToTimelineRange(playhead, max_frame);

        int64_t required_ahead = ready_min;
        int64_t available_ahead = (dir > 0)
            ? std::max<int64_t>(0, max_frame - playhead)
            : std::max<int64_t>(0, playhead - 1);
        required_ahead = std::min(required_ahead, available_ahead);

        if (dir > 0) {
            return (cached_index >= playhead + required_ahead);
        }
        return (cached_index <= playhead - required_ahead);
    }

    void VideoCacheThread::setSpeed(int new_speed)
    {
        // Only update last_speed and last_dir when new_speed != 0
        if (new_speed != 0) {
            last_speed.store(new_speed);
            last_dir.store(new_speed > 0 ? 1 : -1);
            // Leaving paused/scrub context: resume normal cache behavior.
            scrub_active.store(false);
        }
        speed.store(new_speed);
    }

    // Get the size in bytes of a frame (rough estimate)
    int64_t VideoCacheThread::getBytes(int width,
                                       int height,
                                       int sample_rate,
                                       int channels,
                                       float fps)
    {
        // RGBA video frame
        int64_t bytes = static_cast<int64_t>(width) * height * sizeof(char) * 4;
        // Approximate audio: (sample_rate * channels)/fps samples per frame
        bytes += ((sample_rate * channels) / fps) * sizeof(float);
        return bytes;
    }

    /// Start the cache thread at high priority, and return true if it’s actually running.
    bool VideoCacheThread::StartThread()
    {
        // JUCE’s startThread() returns void, so we launch it and then check if
        // the thread actually started:
        startThread(Priority::high);
        return isThreadRunning();
    }

    /// Stop the cache thread, waiting up to timeoutMs ms. Returns true if it actually stopped.
    bool VideoCacheThread::StopThread(int timeoutMs)
    {
        stopThread(timeoutMs);
        return !isThreadRunning();
    }

    void VideoCacheThread::Reader(ReaderBase* new_reader)
    {
        std::lock_guard<std::mutex> guard(seek_state_mutex);
        reader = new_reader;
        seen_timeline_cache_epoch = 0;
        timeline_cache_epoch_initialized = false;
        Play();
    }

    void VideoCacheThread::Seek(int64_t new_position, bool start_preroll)
    {
        const int64_t timeline_end = resolveTimelineEnd();
        const int64_t clamped_new_position = clampToTimelineRange(new_position, timeline_end);
        const int64_t current_requested = requested_display_frame.load();

        bool should_mark_seek = false;
        bool should_preroll = false;
        int64_t new_cached_count = cached_frame_count.load();
        bool entering_scrub = false;
        bool leaving_scrub = false;
        bool cache_contains = false;
        bool should_clear_cache = false;
        CacheBase* cache = reader ? reader->GetCache() : nullptr;
        const bool same_frame_refresh = (new_position == current_requested);
        if (cache) {
            cache_contains = cache->Contains(clamped_new_position);
        }

        if (start_preroll) {
            if (same_frame_refresh) {
                const bool is_paused = (speed.load() == 0);
                if (is_paused) {
                    const bool was_scrubbing = scrub_active.load();
                    if (was_scrubbing && cache && cache_contains) {
                        // Preserve in-range cache for paused scrub preview -> same-frame commit.
                        should_mark_seek = false;
                        should_preroll = false;
                        should_clear_cache = false;
                        new_cached_count = cache->Count();
                    } else {
                        // Paused same-frame edit refresh: force full cache refresh.
                        if (Timeline* timeline = dynamic_cast<Timeline*>(reader)) {
                            timeline->ClearAllCache();
                        }
                        new_cached_count = 0;
                        should_mark_seek = true;
                        should_preroll = true;
                        should_clear_cache = false;
                    }
                } else {
                    // Same-frame refresh during playback should stay lightweight.
                    should_mark_seek = false;
                    should_preroll = false;
                    should_clear_cache = false;
                    if (cache && cache_contains) {
                        cache->Remove(clamped_new_position);
                    }
                    if (cache) {
                        new_cached_count = cache->Count();
                    }
                }
            } else {
                if (cache && !cache_contains) {
                    should_mark_seek = true;
                    // Uncached commit seek: defer cache clear to cache thread loop.
                    new_cached_count = 0;
                    should_preroll = true;
                    should_clear_cache = true;
                }
                else if (cache)
                {
                    // In-range commit seek preserves cache window/baseline.
                    should_mark_seek = false;
                    should_preroll = false;
                    should_clear_cache = false;
                    new_cached_count = cache->Count();
                } else {
                    // No cache object to query: use normal seek behavior.
                    should_mark_seek = true;
                }
            }
            leaving_scrub = true;
        }
        else {
            // Non-preroll seeks cover paused scrubbing and live playback refresh.
            const bool is_paused = (speed.load() == 0);
            if (is_paused && same_frame_refresh) {
                // Same-frame paused refresh updates only that frame.
                should_mark_seek = false;
                should_preroll = false;
                should_clear_cache = false;
                if (cache && cache_contains) {
                    cache->Remove(clamped_new_position);
                }
                if (cache) {
                    new_cached_count = cache->Count();
                }
                leaving_scrub = true;
            }
            else if (is_paused) {
                if (cache && !cache_contains) {
                    should_mark_seek = true;
                    new_cached_count = 0;
                    should_clear_cache = true;
                }
                else if (cache) {
                    // In-range paused seek preserves cache continuity.
                    should_mark_seek = false;
                    new_cached_count = cache->Count();
                } else {
                    should_mark_seek = true;
                }
                entering_scrub = true;
            } else {
                // During playback, keep seek/scrub side effects minimal.
                should_mark_seek = false;
                should_preroll = false;
                should_clear_cache = false;
                if (cache) {
                    new_cached_count = cache->Count();
                }
                leaving_scrub = true;
            }
        }

        {
            std::lock_guard<std::mutex> guard(seek_state_mutex);
            // Reset readiness baseline only when rebuilding cache.
            const int dir = computeDirection();
            if (should_mark_seek || should_preroll || should_clear_cache) {
                last_cached_index.store(clamped_new_position - dir);
            }
            requested_display_frame.store(new_position);
            cached_frame_count.store(new_cached_count);
            preroll_on_next_fill.store(should_preroll);
            // Clear behavior follows the latest seek intent.
            clear_cache_on_next_fill.store(should_clear_cache);
            userSeeked.store(should_mark_seek);
            if (entering_scrub) {
                scrub_active.store(true);
            }
            if (leaving_scrub) {
                scrub_active.store(false);
            }
        }
    }

    void VideoCacheThread::Seek(int64_t new_position)
    {
        NotifyPlaybackPosition(new_position);
    }

    void VideoCacheThread::NotifyPlaybackPosition(int64_t new_position)
    {
        if (new_position <= 0) {
            return;
        }
        if (scrub_active.load()) {
            return;
        }

        int64_t new_cached_count = cached_frame_count.load();
        if (CacheBase* cache = reader ? reader->GetCache() : nullptr) {
            new_cached_count = cache->Count();
        }
        {
            std::lock_guard<std::mutex> guard(seek_state_mutex);
            requested_display_frame.store(new_position);
            cached_frame_count.store(new_cached_count);
        }
    }

    int VideoCacheThread::computeDirection() const
    {
        // If speed ≠ 0, use its sign; if speed==0, keep last_dir
        const int current_speed = speed.load();
        if (current_speed != 0) {
            return (current_speed > 0 ? 1 : -1);
        }
        return last_dir.load();
    }

    void VideoCacheThread::handleUserSeek(int64_t playhead, int dir)
    {
        // Place last_cached_index just “behind” playhead in the given dir
        last_cached_index.store(playhead - dir);
    }

    void VideoCacheThread::handleUserSeekWithPreroll(int64_t playhead,
                                                     int dir,
                                                     int64_t timeline_end,
                                                     int64_t preroll_frames)
    {
        int64_t preroll_start = playhead;
        if (preroll_frames > 0) {
            if (dir > 0) {
                preroll_start = std::max<int64_t>(1, playhead - preroll_frames);
            }
            else {
                preroll_start = std::min<int64_t>(timeline_end, playhead + preroll_frames);
            }
        }
        last_cached_index.store(preroll_start - dir);
    }

    int64_t VideoCacheThread::computePrerollFrames(const Settings* settings) const
    {
        if (!settings) {
            return 0;
        }
        int64_t min_frames = settings->VIDEO_CACHE_MIN_PREROLL_FRAMES;
        int64_t max_frames = settings->VIDEO_CACHE_MAX_PREROLL_FRAMES;
        if (min_frames < 0) {
            return 0;
        }
        if (max_frames > 0 && min_frames > max_frames) {
            min_frames = max_frames;
        }
        return min_frames;
    }

    int64_t VideoCacheThread::resolveTimelineEnd() const
    {
        if (!reader) {
            return 0;
        }
        int64_t timeline_end = reader->info.video_length;
        if (auto* timeline = dynamic_cast<Timeline*>(reader)) {
            const int64_t timeline_max = timeline->GetMaxFrame();
            if (timeline_max > 0) {
                timeline_end = timeline_max;
            }
        }
        return timeline_end;
    }

    int64_t VideoCacheThread::clampToTimelineRange(int64_t frame, int64_t timeline_end) const
    {
        if (timeline_end < 1) {
            return frame;
        }
        return std::clamp<int64_t>(frame, 1, timeline_end);
    }

    bool VideoCacheThread::clearCacheIfPaused(int64_t playhead,
                                              bool paused,
                                              CacheBase* cache)
    {
        const int64_t timeline_end = resolveTimelineEnd();
        int64_t cache_playhead = playhead;
        if (reader) {
            cache_playhead = clampToTimelineRange(playhead, timeline_end);
        }
        if (paused && !cache->Contains(cache_playhead)) {
            // If paused and playhead not in cache, clear everything
            if (Timeline* timeline = dynamic_cast<Timeline*>(reader)) {
                timeline->ClearAllCache();
            }
            cached_frame_count.store(0);
            return true;
        }
        return false;
    }

    void VideoCacheThread::computeWindowBounds(int64_t playhead,
                                               int dir,
                                               int64_t ahead_count,
                                               int64_t timeline_end,
                                               int64_t& window_begin,
                                               int64_t& window_end) const
    {
        if (dir > 0) {
            // Forward window: [playhead ... playhead + ahead_count]
            window_begin = playhead;
            window_end   = playhead + ahead_count;
        }
        else {
            // Backward window: [playhead - ahead_count ... playhead]
            window_begin = playhead - ahead_count;
            window_end   = playhead;
        }
        // Clamp to [1 ... timeline_end]
        window_begin = std::max<int64_t>(window_begin, 1);
        window_end   = std::min<int64_t>(window_end, timeline_end);
    }

    bool VideoCacheThread::prefetchWindow(CacheBase* cache,
                                          int64_t window_begin,
                                          int64_t window_end,
                                          int dir,
                                          ReaderBase* reader,
                                          int64_t max_frames_to_fetch)
    {
        bool window_full = true;
        int64_t next_frame = last_cached_index.load() + dir;
        int64_t fetched_this_pass = 0;

        // Advance from last_cached_index toward window boundary
        while ((dir > 0 && next_frame <= window_end) ||
               (dir < 0 && next_frame >= window_begin))
        {
            if (threadShouldExit()) {
                break;
            }
            // If a Seek was requested mid-caching, bail out immediately
            if (userSeeked.load()) {
                break;
            }

            if (!cache->Contains(next_frame)) {
                // Frame missing, fetch and add
                try {
                    auto framePtr = reader->GetFrame(next_frame);
                    cache->Add(framePtr);
                    cached_frame_count.store(cache->Count());
                    ++fetched_this_pass;
                }
                catch (const OutOfBoundsFrame&) {
                    break;
                }
                window_full = false;
            }
            else {
                cache->Touch(next_frame);
            }

            last_cached_index.store(next_frame);
            next_frame       += dir;

            // In active playback, avoid long uninterrupted prefetch bursts
            // that can delay player thread frame retrieval.
            if (max_frames_to_fetch > 0 && fetched_this_pass >= max_frames_to_fetch) {
                break;
            }
        }

        return window_full;
    }

    void VideoCacheThread::run()
    {
        using micro_sec        = std::chrono::microseconds;
        using double_micro_sec = std::chrono::duration<double, micro_sec::period>;

        while (!threadShouldExit()) {
            Settings* settings = Settings::Instance();
            CacheBase* cache   = reader ? reader->GetCache() : nullptr;
            Timeline* timeline = dynamic_cast<Timeline*>(reader);

            // Process deferred clears even when caching is currently disabled
            // (e.g. active scrub mode), so stale ranges are removed promptly.
            bool should_clear_cache = clear_cache_on_next_fill.exchange(false);
            if (should_clear_cache && timeline) {
                const int dir_on_clear = computeDirection();
                const int64_t clear_playhead = clampToTimelineRange(
                    requested_display_frame.load(), resolveTimelineEnd());
                timeline->ClearAllCache();
                cached_frame_count.store(0);
                // Reset ready baseline immediately after clear. Otherwise a
                // stale last_cached_index from the old cache window can make
                // isReady() report true before new preroll is actually filled.
                last_cached_index.store(clear_playhead - dir_on_clear);
            }

            // If caching disabled or no reader, mark cache as ready and sleep briefly
            if (!settings->ENABLE_PLAYBACK_CACHING || !cache) {
                cached_frame_count.store(cache ? cache->Count() : 0);
                min_frames_ahead.store(-1);
                std::this_thread::sleep_for(double_micro_sec(50000));
                continue;
            }

            // init local vars
            min_frames_ahead.store(settings->VIDEO_CACHE_MIN_PREROLL_FRAMES);

            if (!timeline) {
                std::this_thread::sleep_for(double_micro_sec(50000));
                continue;
            }
            int64_t  timeline_end = resolveTimelineEnd();
            int64_t  raw_playhead = requested_display_frame.load();
            int64_t  playhead     = clampToTimelineRange(raw_playhead, timeline_end);
            bool     paused       = (speed.load() == 0);
            int64_t  preroll_frames = computePrerollFrames(settings);

            cached_frame_count.store(cache->Count());

            // Compute effective direction (±1)
            int dir = computeDirection();
            if (speed.load() != 0) {
                last_dir.store(dir);
            }

            // If timeline-side cache invalidation occurred (e.g. ApplyJsonDiff / SetJson),
            // restart fill from the active playhead window so invalidated gaps self-heal.
            if (timeline) {
                bool epoch_changed = false;
                {
                    std::lock_guard<std::mutex> guard(seek_state_mutex);
                    const uint64_t timeline_epoch = timeline->CacheEpoch();
                    if (!timeline_cache_epoch_initialized) {
                        seen_timeline_cache_epoch = timeline_epoch;
                        timeline_cache_epoch_initialized = true;
                    }
                    else if (timeline_epoch != seen_timeline_cache_epoch) {
                        seen_timeline_cache_epoch = timeline_epoch;
                        epoch_changed = true;
                    }
                }
                if (epoch_changed) {
                    handleUserSeek(playhead, dir);
                }
            }

            // Compute bytes_per_frame, max_bytes, and capacity once
            int64_t bytes_per_frame = getBytes(
                (timeline->preview_width ? timeline->preview_width : reader->info.width),
                (timeline->preview_height ? timeline->preview_height : reader->info.height),
                reader->info.sample_rate,
                reader->info.channels,
                reader->info.fps.ToFloat()
            );
            int64_t max_bytes = cache->GetMaxBytes();
            int64_t capacity  = 0;
            if (max_bytes > 0 && bytes_per_frame > 0) {
                capacity = max_bytes / bytes_per_frame;
                if (capacity > settings->VIDEO_CACHE_MAX_FRAMES) {
                    capacity = settings->VIDEO_CACHE_MAX_FRAMES;
                }
            }

            // Handle a user-initiated seek
            bool did_user_seek = false;
            bool use_preroll = false;
            {
                std::lock_guard<std::mutex> guard(seek_state_mutex);
                raw_playhead = requested_display_frame.load();
                playhead = clampToTimelineRange(raw_playhead, timeline_end);
                did_user_seek = userSeeked.load();
                use_preroll = preroll_on_next_fill.load();
                if (did_user_seek) {
                    userSeeked.store(false);
                    preroll_on_next_fill.store(false);
                }
            }
            if (did_user_seek) {
                // During active playback, prioritize immediate forward readiness
                // from the playhead. Use directional preroll offset only while
                // paused/scrubbing contexts.
                if (use_preroll && paused) {
                    handleUserSeekWithPreroll(playhead, dir, timeline_end, preroll_frames);
                }
                else {
                    handleUserSeek(playhead, dir);
                }
            }
            else if (!paused && capacity >= 1) {
                // In playback mode, check if last_cached_index drifted outside the new window
                int64_t base_ahead = static_cast<int64_t>(capacity * settings->VIDEO_CACHE_PERCENT_AHEAD);

                int64_t window_begin, window_end;
                computeWindowBounds(
                    playhead,
                    dir,
                    base_ahead,
                    timeline_end,
                    window_begin,
                    window_end
                );

                bool outside_window =
                    (dir > 0 && last_cached_index.load() > window_end) ||
                    (dir < 0 && last_cached_index.load() < window_begin);
                if (outside_window) {
                    handleUserSeek(playhead, dir);
                }
            }

            // If a clear was requested by a seek that arrived after the loop
            // began, apply it now before any additional prefetch work. This
            // avoids "build then suddenly clear" behavior during playback.
            bool should_clear_mid_loop = clear_cache_on_next_fill.exchange(false);
            if (should_clear_mid_loop && timeline) {
                timeline->ClearAllCache();
                cached_frame_count.store(0);
                last_cached_index.store(playhead - dir);
            }

            // While user is dragging/scrubbing, skip cache prefetch work.
            if (scrub_active.load()) {
                std::this_thread::sleep_for(double_micro_sec(10000));
                continue;
            }

            // If capacity is insufficient, sleep and retry
            if (capacity < 1) {
                std::this_thread::sleep_for(double_micro_sec(50000));
                continue;
            }
            int64_t ahead_count = static_cast<int64_t>(capacity *
                                           settings->VIDEO_CACHE_PERCENT_AHEAD);
            int64_t window_size = ahead_count + 1;
            if (window_size < 1) {
                window_size = 1;
            }
            int64_t ready_target = window_size - 1;
            if (ready_target < 0) {
                ready_target = 0;
            }
            int64_t configured_min = settings->VIDEO_CACHE_MIN_PREROLL_FRAMES;
            const int64_t required_ahead = std::min<int64_t>(configured_min, ready_target);
            min_frames_ahead.store(required_ahead);

            // If paused and playhead is no longer in cache, clear everything
            bool did_clear = clearCacheIfPaused(playhead, paused, cache);
            if (did_clear) {
                handleUserSeekWithPreroll(playhead, dir, timeline_end, preroll_frames);
            }

            // Compute the current caching window
            int64_t window_begin, window_end;
            computeWindowBounds(playhead,
                                dir,
                                ahead_count,
                                timeline_end,
                                window_begin,
                                window_end);

            // Attempt to fill any missing frames in that window
            int64_t max_frames_to_fetch = -1;
            if (!paused) {
                // Keep cache thread responsive during playback seeks so player
                // can start as soon as pre-roll is met instead of waiting for a
                // full cache window pass.
                max_frames_to_fetch = 8;
            }
            bool window_full = prefetchWindow(
                cache,
                window_begin,
                window_end,
                dir,
                reader,
                max_frames_to_fetch
            );

            // If paused and window was already full, keep playhead fresh
            if (paused && window_full) {
                cache->Touch(playhead);
            }

            // Sleep a short fraction of a frame interval
            int64_t sleep_us = static_cast<int64_t>(
                1000000.0 / reader->info.fps.ToFloat() / 4.0
            );
            std::this_thread::sleep_for(double_micro_sec(sleep_us));
        }
    }

} // namespace openshot
