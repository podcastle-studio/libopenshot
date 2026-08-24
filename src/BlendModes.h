/**
 * @file
 * @brief Header file for blend-mode compositing (W3C Compositing and Blending Level 1)
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * @ref License
 */

// Copyright (c) 2008-2019 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef OPENSHOT_BLEND_MODES_H
#define OPENSHOT_BLEND_MODES_H

#include <string>
#include <vector>

#include <QImage>

#include "Enums.h"

namespace openshot
{
	/// Number of supported blend modes (BLEND_NORMAL .. BLEND_LUMINOSITY)
	static const int BLEND_MODE_COUNT = (int) BLEND_LUMINOSITY + 1;

	/// Get the canonical CSS / PixiJS name of a blend mode (e.g. BLEND_COLOR_DODGE -> "color-dodge").
	/// This is the same spelling used by the HTML canvas `globalCompositeOperation` property, so a
	/// libopenshot render and a front-end render can be matched up by name.
	std::string BlendModeToString(openshot::BlendMode mode);

	/// Look up a blend mode by name. Accepts the canonical CSS/Pixi spelling ("color-dodge"), the
	/// snake_case spelling ("color_dodge"), the camelCase spelling ("colorDodge"), and the canvas
	/// synonym "source-over" for BLEND_NORMAL. Matching is case-insensitive. Returns \p fallback
	/// if the name is unknown.
	openshot::BlendMode BlendModeFromString(const std::string& name, openshot::BlendMode fallback = openshot::BLEND_NORMAL);

	/// Get the human-readable label of a blend mode (e.g. BLEND_COLOR_DODGE -> "Color Dodge"),
	/// suitable for a properties dropdown.
	std::string BlendModeLabel(openshot::BlendMode mode);

	/// Get every blend mode, in enum order. Handy for iterating (demos, dropdowns, tests).
	const std::vector<openshot::BlendMode>& BlendModeList();

	/// Blend a single non-premultiplied colour channel with one of the separable blend modes.
	/// \p Cb is the backdrop channel and \p Cs the source channel, both in the range 0.0 - 1.0.
	/// Returns \p Cs unchanged for the non-separable modes (hue/saturation/color/luminosity),
	/// which cannot be evaluated one channel at a time - use BlendPixel() for those.
	float BlendChannel(openshot::BlendMode mode, float Cb, float Cs);

	/// Blend a non-premultiplied RGB triple. Handles both the separable and the non-separable
	/// modes. Inputs and outputs are in the range 0.0 - 1.0 (results are not clamped for the
	/// separable modes, which are already in range by construction).
	void BlendPixel(openshot::BlendMode mode,
	                float Rb, float Gb, float Bb,
	                float Rs, float Gs, float Bs,
	                float& Rr, float& Gr, float& Br);

	/// Composite \p source onto \p destination in place, using \p mode.
	///
	/// This implements the full W3C "Compositing and Blending Level 1" simple-alpha-compositing
	/// formula, i.e. a blend followed by source-over:
	///
	///     co = as * (1 - ab) * Cs  +  as * ab * B(Cb, Cs)  +  (1 - as) * ab * Cb
	///     ao = as + ab * (1 - as)
	///
	/// which is exactly what an HTML canvas `globalCompositeOperation` (and PixiJS' advanced
	/// blend-mode filters) compute, so the output matches a front-end composite of the same two
	/// images pixel for pixel (up to 8-bit rounding).
	///
	/// Both images are converted to QImage::Format_RGBA8888_Premultiplied if needed (that is
	/// already libopenshot's internal frame format, so the common path is conversion-free). Only
	/// the region the two images have in common, anchored at (0, 0), is touched. BLEND_NORMAL is
	/// a plain source-over composite.
	void BlendImages(QImage& destination, const QImage& source, openshot::BlendMode mode);

}  // namespace openshot

#endif  // OPENSHOT_BLEND_MODES_H
