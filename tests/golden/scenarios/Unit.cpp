// Unit checks that need no rendering. They live in the golden suite because it is this
// project's test suite -- ENABLE_TESTS (Catch2) is off and Catch2 is not installed.
//
// unit.color guards openshot::Color's string parsing, which stopped going through QColor in
// W18 so that Color.h no longer drags Qt into every consumer and the Timeline's background
// clear does not parse a colour through Qt on every frame. Every expectation below was
// RECORDED FROM THE PRE-W18 IMPLEMENTATION (Qt 5.15.13), so the check is a parity test
// against the behaviour that shipped, quirks and all:
//
//   - an unparseable string is opaque black (0,0,0,255), never an error;
//   - "rgbx(1,2,3)" parses as CSS rgb(), because the prefix test is startsWith("rgb");
//   - GetColorHex() is "#rrggbb" and drops alpha, which is why Timeline's GPU background
//     clear forces alpha to 255 rather than reading the alpha curve.
//
// To regenerate after an intentional change, see doc/gpu-migration/GPU-WORKLIST.md W18: build
// a small program that links Qt, run the recorded inputs through the old parser, and paste
// the results back here.

#include "Recipes.h"

#include "Color.h"
#include "Timeline.h"

#include <string>
#include <vector>

using namespace golden;

namespace {

struct ColorCase {
    const char* input;
    int r, g, b, a;
};

// The 147 SVG colour keywords plus "transparent", as QColor::colorNames() reported them.
const ColorCase kNamedCases[] = {
    {"aliceblue", 240, 248, 255, 255},
    {"antiquewhite", 250, 235, 215, 255},
    {"aqua", 0, 255, 255, 255},
    {"aquamarine", 127, 255, 212, 255},
    {"azure", 240, 255, 255, 255},
    {"beige", 245, 245, 220, 255},
    {"bisque", 255, 228, 196, 255},
    {"black", 0, 0, 0, 255},
    {"blanchedalmond", 255, 235, 205, 255},
    {"blue", 0, 0, 255, 255},
    {"blueviolet", 138, 43, 226, 255},
    {"brown", 165, 42, 42, 255},
    {"burlywood", 222, 184, 135, 255},
    {"cadetblue", 95, 158, 160, 255},
    {"chartreuse", 127, 255, 0, 255},
    {"chocolate", 210, 105, 30, 255},
    {"coral", 255, 127, 80, 255},
    {"cornflowerblue", 100, 149, 237, 255},
    {"cornsilk", 255, 248, 220, 255},
    {"crimson", 220, 20, 60, 255},
    {"cyan", 0, 255, 255, 255},
    {"darkblue", 0, 0, 139, 255},
    {"darkcyan", 0, 139, 139, 255},
    {"darkgoldenrod", 184, 134, 11, 255},
    {"darkgray", 169, 169, 169, 255},
    {"darkgreen", 0, 100, 0, 255},
    {"darkgrey", 169, 169, 169, 255},
    {"darkkhaki", 189, 183, 107, 255},
    {"darkmagenta", 139, 0, 139, 255},
    {"darkolivegreen", 85, 107, 47, 255},
    {"darkorange", 255, 140, 0, 255},
    {"darkorchid", 153, 50, 204, 255},
    {"darkred", 139, 0, 0, 255},
    {"darksalmon", 233, 150, 122, 255},
    {"darkseagreen", 143, 188, 143, 255},
    {"darkslateblue", 72, 61, 139, 255},
    {"darkslategray", 47, 79, 79, 255},
    {"darkslategrey", 47, 79, 79, 255},
    {"darkturquoise", 0, 206, 209, 255},
    {"darkviolet", 148, 0, 211, 255},
    {"deeppink", 255, 20, 147, 255},
    {"deepskyblue", 0, 191, 255, 255},
    {"dimgray", 105, 105, 105, 255},
    {"dimgrey", 105, 105, 105, 255},
    {"dodgerblue", 30, 144, 255, 255},
    {"firebrick", 178, 34, 34, 255},
    {"floralwhite", 255, 250, 240, 255},
    {"forestgreen", 34, 139, 34, 255},
    {"fuchsia", 255, 0, 255, 255},
    {"gainsboro", 220, 220, 220, 255},
    {"ghostwhite", 248, 248, 255, 255},
    {"gold", 255, 215, 0, 255},
    {"goldenrod", 218, 165, 32, 255},
    {"gray", 128, 128, 128, 255},
    {"green", 0, 128, 0, 255},
    {"greenyellow", 173, 255, 47, 255},
    {"grey", 128, 128, 128, 255},
    {"honeydew", 240, 255, 240, 255},
    {"hotpink", 255, 105, 180, 255},
    {"indianred", 205, 92, 92, 255},
    {"indigo", 75, 0, 130, 255},
    {"ivory", 255, 255, 240, 255},
    {"khaki", 240, 230, 140, 255},
    {"lavender", 230, 230, 250, 255},
    {"lavenderblush", 255, 240, 245, 255},
    {"lawngreen", 124, 252, 0, 255},
    {"lemonchiffon", 255, 250, 205, 255},
    {"lightblue", 173, 216, 230, 255},
    {"lightcoral", 240, 128, 128, 255},
    {"lightcyan", 224, 255, 255, 255},
    {"lightgoldenrodyellow", 250, 250, 210, 255},
    {"lightgray", 211, 211, 211, 255},
    {"lightgreen", 144, 238, 144, 255},
    {"lightgrey", 211, 211, 211, 255},
    {"lightpink", 255, 182, 193, 255},
    {"lightsalmon", 255, 160, 122, 255},
    {"lightseagreen", 32, 178, 170, 255},
    {"lightskyblue", 135, 206, 250, 255},
    {"lightslategray", 119, 136, 153, 255},
    {"lightslategrey", 119, 136, 153, 255},
    {"lightsteelblue", 176, 196, 222, 255},
    {"lightyellow", 255, 255, 224, 255},
    {"lime", 0, 255, 0, 255},
    {"limegreen", 50, 205, 50, 255},
    {"linen", 250, 240, 230, 255},
    {"magenta", 255, 0, 255, 255},
    {"maroon", 128, 0, 0, 255},
    {"mediumaquamarine", 102, 205, 170, 255},
    {"mediumblue", 0, 0, 205, 255},
    {"mediumorchid", 186, 85, 211, 255},
    {"mediumpurple", 147, 112, 219, 255},
    {"mediumseagreen", 60, 179, 113, 255},
    {"mediumslateblue", 123, 104, 238, 255},
    {"mediumspringgreen", 0, 250, 154, 255},
    {"mediumturquoise", 72, 209, 204, 255},
    {"mediumvioletred", 199, 21, 133, 255},
    {"midnightblue", 25, 25, 112, 255},
    {"mintcream", 245, 255, 250, 255},
    {"mistyrose", 255, 228, 225, 255},
    {"moccasin", 255, 228, 181, 255},
    {"navajowhite", 255, 222, 173, 255},
    {"navy", 0, 0, 128, 255},
    {"oldlace", 253, 245, 230, 255},
    {"olive", 128, 128, 0, 255},
    {"olivedrab", 107, 142, 35, 255},
    {"orange", 255, 165, 0, 255},
    {"orangered", 255, 69, 0, 255},
    {"orchid", 218, 112, 214, 255},
    {"palegoldenrod", 238, 232, 170, 255},
    {"palegreen", 152, 251, 152, 255},
    {"paleturquoise", 175, 238, 238, 255},
    {"palevioletred", 219, 112, 147, 255},
    {"papayawhip", 255, 239, 213, 255},
    {"peachpuff", 255, 218, 185, 255},
    {"peru", 205, 133, 63, 255},
    {"pink", 255, 192, 203, 255},
    {"plum", 221, 160, 221, 255},
    {"powderblue", 176, 224, 230, 255},
    {"purple", 128, 0, 128, 255},
    {"red", 255, 0, 0, 255},
    {"rosybrown", 188, 143, 143, 255},
    {"royalblue", 65, 105, 225, 255},
    {"saddlebrown", 139, 69, 19, 255},
    {"salmon", 250, 128, 114, 255},
    {"sandybrown", 244, 164, 96, 255},
    {"seagreen", 46, 139, 87, 255},
    {"seashell", 255, 245, 238, 255},
    {"sienna", 160, 82, 45, 255},
    {"silver", 192, 192, 192, 255},
    {"skyblue", 135, 206, 235, 255},
    {"slateblue", 106, 90, 205, 255},
    {"slategray", 112, 128, 144, 255},
    {"slategrey", 112, 128, 144, 255},
    {"snow", 255, 250, 250, 255},
    {"springgreen", 0, 255, 127, 255},
    {"steelblue", 70, 130, 180, 255},
    {"tan", 210, 180, 140, 255},
    {"teal", 0, 128, 128, 255},
    {"thistle", 216, 191, 216, 255},
    {"tomato", 255, 99, 71, 255},
    {"transparent", 0, 0, 0, 0},
    {"turquoise", 64, 224, 208, 255},
    {"violet", 238, 130, 238, 255},
    {"wheat", 245, 222, 179, 255},
    {"white", 255, 255, 255, 255},
    {"whitesmoke", 245, 245, 245, 255},
    {"yellow", 255, 255, 0, 255},
    {"yellowgreen", 154, 205, 50, 255},
};

// Hex forms, whitespace handling, the CSS rgb()/rgba() branch, and malformed input.
const ColorCase kFormCases[] = {
    {"#fff", 255, 255, 255, 255},
    {"#abc", 170, 187, 204, 255},
    {"#000", 0, 0, 0, 255},
    {"#ffffff", 255, 255, 255, 255},
    {"#ff0000", 255, 0, 0, 255},
    {"#abc123", 171, 193, 35, 255},
    {"#00ff80", 0, 255, 128, 255},
    {"#80ff0000", 255, 0, 0, 128},
    {"#00000000", 0, 0, 0, 0},
    {"#ffffffff", 255, 255, 255, 255},
    {"#aabbccdd", 187, 204, 221, 170},
    {"#123456789", 18, 69, 120, 255},
    {"#fff000000", 255, 0, 0, 255},
    {"#abcdef012345", 171, 238, 35, 255},
    {"#112233445566", 17, 51, 85, 255},
    {"#ffff00000000", 255, 0, 0, 255},
    {"red", 255, 0, 0, 255},
    {"RED", 255, 0, 0, 255},
    {"Red", 255, 0, 0, 255},
    {"transparent", 0, 0, 0, 0},
    {"TRANSPARENT", 0, 0, 0, 0},
    {"cornflowerblue", 100, 149, 237, 255},
    {"darkslategrey", 47, 79, 79, 255},
    {"  #ff0000  ", 255, 0, 0, 255},
    {"\tred\n", 255, 0, 0, 255},
    {" transparent ", 0, 0, 0, 0},
    {"rgb(1,2,3)", 1, 2, 3, 255},
    {"rgb( 10 , 20 , 30 )", 10, 20, 30, 255},
    {"RGB(1,2,3)", 1, 2, 3, 255},
    {"rgb(1.4,2.6,3.5)", 1, 3, 4, 255},
    {"rgba(1,2,3,0.5)", 1, 2, 3, 128},
    {"rgba(1,2,3,1)", 1, 2, 3, 255},
    {"rgba(1,2,3,2)", 1, 2, 3, 2},
    {"rgba(1,2,3,255)", 1, 2, 3, 255},
    {"rgba(1,2,3,0)", 1, 2, 3, 0},
    {"RGBA(1,2,3,0.25)", 1, 2, 3, 64},
    {"rgb(-5,-5,-5)", 0, 0, 0, 255},
    {"rgb(300,300,300)", 255, 255, 255, 255},
    {"rgb(1,2,3,4,5)", 1, 2, 3, 4},
    {"rgba(1,2,3,)", 1, 2, 3, 0},
    {"", 0, 0, 0, 255},
    {" ", 0, 0, 0, 255},
    {"garbage", 0, 0, 0, 255},
    {"#", 0, 0, 0, 255},
    {"#f", 0, 0, 0, 255},
    {"#ff", 0, 0, 0, 255},
    {"#ffff", 0, 0, 0, 255},
    {"#fffff", 0, 0, 0, 255},
    {"#fffffff", 0, 0, 0, 255},
    {"#ffffffff0", 255, 255, 254, 255},
    {"#gggggg", 0, 0, 0, 255},
    {"#12345g", 0, 0, 0, 255},
    {"REDD", 0, 0, 0, 255},
    {"rgb(1,2)", 0, 0, 0, 255},
    {"rgb()", 0, 0, 0, 255},
    {"rgb(", 0, 0, 0, 255},
    {"rgb", 0, 0, 0, 255},
    {"rgbx(1,2,3)", 1, 2, 3, 255},
    {"rgb(a,b,c)", 0, 0, 0, 255},
};

std::string rgbaText(const std::vector<int>& v) {
    return "(" + std::to_string(v[0]) + "," + std::to_string(v[1]) + "," +
           std::to_string(v[2]) + "," + std::to_string(v[3]) + ")";
}

// Run one table, reporting a single check for it and naming the first input that disagrees.
void checkTable(const char* name, const ColorCase* cases, std::size_t count,
                std::vector<Check>& checks) {
    std::size_t failed = 0;
    std::string first_failure;
    for (std::size_t i = 0; i < count; ++i) {
        const ColorCase& c = cases[i];
        const std::vector<int> got = openshot::Color(std::string(c.input)).GetColorRGBA(0);
        if (got[0] == c.r && got[1] == c.g && got[2] == c.b && got[3] == c.a) continue;
        if (failed++ == 0)
            first_failure = std::string("\"") + c.input + "\" expected (" +
                std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b) +
                "," + std::to_string(c.a) + ") got " + rgbaText(got);
    }
    checks.push_back({name, failed == 0,
                      failed == 0 ? std::to_string(count) + " inputs match"
                                  : std::to_string(failed) + " of " + std::to_string(count) +
                                    " differ, first: " + first_failure});
}

void unitScene(Scene& s) {
    // The harness requires a timeline even when a scenario renders nothing.
    s.makeTimeline().Open();
}

} // namespace

void golden::registerUnitScenarios() {
    addCustom("unit.color", {"unit"}, unitScene,
        [](Scene&, std::vector<Captured>&, std::vector<Check>& checks) {
            checkTable("named_colors", kNamedCases,
                       sizeof(kNamedCases) / sizeof(kNamedCases[0]), checks);
            checkTable("hex_rgb_and_malformed", kFormCases,
                       sizeof(kFormCases) / sizeof(kFormCases[0]), checks);

            // Named lookup is case-insensitive, as QColor's was.
            std::size_t case_failures = 0;
            for (const ColorCase& c : kNamedCases) {
                std::string upper(c.input);
                for (char& ch : upper) ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
                const std::vector<int> got = openshot::Color(upper).GetColorRGBA(0);
                if (got[0] != c.r || got[1] != c.g || got[2] != c.b || got[3] != c.a) ++case_failures;
            }
            checks.push_back({"named_colors_uppercase", case_failures == 0,
                              case_failures == 0 ? "all match"
                                                 : std::to_string(case_failures) + " differ"});

            // GetColorHex is "#rrggbb", lowercase, alpha dropped -- exactly QColor::name().
            std::size_t hex_failures = 0;
            std::string first_hex_failure;
            for (int v = 0; v < 256; ++v) {
                openshot::Color c(static_cast<unsigned char>(v),
                                  static_cast<unsigned char>(255 - v),
                                  static_cast<unsigned char>((v * 7) % 256),
                                  static_cast<unsigned char>((v * 13) % 256));
                char want[8];
                std::snprintf(want, sizeof(want), "#%02x%02x%02x", v, 255 - v, (v * 7) % 256);
                const std::string got = c.GetColorHex(0);
                if (got != want && hex_failures++ == 0)
                    first_hex_failure = std::string("expected ") + want + " got " + got;
            }
            checks.push_back({"hex_round_trip", hex_failures == 0,
                              hex_failures == 0 ? "256 colours match"
                                                : first_hex_failure});

            // Parsing a colour and formatting it back must be a fixed point for "#rrggbb".
            std::size_t round_trip_failures = 0;
            for (const ColorCase& c : kNamedCases) {
                openshot::Color parsed{std::string(c.input)};
                const std::string hex = parsed.GetColorHex(0);
                const std::vector<int> again = openshot::Color(hex).GetColorRGBA(0);
                if (again[0] != c.r || again[1] != c.g || again[2] != c.b) ++round_trip_failures;
            }
            checks.push_back({"name_to_hex_to_rgb", round_trip_failures == 0,
                              round_trip_failures == 0 ? "all match"
                                                       : std::to_string(round_trip_failures) + " differ"});
        });
}
