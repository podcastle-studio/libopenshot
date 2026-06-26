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

#include <cmath>

#include "Color.h"
#include "Exceptions.h"

#include <QColor>
#include <QString>
#include <QStringList>

using namespace openshot;

namespace {
	// Parse a color string into a QColor, accepting either:
	//   - hex or named colors handled by QColor (e.g. "#RRGGBB", "#AARRGGBB", "red"), or
	//   - CSS-style "rgb(r, g, b)" / "rgba(r, g, b, a)" where r,g,b are 0-255.
	// The alpha in rgba() is treated as a 0-1 fraction (CSS convention) when <= 1,
	// otherwise as a 0-255 value.
	QColor parseColorString(const QString& input) {
		const QString s = input.trimmed();
		if (s.startsWith("rgb", Qt::CaseInsensitive)) {
			const int open = s.indexOf('(');
			const int close = s.lastIndexOf(')');
			if (open >= 0 && close > open) {
				const QStringList parts = s.mid(open + 1, close - open - 1).split(',');
				if (parts.size() >= 3) {
					const int r = qBound(0, (int) std::lround(parts[0].trimmed().toDouble()), 255);
					const int g = qBound(0, (int) std::lround(parts[1].trimmed().toDouble()), 255);
					const int b = qBound(0, (int) std::lround(parts[2].trimmed().toDouble()), 255);
					int a = 255;
					if (parts.size() >= 4) {
						const double av = parts[3].trimmed().toDouble();
						a = qBound(0, (int) std::lround(av <= 1.0 ? av * 255.0 : av), 255);
					}
					return QColor(r, g, b, a);
				}
			}
			return QColor(); // malformed rgb()/rgba()
		}
		// Fall back to QColor's own hex/named-color parsing
		return QColor(s);
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


// Constructor which takes a color string (hex, named, or CSS rgb()/rgba())
Color::Color(std::string color_hex)
    : Color::Color(parseColorString(QString::fromStdString(color_hex))) {}

Color::Color(const char* color_hex)
    : Color::Color(parseColorString(QString(color_hex))) {}

// Get the HEX value of a color at a specific frame
std::string Color::GetColorHex(int64_t frame_number) {

	int r = red.GetInt(frame_number);
	int g = green.GetInt(frame_number);
	int b = blue.GetInt(frame_number);
	int a = alpha.GetInt(frame_number);

	return QColor( r,g,b,a ).name().toStdString();
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
