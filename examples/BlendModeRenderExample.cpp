/**
 * @file
 * @brief Rendering sample for libopenshot's clip blend modes
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * Runs the real libopenshot render path - openshot::Timeline composited by
 * openshot::FFmpegWriter into an H.264 MP4 - over two video clips, once per
 * openshot::BlendMode. This is the same pipeline a production render uses; the only thing
 * that changes between outputs is `overlay_clip.Blend(mode)`.
 *
 * Writes into <out>:
 *   NN-<mode>.mp4         the backdrop with the overlay blended at that mode
 *   blend-cycle.mp4       one file cycling every mode, with the mode name burnt in
 *   frames/NN-<mode>.png  a still from the middle of each segment, for pixel comparison
 *   README.txt            what the files are, and how to set the mode in clip JSON
 *
 * Usage:
 *   openshot-blend-render [--out DIR] [--base VIDEO] [--overlay VIDEO]
 *                         [--size WxH] [--fps N] [--seconds N] [--mode NAME]
 *                         [--audio] [--no-cycle] [--no-clips]
 *
 * @ref License
 */

// Copyright (c) 2008-2019 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "BlendModes.h"
#include "Clip.h"
#include "Enums.h"
#include "FFmpegWriter.h"
#include "Frame.h"
#include "QtTextReader.h"
#include "Timeline.h"

#include <QDir>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QPainter>
#include <QImage>
#include <QString>
#include <QTextStream>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifndef TEST_MEDIA_PATH
	#define TEST_MEDIA_PATH ""
#endif

using openshot::BlendMode;

namespace {

struct RenderSettings {
	std::string out_dir = "blend-render";
	std::string base_path = std::string(TEST_MEDIA_PATH) + "test_video.mp4";
	std::string overlay_path = std::string(TEST_MEDIA_PATH) + "run.mp4";
	double base_start = 4.0;      // skip past the dark lead-in of the default clip
	double overlay_start = 0.0;
	int width = 1280;
	int height = 720;
	int fps = 30;
	double seconds = 3.0;
	int crf = 23;                 // FFmpegWriter's "crf" option puts libx264 in constant-quality
	                              // mode, where it ignores the bitrate - so crf is the size knob
	std::string only_mode;   // empty == every mode
	bool audio = false;
	bool cycle = true;
	bool clips = true;
};

// Configure a writer the way a production render would, and open it
void open_writer(openshot::FFmpegWriter& writer, const RenderSettings& settings) {
	if (settings.audio)
		writer.SetAudioOptions(true, "aac", 48000, 2, openshot::LAYOUT_STEREO, 192000);
	else
		writer.SetAudioOptions(false, "", 48000, 2, openshot::LAYOUT_STEREO, 0);

	writer.SetVideoOptions(true, "libx264", openshot::Fraction(settings.fps, 1),
	                       settings.width, settings.height, openshot::Fraction(1, 1),
	                       false, false, 8000000);
	writer.PrepareStreams();
	writer.SetOption(openshot::VIDEO_STREAM, "crf", std::to_string(settings.crf));
	writer.SetOption(openshot::VIDEO_STREAM, "preset", "medium");
	// Tag the stream as bt709 so a browser and a player agree on how to decode the colours -
	// otherwise a comparison against a front-end render drifts on the untagged file alone.
	writer.SetOption(openshot::VIDEO_STREAM, "x264-params",
	                 "colorprim=bt709:transfer=bt709:colormatrix=bt709");
	writer.Open();
}

// Both layers are stretched to the frame, so the overlay covers the backdrop 1:1 and the
// blend mode is the only difference between one output and the next.
void configure_layer(openshot::Clip& clip, int layer, double position,
                     double source_start, double seconds) {
	clip.Layer(layer);
	clip.Position(position);
	clip.Start(source_start);
	clip.End(source_start + seconds);
	clip.scale = openshot::SCALE_STRETCH;
	clip.gravity = openshot::GRAVITY_TOP_LEFT;
}

/* ------------------------------- one file per mode -------------------------------- */

bool render_mode(const RenderSettings& settings, BlendMode mode, int index,
                 const QString& out_prefix, const QString& frames_prefix,
                 std::vector<QImage>& stills) {
	const std::string mode_name = openshot::BlendModeToString(mode);
	char filename[64];
	std::snprintf(filename, sizeof(filename), "%02d-%s", index, mode_name.c_str());

	const int64_t total_frames = (int64_t) std::lround(settings.seconds * settings.fps);

	openshot::Timeline timeline(settings.width, settings.height,
	                           openshot::Fraction(settings.fps, 1), 48000, 2,
	                           openshot::LAYOUT_STEREO);

	openshot::Clip base_clip(settings.base_path);
	configure_layer(base_clip, 1, 0.0, settings.base_start, settings.seconds);

	openshot::Clip overlay_clip(settings.overlay_path);
	configure_layer(overlay_clip, 2, 0.0, settings.overlay_start, settings.seconds);
	overlay_clip.Blend(mode);   // <-- the only thing that varies between renders

	timeline.AddClip(&base_clip);
	timeline.AddClip(&overlay_clip);
	timeline.Open();

	// Still from the middle of the clip, for a frame-accurate comparison against a
	// front-end render of the same two source frames
	const int64_t still_frame = std::max<int64_t>(1, total_frames / 2);
	auto frame = timeline.GetFrame(still_frame);
	frame->GetImage()->save(frames_prefix + QString::fromUtf8(filename) + ".png", "PNG");
	stills.push_back(frame->GetImage()->copy());

	const QString video_file = out_prefix + QString::fromUtf8(filename) + ".mp4";
	openshot::FFmpegWriter writer(video_file.toStdString());
	open_writer(writer, settings);
	writer.WriteFrame(&timeline, 1, total_frames);
	writer.Close();

	timeline.Close();

	std::cout << "  " << video_file.toStdString() << "  (+ still frame " << still_frame << ")\n";
	return true;
}

/* ---------------------------------- contact sheet -------------------------------- */

// One labelled grid of every mode's still, so the whole set can be eyeballed at a glance
void write_contact_sheet(const QString& out_file,
                         const std::vector<QImage>& stills,
                         const std::vector<std::string>& labels,
                         int columns, int cell_width) {
	if (stills.empty())
		return;

	const double aspect = (double) stills[0].height() / stills[0].width();
	const int cell_height = (int) std::lround(cell_width * aspect);
	const int label_height = 26;
	const int rows = (int) ((stills.size() + columns - 1) / columns);

	QImage sheet(columns * cell_width, rows * (cell_height + label_height),
	             QImage::Format_RGBA8888_Premultiplied);
	sheet.fill(QColor("#0d0c10"));

	QPainter painter(&sheet);
	painter.setRenderHints(QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing, true);
	QFont font("monospace", 0, QFont::Bold);
	font.setPixelSize(13);
	painter.setFont(font);

	for (size_t i = 0; i < stills.size(); ++i) {
		const int x = (int) (i % columns) * cell_width;
		const int y = (int) (i / columns) * (cell_height + label_height);
		painter.drawImage(QRect(x, y, cell_width, cell_height), stills[i]);
		painter.setPen(QColor("#d4ff4f"));
		painter.drawText(QRect(x + 8, y + cell_height, cell_width - 16, label_height),
		                 Qt::AlignVCenter | Qt::AlignLeft, QString::fromStdString(labels[i]));
	}
	painter.end();
	sheet.save(out_file, "PNG");
}

/* --------------------------- one file cycling every mode -------------------------- */

bool render_cycle(const RenderSettings& settings, const QString& out_file) {
	const int64_t segment_frames = (int64_t) std::lround(settings.seconds * settings.fps);
	const auto& modes = openshot::BlendModeList();

	openshot::Timeline timeline(settings.width, settings.height,
	                           openshot::Fraction(settings.fps, 1), 48000, 2,
	                           openshot::LAYOUT_STEREO);

	// The clips and their readers have to outlive the render, so keep them owned here
	std::vector<std::unique_ptr<openshot::Clip>> clips;
	std::vector<std::unique_ptr<openshot::QtTextReader>> labels;

	QFont label_font("monospace");
	label_font.setPixelSize(std::max(18, settings.height / 18));
	label_font.setBold(true);

	for (size_t i = 0; i < modes.size(); ++i) {
		const double position = i * settings.seconds;

		// Every segment replays the same source frames, so only the blend mode differs
		auto base_clip = std::make_unique<openshot::Clip>(settings.base_path);
		configure_layer(*base_clip, 1, position, settings.base_start, settings.seconds);

		auto overlay_clip = std::make_unique<openshot::Clip>(settings.overlay_path);
		configure_layer(*overlay_clip, 2, position, settings.overlay_start, settings.seconds);
		overlay_clip->Blend(modes[i]);

		// Burn the mode name in, so the file is watchable on its own
		auto label_reader = std::make_unique<openshot::QtTextReader>(
			settings.width, settings.height,
			settings.width / 24, -settings.height / 24,
			openshot::GRAVITY_BOTTOM_LEFT,
			openshot::BlendModeToString(modes[i]),
			label_font, "#ffffff", "transparent");
		label_reader->SetTextBackgroundColor("#c0000000");

		auto label_clip = std::make_unique<openshot::Clip>(label_reader.get());
		configure_layer(*label_clip, 3, position, 0.0, settings.seconds);

		timeline.AddClip(base_clip.get());
		timeline.AddClip(overlay_clip.get());
		timeline.AddClip(label_clip.get());

		clips.push_back(std::move(base_clip));
		clips.push_back(std::move(overlay_clip));
		clips.push_back(std::move(label_clip));
		labels.push_back(std::move(label_reader));
	}

	timeline.Open();

	openshot::FFmpegWriter writer(out_file.toStdString());
	open_writer(writer, settings);
	writer.WriteFrame(&timeline, 1, segment_frames * (int64_t) modes.size());
	writer.Close();

	timeline.Close();

	std::cout << "  " << out_file.toStdString() << "  ("
	          << (settings.seconds * modes.size()) << "s, "
	          << modes.size() << " segments)\n";
	return true;
}

/* ------------------------------------- README ------------------------------------ */

void write_readme(const QString& path, const RenderSettings& settings,
                  const std::vector<std::string>& rendered) {
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
		return;

	QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
	out.setCodec("UTF-8");
#endif
	out << "libopenshot blend-mode render sample\n"
	    << "====================================\n\n"
	    << "Rendered by examples/BlendModeRenderExample.cpp (openshot-blend-render) through the\n"
	    << "normal libopenshot render path: openshot::Timeline -> openshot::FFmpegWriter (libx264).\n\n"
	    << "Scene\n"
	    << "-----\n"
	    << "  layer 1 (backdrop) : " << QString::fromStdString(settings.base_path)
	    << " (from " << settings.base_start << "s)\n"
	    << "  layer 2 (blended)  : " << QString::fromStdString(settings.overlay_path)
	    << " (from " << settings.overlay_start << "s)\n"
	    << "  frame              : " << settings.width << "x" << settings.height
	    << " @ " << settings.fps << " fps, " << settings.seconds << "s per mode, "
	    << "h264 crf " << settings.crf << "\n"
	    << "  audio              : " << (settings.audio ? "aac 192k" : "none (video-only)") << "\n"
	    << "  geometry           : both layers stretched to the frame, so the overlay covers the\n"
	    << "                       backdrop 1:1 - the blend mode is the ONLY difference between files\n\n"
	    << "Files\n"
	    << "-----\n"
	    << "  NN-<mode>.mp4        one render per blend mode, in enum order\n"
	    << "  blend-cycle.mp4      every mode back to back, mode name burnt in\n"
	    << "  frames/NN-<mode>.png the middle frame of each segment, lossless, for pixel diffing\n"
	    << "  contact-sheet.png    all 16 stills in one labelled grid\n\n"
	    << "Setting the mode\n"
	    << "----------------\n"
	    << "  clip JSON : { \"layer\": 2, \"blendMode\": \"color-dodge\" }\n"
	    << "  C++       : clip.Blend(openshot::BLEND_COLOR_DODGE);\n\n"
	    << "  \"blendMode\" takes the CSS / canvas / PixiJS spelling, and the snake_case and\n"
	    << "  camelCase variants of it. An unknown name is ignored rather than silently changing\n"
	    << "  the render. \"source-over\" is accepted as a synonym for \"normal\".\n\n"
	    << "  The mode applies where the clip is flattened onto the layers beneath it, so it\n"
	    << "  composites against whatever the lower tracks have already produced.\n\n"
	    << "Modes rendered (" << (int) rendered.size() << ")\n"
	    << "-----------------\n";
	for (size_t i = 0; i < rendered.size(); ++i)
		out << "  " << QString::fromStdString(rendered[i]) << "\n";
	out << "\nBlend math follows W3C Compositing and Blending Level 1 (https://www.w3.org/TR/compositing-1/),\n"
	    << "the same spec behind canvas globalCompositeOperation, CSS mix-blend-mode and PixiJS\n"
	    << "blend modes. For a still-image parity check against a live canvas 2D render, see the\n"
	    << "comparison.html produced by openshot-blend-modes.\n";
	out.flush();
}

void print_usage() {
	std::cout <<
		"usage: openshot-blend-render [options]\n"
		"  --out DIR        output directory (default: ./blend-render)\n"
		"  --base PATH      backdrop video/image (layer 1)\n"
		"  --overlay PATH   blended video/image (layer 2)\n"
		"  --size WxH       render size (default: 1280x720)\n"
		"  --fps N          frame rate (default: 30)\n"
		"  --seconds N      duration per mode (default: 3)\n"
		"  --base-start N   seconds into the backdrop source to start (default: 4)\n"
		"  --overlay-start N seconds into the overlay source to start (default: 0)\n"
		"  --mode NAME      render only this mode (e.g. multiply, color-dodge)\n"
		"  --crf N          libx264 quality, 0 (lossless) - 51 (default: 23)\n"
		"  --audio          include an AAC audio track (default: video-only)\n"
		"  --no-clips       skip the per-mode MP4s\n"
		"  --no-cycle       skip the combined blend-cycle.mp4\n";
}

}  // anonymous namespace

int main(int argc, char** argv) {
	// QtTextReader (the burnt-in mode labels) needs a Qt application instance
	QGuiApplication app(argc, argv);

	RenderSettings settings;

	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		const bool has_value = (i + 1 < argc);
		if (arg == "--help" || arg == "-h") {
			print_usage();
			return 0;
		} else if (arg == "--out" && has_value) {
			settings.out_dir = argv[++i];
		} else if (arg == "--base" && has_value) {
			settings.base_path = argv[++i];
		} else if (arg == "--overlay" && has_value) {
			settings.overlay_path = argv[++i];
		} else if (arg == "--mode" && has_value) {
			settings.only_mode = argv[++i];
		} else if (arg == "--fps" && has_value) {
			settings.fps = std::stoi(argv[++i]);
		} else if (arg == "--seconds" && has_value) {
			settings.seconds = std::stod(argv[++i]);
		} else if (arg == "--crf" && has_value) {
			settings.crf = std::stoi(argv[++i]);
		} else if (arg == "--base-start" && has_value) {
			settings.base_start = std::stod(argv[++i]);
		} else if (arg == "--overlay-start" && has_value) {
			settings.overlay_start = std::stod(argv[++i]);
		} else if (arg == "--audio") {
			settings.audio = true;
		} else if (arg == "--no-cycle") {
			settings.cycle = false;
		} else if (arg == "--no-clips") {
			settings.clips = false;
		} else if (arg == "--size" && has_value) {
			const std::string size = argv[++i];
			const size_t x_pos = size.find('x');
			if (x_pos == std::string::npos) {
				std::cerr << "error: --size expects WxH, got '" << size << "'\n";
				return 1;
			}
			settings.width = std::stoi(size.substr(0, x_pos));
			settings.height = std::stoi(size.substr(x_pos + 1));
		} else {
			std::cerr << "error: unknown argument '" << arg << "'\n";
			print_usage();
			return 1;
		}
	}

	for (const auto* path : { &settings.base_path, &settings.overlay_path }) {
		if (!QFile::exists(QString::fromStdString(*path))) {
			std::cerr << "error: source not found: " << *path << "\n"
			          << "       pass --base / --overlay to point at your own media\n";
			return 1;
		}
	}

	const QString out_dir = QString::fromStdString(settings.out_dir);
	if (!QDir().mkpath(out_dir) || !QDir().mkpath(out_dir + "/frames")) {
		std::cerr << "error: could not create output directory '" << settings.out_dir << "'\n";
		return 1;
	}
	const QString out_prefix = out_dir + "/";
	const QString frames_prefix = out_dir + "/frames/";

	// Which modes to render
	std::vector<BlendMode> modes;
	if (settings.only_mode.empty()) {
		modes = openshot::BlendModeList();
	} else {
		// Parse twice with different fallbacks: an unrecognised name returns each fallback,
		// a recognised one returns the same mode both times.
		const BlendMode requested =
			openshot::BlendModeFromString(settings.only_mode, openshot::BLEND_NORMAL);
		if (requested != openshot::BlendModeFromString(settings.only_mode, openshot::BLEND_MULTIPLY)) {
			std::cerr << "error: unknown blend mode '" << settings.only_mode << "'\n";
			return 1;
		}
		modes.push_back(requested);
	}

	std::cout << "libopenshot blend-mode render sample\n"
	          << "  backdrop : " << settings.base_path << " (from " << settings.base_start << "s)\n"
	          << "  overlay  : " << settings.overlay_path << " (from " << settings.overlay_start << "s)\n"
	          << "  render   : " << settings.width << "x" << settings.height
	          << " @ " << settings.fps << " fps, " << settings.seconds << "s per mode"
	          << (settings.audio ? ", aac audio" : ", video-only") << "\n\n";

	const auto started = std::chrono::steady_clock::now();
	std::vector<std::string> rendered;
	std::vector<QImage> stills;

	if (settings.clips) {
		std::cout << "Per-mode renders:\n";
		for (size_t i = 0; i < modes.size(); ++i) {
			// Keep the file index aligned with the enum even when --mode selects just one
			const int index = settings.only_mode.empty() ? (int) i : (int) modes[i];
			if (!render_mode(settings, modes[i], index, out_prefix, frames_prefix, stills))
				return 1;
			rendered.push_back(openshot::BlendModeToString(modes[i]));
		}
	}

	if (stills.size() > 1) {
		const QString sheet_file = out_prefix + "contact-sheet.png";
		write_contact_sheet(sheet_file, stills, rendered, 4, 480);
		std::cout << "  " << sheet_file.toStdString() << "\n";
	}

	if (settings.cycle && settings.only_mode.empty()) {
		std::cout << "Combined render:\n";
		if (!render_cycle(settings, out_prefix + "blend-cycle.mp4"))
			return 1;
	}

	if (rendered.empty())
		for (const auto mode : modes)
			rendered.push_back(openshot::BlendModeToString(mode));
	write_readme(out_prefix + "README.txt", settings, rendered);

	const double elapsed = std::chrono::duration<double>(
		std::chrono::steady_clock::now() - started).count();
	std::cout << "\nDone in " << elapsed << "s -> " << settings.out_dir << "/\n";
	return 0;
}
