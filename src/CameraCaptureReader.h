/**
 * @file
 * @brief Header file for live camera capture readers
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * @ref License
 */

// Copyright (c) 2008-2026 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef OPENSHOT_CAMERACAPTUREREADER_H
#define OPENSHOT_CAMERACAPTUREREADER_H

#include "AudioDevices.h"
#include "ScreenCaptureReader.h"

#include <memory>

namespace openshot
{
	enum CameraCaptureBackend
	{
		CAMERA_CAPTURE_AUTO = 0,
		CAMERA_CAPTURE_V4L2 = 1,
		CAMERA_CAPTURE_WINDOWS_DSHOW = 2,
		CAMERA_CAPTURE_MAC_AVFOUNDATION = 3
	};

	struct CameraCaptureSettings
	{
		CameraCaptureBackend backend = CAMERA_CAPTURE_AUTO;
		std::string device = "/dev/video0";
		int width = 1280;
		int height = 720;
		openshot::Fraction fps = openshot::Fraction(30, 1);
		std::map<std::string, std::string> options;
	};

	class CameraCaptureReader : public ReaderBase
	{
	public:
		explicit CameraCaptureReader(const CameraCaptureSettings& settings);
		~CameraCaptureReader() override;

		void Close() override;
		CacheBase* GetCache() override { return NULL; };
		std::shared_ptr<openshot::Frame> GetFrame(int64_t number) override;
		bool IsOpen() override { return reader && reader->IsOpen(); };
		std::string Name() override { return "CameraCaptureReader"; };
		std::string Json() const override;
		void SetJson(const std::string value) override;
		Json::Value JsonValue() const override;
		void SetJsonValue(const Json::Value root) override;
		void Open() override;

		openshot::CaptureReaderStats GetStats() const;
		CameraCaptureSettings GetSettings() const { return settings; };
		static bool IsBackendSupported(CameraCaptureBackend backend);
		static CameraCaptureBackend DefaultBackend();
		static AudioDeviceList GetDeviceNames(CameraCaptureBackend backend = CAMERA_CAPTURE_AUTO);

	private:
		void ValidateSettings() const;
		ScreenCaptureSettings ToDeviceSettings() const;
		void RebuildReader();

		CameraCaptureSettings settings;
		std::unique_ptr<ScreenCaptureReader> reader;
	};
}

#endif
