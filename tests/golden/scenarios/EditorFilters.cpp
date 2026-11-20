// unit.editor_filters: the LIGHT and COLOR clip filters against the web editor's shaders.
//
// The editor previews these filters with its own GLSL (podcastle-frontend,
// libs/video-project/src/preview/filters/{light,color}/*.frag) and the export draws them with
// LightAdjustment and ColorAdjustment, so the export is right only when it draws what the editor
// drew. The reference below is those two fragments transcribed line for line, in float on 0..1, as
// the editor runs them. On 2026-10-30 they had drifted apart in four places -- negative contrast,
// highlights, the vibrance formula and the vibrance/saturation order -- and a clip at contrast -1
// exported with a pink, posterised face (dev export 6ae48eb4). Change a formula on either side and
// this fails until the other follows.
//
// The effects run on a bare frame, so with a GPU active this checks the SkSL twins and without
// one the C++: the suite's GPU and CPU runs between them cover both.

#include "Recipes.h"

#include "Frame.h"
#include "Timeline.h"
#include "effects/ColorAdjustment.h"
#include "effects/LightAdjustment.h"

#include <QImage>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace golden;

namespace {

struct Rgb { float r, g, b; };

float clamp01(float v) { return std::min(1.0f, std::max(0.0f, v)); }
Rgb clamp01(Rgb c) { return {clamp01(c.r), clamp01(c.g), clamp01(c.b)}; }

// ---- light.frag ----------------------------------------------------------------------------

float editorToneCurve(float v, float contrast)
{
	const float midpoint = 0.5f;
	if (contrast > 0.0f) {
		const float amount = contrast;
		if (v < midpoint)
			return midpoint * std::pow(v / midpoint, 1.0f + amount);
		return 1.0f - (1.0f - midpoint) * std::pow((1.0f - v) / (1.0f - midpoint), 1.0f + amount);
	}
	if (contrast < 0.0f) {
		const float amount = -contrast;
		return v + (midpoint - v) * amount * std::sin(3.14159265f * v) * 0.5f;
	}
	return v;
}

Rgb editorLight(Rgb c, float brightness, float contrast, float highlights, float shadows,
				float whites, float blacks)
{
	if (brightness != 0.0f) {
		const float f = brightness > 0.0f ? std::pow(2.0f, brightness) : 1.0f + brightness * 0.7f;
		c = {c.r * f, c.g * f, c.b * f};
	}
	if (contrast != 0.0f)
		c = {editorToneCurve(clamp01(c.r), contrast), editorToneCurve(clamp01(c.g), contrast),
			 editorToneCurve(clamp01(c.b), contrast)};
	if (blacks != 0.0f) {
		const float bp = blacks * 0.1f;
		if (blacks > 0.0f) {
			c = {bp + c.r * (1.0f - bp), bp + c.g * (1.0f - bp), bp + c.b * (1.0f - bp)};
		} else {
			const float factor = 1.0f / (1.0f + bp);
			c = {std::max(c.r - std::abs(bp), 0.0f) * factor, std::max(c.g - std::abs(bp), 0.0f) * factor,
				 std::max(c.b - std::abs(bp), 0.0f) * factor};
		}
	}
	if (whites != 0.0f) {
		const float wp = 1.0f - std::abs(whites) * 0.1f;
		if (whites > 0.0f)
			c = clamp01(Rgb{c.r / wp, c.g / wp, c.b / wp});
		else
			c = {c.r * wp, c.g * wp, c.b * wp};
	}
	if (shadows != 0.0f) {
		const float y = c.r * 0.299f + c.g * 0.587f + c.b * 0.114f;
		float mask = 0.0f;
		if (y < 0.15f) {
			mask = 1.0f;
		} else if (y < 0.35f) {
			const float t = (y - 0.15f) / 0.20f;
			mask = 1.0f - t * t;
		}
		if (mask > 0.0f) {
			const float factor = shadows > 0.0f ? 1.0f + (shadows * mask * 0.5f) * (1.0f - y * 2.0f)
												: 1.0f - (-shadows) * mask * 0.9f;
			c = {c.r * factor, c.g * factor, c.b * factor};
		}
	}
	if (highlights != 0.0f) {
		const float y = c.r * 0.299f + c.g * 0.587f + c.b * 0.114f;
		if (y > 0.6f) {
			const float mask = (y - 0.6f) / 0.4f;
			if (highlights < 0.0f) {
				const float f = 1.0f - (-highlights) * mask * 0.2f;
				c = {c.r * f, c.g * f, c.b * f};
			} else {
				const float boost = highlights * mask * 0.45f;
				c = {c.r + (1.0f - c.r) * boost, c.g + (1.0f - c.g) * boost, c.b + (1.0f - c.b) * boost};
			}
		}
	}
	return clamp01(c);
}

// ---- color.frag ----------------------------------------------------------------------------

Rgb editorColor(Rgb c, float temperature, float tint, float vibrance, float saturation)
{
	c.r = c.r * (1.0f + temperature * 0.1f);
	c.b = c.b * (1.0f - temperature * 0.1f);
	c.g = c.g * (1.0f - tint * 0.1f);
	c.r = c.r * (1.0f + tint * 0.05f);
	c.b = c.b * (1.0f + tint * 0.05f);
	c = clamp01(c);

	{
		const float maxC = std::max(c.r, std::max(c.g, c.b));
		const float minC = std::min(c.r, std::min(c.g, c.b));
		const float sat = maxC > 0.0f ? (maxC - minC) / maxC : 0.0f;
		const float y = c.r * 0.2126f + c.g * 0.7152f + c.b * 0.0722f;
		const float scale = 1.0f + (vibrance * 2.0f) * (1.0f - sat);
		c = clamp01(Rgb{y + (c.r - y) * scale, y + (c.g - y) * scale, y + (c.b - y) * scale});
	}
	{
		const float y = c.r * 0.299f + c.g * 0.587f + c.b * 0.114f;
		const float k = 1.0f + saturation;
		c = {y + (c.r - y) * k, y + (c.g - y) * k, y + (c.b - y) * k};
	}
	return clamp01(c);   // the 8-bit render target
}

// ---- the check -----------------------------------------------------------------------------

// Every colour on a 0, 15, ..., 255 lattice (18^3 = 5832 pixels), opaque.
std::shared_ptr<openshot::Frame> latticeFrame()
{
	QImage img(108, 54, QImage::Format_RGBA8888_Premultiplied);
	int i = 0;
	for (int r = 0; r <= 255; r += 15)
		for (int g = 0; g <= 255; g += 15)
			for (int b = 0; b <= 255; b += 15, ++i) {
				uchar* px = img.scanLine(i / 108) + (i % 108) * 4;
				px[0] = r; px[1] = g; px[2] = b; px[3] = 255;
			}
	// AddImage, not an assignment through GetImage(): it sets the frame's size, which the GPU
	// path draws at.
	auto frame = std::make_shared<openshot::Frame>();
	frame->AddImage(std::make_shared<QImage>(img));
	return frame;
}

// Off by one or two where the C++ truncates and the editor rounds -- about half a level on
// average per pass, a little more with six stages stacked -- or where the contrast LUT is indexed
// by a truncated byte on a steep part of the curve. A formula that differs is 9 to 69 levels off
// somewhere on the lattice (the 2026-10-30 drift; contrast -1 was 22).
constexpr int kMaxLevels = 3;
constexpr double kMeanLevels = 1.0;

void checkAgainstEditor(const std::string& name, openshot::EffectBase& effect,
			 const std::function<Rgb(Rgb)>& reference, std::vector<Check>& checks)
{
	auto source = latticeFrame();
	const QImage in = *source->GetImage();
	auto input = std::make_shared<openshot::Frame>();
	input->AddImage(std::make_shared<QImage>(in.copy()));
	auto frame = effect.GetFrame(input, 1);
	// A GPU frame reads back in whatever layout the surface had; compare bytes as RGBA.
	const QImage out = frame->GetImage()->convertToFormat(QImage::Format_RGBA8888_Premultiplied);
	int worst = 0;
	long long sum = 0, n = 0;
	for (int y = 0; y < in.height(); ++y)
		for (int x = 0; x < in.width(); ++x) {
			const uchar* s = in.constScanLine(y) + x * 4;
			const uchar* o = out.constScanLine(y) + x * 4;
			const Rgb ref = reference({s[0] / 255.0f, s[1] / 255.0f, s[2] / 255.0f});
			const float want[3] = {ref.r, ref.g, ref.b};
			for (int c = 0; c < 3; ++c) {
				const int e = std::abs(static_cast<int>(std::lround(want[c] * 255.0f)) - o[c]);
				worst = std::max(worst, e);
				sum += e;
				++n;
			}
		}
	const double mean = static_cast<double>(sum) / n;
	std::ostringstream msg;
	msg << "max " << worst << " (limit " << kMaxLevels << "), mean " << mean << " (limit "
		<< kMeanLevels << ")";
	checks.push_back({name, worst <= kMaxLevels && mean <= kMeanLevels, msg.str()});
}

struct LightCase { const char* name; float br, co, hi, sh, wh, bl; };
const LightCase kLightCases[] = {
	{"light contrast -1", 0, -1, 0, 0, 0, 0},
	{"light contrast -0.4", 0, -0.4f, 0, 0, 0, 0},
	{"light contrast +0.6", 0, 0.6f, 0, 0, 0, 0},
	{"light highlights +0.7", 0, 0, 0.7f, 0, 0, 0},
	{"light highlights -0.7", 0, 0, -0.7f, 0, 0, 0},
	{"light brightness -0.5, shadows +0.6", -0.5f, 0, 0, 0.6f, 0, 0},
	{"light shadows -0.65, whites -0.5, blacks +0.4", 0, 0, 0, -0.65f, -0.5f, 0.4f},
	{"light dev export 6ae48eb4", 0.32f, -1, 0, 0.09f, 1, -0.07f},
	{"light all", 0.2f, 0.5f, -0.3f, 0.4f, 0.25f, -0.35f},
};

struct ColorCase { const char* name; float te, ti, vi, sa; };
const ColorCase kColorCases[] = {
	{"color temperature +0.6, tint -0.4", 0.6f, -0.4f, 0, 0},
	{"color vibrance +0.8", 0, 0, 0.8f, 0},
	{"color vibrance -0.6", 0, 0, -0.6f, 0},
	{"color saturation +0.7", 0, 0, 0, 0.7f},
	{"color saturation -1", 0, 0, 0, -1},
	{"color vibrance then saturation", 0, 0, 0.5f, 0.45f},
	{"color dev export 6ae48eb4", -0.46f, -0.08f, 0.15f, 0},
	{"color all", 0.3f, 0.25f, -0.5f, 0.45f},
};

void editorScene(Scene& s) {
	// The harness requires a timeline even when a scenario renders nothing.
	s.makeTimeline().Open();
}

} // namespace

void golden::registerEditorFilterScenarios() {
	addCustom("unit.editor_filters", {"unit", "filters"}, editorScene,
		[](Scene&, std::vector<Captured>&, std::vector<Check>& checks) {
			for (const LightCase& c : kLightCases) {
				openshot::LightAdjustment effect(c.br, c.co, c.hi, c.sh, c.wh, c.bl);
				checkAgainstEditor(c.name, effect, [&](Rgb p) {
					return editorLight(p, c.br, c.co, c.hi, c.sh, c.wh, c.bl);
				}, checks);
			}
			for (const ColorCase& c : kColorCases) {
				openshot::ColorAdjustment effect(c.te, c.ti, c.vi, c.sa);
				checkAgainstEditor(c.name, effect, [&](Rgb p) {
					return editorColor(p, c.te, c.ti, c.vi, c.sa);
				}, checks);
			}
		});
}
