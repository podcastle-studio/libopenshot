/**
 * @file
 * @brief Shared benchmark CLI option parsing helpers
 * @author OpenShot Studios, LLC
 *
 * @ref License
 */

// Copyright (c) 2026 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef OPENSHOT_BENCHMARK_OPTIONS_H
#define OPENSHOT_BENCHMARK_OPTIONS_H

#include <string>
#include <vector>

namespace openshot {
namespace benchmark {

struct BenchmarkOptions {
	std::string filter_test;
	bool list_only = false;
	bool show_help = false;
	int omp_threads = 0;
	int ff_threads = 0;
};

std::string BenchmarkUsage();
BenchmarkOptions ParseBenchmarkOptions(const std::vector<std::string>& args);

} // namespace benchmark
} // namespace openshot

#endif
