/**
 * @file
 * @brief Header file for the GpuSurfacePool class
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * @ref License
 */

// Copyright (c) 2008-2024 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef OPENSHOT_GPU_SURFACE_POOL_H
#define OPENSHOT_GPU_SURFACE_POOL_H

#include <cstddef>
#include <memory>
#include <vector>

#include "skia/include/core/SkColorType.h"
#include "skia/include/core/SkRefCnt.h"
#include "skia/include/core/SkSurface.h"

namespace openshot
{
	/**
	 * @brief Reuses GPU render targets instead of allocating one per frame.
	 *
	 * The glow pass allocates three surfaces per frame at 1080p and more at 4K.
	 * Allocating and freeing VRAM at that rate costs more than the draw does, so
	 * every GPU surface comes from here and goes back when it is done with.
	 * Surfaces are keyed by width, height and colour type; a release() followed by
	 * an acquire() with the same key hands back the identical allocation.
	 *
	 * **One pool per thread.** A Graphite surface belongs to the @c Recorder that
	 * created it, and GpuDevice hands out one recorder per thread, so Instance()
	 * returns a thread-local pool. Never move a surface between threads.
	 *
	 * The pool holds surfaces until clear() or thread exit. Nothing here is a
	 * cache with an eviction policy yet: the render path uses a handful of distinct
	 * sizes per export, so the working set is small and bounded. If that stops
	 * being true (many resolutions in one process), add a cap here.
	 */
	class GpuSurfacePool
	{
	public:
		/// Counters, for tests and for diagnosing a pool that is not being reused
		struct Stats
		{
			std::size_t created = 0;   ///< surfaces allocated from Skia
			std::size_t reused = 0;    ///< acquires satisfied from the free list
			std::size_t in_use = 0;    ///< handed out and not yet released
			std::size_t idle = 0;      ///< held on the free list
			std::size_t bytes = 0;     ///< approximate VRAM held (4 bytes/px)
		};

		/// This thread's pool
		static GpuSurfacePool& Instance();

		/// Empty every thread's pool. Called by GpuDevice::DestroyInstance()
		/// *before* the context goes away, because a surface outliving the context
		/// that made it crashes when it is finally released. Carries the same
		/// requirement as DestroyInstance(): no other thread may be rendering.
		static void DiscardAllPools();

		/// A render target of this size and colour type, or null when the GPU is
		/// unavailable or allocation failed. Call release() when finished.
		sk_sp<SkSurface> acquire(int width, int height, SkColorType color_type);

		/// Return a surface from acquire(). Passing null, or a surface this pool
		/// did not hand out, is ignored.
		void release(sk_sp<SkSurface> surface);

		/// Drop every idle surface. Surfaces still in use are left alone.
		void clear();

		Stats stats() const;

		GpuSurfacePool(const GpuSurfacePool&) = delete;
		GpuSurfacePool& operator=(const GpuSurfacePool&) = delete;

	private:
		GpuSurfacePool();
		~GpuSurfacePool();

		struct Entry
		{
			int width = 0;
			int height = 0;
			SkColorType color_type = kUnknown_SkColorType;
			sk_sp<SkSurface> surface;
			bool in_use = false;
		};

		/// Drop everything if the device has been rebuilt since we last looked
		void discardIfStale();

		/// Drop every surface, in use or not
		void discardAll();

		std::vector<Entry> entries;
		Stats counters;
		unsigned long long generation = 0;
	};
}

#endif
