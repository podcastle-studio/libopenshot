/**
 * @file
 * @brief Unit tests for benchmark CLI option parsing
 * @author OpenShot Studios, LLC
 *
 * @ref License
 */

// Copyright (c) 2026 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "openshot_catch.h"

#include "BenchmarkOptions.h"

using namespace openshot::benchmark;

static void CHECK_RUNTIME_ERROR_CONTAINS(const std::vector<std::string>& args,
										 const std::string& expected_fragment) {
	try {
		(void) ParseBenchmarkOptions(args);
		FAIL("Expected ParseBenchmarkOptions() to throw std::runtime_error");
	} catch (const std::runtime_error& e) {
		CHECK(std::string(e.what()).find(expected_fragment) != std::string::npos);
	} catch (...) {
		FAIL("Expected std::runtime_error");
	}
}

TEST_CASE("Benchmark usage string includes new thread flags", "[benchmark][args]") {
	const std::string usage = BenchmarkUsage();
	CHECK(usage.find("--omp-threads <n>") != std::string::npos);
	CHECK(usage.find("--ff-threads <n>") != std::string::npos);
}

TEST_CASE("Benchmark args default correctly", "[benchmark][args]") {
	const BenchmarkOptions options = ParseBenchmarkOptions({});
	CHECK(options.filter_test.empty());
	CHECK_FALSE(options.list_only);
	CHECK_FALSE(options.show_help);
	CHECK(options.omp_threads == 0);
	CHECK(options.ff_threads == 0);
}

TEST_CASE("Benchmark args parse valid values", "[benchmark][args]") {
	const BenchmarkOptions options = ParseBenchmarkOptions({
		"--test", "Timeline",
		"--list",
		"--omp-threads", "12",
		"--ff-threads", "16"
	});

	CHECK(options.filter_test == "Timeline");
	CHECK(options.list_only);
	CHECK_FALSE(options.show_help);
	CHECK(options.omp_threads == 12);
	CHECK(options.ff_threads == 16);
}

TEST_CASE("Benchmark args reject invalid thread values", "[benchmark][args]") {
	CHECK_RUNTIME_ERROR_CONTAINS({"--omp-threads", "0"}, "Invalid --omp-threads value");
	CHECK_RUNTIME_ERROR_CONTAINS({"--omp-threads", "1"}, "Invalid --omp-threads value");
	CHECK_RUNTIME_ERROR_CONTAINS({"--omp-threads", "-1"}, "Invalid --omp-threads value");
	CHECK_RUNTIME_ERROR_CONTAINS({"--ff-threads", "0"}, "Invalid --ff-threads value");
	CHECK_RUNTIME_ERROR_CONTAINS({"--ff-threads", "1"}, "Invalid --ff-threads value");
	CHECK_RUNTIME_ERROR_CONTAINS({"--ff-threads", "-1"}, "Invalid --ff-threads value");
	CHECK_RUNTIME_ERROR_CONTAINS({"--omp-threads", "abc"}, "Invalid --omp-threads value");
}

TEST_CASE("Benchmark args reject missing values and unknown args", "[benchmark][args]") {
	CHECK_RUNTIME_ERROR_CONTAINS({"--test"}, "Missing value for --test");
	CHECK_RUNTIME_ERROR_CONTAINS({"--ff-threads"}, "Missing value for --ff-threads");
	CHECK_RUNTIME_ERROR_CONTAINS({"--wat"}, "Unknown argument");
}
