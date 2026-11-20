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

#include "BenchmarkOptions.h"

#include <stdexcept>

namespace openshot {
namespace benchmark {

std::string BenchmarkUsage() {
	return "Usage: openshot-benchmark [--test <name>] [--list] [--omp-threads <n>] [--ff-threads <n>]";
}

static int ParseThreadArg(const std::string& flag, const std::string& value) {
	int parsed = 0;
	try {
		size_t consumed = 0;
		parsed = std::stoi(value, &consumed);
		if (consumed != value.size()) {
			throw std::invalid_argument("extra characters");
		}
	} catch (const std::exception&) {
		throw std::runtime_error("Invalid " + flag + " value: " + value + " (expected >= 2)");
	}

	if (parsed < 2) {
		throw std::runtime_error("Invalid " + flag + " value: " + value + " (expected >= 2)");
	}

	return parsed;
}

BenchmarkOptions ParseBenchmarkOptions(const std::vector<std::string>& args) {
	BenchmarkOptions options;

	for (size_t i = 0; i < args.size(); ++i) {
		const std::string& arg = args[i];
		if (arg == "--test" || arg == "-t") {
			if (i + 1 >= args.size()) {
				throw std::runtime_error("Missing value for --test");
			}
			options.filter_test = args[++i];
		} else if (arg == "--omp-threads") {
			if (i + 1 >= args.size()) {
				throw std::runtime_error("Missing value for --omp-threads");
			}
			options.omp_threads = ParseThreadArg("--omp-threads", args[++i]);
		} else if (arg == "--ff-threads") {
			if (i + 1 >= args.size()) {
				throw std::runtime_error("Missing value for --ff-threads");
			}
			options.ff_threads = ParseThreadArg("--ff-threads", args[++i]);
		} else if (arg == "--list" || arg == "-l") {
			options.list_only = true;
		} else if (arg == "--help" || arg == "-h") {
			options.show_help = true;
		} else {
			throw std::runtime_error("Unknown argument: " + arg);
		}
	}

	return options;
}

} // namespace benchmark
} // namespace openshot
