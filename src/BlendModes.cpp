/**
 * @file
 * @brief Source file for blend-mode compositing (W3C Compositing and Blending Level 1)
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * @ref License
 */

// Copyright (c) 2008-2019 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "BlendModes.h"

#include <QPainter>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <unordered_map>

#include "OpenMPUtilities.h"

using namespace openshot;

namespace {

	// Name table. The first spelling is the canonical CSS / canvas / PixiJS name, which is what
	// BlendModeToString() returns and what the JSON contract uses.
	struct BlendModeNames {
		openshot::BlendMode mode;
		const char* css_name;
		const char* label;
	};

	const BlendModeNames NAMES[] = {
		{ BLEND_NORMAL,      "normal",      "Normal" },
		{ BLEND_MULTIPLY,    "multiply",    "Multiply" },
		{ BLEND_SCREEN,      "screen",      "Screen" },
		{ BLEND_OVERLAY,     "overlay",     "Overlay" },
		{ BLEND_DARKEN,      "darken",      "Darken" },
		{ BLEND_LIGHTEN,     "lighten",     "Lighten" },
		{ BLEND_COLOR_DODGE, "color-dodge", "Color Dodge" },
		{ BLEND_COLOR_BURN,  "color-burn",  "Color Burn" },
		{ BLEND_HARD_LIGHT,  "hard-light",  "Hard Light" },
		{ BLEND_SOFT_LIGHT,  "soft-light",  "Soft Light" },
		{ BLEND_DIFFERENCE,  "difference",  "Difference" },
		{ BLEND_EXCLUSION,   "exclusion",   "Exclusion" },
		{ BLEND_HUE,         "hue",         "Hue" },
		{ BLEND_SATURATION,  "saturation",  "Saturation" },
		{ BLEND_COLOR,       "color",       "Color" },
		{ BLEND_LUMINOSITY,  "luminosity",  "Luminosity" },
	};

	static_assert(sizeof(NAMES) / sizeof(NAMES[0]) == (size_t) openshot::BLEND_MODE_COUNT,
	              "BlendModes.cpp name table is out of sync with openshot::BlendMode");

	// Reduce a user-supplied name to a lookup key: lowercased, with '-', '_' and ' ' removed.
	// That collapses "color-dodge", "color_dodge", "colorDodge" and "Color Dodge" onto one key.
	std::string normalize_name(const std::string& name) {
		std::string key;
		key.reserve(name.size());
		for (char c : name) {
			if (c == '-' || c == '_' || c == ' ')
				continue;
			key.push_back((char) std::tolower((unsigned char) c));
		}
		return key;
	}

	/* ---------------- separable blend functions (W3C spec, section 9) ---------------- */

	inline float blend_multiply(float Cb, float Cs) { return Cb * Cs; }
	inline float blend_screen(float Cb, float Cs)   { return Cb + Cs - Cb * Cs; }

	inline float blend_hard_light(float Cb, float Cs) {
		// HardLight(Cb, Cs) = Multiply(Cb, 2 x Cs)      if Cs <= 0.5
		//                     Screen(Cb, 2 x Cs - 1)    otherwise
		return (Cs <= 0.5f) ? blend_multiply(Cb, 2.0f * Cs)
		                    : blend_screen(Cb, 2.0f * Cs - 1.0f);
	}

	inline float blend_overlay(float Cb, float Cs) {
		// Overlay is HardLight with the layers swapped
		return blend_hard_light(Cs, Cb);
	}

	inline float blend_color_dodge(float Cb, float Cs) {
		if (Cb <= 0.0f) return 0.0f;
		if (Cs >= 1.0f) return 1.0f;
		return std::min(1.0f, Cb / (1.0f - Cs));
	}

	inline float blend_color_burn(float Cb, float Cs) {
		if (Cb >= 1.0f) return 1.0f;
		if (Cs <= 0.0f) return 0.0f;
		return 1.0f - std::min(1.0f, (1.0f - Cb) / Cs);
	}

	inline float blend_soft_light(float Cb, float Cs) {
		if (Cs <= 0.5f)
			return Cb - (1.0f - 2.0f * Cs) * Cb * (1.0f - Cb);

		// D(Cb), the spec's auxiliary "darkening" function
		const float D = (Cb <= 0.25f) ? ((16.0f * Cb - 12.0f) * Cb + 4.0f) * Cb
		                              : std::sqrt(Cb);
		return Cb + (2.0f * Cs - 1.0f) * (D - Cb);
	}

	inline float blend_difference(float Cb, float Cs) { return std::fabs(Cb - Cs); }
	inline float blend_exclusion(float Cb, float Cs)  { return Cb + Cs - 2.0f * Cb * Cs; }

	/* -------------- non-separable blend functions (W3C spec, section 9.3) -------------- */

	inline float lum(float R, float G, float B) {
		return 0.3f * R + 0.59f * G + 0.11f * B;
	}

	// ClipColor(C): pull an out-of-gamut colour back into [0, 1] while preserving its luminosity
	inline void clip_color(float& R, float& G, float& B) {
		const float L = lum(R, G, B);
		const float n = std::min(R, std::min(G, B));
		const float x = std::max(R, std::max(G, B));
		if (n < 0.0f && L != n) {
			const float s = L / (L - n);
			R = L + (R - L) * s;
			G = L + (G - L) * s;
			B = L + (B - L) * s;
		}
		if (x > 1.0f && x != L) {
			const float s = (1.0f - L) / (x - L);
			R = L + (R - L) * s;
			G = L + (G - L) * s;
			B = L + (B - L) * s;
		}
	}

	// SetLum(C, l): shift a colour's luminosity to l
	inline void set_lum(float& R, float& G, float& B, float l) {
		const float d = l - lum(R, G, B);
		R += d;
		G += d;
		B += d;
		clip_color(R, G, B);
	}

	inline float sat(float R, float G, float B) {
		return std::max(R, std::max(G, B)) - std::min(R, std::min(G, B));
	}

	// SetSat(C, s): rescale a colour's saturation to s, keeping its relative channel ordering
	inline void set_sat(float& R, float& G, float& B, float s) {
		// Sort the three channels by value so we can address them as min/mid/max
		float* c[3] = { &R, &G, &B };
		if (*c[0] > *c[1]) std::swap(c[0], c[1]);
		if (*c[1] > *c[2]) std::swap(c[1], c[2]);
		if (*c[0] > *c[1]) std::swap(c[0], c[1]);

		float& cmin = *c[0];
		float& cmid = *c[1];
		float& cmax = *c[2];

		if (cmax > cmin) {
			cmid = (cmid - cmin) * s / (cmax - cmin);
			cmax = s;
		} else {
			cmid = cmax = 0.0f;
		}
		cmin = 0.0f;
	}

	inline void blend_nonseparable(openshot::BlendMode mode,
	                               float Rb, float Gb, float Bb,
	                               float Rs, float Gs, float Bs,
	                               float& Rr, float& Gr, float& Br) {
		switch (mode) {
			case BLEND_HUE:
				// SetLum(SetSat(Cs, Sat(Cb)), Lum(Cb))
				Rr = Rs; Gr = Gs; Br = Bs;
				set_sat(Rr, Gr, Br, sat(Rb, Gb, Bb));
				set_lum(Rr, Gr, Br, lum(Rb, Gb, Bb));
				break;

			case BLEND_SATURATION:
				// SetLum(SetSat(Cb, Sat(Cs)), Lum(Cb))
				Rr = Rb; Gr = Gb; Br = Bb;
				set_sat(Rr, Gr, Br, sat(Rs, Gs, Bs));
				set_lum(Rr, Gr, Br, lum(Rb, Gb, Bb));
				break;

			case BLEND_COLOR:
				// SetLum(Cs, Lum(Cb))
				Rr = Rs; Gr = Gs; Br = Bs;
				set_lum(Rr, Gr, Br, lum(Rb, Gb, Bb));
				break;

			case BLEND_LUMINOSITY:
				// SetLum(Cb, Lum(Cs))
				Rr = Rb; Gr = Gb; Br = Bb;
				set_lum(Rr, Gr, Br, lum(Rs, Gs, Bs));
				break;

			default:
				Rr = Rs; Gr = Gs; Br = Bs;
				break;
		}
	}

	inline bool is_nonseparable(openshot::BlendMode mode) {
		return mode == BLEND_HUE || mode == BLEND_SATURATION
		    || mode == BLEND_COLOR || mode == BLEND_LUMINOSITY;
	}

	// 8-bit reciprocal table for un-premultiplying: RECIP[a] == 255.0 / a (0 for a == 0)
	struct ReciprocalTable {
		float value[256];
		ReciprocalTable() {
			value[0] = 0.0f;
			for (int a = 1; a < 256; ++a)
				value[a] = 255.0f / (float) a;
		}
	};
	const ReciprocalTable RECIP;

	inline unsigned char to_byte(float v) {
		const int i = (int) std::lround(v * 255.0f);
		return (unsigned char) std::min(255, std::max(0, i));
	}

}  // anonymous namespace

namespace openshot {

std::string BlendModeToString(openshot::BlendMode mode) {
	const int i = (int) mode;
	if (i < 0 || i >= BLEND_MODE_COUNT)
		return NAMES[(int) BLEND_NORMAL].css_name;
	return NAMES[i].css_name;
}

std::string BlendModeLabel(openshot::BlendMode mode) {
	const int i = (int) mode;
	if (i < 0 || i >= BLEND_MODE_COUNT)
		return NAMES[(int) BLEND_NORMAL].label;
	return NAMES[i].label;
}

openshot::BlendMode BlendModeFromString(const std::string& name, openshot::BlendMode fallback) {
	static const std::unordered_map<std::string, openshot::BlendMode> lookup = [] {
		std::unordered_map<std::string, openshot::BlendMode> map;
		for (const auto& entry : NAMES)
			map[normalize_name(entry.css_name)] = entry.mode;
		// Canvas / Pixi synonyms for "no blending"
		map["sourceover"] = BLEND_NORMAL;
		map["srcover"] = BLEND_NORMAL;
		map["none"] = BLEND_NORMAL;
		return map;
	}();

	const auto found = lookup.find(normalize_name(name));
	return (found != lookup.end()) ? found->second : fallback;
}

const std::vector<openshot::BlendMode>& BlendModeList() {
	static const std::vector<openshot::BlendMode> modes = [] {
		std::vector<openshot::BlendMode> list;
		list.reserve(BLEND_MODE_COUNT);
		for (const auto& entry : NAMES)
			list.push_back(entry.mode);
		return list;
	}();
	return modes;
}

float BlendChannel(openshot::BlendMode mode, float Cb, float Cs) {
	switch (mode) {
		case BLEND_MULTIPLY:    return blend_multiply(Cb, Cs);
		case BLEND_SCREEN:      return blend_screen(Cb, Cs);
		case BLEND_OVERLAY:     return blend_overlay(Cb, Cs);
		case BLEND_DARKEN:      return std::min(Cb, Cs);
		case BLEND_LIGHTEN:     return std::max(Cb, Cs);
		case BLEND_COLOR_DODGE: return blend_color_dodge(Cb, Cs);
		case BLEND_COLOR_BURN:  return blend_color_burn(Cb, Cs);
		case BLEND_HARD_LIGHT:  return blend_hard_light(Cb, Cs);
		case BLEND_SOFT_LIGHT:  return blend_soft_light(Cb, Cs);
		case BLEND_DIFFERENCE:  return blend_difference(Cb, Cs);
		case BLEND_EXCLUSION:   return blend_exclusion(Cb, Cs);
		case BLEND_NORMAL:
		default:                return Cs;
	}
}

void BlendPixel(openshot::BlendMode mode,
                float Rb, float Gb, float Bb,
                float Rs, float Gs, float Bs,
                float& Rr, float& Gr, float& Br) {
	if (is_nonseparable(mode)) {
		blend_nonseparable(mode, Rb, Gb, Bb, Rs, Gs, Bs, Rr, Gr, Br);
	} else {
		Rr = BlendChannel(mode, Rb, Rs);
		Gr = BlendChannel(mode, Gb, Gs);
		Br = BlendChannel(mode, Bb, Bs);
	}
}

void BlendImages(QImage& destination, const QImage& source, openshot::BlendMode mode) {
	// The whole point of a blend mode is that it reads the backdrop, so both images must be in
	// the same premultiplied 8-bit RGBA layout before we can walk them together. That is already
	// libopenshot's internal frame format, so these conversions are normally no-ops.
	if (destination.format() != QImage::Format_RGBA8888_Premultiplied)
		destination = destination.convertToFormat(QImage::Format_RGBA8888_Premultiplied);

	const QImage* src_ptr = &source;
	QImage converted_source;
	if (source.format() != QImage::Format_RGBA8888_Premultiplied) {
		converted_source = source.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
		src_ptr = &converted_source;
	}
	const QImage& src = *src_ptr;

	// Only the region the two images share, anchored at the top-left
	const int width = std::min(destination.width(), src.width());
	const int height = std::min(destination.height(), src.height());
	if (width <= 0 || height <= 0)
		return;

	// BLEND_NORMAL reduces to plain source-over, which Qt's raster engine already does with
	// SIMD - the generic loop below would give the same answer, just slower.
	if (mode == BLEND_NORMAL) {
		QPainter painter(&destination);
		painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
		painter.drawImage(0, 0, src, 0, 0, width, height);
		painter.end();
		return;
	}

	const bool nonseparable = is_nonseparable(mode);

	#pragma omp parallel for num_threads(OPEN_MP_NUM_PROCESSORS)
	for (int y = 0; y < height; ++y) {
		unsigned char* dst_row = destination.scanLine(y);
		const unsigned char* src_row = src.constScanLine(y);

		for (int x = 0; x < width; ++x) {
			unsigned char* d = dst_row + x * 4;
			const unsigned char* s = src_row + x * 4;

			const int sa8 = s[3];
			// A fully transparent source leaves the backdrop untouched for every blend mode
			if (sa8 == 0)
				continue;

			const int da8 = d[3];

			// An opaque source over an empty backdrop is just the source: B(Cb, Cs) has no
			// backdrop to read, and the composite collapses to co = Cs, ao = as.
			if (da8 == 0) {
				d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
				continue;
			}

			const float as = (float) sa8 / 255.0f;
			const float ab = (float) da8 / 255.0f;

			// Un-premultiply to the straight 0-1 colours the spec's formulas are defined on
			const float s_recip = RECIP.value[sa8];
			const float d_recip = RECIP.value[da8];
			const float Rs = (float) s[0] * s_recip / 255.0f;
			const float Gs = (float) s[1] * s_recip / 255.0f;
			const float Bs = (float) s[2] * s_recip / 255.0f;
			const float Rb = (float) d[0] * d_recip / 255.0f;
			const float Gb = (float) d[1] * d_recip / 255.0f;
			const float Bb = (float) d[2] * d_recip / 255.0f;

			float Rr, Gr, Br;
			if (nonseparable) {
				blend_nonseparable(mode, Rb, Gb, Bb, Rs, Gs, Bs, Rr, Gr, Br);
			} else {
				Rr = BlendChannel(mode, Rb, Rs);
				Gr = BlendChannel(mode, Gb, Gs);
				Br = BlendChannel(mode, Bb, Bs);
			}

			// W3C simple alpha compositing, straight to a premultiplied result:
			//   co = as * (1 - ab) * Cs  +  as * ab * B(Cb, Cs)  +  (1 - as) * ab * Cb
			//   ao = as + ab * (1 - as)
			const float w_src   = as * (1.0f - ab);
			const float w_blend = as * ab;
			const float w_dst   = (1.0f - as) * ab;

			d[0] = to_byte(w_src * Rs + w_blend * Rr + w_dst * Rb);
			d[1] = to_byte(w_src * Gs + w_blend * Gr + w_dst * Gb);
			d[2] = to_byte(w_src * Bs + w_blend * Br + w_dst * Bb);
			d[3] = to_byte(as + ab * (1.0f - as));
		}
	}
}

}  // namespace openshot
