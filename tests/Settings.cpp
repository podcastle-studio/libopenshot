/**
 * @file
 * @brief Unit tests for openshot::Color
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * @ref License
 */

// Copyright (c) 2008-2019 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "openshot_catch.h"

#include <algorithm>
#include "OpenMPUtilities.h"
#include "Settings.h"
#include <omp.h>


using namespace openshot;

TEST_CASE( "Constructor", "[libopenshot][settings]" )
{
	int cpu_count = std::max(2, omp_get_num_procs());

	// Create an empty color
	Settings *s = Settings::Instance();

	CHECK(s->OMP_THREADS == cpu_count);
	CHECK(s->FF_THREADS == cpu_count);
	CHECK(s->DefaultOMPThreads() == cpu_count);
	CHECK(s->DefaultFFThreads() == cpu_count);
	CHECK(s->EffectiveOMPThreads() == cpu_count);
	CHECK(omp_get_max_threads() == cpu_count);
	CHECK_FALSE(s->HIGH_QUALITY_SCALING);
}

TEST_CASE( "Change settings", "[libopenshot][settings]" )
{
	// Create an empty color
	Settings *s = Settings::Instance();
	int original_runtime_threads = omp_get_max_threads();
	int original_omp_threads = s->OMP_THREADS;
	int original_ff_threads = s->FF_THREADS;
	const int requested_omp_threads = std::min(13, s->MaxAllowedThreads());
	s->OMP_THREADS = requested_omp_threads;
	s->FF_THREADS = 12;
	s->HIGH_QUALITY_SCALING = true;
	Settings::Instance();

	CHECK(s->OMP_THREADS == requested_omp_threads);
	CHECK(s->FF_THREADS == 12);
	CHECK(s->EffectiveOMPThreads() == requested_omp_threads);
	CHECK(s->HIGH_QUALITY_SCALING == true);
	CHECK(omp_get_max_threads() == requested_omp_threads);

	CHECK(Settings::Instance()->OMP_THREADS == requested_omp_threads);
	CHECK(Settings::Instance()->FF_THREADS == 12);
	CHECK(Settings::Instance()->EffectiveOMPThreads() == requested_omp_threads);
	CHECK(Settings::Instance()->HIGH_QUALITY_SCALING == true);

	// Restore prior OpenMP runtime state for later tests.
	s->OMP_THREADS = original_omp_threads;
	s->FF_THREADS = original_ff_threads;
	Settings::Instance();
	omp_set_num_threads(original_runtime_threads);
}

TEST_CASE( "Clamp settings to machine limits", "[libopenshot][settings]" )
{
	Settings *s = Settings::Instance();
	const int original_omp_threads = s->OMP_THREADS;
	const int original_ff_threads = s->FF_THREADS;
	const int original_runtime_threads = omp_get_max_threads();
	const int max_threads = s->MaxAllowedThreads();

	s->OMP_THREADS = max_threads + 50;
	s->FF_THREADS = max_threads + 50;
	Settings::Instance();

	CHECK(s->EffectiveOMPThreads() == max_threads);
	CHECK(OPEN_MP_NUM_PROCESSORS == max_threads);
	CHECK(FF_VIDEO_NUM_PROCESSORS == max_threads);
	CHECK(FF_AUDIO_NUM_PROCESSORS == max_threads);
	CHECK(omp_get_max_threads() == max_threads);

	s->OMP_THREADS = original_omp_threads;
	s->FF_THREADS = original_ff_threads;
	Settings::Instance();
	omp_set_num_threads(original_runtime_threads);
}

TEST_CASE( "Debug logging", "[libopenshot][settings][environment]")
{
	// Check the environment
	auto envvar = std::getenv("LIBOPENSHOT_DEBUG");
	const auto is_enabled = bool(envvar != nullptr);

	CHECK(Settings::Instance()->DEBUG_TO_STDERR == is_enabled);
}
