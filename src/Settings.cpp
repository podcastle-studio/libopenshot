/**
 * @file
 * @brief Source file for global Settings class
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * @ref License
 */

// Copyright (c) 2008-2019 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <algorithm>
#include <cstdlib>
#include <omp.h>
#include "Settings.h"

using namespace openshot;

// Global reference to Settings
Settings *Settings::m_pInstance = nullptr;

int Settings::EffectiveOMPThreads() const
{
	return std::clamp(OMP_THREADS, 2, MaxAllowedThreads());
}

int Settings::MaxAllowedThreads() const
{
	return std::max(2, std::max(2, omp_get_num_procs()) * 3);
}

void Settings::ApplyOpenMPSettings()
{
	const int requested_threads = EffectiveOMPThreads();
	if (applied_omp_threads != requested_threads) {
		omp_set_num_threads(requested_threads);
		applied_omp_threads = requested_threads;
	}
}

// Create or Get an instance of the settings singleton
Settings *Settings::Instance()
{
	if (!m_pInstance) {
		// Create the actual instance of Settings only once
		m_pInstance = new Settings;
		const int machine_threads = std::max(2, omp_get_num_procs());
		m_pInstance->default_omp_threads = machine_threads;
		m_pInstance->default_ff_threads = machine_threads;
		m_pInstance->OMP_THREADS = machine_threads;
		m_pInstance->FF_THREADS = machine_threads;
		auto env_debug = std::getenv("LIBOPENSHOT_DEBUG");
		if (env_debug != nullptr)
			m_pInstance->DEBUG_TO_STDERR = true;
	}

	m_pInstance->ApplyOpenMPSettings();

	return m_pInstance;
}
