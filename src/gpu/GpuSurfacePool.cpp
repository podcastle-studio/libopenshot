// Reuses GPU render targets instead of allocating one per frame. See GpuSurfacePool.h.

#include "GpuSurfacePool.h"

#include "GpuDevice.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>

#include "skia/include/core/SkAlphaType.h"
#include "skia/include/core/SkCanvas.h"
#include "skia/include/core/SkColorSpace.h"
#include "skia/include/core/SkImageInfo.h"

#ifdef OPENSHOT_HAVE_SKIA_GPU
#include "skia/include/gpu/graphite/Recorder.h"
#include "skia/include/gpu/graphite/Surface.h"
#endif

using namespace openshot;

namespace
{
	// Every live pool, so a device teardown can empty them all while the context
	// they draw from is still alive. Pools are thread-local; this is the only
	// thing that knows about all of them.
	std::mutex& poolRegistryMutex()
	{
		static std::mutex m;
		return m;
	}
	std::vector<GpuSurfacePool*>& poolRegistry()
	{
		static std::vector<GpuSurfacePool*> pools;
		return pools;
	}

	// The calling thread's pool once Instance() has built it. A plain pointer, so
	// DiscardCurrentThread() can ask during thread exit without constructing one.
	thread_local GpuSurfacePool* tThisThreadPool = nullptr;

	std::atomic<std::size_t> gCreated{0}, gInUse{0}, gBytes{0}, gMisses{0}, gEvicted{0};

	std::size_t entryBytes(int width, int height)
	{
		return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
	}

	// OPENSHOT_GPU_POOL_TRACE=1: log every surface the pool allocates or evicts (size, colour
	// type, what the pool holds) to stderr. Diagnostics only; read once.
	bool traceEnabled()
	{
		static const bool enabled = [] {
			const char* value = std::getenv("OPENSHOT_GPU_POOL_TRACE");
			return value && *value && *value != '0';
		}();
		return enabled;
	}

	// A non-negative integer from the environment, or the default when unset or unparsable.
	long long envNumber(const char* name, long long fallback)
	{
		const char* value = std::getenv(name);
		if (!value || !*value)
			return fallback;
		char* end = nullptr;
		const long long parsed = std::strtoll(value, &end, 10);
		if (end == value || parsed < 0)
			return fallback;
		return parsed;
	}
}

GpuSurfacePool::GpuSurfacePool()
{
	tThisThreadPool = this;
	std::lock_guard<std::mutex> lock(poolRegistryMutex());
	poolRegistry().push_back(this);
}

GpuSurfacePool::~GpuSurfacePool()
{
	discardAll();   // keeps the global counters true for a pool that dies with its thread
	if (tThisThreadPool == this)
		tThisThreadPool = nullptr;
	std::lock_guard<std::mutex> lock(poolRegistryMutex());
	std::vector<GpuSurfacePool*>& pools = poolRegistry();
	for (auto it = pools.begin(); it != pools.end(); ++it) {
		if (*it == this) {
			pools.erase(it);
			break;
		}
	}
}

GpuSurfacePool& GpuSurfacePool::Instance()
{
	// Thread-local: a Graphite surface belongs to the recorder that made it, and
	// GpuDevice hands out one recorder per thread.
	thread_local GpuSurfacePool pool;
	return pool;
}

void GpuSurfacePool::ConstructStatics()
{
	(void)poolRegistryMutex();
	(void)poolRegistry();
}

namespace
{
	// The limits, from the environment once, until SetIdlePolicy() overrides them.
	std::atomic<long long>& idleLimitMs()
	{
		static std::atomic<long long> ms{envNumber("OPENSHOT_GPU_POOL_IDLE_MS", 2000)};
		return ms;
	}
	std::atomic<long long>& idleBytesLimit()
	{
		static std::atomic<long long> bytes{envNumber("OPENSHOT_GPU_POOL_IDLE_MB", 512) << 20};
		return bytes;
	}
}

std::chrono::milliseconds GpuSurfacePool::IdleLimit()
{
	return std::chrono::milliseconds(idleLimitMs().load(std::memory_order_relaxed));
}

std::size_t GpuSurfacePool::IdleBytesLimit()
{
	return static_cast<std::size_t>(idleBytesLimit().load(std::memory_order_relaxed));
}

void GpuSurfacePool::SetIdlePolicy(std::chrono::milliseconds idle_limit, std::size_t idle_bytes_limit)
{
	idleLimitMs().store(std::max<long long>(0, idle_limit.count()), std::memory_order_relaxed);
	idleBytesLimit().store(static_cast<long long>(idle_bytes_limit), std::memory_order_relaxed);
}

void GpuSurfacePool::DiscardAllPools()
{
	std::lock_guard<std::mutex> lock(poolRegistryMutex());
	for (GpuSurfacePool* pool : poolRegistry())
		pool->discardAll();
}

void GpuSurfacePool::DiscardCurrentThread()
{
	if (GpuSurfacePool* pool = tThisThreadPool)
		pool->discardAll();
}

void GpuSurfacePool::discardAll()
{
	for (const Entry& entry : entries) {
		gBytes -= entryBytes(entry.width, entry.height);
		if (entry.in_use)
			gInUse--;
	}
	entries.clear();
}

void GpuSurfacePool::discardIfStale()
{
	const unsigned long long current = GpuDevice::Generation();
	if (generation == current)
		return;
	// Belt and braces. DestroyInstance() empties every registered pool while the
	// context is still alive, which is the path that actually has to work; this
	// only catches a pool that somehow missed that sweep, and by now its surfaces
	// are already dangling, so there is nothing better to do than drop them.
	discardAll();
	generation = current;
}

sk_sp<SkSurface> GpuSurfacePool::acquire(int width, int height, SkColorType color_type,
										 sk_sp<SkColorSpace> color_space)
{
	if (width <= 0 || height <= 0 || color_type == kUnknown_SkColorType)
		return nullptr;

	discardIfStale();

	const auto now = std::chrono::steady_clock::now();
	for (Entry& entry : entries) {
		if (!entry.in_use && entry.width == width && entry.height == height &&
			entry.color_type == color_type &&
			SkColorSpace::Equals(entry.color_space.get(), color_space.get())) {
			entry.in_use = true;
			entry.last_used = now;
			gInUse++;
			counters.reused++;
			resetCanvas(entry.surface.get());
			// The match first, so a surface that is wanted right now is never the one evicted --
			// and a copy of it first, because evictIdle() erases from `entries` and `entry` is a
			// reference into it. Returning through the reference handed back a moved-from null
			// about one run in three (golden `unit.gpu_crop`, 2026-09-30).
			sk_sp<SkSurface> surface = entry.surface;
			evictIdle(now);
			return surface;
		}
	}
	evictIdle(now);

#ifdef OPENSHOT_HAVE_SKIA_GPU
	skgpu::graphite::Recorder* recorder = GpuDevice::Instance().recorder();
	if (!recorder) {
		if (traceEnabled())
			std::fprintf(stderr, "GpuSurfacePool: no recorder for %dx%d\n", width, height);
		return nullptr;
	}

	const SkImageInfo info =
		SkImageInfo::Make(width, height, color_type, kPremul_SkAlphaType, color_space);
	sk_sp<SkSurface> surface = SkSurfaces::RenderTarget(recorder, info);
	if (!surface) {
		if (traceEnabled())
			std::fprintf(stderr, "GpuSurfacePool: SkSurfaces::RenderTarget failed for %dx%d ct=%d\n",
						 width, height, static_cast<int>(color_type));
		return nullptr;
	}

	Entry entry;
	entry.width = width;
	entry.height = height;
	entry.color_type = color_type;
	entry.color_space = color_space;
	entry.surface = surface;
	entry.in_use = true;
	entry.last_used = now;
	entries.push_back(entry);
	counters.created++;
	gCreated++;
	gInUse++;
	gBytes += entryBytes(width, height);
	if (traceEnabled()) {
		// One line per allocation, so a memory trace can say which sizes fill the pool.
		std::size_t idle = 0;
		for (const Entry& e : entries)
			if (!e.in_use)
				idle++;
		std::fprintf(stderr, "GpuSurfacePool: new %dx%d ct=%d (%zu MiB) pool: %zu entries, %zu idle, %zu MiB\n",
					 width, height, static_cast<int>(color_type), entryBytes(width, height) >> 20,
					 entries.size(), idle, gBytes.load() >> 20);
	}
	return surface;
#else
	return nullptr;
#endif
}

void GpuSurfacePool::resetCanvas(SkSurface* surface)
{
	if (!surface)
		return;
	SkCanvas* canvas = surface->getCanvas();
	if (!canvas)
		return;
	// A surface carries its canvas, and a recycled surface carries the previous
	// user's canvas STATE with it — the transform and clip are not part of the
	// pixels, so clearing the surface does not touch them. SkSurfaces::Raster
	// hands out a fresh canvas every time, so the call sites this pool replaced
	// were entitled to assume an identity transform, and several of them scale or
	// translate without a matching save/restore. Left alone, those compound on
	// every acquire: a glow silhouette drawn at scale s comes out at s^2 on the
	// second frame and s^3 on the third.
	//
	// restoreToCount(1) unwinds any unbalanced save, and resetMatrix() clears a
	// transform applied at the base level, where there is nothing to restore to.
	// Together they give back the same canvas state a new surface would.
	canvas->restoreToCount(1);
	canvas->resetMatrix();
}

void GpuSurfacePool::release(sk_sp<SkSurface> surface)
{
	if (!surface)
		return;
	discardIfStale();
	for (Entry& entry : entries) {
		if (entry.surface == surface) {
			if (entry.in_use)
				gInUse--;
			entry.in_use = false;
			entry.last_used = std::chrono::steady_clock::now();
			return;
		}
	}
	gMisses++;
	if (traceEnabled())
		std::fprintf(stderr, "GpuSurfacePool: release of %dx%d matched nothing in this thread's pool\n",
					 surface->width(), surface->height());
}

void GpuSurfacePool::clear()
{
	for (auto it = entries.begin(); it != entries.end();) {
		if (it->in_use) {
			++it;
		} else {
			gBytes -= entryBytes(it->width, it->height);
			it = entries.erase(it);
		}
	}
}

void GpuSurfacePool::evictIdle(std::chrono::steady_clock::time_point now)
{
	const std::chrono::milliseconds idle_limit = IdleLimit();
	const std::size_t bytes_limit = IdleBytesLimit();
	if (idle_limit.count() <= 0 && bytes_limit == 0)
		return;

	std::size_t idle_bytes = 0;
	for (const Entry& entry : entries)
		if (!entry.in_use)
			idle_bytes += entryBytes(entry.width, entry.height);

	std::size_t evicted = 0;
	auto drop = [&](std::vector<Entry>::iterator it) {
		const std::size_t bytes = entryBytes(it->width, it->height);
		if (traceEnabled())
			std::fprintf(stderr, "GpuSurfacePool: evict %dx%d ct=%d (%zu MiB) idle %lld ms\n",
						 it->width, it->height, static_cast<int>(it->color_type), bytes >> 20,
						 static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
							 now - it->last_used).count()));
		gBytes -= bytes;
		idle_bytes -= bytes;
		evicted++;
		return entries.erase(it);
	};

	// 1. Anything idle for longer than the limit.
	if (idle_limit.count() > 0) {
		for (auto it = entries.begin(); it != entries.end();) {
			if (!it->in_use && now - it->last_used > idle_limit)
				it = drop(it);
			else
				++it;
		}
	}

	// 2. Least recently used first while the idle set is over its byte cap.
	while (bytes_limit > 0 && idle_bytes > bytes_limit) {
		auto oldest = entries.end();
		for (auto it = entries.begin(); it != entries.end(); ++it)
			if (!it->in_use && (oldest == entries.end() || it->last_used < oldest->last_used))
				oldest = it;
		if (oldest == entries.end())
			break;
		drop(oldest);
	}

	if (evicted == 0)
		return;
	counters.evicted += evicted;
	gEvicted += evicted;
	// The surfaces are gone from here, but Skia keeps their textures as purgeable
	// entries in this thread's recorder cache until something asks it to let go.
	GpuDevice::PerformDeferredCleanup(idle_limit);
}

GpuSurfacePool::GlobalStats GpuSurfacePool::Global()
{
	GlobalStats result;
	{
		std::lock_guard<std::mutex> lock(poolRegistryMutex());
		result.pools = poolRegistry().size();
	}
	result.created = gCreated.load();
	result.in_use = gInUse.load();
	result.bytes = gBytes.load();
	result.misses = gMisses.load();
	result.evicted = gEvicted.load();
	return result;
}

GpuSurfacePool::Stats GpuSurfacePool::stats() const
{
	Stats result = counters;
	result.in_use = 0;
	result.idle = 0;
	result.bytes = 0;
	for (const Entry& entry : entries) {
		const std::size_t bytes = entryBytes(entry.width, entry.height);
		if (entry.in_use) {
			result.in_use++;
		} else {
			result.idle++;
			result.idle_bytes += bytes;
		}
		result.bytes += bytes;
	}
	return result;
}
