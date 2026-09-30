#pragma once

// Reuses GPU render targets instead of allocating one per frame.

#include <chrono>
#include <cstddef>
#include <memory>
#include <vector>

#include "skia/include/core/SkColorSpace.h"
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
	 * **Idle surfaces are evicted.** A surface nobody has acquired for
	 * IdleLimit() (default 2 s, `OPENSHOT_GPU_POOL_IDLE_MS`) is dropped on the next
	 * acquire(), and so is the least recently used idle surface while the idle set
	 * exceeds IdleBytesLimit() (default 512 MiB, `OPENSHOT_GPU_POOL_IDLE_MB`). The
	 * sizes an export uses are not a handful: every text clip has its own frame
	 * size, and a keyframed glow's working surface grew by a few pixels every frame
	 * -- a 4K export with 45 text clips held 2.7 GB in 90 surfaces with 10 in use
	 * (2026-09-30). Anything drawn every frame is reused as before; what a finished
	 * clip leaves behind goes within seconds. Dropping a surface only makes its
	 * texture purgeable in the recorder's cache, so an eviction also asks
	 * GpuDevice to clean up what has sat unused that long.
	 *
	 * The rest is held until clear(), GpuDevice::ReleaseThreadResources() or
	 * thread exit (which calls it); a thread that outlives its export (a
	 * service's worker thread) should call ReleaseThreadResources() when the
	 * export is done.
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
			std::size_t idle_bytes = 0; ///< the part of @c bytes on the free list
			std::size_t evicted = 0;   ///< idle surfaces dropped by the eviction policy
		};

		/// This thread's pool
		static GpuSurfacePool& Instance();

		/// Empty every thread's pool. Called by GpuDevice::DestroyInstance()
		/// *before* the context goes away, because a surface outliving the context
		/// that made it crashes when it is finally released. Carries the same
		/// requirement as DestroyInstance(): no other thread may be rendering.
		static void DiscardAllPools();

		/// Empty the calling thread's pool, if it has one, without creating it.
		/// GpuDevice::ReleaseThreadResources() calls it just before that thread's
		/// recorder goes, because every surface in the pool belongs to it.
		static void DiscardCurrentThread();

		/// Construct the statics DiscardAllPools() uses, taking no lock. GpuDevice calls it before
		/// registering its exit-time teardown, so they are destroyed after that teardown runs.
		static void ConstructStatics();

		/// A render target of this size, colour type and colour space, or null when
		/// the GPU is unavailable or allocation failed. Call release() when finished.
		///
		/// The surface's canvas comes back in the state a new surface's would be —
		/// identity transform, empty save stack, and no clip beyond one set at the
		/// base save level — because a recycled surface otherwise carries the
		/// previous user's transform, which is not part of the pixels and so
		/// survives clearing them. SkCanvas gives no way to drop a base-level clip,
		/// so a user that clips must do so inside a save(); GpuOffscreen does. Its
		/// CONTENTS are still undefined; clear them.
		///
		/// @a color_space defaults to null, which is Skia's legacy mode: no gamma
		/// conversion on blending or on readback. That is deliberate — the raster
		/// surfaces this replaces are built with SkImageInfo::MakeN32Premul, which
		/// also carries no colour space, and attaching sRGB here would silently make
		/// every blend gamma-correct and change the output.
		sk_sp<SkSurface> acquire(int width, int height, SkColorType color_type,
								 sk_sp<SkColorSpace> color_space = nullptr);

		/// Return a surface from acquire(). Passing null, or a surface this pool
		/// did not hand out, is ignored.
		void release(sk_sp<SkSurface> surface);

		/// Drop every idle surface. Surfaces still in use are left alone.
		void clear();

		Stats stats() const;

		/// How long a surface may sit idle before acquire() drops it
		/// (`OPENSHOT_GPU_POOL_IDLE_MS`, default 2000; 0 keeps every idle surface).
		static std::chrono::milliseconds IdleLimit();

		/// Idle bytes the pool keeps at most, least recently used dropped first
		/// (`OPENSHOT_GPU_POOL_IDLE_MB`, default 512; 0 = no cap).
		static std::size_t IdleBytesLimit();

		/// Override both limits for every pool in the process (a service's config, or a
		/// test). Takes effect on the next acquire() on each thread.
		static void SetIdlePolicy(std::chrono::milliseconds idle_limit, std::size_t idle_bytes_limit);

		/// Every pool in the process at once, from counters kept as surfaces come
		/// and go (no pool is walked, so any thread may ask while others render).
		/// @c misses counts release() calls that matched nothing: a surface given
		/// back on a thread other than the one whose pool made it, which that pool
		/// then holds as in use until clear() or thread exit.
		struct GlobalStats
		{
			std::size_t pools = 0;
			std::size_t created = 0;
			std::size_t in_use = 0;
			std::size_t bytes = 0;
			std::size_t misses = 0;
			std::size_t evicted = 0;
		};
		static GlobalStats Global();

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
			sk_sp<SkColorSpace> color_space;
			sk_sp<SkSurface> surface;
			bool in_use = false;
			std::chrono::steady_clock::time_point last_used;   ///< last acquire or release
		};

		/// Put a recycled surface's canvas back into a new surface's state
		static void resetCanvas(SkSurface* surface);

		/// Drop everything if the device has been rebuilt since we last looked
		void discardIfStale();

		/// Drop every surface, in use or not
		void discardAll();

		/// Apply the eviction policy to the free list; see the class comment.
		void evictIdle(std::chrono::steady_clock::time_point now);

		std::vector<Entry> entries;
		Stats counters;
		unsigned long long generation = 0;
	};
}

