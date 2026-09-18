/**
 * @file
 * @brief Source file for EffectBase class
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * @ref License
 */

// Copyright (c) 2008-2019 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "Color.h"
#include "Exceptions.h"

#include <QColor>

using namespace openshot;

namespace {
	// Colour parsing used to go through QColor. It no longer does: a colour string is parsed
	// on the render path (the Timeline's background clear asks for one every frame), and
	// Color.h is a public header that should not drag Qt into every consumer of it.
	//
	// Everything below reproduces QColor's behaviour exactly, including the parts that are
	// surprising: an unparseable string yields opaque black (0,0,0,255) rather than an error,
	// and the named-colour list is the 147 SVG keywords plus "transparent". The golden suite's
	// `unit.color` scenario holds this against a recorded Qt oracle, and
	// tests/golden/scenarios/Unit.cpp says how to regenerate both the table and the oracle.

	struct Rgba {
		int r, g, b, a;
	};

	// An unparseable colour, which is what QColor reports for one: black, but opaque.
	constexpr Rgba kInvalid{0, 0, 0, 255};

	struct NamedColor {
		const char* name;
		unsigned char r, g, b, a;
	};

	// Generated from Qt 5.15.13: QColor::colorNames(), with each name's QColor components.
	// Sorted by name, as QColor::colorNames() returns them, so it can be binary-searched.
	constexpr NamedColor kNamedColors[] = {
	{"aliceblue", 0xf0, 0xf8, 0xff, 0xff},
	{"antiquewhite", 0xfa, 0xeb, 0xd7, 0xff},
	{"aqua", 0x00, 0xff, 0xff, 0xff},
	{"aquamarine", 0x7f, 0xff, 0xd4, 0xff},
	{"azure", 0xf0, 0xff, 0xff, 0xff},
	{"beige", 0xf5, 0xf5, 0xdc, 0xff},
	{"bisque", 0xff, 0xe4, 0xc4, 0xff},
	{"black", 0x00, 0x00, 0x00, 0xff},
	{"blanchedalmond", 0xff, 0xeb, 0xcd, 0xff},
	{"blue", 0x00, 0x00, 0xff, 0xff},
	{"blueviolet", 0x8a, 0x2b, 0xe2, 0xff},
	{"brown", 0xa5, 0x2a, 0x2a, 0xff},
	{"burlywood", 0xde, 0xb8, 0x87, 0xff},
	{"cadetblue", 0x5f, 0x9e, 0xa0, 0xff},
	{"chartreuse", 0x7f, 0xff, 0x00, 0xff},
	{"chocolate", 0xd2, 0x69, 0x1e, 0xff},
	{"coral", 0xff, 0x7f, 0x50, 0xff},
	{"cornflowerblue", 0x64, 0x95, 0xed, 0xff},
	{"cornsilk", 0xff, 0xf8, 0xdc, 0xff},
	{"crimson", 0xdc, 0x14, 0x3c, 0xff},
	{"cyan", 0x00, 0xff, 0xff, 0xff},
	{"darkblue", 0x00, 0x00, 0x8b, 0xff},
	{"darkcyan", 0x00, 0x8b, 0x8b, 0xff},
	{"darkgoldenrod", 0xb8, 0x86, 0x0b, 0xff},
	{"darkgray", 0xa9, 0xa9, 0xa9, 0xff},
	{"darkgreen", 0x00, 0x64, 0x00, 0xff},
	{"darkgrey", 0xa9, 0xa9, 0xa9, 0xff},
	{"darkkhaki", 0xbd, 0xb7, 0x6b, 0xff},
	{"darkmagenta", 0x8b, 0x00, 0x8b, 0xff},
	{"darkolivegreen", 0x55, 0x6b, 0x2f, 0xff},
	{"darkorange", 0xff, 0x8c, 0x00, 0xff},
	{"darkorchid", 0x99, 0x32, 0xcc, 0xff},
	{"darkred", 0x8b, 0x00, 0x00, 0xff},
	{"darksalmon", 0xe9, 0x96, 0x7a, 0xff},
	{"darkseagreen", 0x8f, 0xbc, 0x8f, 0xff},
	{"darkslateblue", 0x48, 0x3d, 0x8b, 0xff},
	{"darkslategray", 0x2f, 0x4f, 0x4f, 0xff},
	{"darkslategrey", 0x2f, 0x4f, 0x4f, 0xff},
	{"darkturquoise", 0x00, 0xce, 0xd1, 0xff},
	{"darkviolet", 0x94, 0x00, 0xd3, 0xff},
	{"deeppink", 0xff, 0x14, 0x93, 0xff},
	{"deepskyblue", 0x00, 0xbf, 0xff, 0xff},
	{"dimgray", 0x69, 0x69, 0x69, 0xff},
	{"dimgrey", 0x69, 0x69, 0x69, 0xff},
	{"dodgerblue", 0x1e, 0x90, 0xff, 0xff},
	{"firebrick", 0xb2, 0x22, 0x22, 0xff},
	{"floralwhite", 0xff, 0xfa, 0xf0, 0xff},
	{"forestgreen", 0x22, 0x8b, 0x22, 0xff},
	{"fuchsia", 0xff, 0x00, 0xff, 0xff},
	{"gainsboro", 0xdc, 0xdc, 0xdc, 0xff},
	{"ghostwhite", 0xf8, 0xf8, 0xff, 0xff},
	{"gold", 0xff, 0xd7, 0x00, 0xff},
	{"goldenrod", 0xda, 0xa5, 0x20, 0xff},
	{"gray", 0x80, 0x80, 0x80, 0xff},
	{"green", 0x00, 0x80, 0x00, 0xff},
	{"greenyellow", 0xad, 0xff, 0x2f, 0xff},
	{"grey", 0x80, 0x80, 0x80, 0xff},
	{"honeydew", 0xf0, 0xff, 0xf0, 0xff},
	{"hotpink", 0xff, 0x69, 0xb4, 0xff},
	{"indianred", 0xcd, 0x5c, 0x5c, 0xff},
	{"indigo", 0x4b, 0x00, 0x82, 0xff},
	{"ivory", 0xff, 0xff, 0xf0, 0xff},
	{"khaki", 0xf0, 0xe6, 0x8c, 0xff},
	{"lavender", 0xe6, 0xe6, 0xfa, 0xff},
	{"lavenderblush", 0xff, 0xf0, 0xf5, 0xff},
	{"lawngreen", 0x7c, 0xfc, 0x00, 0xff},
	{"lemonchiffon", 0xff, 0xfa, 0xcd, 0xff},
	{"lightblue", 0xad, 0xd8, 0xe6, 0xff},
	{"lightcoral", 0xf0, 0x80, 0x80, 0xff},
	{"lightcyan", 0xe0, 0xff, 0xff, 0xff},
	{"lightgoldenrodyellow", 0xfa, 0xfa, 0xd2, 0xff},
	{"lightgray", 0xd3, 0xd3, 0xd3, 0xff},
	{"lightgreen", 0x90, 0xee, 0x90, 0xff},
	{"lightgrey", 0xd3, 0xd3, 0xd3, 0xff},
	{"lightpink", 0xff, 0xb6, 0xc1, 0xff},
	{"lightsalmon", 0xff, 0xa0, 0x7a, 0xff},
	{"lightseagreen", 0x20, 0xb2, 0xaa, 0xff},
	{"lightskyblue", 0x87, 0xce, 0xfa, 0xff},
	{"lightslategray", 0x77, 0x88, 0x99, 0xff},
	{"lightslategrey", 0x77, 0x88, 0x99, 0xff},
	{"lightsteelblue", 0xb0, 0xc4, 0xde, 0xff},
	{"lightyellow", 0xff, 0xff, 0xe0, 0xff},
	{"lime", 0x00, 0xff, 0x00, 0xff},
	{"limegreen", 0x32, 0xcd, 0x32, 0xff},
	{"linen", 0xfa, 0xf0, 0xe6, 0xff},
	{"magenta", 0xff, 0x00, 0xff, 0xff},
	{"maroon", 0x80, 0x00, 0x00, 0xff},
	{"mediumaquamarine", 0x66, 0xcd, 0xaa, 0xff},
	{"mediumblue", 0x00, 0x00, 0xcd, 0xff},
	{"mediumorchid", 0xba, 0x55, 0xd3, 0xff},
	{"mediumpurple", 0x93, 0x70, 0xdb, 0xff},
	{"mediumseagreen", 0x3c, 0xb3, 0x71, 0xff},
	{"mediumslateblue", 0x7b, 0x68, 0xee, 0xff},
	{"mediumspringgreen", 0x00, 0xfa, 0x9a, 0xff},
	{"mediumturquoise", 0x48, 0xd1, 0xcc, 0xff},
	{"mediumvioletred", 0xc7, 0x15, 0x85, 0xff},
	{"midnightblue", 0x19, 0x19, 0x70, 0xff},
	{"mintcream", 0xf5, 0xff, 0xfa, 0xff},
	{"mistyrose", 0xff, 0xe4, 0xe1, 0xff},
	{"moccasin", 0xff, 0xe4, 0xb5, 0xff},
	{"navajowhite", 0xff, 0xde, 0xad, 0xff},
	{"navy", 0x00, 0x00, 0x80, 0xff},
	{"oldlace", 0xfd, 0xf5, 0xe6, 0xff},
	{"olive", 0x80, 0x80, 0x00, 0xff},
	{"olivedrab", 0x6b, 0x8e, 0x23, 0xff},
	{"orange", 0xff, 0xa5, 0x00, 0xff},
	{"orangered", 0xff, 0x45, 0x00, 0xff},
	{"orchid", 0xda, 0x70, 0xd6, 0xff},
	{"palegoldenrod", 0xee, 0xe8, 0xaa, 0xff},
	{"palegreen", 0x98, 0xfb, 0x98, 0xff},
	{"paleturquoise", 0xaf, 0xee, 0xee, 0xff},
	{"palevioletred", 0xdb, 0x70, 0x93, 0xff},
	{"papayawhip", 0xff, 0xef, 0xd5, 0xff},
	{"peachpuff", 0xff, 0xda, 0xb9, 0xff},
	{"peru", 0xcd, 0x85, 0x3f, 0xff},
	{"pink", 0xff, 0xc0, 0xcb, 0xff},
	{"plum", 0xdd, 0xa0, 0xdd, 0xff},
	{"powderblue", 0xb0, 0xe0, 0xe6, 0xff},
	{"purple", 0x80, 0x00, 0x80, 0xff},
	{"red", 0xff, 0x00, 0x00, 0xff},
	{"rosybrown", 0xbc, 0x8f, 0x8f, 0xff},
	{"royalblue", 0x41, 0x69, 0xe1, 0xff},
	{"saddlebrown", 0x8b, 0x45, 0x13, 0xff},
	{"salmon", 0xfa, 0x80, 0x72, 0xff},
	{"sandybrown", 0xf4, 0xa4, 0x60, 0xff},
	{"seagreen", 0x2e, 0x8b, 0x57, 0xff},
	{"seashell", 0xff, 0xf5, 0xee, 0xff},
	{"sienna", 0xa0, 0x52, 0x2d, 0xff},
	{"silver", 0xc0, 0xc0, 0xc0, 0xff},
	{"skyblue", 0x87, 0xce, 0xeb, 0xff},
	{"slateblue", 0x6a, 0x5a, 0xcd, 0xff},
	{"slategray", 0x70, 0x80, 0x90, 0xff},
	{"slategrey", 0x70, 0x80, 0x90, 0xff},
	{"snow", 0xff, 0xfa, 0xfa, 0xff},
	{"springgreen", 0x00, 0xff, 0x7f, 0xff},
	{"steelblue", 0x46, 0x82, 0xb4, 0xff},
	{"tan", 0xd2, 0xb4, 0x8c, 0xff},
	{"teal", 0x00, 0x80, 0x80, 0xff},
	{"thistle", 0xd8, 0xbf, 0xd8, 0xff},
	{"tomato", 0xff, 0x63, 0x47, 0xff},
	{"transparent", 0x00, 0x00, 0x00, 0x00},
	{"turquoise", 0x40, 0xe0, 0xd0, 0xff},
	{"violet", 0xee, 0x82, 0xee, 0xff},
	{"wheat", 0xf5, 0xde, 0xb3, 0xff},
	{"white", 0xff, 0xff, 0xff, 0xff},
	{"whitesmoke", 0xf5, 0xf5, 0xf5, 0xff},
	{"yellow", 0xff, 0xff, 0x00, 0xff},
	{"yellowgreen", 0x9a, 0xcd, 0x32, 0xff},
	};

	int hexDigit(char c) {
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	}

	// Widen an n-digit hex component to 8 bits the way QColor does, by rescaling rather than
	// truncating: "#abcdef012345" gives green 0xee, not the 0xef a shift would give.
	int rescale(int value, int max_in) {
		return (value * 255 + max_in / 2) / max_in;
	}

	// "#RGB", "#RRGGBB", "#AARRGGBB", "#RRRGGGBBB" and "#RRRRGGGGBBBB", as QColor accepts them.
	bool parseHex(const std::string& s, Rgba& out) {
		int per_component = 0;
		bool has_alpha = false;
		switch (s.size() - 1) {
			case 3:  per_component = 1; break;
			case 6:  per_component = 2; break;
			case 8:  per_component = 2; has_alpha = true; break;
			case 9:  per_component = 3; break;
			case 12: per_component = 4; break;
			default: return false;
		}

		int component[4] = {0, 0, 0, 255};   // r, g, b, a
		const int count = has_alpha ? 4 : 3;
		std::size_t pos = 1;
		for (int i = 0; i < count; ++i) {
			int value = 0;
			for (int d = 0; d < per_component; ++d) {
				const int digit = hexDigit(s[pos++]);
				if (digit < 0) return false;
				value = (value << 4) | digit;
			}
			// "#AARRGGBB" leads with alpha; every other form is r, g, b in order.
			component[has_alpha ? (i + 3) % 4 : i] = value;
		}

		int max_in = 0;
		for (int d = 0; d < per_component; ++d) max_in = (max_in << 4) | 0xf;

		out.r = rescale(component[0], max_in);
		out.g = rescale(component[1], max_in);
		out.b = rescale(component[2], max_in);
		out.a = has_alpha ? rescale(component[3], max_in) : 255;
		return true;
	}

	bool parseNamed(const std::string& s, Rgba& out) {
		std::string key = s;
		std::transform(key.begin(), key.end(), key.begin(),
					   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		const auto* end = std::end(kNamedColors);
		const auto* found = std::lower_bound(std::begin(kNamedColors), end, key,
			[](const NamedColor& entry, const std::string& name) {
				return std::strcmp(entry.name, name.c_str()) < 0;
			});
		if (found == end || key != found->name) return false;

		out = Rgba{found->r, found->g, found->b, found->a};
		return true;
	}

	int clampByte(double v) {
		if (std::isnan(v)) return 0;
		return static_cast<int>(std::min(255.0, std::max(0.0, std::round(v))));
	}

	// Parse a color string into RGBA, accepting either:
	//   - hex or named colors handled by QColor (e.g. "#RRGGBB", "#AARRGGBB", "red"), or
	//   - CSS-style "rgb(r, g, b)" / "rgba(r, g, b, a)" where r,g,b are 0-255.
	// The alpha in rgba() is treated as a 0-1 fraction (CSS convention) when <= 1,
	// otherwise as a 0-255 value.
	Rgba parseColorString(const std::string& input) {
		static const char* kSpace = " \t\n\r\f\v";
		const auto first = input.find_first_not_of(kSpace);
		if (first == std::string::npos) return kInvalid;
		const auto last = input.find_last_not_of(kSpace);
		const std::string s = input.substr(first, last - first + 1);

		const bool is_rgb_form = s.size() >= 3 &&
			(s[0] == 'r' || s[0] == 'R') && (s[1] == 'g' || s[1] == 'G') && (s[2] == 'b' || s[2] == 'B');
		if (is_rgb_form) {
			const auto open = s.find('(');
			const auto close = s.rfind(')');
			if (open == std::string::npos || close == std::string::npos || close <= open)
				return kInvalid;   // malformed rgb()/rgba()

			std::vector<std::string> parts;
			const std::string inner = s.substr(open + 1, close - open - 1);
			for (std::size_t pos = 0; ; ) {
				const auto comma = inner.find(',', pos);
				if (comma == std::string::npos) { parts.push_back(inner.substr(pos)); break; }
				parts.push_back(inner.substr(pos, comma - pos));
				pos = comma + 1;
			}
			if (parts.size() < 3) return kInvalid;

			// strtod reads the leading number and stops, which is what QString::toDouble() did
			// for these fields; a field that is not a number at all still reads as 0.
			auto number = [](const std::string& text) { return std::strtod(text.c_str(), nullptr); };

			Rgba out{};
			out.r = clampByte(number(parts[0]));
			out.g = clampByte(number(parts[1]));
			out.b = clampByte(number(parts[2]));
			out.a = 255;
			if (parts.size() >= 4) {
				const double av = number(parts[3]);
				out.a = clampByte(av <= 1.0 ? av * 255.0 : av);
			}
			return out;
		}

		Rgba out{};
		if (s[0] == '#')
			return parseHex(s, out) ? out : kInvalid;
		return parseNamed(s, out) ? out : kInvalid;
	}
}

// Constructor which takes R,G,B,A
Color::Color(unsigned char Red, unsigned char Green, unsigned char Blue, unsigned char Alpha) :
    red(static_cast<double>(Red)),
    green(static_cast<double>(Green)),
    blue(static_cast<double>(Blue)),
    alpha(static_cast<double>(Alpha)) { }

// Constructor which takes 4 existing Keyframe curves
Color::Color(Keyframe Red, Keyframe Green, Keyframe Blue, Keyframe Alpha) :
    red(Red), green(Green), blue(Blue), alpha(Alpha) { }

// Constructor which takes a QColor
Color::Color(QColor qcolor) :
    red(qcolor.red()),
    green(qcolor.green()),
    blue(qcolor.blue()),
    alpha(qcolor.alpha()) { }

namespace {
	Color fromRgba(const Rgba& c) {
		return Color(static_cast<unsigned char>(c.r), static_cast<unsigned char>(c.g),
					 static_cast<unsigned char>(c.b), static_cast<unsigned char>(c.a));
	}
}

// Constructor which takes a color string (hex, named, or CSS rgb()/rgba())
Color::Color(std::string color_hex)
    : Color::Color(fromRgba(parseColorString(color_hex))) {}

Color::Color(const char* color_hex)
    : Color::Color(fromRgba(parseColorString(color_hex ? color_hex : std::string()))) {}

// Get the HEX value of a color at a specific frame
std::string Color::GetColorHex(int64_t frame_number) {

	int r = red.GetInt(frame_number);
	int g = green.GetInt(frame_number);
	int b = blue.GetInt(frame_number);

	// QColor::name() emitted "#rrggbb" and dropped alpha, and callers still expect exactly
	// that -- Frame::AddColor and the Timeline's background clear both round-trip through it.
	char buffer[8];
	std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x",
				  std::min(255, std::max(0, r)),
				  std::min(255, std::max(0, g)),
				  std::min(255, std::max(0, b)));
	return std::string(buffer);
}
// Get RGBA values for a specific frame as an integer vector
std::vector<int> Color::GetColorRGBA(int64_t frame_number) {
	std::vector<int> rgba;
	rgba.push_back(red.GetInt(frame_number));
	rgba.push_back(green.GetInt(frame_number));
	rgba.push_back(blue.GetInt(frame_number));
	rgba.push_back(alpha.GetInt(frame_number));

	return rgba;
}

// Get the distance between 2 RGB pairs (alpha is ignored)
long Color::GetDistance(long R1, long G1, long B1, long R2, long G2, long B2)
{
	  long rmean = ( R1 + R2 ) / 2;
	  long r = R1 - R2;
	  long g = G1 - G2;
	  long b = B1 - B2;
	  return sqrt((((512+rmean)*r*r)>>8) + 4*g*g + (((767-rmean)*b*b)>>8));
}

// Generate JSON string of this object
std::string Color::Json() const {

	// Return formatted string
	return JsonValue().toStyledString();
}

// Generate Json::Value for this object
Json::Value Color::JsonValue() const {

	// Create root json object
	Json::Value root;
	root["red"] = red.JsonValue();
	root["green"] = green.JsonValue();
	root["blue"] = blue.JsonValue();
	root["alpha"] = alpha.JsonValue();

	// return JsonValue
	return root;
}

// Load JSON string into this object
void Color::SetJson(const std::string value) {

	// Parse JSON string into JSON objects
	try
	{
		const Json::Value root = openshot::stringToJson(value);
		// Set all values that match
		SetJsonValue(root);
	}
	catch (const std::exception& e)
	{
		// Error parsing JSON (or missing keys)
		throw InvalidJSON("JSON is invalid (missing keys or invalid data types)");
	}
}

// Load Json::Value into this object
void Color::SetJsonValue(const Json::Value root) {

	// Set data from Json (if key is found)
	if (!root["red"].isNull())
		red.SetJsonValue(root["red"]);
	if (!root["green"].isNull())
		green.SetJsonValue(root["green"]);
	if (!root["blue"].isNull())
		blue.SetJsonValue(root["blue"]);
	if (!root["alpha"].isNull())
		alpha.SetJsonValue(root["alpha"]);
}
