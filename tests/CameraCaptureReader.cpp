/**
 * @file
 * @brief Unit tests for openshot::CameraCaptureReader settings and metadata
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * @ref License
 */

// Copyright (c) 2008-2026 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "openshot_catch.h"

#include "CameraCaptureReader.h"
#include "Exceptions.h"

using namespace openshot;

TEST_CASE("Camera capture settings validation", "[libopenshot][cameracapturereader]")
{
	CameraCaptureSettings settings;
#if defined(__linux__)
	settings.backend = CAMERA_CAPTURE_V4L2;
	settings.device = "/dev/video0";
#elif defined(_WIN32)
	settings.backend = CAMERA_CAPTURE_WINDOWS_DSHOW;
	settings.device = "Integrated Camera";
#elif defined(__APPLE__)
	settings.backend = CAMERA_CAPTURE_MAC_AVFOUNDATION;
	settings.device = "0:none";
#endif
#if defined(__linux__) || defined(_WIN32) || defined(__APPLE__)
	settings.width = 640;
	settings.height = 480;
	settings.fps = Fraction(30, 1);

	CHECK_NOTHROW([&settings]() { CameraCaptureReader reader(settings); }());

	settings.device = "";
	CHECK_THROWS_AS([&settings]() { CameraCaptureReader reader(settings); }(), InvalidOptions);

	settings.device = "/dev/video0";
	settings.height = 0;
	CHECK_THROWS_AS([&settings]() { CameraCaptureReader reader(settings); }(), InvalidOptions);
#else
	CHECK_FALSE(CameraCaptureReader::IsBackendSupported(CAMERA_CAPTURE_V4L2));
#endif
}

TEST_CASE("Camera capture reader reports configured video info", "[libopenshot][cameracapturereader]")
{
	CameraCaptureSettings settings;
#if defined(__linux__)
	settings.backend = CAMERA_CAPTURE_V4L2;
	settings.device = "/dev/video9";
#elif defined(_WIN32)
	settings.backend = CAMERA_CAPTURE_WINDOWS_DSHOW;
	settings.device = "Integrated Camera";
#elif defined(__APPLE__)
	settings.backend = CAMERA_CAPTURE_MAC_AVFOUNDATION;
	settings.device = "0:none";
#endif
#if defined(__linux__) || defined(_WIN32) || defined(__APPLE__)
	settings.width = 1280;
	settings.height = 720;
	settings.fps = Fraction(24, 1);

	CameraCaptureReader reader(settings);
	CHECK(reader.Name() == "CameraCaptureReader");
	CHECK(reader.info.has_video == true);
	CHECK(reader.info.has_audio == false);
	CHECK(reader.info.width == 1280);
	CHECK(reader.info.height == 720);
	CHECK(reader.info.fps.num == 24);

	const Json::Value json = reader.JsonValue();
	CHECK(json["type"].asString() == "CameraCaptureReader");
#if defined(__linux__)
	CHECK(json["device"].asString() == "/dev/video9");
#elif defined(_WIN32)
	CHECK(json["device"].asString() == "Integrated Camera");
#elif defined(__APPLE__)
	CHECK(json["device"].asString() == "0:none");
#endif
	CHECK(json["width"].asInt() == 1280);
	CHECK(json["height"].asInt() == 720);
#else
	CHECK_FALSE(CameraCaptureReader::IsBackendSupported(CAMERA_CAPTURE_V4L2));
#endif
}

TEST_CASE("Camera capture default backend follows platform", "[libopenshot][cameracapturereader]")
{
#if defined(_WIN32)
	CHECK(CameraCaptureReader::IsBackendSupported(CAMERA_CAPTURE_WINDOWS_DSHOW));
	CHECK(CameraCaptureReader::DefaultBackend() == CAMERA_CAPTURE_WINDOWS_DSHOW);
#elif defined(__APPLE__)
	CHECK(CameraCaptureReader::IsBackendSupported(CAMERA_CAPTURE_MAC_AVFOUNDATION));
	CHECK(CameraCaptureReader::DefaultBackend() == CAMERA_CAPTURE_MAC_AVFOUNDATION);
#endif
}
