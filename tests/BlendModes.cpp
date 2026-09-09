/**
 * @file
 * @brief Unit tests for openshot::BlendMode compositing
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * @ref License
 */

// Copyright (c) 2008-2019 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <cmath>
#include <string>

#include "openshot_catch.h"

#include <QImage>

#include "BlendModes.h"
#include "Clip.h"
#include "Enums.h"
#include "Json.h"

using namespace openshot;

namespace {

	// Build a 1x1 premultiplied RGBA image from straight 0-255 components
	QImage single_pixel(int r, int g, int b, int a) {
		QImage image(1, 1, QImage::Format_RGBA8888_Premultiplied);
		unsigned char* pixel = image.bits();
		pixel[0] = (unsigned char) std::lround(r * a / 255.0);
		pixel[1] = (unsigned char) std::lround(g * a / 255.0);
		pixel[2] = (unsigned char) std::lround(b * a / 255.0);
		pixel[3] = (unsigned char) a;
		return image;
	}

	// Blend two opaque 1x1 pixels and return the (straight == premultiplied) result
	QColor blend_opaque(BlendMode mode, QColor backdrop, QColor source) {
		QImage dest = single_pixel(backdrop.red(), backdrop.green(), backdrop.blue(), 255);
		const QImage src = single_pixel(source.red(), source.green(), source.blue(), 255);
		BlendImages(dest, src, mode);
		const unsigned char* pixel = dest.constBits();
		return QColor(pixel[0], pixel[1], pixel[2], pixel[3]);
	}

}  // anonymous namespace

TEST_CASE( "mode names round-trip", "[libopenshot][blendmodes]" )
{
	CHECK(BlendModeToString(BLEND_NORMAL) == "normal");
	CHECK(BlendModeToString(BLEND_COLOR_DODGE) == "color-dodge");
	CHECK(BlendModeToString(BLEND_LUMINOSITY) == "luminosity");

	// Every mode's canonical name must parse back to the same mode
	for (const auto mode : BlendModeList())
		CHECK(BlendModeFromString(BlendModeToString(mode)) == mode);

	CHECK(BlendModeList().size() == (size_t) BLEND_MODE_COUNT);
}

TEST_CASE( "mode names accept front-end spellings", "[libopenshot][blendmodes]" )
{
	// CSS / canvas / Pixi spelling, plus the snake_case and camelCase variants
	CHECK(BlendModeFromString("color-dodge") == BLEND_COLOR_DODGE);
	CHECK(BlendModeFromString("color_dodge") == BLEND_COLOR_DODGE);
	CHECK(BlendModeFromString("colorDodge") == BLEND_COLOR_DODGE);
	CHECK(BlendModeFromString("Color Dodge") == BLEND_COLOR_DODGE);
	CHECK(BlendModeFromString("HARD-LIGHT") == BLEND_HARD_LIGHT);

	// Canvas synonyms for "no blending"
	CHECK(BlendModeFromString("source-over") == BLEND_NORMAL);
	CHECK(BlendModeFromString("normal") == BLEND_NORMAL);

	// Unknown names fall back, so a typo in a payload cannot change the render silently
	CHECK(BlendModeFromString("not-a-mode") == BLEND_NORMAL);
	CHECK(BlendModeFromString("not-a-mode", BLEND_SCREEN) == BLEND_SCREEN);
}

TEST_CASE( "separable blend formulas match the W3C spec", "[libopenshot][blendmodes]" )
{
	const float margin = 0.00001;

	// B(Cb, Cs) at values the spec pins down exactly
	CHECK(BlendChannel(BLEND_NORMAL, 0.25f, 0.75f) == Approx(0.75f).margin(margin));
	CHECK(BlendChannel(BLEND_MULTIPLY, 0.5f, 0.5f) == Approx(0.25f).margin(margin));
	CHECK(BlendChannel(BLEND_SCREEN, 0.5f, 0.5f) == Approx(0.75f).margin(margin));
	CHECK(BlendChannel(BLEND_DARKEN, 0.25f, 0.75f) == Approx(0.25f).margin(margin));
	CHECK(BlendChannel(BLEND_LIGHTEN, 0.25f, 0.75f) == Approx(0.75f).margin(margin));
	CHECK(BlendChannel(BLEND_DIFFERENCE, 0.25f, 0.75f) == Approx(0.5f).margin(margin));
	CHECK(BlendChannel(BLEND_EXCLUSION, 0.5f, 0.5f) == Approx(0.5f).margin(margin));

	// Overlay is HardLight with the layers swapped
	CHECK(BlendChannel(BLEND_OVERLAY, 0.3f, 0.8f)
	      == Approx(BlendChannel(BLEND_HARD_LIGHT, 0.8f, 0.3f)).margin(margin));

	// A 0.5 source is the identity for both hard-light and soft-light
	CHECK(BlendChannel(BLEND_HARD_LIGHT, 0.42f, 0.5f) == Approx(0.42f).margin(margin));
	CHECK(BlendChannel(BLEND_SOFT_LIGHT, 0.42f, 0.5f) == Approx(0.42f).margin(margin));

	// Dodge/burn division guards, which the spec calls out explicitly
	CHECK(BlendChannel(BLEND_COLOR_DODGE, 0.0f, 0.5f) == Approx(0.0f).margin(margin));  // Cb == 0
	CHECK(BlendChannel(BLEND_COLOR_DODGE, 0.5f, 1.0f) == Approx(1.0f).margin(margin));  // Cs == 1
	CHECK(BlendChannel(BLEND_COLOR_DODGE, 0.25f, 0.5f) == Approx(0.5f).margin(margin));
	CHECK(BlendChannel(BLEND_COLOR_BURN, 1.0f, 0.5f) == Approx(1.0f).margin(margin));   // Cb == 1
	CHECK(BlendChannel(BLEND_COLOR_BURN, 0.5f, 0.0f) == Approx(0.0f).margin(margin));   // Cs == 0
	CHECK(BlendChannel(BLEND_COLOR_BURN, 0.5f, 0.5f) == Approx(0.0f).margin(margin));

	// Every separable mode leaves a black backdrop under a black source at black
	for (const auto mode : BlendModeList()) {
		if (mode == BLEND_HUE || mode == BLEND_SATURATION
		    || mode == BLEND_COLOR || mode == BLEND_LUMINOSITY)
			continue;
		CHECK(BlendChannel(mode, 0.0f, 0.0f) == Approx(0.0f).margin(margin));
	}
}

TEST_CASE( "non-separable blend formulas match the W3C spec", "[libopenshot][blendmodes]" )
{
	// Luminosity of a white source over any backdrop is white: SetLum(Cb, 1) clips to (1,1,1)
	CHECK(blend_opaque(BLEND_LUMINOSITY, QColor(255, 0, 0), QColor(255, 255, 255))
	      == QColor(255, 255, 255, 255));

	// Colour takes the source's hue+saturation at the backdrop's luminosity, so a grey source
	// leaves a grey result
	const QColor color_result = blend_opaque(BLEND_COLOR, QColor(255, 0, 0), QColor(128, 128, 128));
	CHECK(color_result.red() == color_result.green());
	CHECK(color_result.green() == color_result.blue());

	// Saturation with a fully-desaturated source strips the backdrop's saturation
	const QColor sat_result = blend_opaque(BLEND_SATURATION, QColor(255, 0, 0), QColor(128, 128, 128));
	CHECK(sat_result.red() == sat_result.green());
	CHECK(sat_result.green() == sat_result.blue());

	// Hue keeps the backdrop's luminosity: Lum(result) == Lum(backdrop), to 8-bit rounding
	const QColor backdrop(40, 90, 200);
	const QColor hue_result = blend_opaque(BLEND_HUE, backdrop, QColor(0, 255, 0));
	const double lum_backdrop = 0.3 * backdrop.red() + 0.59 * backdrop.green() + 0.11 * backdrop.blue();
	const double lum_result = 0.3 * hue_result.red() + 0.59 * hue_result.green() + 0.11 * hue_result.blue();
	CHECK(lum_result == Approx(lum_backdrop).margin(1.5));

	// BlendChannel cannot evaluate these one channel at a time, and says so by returning Cs
	CHECK(BlendChannel(BLEND_HUE, 0.25f, 0.75f) == Approx(0.75f).margin(0.00001));
}

TEST_CASE( "alpha compositing follows the W3C formula", "[libopenshot][blendmodes]" )
{
	// A transparent source leaves the backdrop untouched, whatever the mode
	for (const auto mode : BlendModeList()) {
		QImage dest = single_pixel(200, 100, 50, 255);
		BlendImages(dest, single_pixel(0, 255, 0, 0), mode);
		const unsigned char* pixel = dest.constBits();
		CHECK(pixel[0] == 200);
		CHECK(pixel[1] == 100);
		CHECK(pixel[2] == 50);
		CHECK(pixel[3] == 255);
	}

	// An empty backdrop has nothing to blend with, so the result is just the source
	for (const auto mode : BlendModeList()) {
		QImage dest = single_pixel(0, 0, 0, 0);
		BlendImages(dest, single_pixel(20, 180, 90, 255), mode);
		const unsigned char* pixel = dest.constBits();
		CHECK(pixel[0] == 20);
		CHECK(pixel[1] == 180);
		CHECK(pixel[2] == 90);
		CHECK(pixel[3] == 255);
	}

	// Half-opaque multiply over an opaque backdrop:
	//   co = as*(1-ab)*Cs + as*ab*B + (1-as)*ab*Cb  with as = 0.5, ab = 1
	//      = 0.5*B(Cb, Cs) + 0.5*Cb
	// Cb = Cs = 0.5 -> B = 0.25 -> co = 0.375 -> 96
	QImage dest = single_pixel(128, 128, 128, 255);
	BlendImages(dest, single_pixel(128, 128, 128, 128), BLEND_MULTIPLY);
	const unsigned char* pixel = dest.constBits();
	CHECK(pixel[0] == Approx(96).margin(2));
	CHECK(pixel[3] == 255);
}

TEST_CASE( "BLEND_NORMAL is a plain source-over composite", "[libopenshot][blendmodes]" )
{
	// Half-opaque white over black: 0.5*1 + 0.5*0 = 0.5 -> 128
	QImage dest = single_pixel(0, 0, 0, 255);
	BlendImages(dest, single_pixel(255, 255, 255, 128), BLEND_NORMAL);
	const unsigned char* pixel = dest.constBits();
	CHECK(pixel[0] == Approx(128).margin(1));
	CHECK(pixel[3] == 255);
}

TEST_CASE( "blend region is clipped to the shared area", "[libopenshot][blendmodes]" )
{
	// A source smaller than the backdrop only affects the overlapping top-left region
	QImage dest(2, 1, QImage::Format_RGBA8888_Premultiplied);
	dest.fill(QColor(255, 255, 255, 255));
	QImage src = single_pixel(0, 0, 0, 255);

	BlendImages(dest, src, BLEND_MULTIPLY);
	CHECK(dest.pixelColor(0, 0) == QColor(0, 0, 0, 255));        // blended
	CHECK(dest.pixelColor(1, 0) == QColor(255, 255, 255, 255));  // untouched
}

TEST_CASE( "clip blend mode round-trips through JSON", "[libopenshot][blendmodes]" )
{
	openshot::Clip clip;
	CHECK(clip.Blend() == BLEND_NORMAL);

	// The front-end contract: a CSS/Pixi name under "blendMode"
	clip.SetJson("{ \"blendMode\": \"color-dodge\" }");
	CHECK(clip.Blend() == BLEND_COLOR_DODGE);

	// The numeric enum under the properties-UI key
	clip.SetJson("{ \"blend_mode\": 1 }");
	CHECK(clip.Blend() == BLEND_MULTIPLY);

	// Out-of-range values are ignored rather than producing an invalid mode
	clip.SetJson("{ \"blend_mode\": 999 }");
	CHECK(clip.Blend() == BLEND_MULTIPLY);

	// JsonValue() emits both spellings
	clip.Blend(BLEND_SOFT_LIGHT);
	auto root = clip.JsonValue();
	CHECK(root["blendMode"].asString() == "soft-light");
	CHECK(root["blend_mode"].asInt() == (int) BLEND_SOFT_LIGHT);

	// ...and a full round-trip preserves the mode
	openshot::Clip restored;
	restored.SetJson(clip.Json());
	CHECK(restored.Blend() == BLEND_SOFT_LIGHT);
}
