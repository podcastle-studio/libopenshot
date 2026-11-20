/**
 * @file
 * @brief Unit tests for openshot::ImageReader
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * @ref License
 */

// Copyright (c) 2008-2025 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "openshot_catch.h"

#include "ImageReader.h"
#include "Exceptions.h"
#include "Frame.h"
#include <sstream>

using namespace openshot;

TEST_CASE( "Invalid_Path_ImageReader", "[libopenshot][imagereader]" )
{
	// Check invalid path and error details
	const std::string invalid_path = "/tmp/__openshot_missing_test_file__.png";
	try {
		ImageReader r(invalid_path);
		FAIL("Expected InvalidFile for missing image path");
	} catch (const InvalidFile& e) {
		const std::string message = e.what();
		CHECK(message.find("ImageReader could not open image file.") != std::string::npos);
		CHECK(message.find(invalid_path) != std::string::npos);
	}
}

TEST_CASE( "Duration_And_Length_ImageReader", "[libopenshot][imagereader]" )
{
	// Create a reader
	std::stringstream path;
	path << TEST_MEDIA_PATH << "front.png";
	ImageReader r(path.str());
	r.Open();

	// Duration and frame count should be aligned to fps (1 hour at 30 fps)
	CHECK(r.info.fps.num == 30);
	CHECK(r.info.fps.den == 1);
	CHECK(r.info.video_length == 108000);
	CHECK(r.info.duration == Approx(3600.0f).margin(0.001f));

	r.Close();
}
