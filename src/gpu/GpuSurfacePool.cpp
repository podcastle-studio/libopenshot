// Reuses GPU render targets instead of allocating one per frame. See GpuSurfacePool.h.

#include "GpuSurfacePool.h"

#include "GpuDevice.h"

#include <mutex>

#include "skia/include/core/SkAlphaType.h"
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
}

GpuSurfacePool::GpuSurfacePool()
{
	std::lock_guard<std::mutex> lock(poolRegistryMutex());
	poolRegistry().push_back(this);
}

GpuSurfacePool::~GpuSurfacePool()
{
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

void GpuSurfacePool::DiscardAllPools()
{
	std::lock_guard<std::mutex> lock(poolRegistryMutex());
	for (GpuSurfacePool* pool : poolRegistry())
		pool->discardAll();
}

void GpuSurfacePool::discardAll()
{
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
	entries.clear();
	generation = current;
}

sk_sp<SkSurface> GpuSurfacePool::acquire(int width, int height, SkColorType color_type,
										 sk_sp<SkColorSpace> color_space)
{
	if (width <= 0 || height <= 0 || color_type == kUnknown_SkColorType)
		return nullptr;

	discardIfStale();

	for (Entry& entry : entries) {
		if (!entry.in_use && entry.width == width && entry.height == height &&
			entry.color_type == color_type &&
			SkColorSpace::Equals(entry.color_space.get(), color_space.get())) {
			entry.in_use = true;
			counters.reused++;
			return entry.surface;
		}
	}

#ifdef OPENSHOT_HAVE_SKIA_GPU
	skgpu::graphite::Recorder* recorder = GpuDevice::Instance().recorder();
	if (!recorder)
		return nullptr;

	const SkImageInfo info =
		SkImageInfo::Make(width, height, color_type, kPremul_SkAlphaType, color_space);
	sk_sp<SkSurface> surface = SkSurfaces::RenderTarget(recorder, info);
	if (!surface)
		return nullptr;

	Entry entry;
	entry.width = width;
	entry.height = height;
	entry.color_type = color_type;
	entry.color_space = color_space;
	entry.surface = surface;
	entry.in_use = true;
	entries.push_back(entry);
	counters.created++;
	return surface;
#else
	return nullptr;
#endif
}

void GpuSurfacePool::release(sk_sp<SkSurface> surface)
{
	if (!surface)
		return;
	discardIfStale();
	for (Entry& entry : entries) {
		if (entry.surface == surface) {
			entry.in_use = false;
			return;
		}
	}
}

void GpuSurfacePool::clear()
{
	for (auto it = entries.begin(); it != entries.end();) {
		if (it->in_use)
			++it;
		else
			it = entries.erase(it);
	}
}

GpuSurfacePool::Stats GpuSurfacePool::stats() const
{
	Stats result = counters;
	result.in_use = 0;
	result.idle = 0;
	result.bytes = 0;
	for (const Entry& entry : entries) {
		if (entry.in_use)
			result.in_use++;
		else
			result.idle++;
		result.bytes += static_cast<std::size_t>(entry.width) *
						static_cast<std::size_t>(entry.height) * 4u;
	}
	return result;
}
