// Port of easing.ts and parse.ts (async-counters-handoff-v4). Keep line-for-line with the TS.

#include "CounterEngine.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace openshot {
namespace text {
namespace counter {

// ── easing.ts ────────────────────────────────────────────────────────────────

namespace {
inline double A(double p1, double p2) { return 1 - 3 * p2 + 3 * p1; }
inline double B(double p1, double p2) { return 3 * p2 - 6 * p1; }
inline double C(double p1) { return 3 * p1; }
inline double poly(double p1, double p2, double s) { return ((A(p1, p2) * s + B(p1, p2)) * s + C(p1)) * s; }
inline double slope(double p1, double p2, double s) { return (3 * A(p1, p2) * s + 2 * B(p1, p2)) * s + C(p1); }
} // namespace

double ease(const Bezier& b, double x) {
    if (x <= 0) return 0;
    if (x >= 1) return 1;
    const double x1 = b.x1, y1 = b.y1, x2 = b.x2, y2 = b.y2;
    if (x1 == y1 && x2 == y2) return x;
    double s = x;
    for (int i = 0; i < 6; i++) {
        const double err = poly(x1, x2, s) - x;
        if (std::abs(err) < 1e-10) return poly(y1, y2, s);
        const double d = slope(x1, x2, s);
        if (std::abs(d) < 1e-6) break;
        s -= err / d;
    }
    if (s < 0 || s > 1 || std::abs(poly(x1, x2, s) - x) > 1e-8) {
        double lo = 0;
        double hi = 1;
        for (int i = 0; i < 44; i++) {
            s = (lo + hi) / 2;
            if (poly(x1, x2, s) < x) lo = s;
            else hi = s;
        }
    }
    return poly(y1, y2, s);
}

double easeInverse(const Bezier& b, double y) {
    if (y <= 0) return 0;
    if (y >= 1) return 1;
    const double x1 = b.x1, y1 = b.y1, x2 = b.x2, y2 = b.y2;
    if (x1 == y1 && x2 == y2) return y;
    double lo = 0;
    double hi = 1;
    for (int i = 0; i < 44; i++) {
        const double s = (lo + hi) / 2;
        if (poly(y1, y2, s) < y) lo = s;
        else hi = s;
    }
    return poly(x1, x2, hi);
}

double smoothstep(double x) {
    const double t = clamp(x);
    return t * t * (3 - 2 * t);
}

std::optional<Bezier> easingByName(const std::string& n) {
    if (n == "linear") return Bezier{0, 0, 1, 1};
    if (n == "easeOut") return Bezier{0.22, 0.61, 0.36, 1};
    if (n == "smoothOut") return Bezier{0.16, 1, 0.3, 1};
    if (n == "strongOut") return Bezier{0.05, 0.9, 0.1, 1};
    if (n == "easeInOut") return Bezier{0.65, 0, 0.35, 1};
    if (n == "softInOut") return Bezier{0.45, 0, 0.55, 1};
    if (n == "easeIn") return Bezier{0.55, 0, 1, 0.45};
    if (n == "lateRush") return Bezier{0.7, 0, 0.84, 0};
    if (n == "quickStart") return Bezier{0, 0.55, 0.45, 1};
    return std::nullopt;
}

// ── JS number helpers ────────────────────────────────────────────────────────

double jsRound(double x) {
    if (!std::isfinite(x)) return x;
    const double r = std::floor(x);
    return (x - r >= 0.5) ? r + 1 : r;
}

// Number.prototype.toString for an integral double: the shortest round-trip digits, padded with
// zeros (String(2 ** 60) is "1152921504606847000", not the exact 1152921504606846976).
std::string jsIntegerString(double v) {
    if (!std::isfinite(v)) return std::isnan(v) ? "NaN" : (v < 0 ? "-Infinity" : "Infinity");
    if (v == 0) return "0";
    const bool neg = v < 0;
    const double a = std::abs(v);
    char buf[64];
    if (a < 9007199254740992.0) {
        std::snprintf(buf, sizeof buf, "%.0f", a);
        return (neg ? "-" : "") + std::string(buf);
    }
    for (int p = 1; p <= 17; ++p) {
        std::snprintf(buf, sizeof buf, "%.*e", p - 1, a);
        if (std::strtod(buf, nullptr) == a) break;
    }
    // buf = d.ddddde+XX
    std::string s(buf);
    const size_t e = s.find('e');
    std::string mant = s.substr(0, e);
    const int exp = std::atoi(s.c_str() + e + 1);
    std::string digits;
    for (char c : mant) if (c != '.') digits += c;
    // digits represent d.ddd * 10^exp  →  integer with exp+1 digits
    const int intLen = exp + 1;
    while (static_cast<int>(digits.size()) < intLen) digits += '0';
    if (static_cast<int>(digits.size()) > intLen) digits.resize(intLen);   // cannot happen for integers
    return (neg ? "-" : "") + digits;
}

namespace {

// 10 ** n and 1000 ** n as V8 computes them for the small integer exponents used here.
inline double pow10i(int n) { return std::pow(10.0, n); }
inline double pow1000i(int n) { return std::pow(1000.0, n); }

std::string padStart(const std::string& s, int n, char c = '0') {
    // String.prototype.padStart counts UTF-16 units; every padded string here is ASCII digits.
    if (static_cast<int>(s.size()) >= n) return s;
    return std::string(n - s.size(), c) + s;
}

} // namespace

// ── UTF-8 ↔ codepoints ───────────────────────────────────────────────────────

std::u32string toCodepoints(const std::string& s) {
    std::u32string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        const auto b = static_cast<unsigned char>(s[i]);
        char32_t cp;
        size_t len;
        if (b < 0x80) { cp = b; len = 1; }
        else if ((b & 0xE0) == 0xC0 && i + 1 < s.size()) { cp = ((b & 0x1F) << 6) | (s[i + 1] & 0x3F); len = 2; }
        else if ((b & 0xF0) == 0xE0 && i + 2 < s.size()) {
            cp = ((b & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F); len = 3;
        } else if ((b & 0xF8) == 0xF0 && i + 3 < s.size()) {
            cp = ((b & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F); len = 4;
        } else { cp = 0xFFFD; len = 1; }
        out.push_back(cp);
        i += len;
    }
    return out;
}

std::string fromCodepoints(const std::u32string& cps, size_t from, size_t to) {
    std::string out;
    if (to > cps.size()) to = cps.size();
    for (size_t i = from; i < to; ++i) {
        const char32_t c = cps[i];
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0x800) { out += static_cast<char>(0xC0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18)); out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

// ── parse.ts ─────────────────────────────────────────────────────────────────

namespace {

using Text = std::u32string;
constexpr char32_t NONE = 0xFFFFFFFF;   // `text[k]` past either end

inline char32_t at(const Text& t, long k) { return k >= 0 && k < static_cast<long>(t.size()) ? t[k] : NONE; }
inline bool isDigit(char32_t c) { return c >= U'0' && c <= U'9'; }

// \p{L}. A table of the letter blocks rather than ICU, so the engine stays dependency-free:
// Latin, Greek, Cyrillic, Armenian, Hebrew, Arabic, the Indic blocks, Thai, Georgian, Hangul,
// kana, CJK. Covers what can stand next to a number in a caption; documented as a limit.
bool isLetter(char32_t c) {
    if (c == NONE) return false;
    if ((c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z')) return true;
    if (c < 0xAA) return false;
    if (c == 0xAA || c == 0xB5 || c == 0xBA) return true;
    if (c >= 0xC0 && c <= 0x24F) return c != 0xD7 && c != 0xF7;
    if (c >= 0x250 && c <= 0x2AF) return true;               // IPA
    if (c >= 0x370 && c <= 0x3FF) return c != 0x375 && c != 0x37E && c != 0x384 && c != 0x385 && c != 0x387;
    if (c >= 0x400 && c <= 0x52F) return !(c >= 0x482 && c <= 0x489);
    if (c >= 0x531 && c <= 0x587) return true;
    if (c >= 0x5D0 && c <= 0x5F2) return true;
    if (c >= 0x620 && c <= 0x64A) return true;
    if (c >= 0x671 && c <= 0x6D3) return true;
    if (c >= 0x904 && c <= 0xDFF) return true;               // Indic (approximate: includes marks)
    if (c >= 0xE01 && c <= 0xE30) return true;               // Thai
    if (c >= 0x10A0 && c <= 0x10FF) return true;             // Georgian
    if (c >= 0x1E00 && c <= 0x1FFF) return true;             // Latin ext. additional, Greek ext.
    if (c >= 0x3041 && c <= 0x30FF) return c != 0x30FB;      // kana
    if (c >= 0x3400 && c <= 0x9FFF) return true;             // CJK
    if (c >= 0xAC00 && c <= 0xD7A3) return true;             // Hangul
    if (c >= 0xFF21 && c <= 0xFF3A) return true;
    if (c >= 0xFF41 && c <= 0xFF5A) return true;
    return false;
}

bool isGroupSep(char32_t c) {
    return c == U',' || c == U'.' || c == U' ' || c == 0x00A0 || c == 0x202F || c == 0x2009 || c == U'\'' || c == 0x2019;
}

char signOf(char32_t c) {
    if (c == U'+') return '+';
    if (c == U'-' || c == 0x2212) return '-';
    return '\0';
}

bool isCurrency(char32_t c) {
    switch (c) {
    case U'$': case 0x20AC: case 0xA3: case 0xA5: case 0x20BD: case 0x20B9: case 0x20A9: case 0x20AA:
    case 0x20BA: case 0x20B4: case 0x20B8: case 0x20BF: case 0xA2:
        return true;
    default:
        return false;
    }
}

std::string cp(char32_t c) { return fromCodepoints(Text(1, c)); }
std::string slice(const Text& t, long a, long b) {
    if (a < 0) a = 0;
    if (b > static_cast<long>(t.size())) b = static_cast<long>(t.size());
    if (b <= a) return {};
    return fromCodepoints(t, a, b);
}

NumberToken blank() { return NumberToken{}; }

// /(\d{1,3}):([0-5]\d)(?::([0-5]\d))?(?:([.,])(\d{1,3}))?(?!\d)/y with the regex's backtracking.
std::optional<NumberToken> scanTime(const Text& t, long i) {
    if (at(t, i - 1) == U':') return std::nullopt;
    long j = i;
    while (isDigit(at(t, j)) && j - i < 3) j++;
    // \d{1,3} is greedy; only the whole run can be followed by ':', so try the longest first.
    for (long aEnd = j; aEnd > i; --aEnd) {
        if (at(t, aEnd) != U':') continue;
        long k = aEnd + 1;
        const char32_t b0 = at(t, k), b1 = at(t, k + 1);
        if (!(b0 >= U'0' && b0 <= U'5' && isDigit(b1))) continue;
        const long bEnd = k + 2;
        // Optional seconds group (greedy: with it first), then optional fraction, then (?!\d).
        for (int withSecs = 1; withSecs >= 0; --withSecs) {
            long p = bEnd;
            long cStart = -1;
            if (withSecs) {
                const char32_t c0 = at(t, p + 1), c1 = at(t, p + 2);
                if (!(at(t, p) == U':' && c0 >= U'0' && c0 <= U'5' && isDigit(c1))) continue;
                cStart = p + 1;
                p += 3;
            }
            // fraction: ([.,])(\d{1,3}) greedy, else none
            struct Try { bool frac; long fEnd; };
            std::vector<Try> tries;
            if (at(t, p) == U'.' || at(t, p) == U',') {
                long q = p + 1;
                while (isDigit(at(t, q)) && q - (p + 1) < 3) q++;
                for (long fe = q; fe > p + 1; --fe) tries.push_back({true, fe});
            }
            tries.push_back({false, p});
            for (const Try& tr : tries) {
                const long endPos = tr.fEnd;
                if (isDigit(at(t, endPos))) continue;
                NumberToken tok = blank();
                tok.isTime = true;
                tok.start = static_cast<int>(i);
                tok.end = static_cast<int>(endPos);
                const int fields = withSecs ? 3 : 2;
                const double a = std::strtod(slice(t, i, aEnd).c_str(), nullptr);
                const double b = std::strtod(slice(t, k, bEnd).c_str(), nullptr);
                const double c = withSecs ? std::strtod(slice(t, cStart, cStart + 2).c_str(), nullptr) : 0;
                const double whole = fields == 3 ? a * 3600 + b * 60 + c : a * 60 + b;
                std::string frac = tr.frac ? slice(t, p + 1, tr.fEnd) : std::string();
                tok.decimals = static_cast<int>(frac.size());
                tok.value = whole + (!frac.empty() ? std::strtod(frac.c_str(), nullptr) / pow10i(static_cast<int>(frac.size())) : 0);
                TimeInfo ti;
                ti.fields = fields;
                ti.lead = static_cast<int>(aEnd - i);
                ti.frac = tok.decimals;
                ti.fracSep = tr.frac ? cp(at(t, p)) : ".";
                tok.time = ti;
                return tok;
            }
        }
    }
    return std::nullopt;
}

// Does the word `w` (codepoints) sit at t[j]?
bool startsWith(const Text& t, long j, const char32_t* w) {
    for (long k = 0; w[k]; ++k) if (at(t, j + k) != w[k]) return false;
    return true;
}
long wlen(const char32_t* w) { long n = 0; while (w[n]) n++; return n; }

NumberToken scanNumber(const Text& t, long i) {
    NumberToken tok = blank();
    long j = i;
    while (isDigit(at(t, j))) j++;
    const std::string first = slice(t, i, j);
    std::string intDigits = first;

    auto three = [&](long k) {
        return isDigit(at(t, k)) && isDigit(at(t, k + 1)) && isDigit(at(t, k + 2)) && !isDigit(at(t, k + 3));
    };
    const char32_t sep = at(t, j);
    if (first.size() <= 3 && first != "0" && sep != NONE && isGroupSep(sep) && three(j + 1)) {
        long k = j;
        int groups = 0;
        std::string digits = first;
        while (at(t, k) == sep && three(k + 1)) {
            digits += slice(t, k + 1, k + 4);
            k += 4;
            groups++;
        }
        const bool decimalFollows = (at(t, k) == U'.' || at(t, k) == U',') && at(t, k) != sep && isDigit(at(t, k + 1));
        const bool reallyDecimal = sep == U'.' && groups == 1 && !decimalFollows;
        if (!reallyDecimal) {
            tok.groupSep = cp(sep);
            intDigits = digits;
            j = k;
        }
    }

    std::string frac;
    const std::string groupSep = tok.groupSep;
    auto isGroupSepChar = [&](char32_t c) { return !groupSep.empty() && cp(c) == groupSep; };
    if ((at(t, j) == U'.' || at(t, j) == U',') && !isGroupSepChar(at(t, j)) && isDigit(at(t, j + 1))) {
        tok.decimalSep = cp(at(t, j));
        long k = j + 1;
        while (isDigit(at(t, k))) k++;
        frac = slice(t, j + 1, k);
        j = k;
    } else if (tok.groupSep == ".") {
        tok.decimalSep = ",";
    }
    tok.decimals = static_cast<int>(frac.size());
    if (tok.groupSep.empty() && first.size() > 1 && first[0] == '0') tok.minInt = static_cast<int>(first.size());

    double value = std::strtod((intDigits + (!frac.empty() ? "." + frac : "")).c_str(), nullptr);

    // LATIN_COMPACT = /(K|k|M|B|bn|T)(?!\p{L})/uy
    bool latin = false;
    std::string latinS;
    long latinLen = 0;
    {
        const char32_t c = at(t, j);
        auto tryAlt = [&](const char32_t* w, const char* s) {
            if (latin) return;
            if (!startsWith(t, j, w)) return;
            const long n = wlen(w);
            if (isLetter(at(t, j + n))) return;
            latin = true;
            latinS = s;
            latinLen = n;
        };
        (void)c;
        tryAlt(U"K", "K");
        tryAlt(U"k", "k");
        tryAlt(U"M", "M");
        tryAlt(U"B", "B");
        tryAlt(U"bn", "bn");
        tryAlt(U"T", "T");
    }
    // RU_COMPACT = /([   ]?)(тыс\.?|млн\.?|млрд\.?|трлн\.?)(?!\p{L})/uy
    bool ru = false;
    std::string ruGap, ruWord;
    long ruLen = 0;
    if (!latin) {
        static const char32_t* words[] = {U"тыс", U"млн", U"млрд",
                                          U"трлн"};
        const char32_t g = at(t, j);
        const bool hasGap = g == U' ' || g == 0x00A0 || g == 0x202F;
        for (int withGap = hasGap ? 1 : 0; withGap >= 0 && !ru; --withGap) {
            const long w0 = j + withGap;
            for (const char32_t* w : words) {
                if (!startsWith(t, w0, w)) continue;
                const long n = wlen(w);
                // \.? greedy, then (?!\p{L})
                if (at(t, w0 + n) == U'.' && !isLetter(at(t, w0 + n + 1))) {
                    ru = true; ruLen = withGap + n + 1; ruWord = slice(t, w0, w0 + n + 1);
                } else if (!isLetter(at(t, w0 + n))) {
                    ru = true; ruLen = withGap + n; ruWord = slice(t, w0, w0 + n);
                }
                if (ru) { ruGap = withGap ? cp(g) : std::string(); break; }
            }
        }
    }
    if (latin) {
        const std::string& s = latinS;
        const int tier = (s == "K" || s == "k") ? 1 : s == "M" ? 2 : s == "T" ? 4 : 3;
        CompactInfo ci;
        ci.ladder = {"", "K", "M", "B", "T"};
        ci.ladder[tier] = s;
        ci.tier = tier;
        tok.compact = ci;
        j += latinLen;
    } else if (ru) {
        const std::string tys = "\xD1\x82\xD1\x8B\xD1\x81";          // тыс
        const std::string mln = "\xD0\xBC\xD0\xBB\xD0\xBD";          // млн
        const std::string mlrd = "\xD0\xBC\xD0\xBB\xD1\x80\xD0\xB4"; // млрд
        const std::string trln = "\xD1\x82\xD1\x80\xD0\xBB\xD0\xBD"; // трлн
        auto sw = [&](const std::string& p) { return ruWord.compare(0, p.size(), p) == 0; };
        const int tier = sw(tys) ? 1 : sw(mlrd) ? 3 : sw(mln) ? 2 : 4;
        CompactInfo ci;
        ci.ladder = {"", ruGap + tys + ".", ruGap + mln, ruGap + mlrd, ruGap + trln};
        ci.ladder[tier] = ruGap + ruWord;
        ci.tier = tier;
        tok.compact = ci;
        j += ruLen;
    } else if (frac.empty()) {
        // ORDINAL = /(st|nd|rd|th)(?!\p{L})/uy
        for (const char32_t* w : {U"st", U"nd", U"rd", U"th"}) {
            if (startsWith(t, j, w) && !isLetter(at(t, j + 2))) {
                tok.ordinal = true;
                j += 2;
                break;
            }
        }
    }
    const double scale = pow10i(tok.decimals);
    const double multiplier = pow1000i(tok.compact ? tok.compact->tier : 0);
    if (tok.compact) value = (jsRound(value * scale) * multiplier) / scale;
    const double typedUnits = std::strtod((intDigits + frac).c_str(), nullptr);
    const bool safe = std::isfinite(typedUnits) && std::floor(typedUnits) == typedUnits && std::abs(typedUnits) <= 9007199254740991.0;
    if (!safe || jsRound((value / multiplier) * scale) != typedUnits) tok.precisionLost = true;

    // A sign belongs to the number when it touches it, or touches its currency sign: -5, +12.4%, −$300.
    long s = i - 1;
    if (s >= 0 && isCurrency(at(t, s))) s--;
    const char sign = s >= 0 ? signOf(at(t, s)) : '\0';
    if (sign && !isDigit(at(t, s - 1)) && !isLetter(at(t, s - 1))) {
        tok.signAt = static_cast<int>(s);
        tok.sign = sign;
        if (sign == '-') {
            tok.minusChar = cp(at(t, s));
            value = -value;
        }
    }

    tok.start = static_cast<int>(i);
    tok.end = static_cast<int>(j);
    tok.value = value;
    return tok;
}

} // namespace

std::vector<NumberToken> parseText(const std::string& text) {
    const Text t = toCodepoints(text);
    std::vector<NumberToken> out;
    long i = 0;
    const long n = static_cast<long>(t.size());
    while (i < n) {
        if (!isDigit(t[i])) {
            i++;
            continue;
        }
        std::optional<NumberToken> time = scanTime(t, i);
        NumberToken tok = time ? *time : scanNumber(t, i);
        tok.index = static_cast<int>(out.size());
        tok.source = slice(t, tok.start, tok.end);
        out.push_back(tok);
        i = std::max<long>(tok.end, i + 1);
    }
    return out;
}

// ── Writing a value back ─────────────────────────────────────────────────────

std::string ordinalSuffix(double n) {
    const double a = std::fmod(std::abs(n), 100.0);
    if (a >= 11 && a <= 13) return "th";
    const int k = static_cast<int>(std::fmod(a, 10.0));
    static const char* s[] = {"th", "st", "nd", "rd"};
    // ['th','st','nd','rd'][a % 10] ?? 'th' — a non-integer index yields undefined → 'th'
    if (std::fmod(a, 10.0) != k) return "th";
    return k < 4 ? s[k] : "th";
}

namespace {
std::string groupDigits(const std::string& digits, const std::string& sep) {
    if (sep.empty()) return digits;
    std::string out;
    const size_t n = digits.size();
    for (size_t k = 0; k < n; k++) {
        if (k > 0 && (n - k) % 3 == 0) out += sep;
        out += digits[k];
    }
    return out;
}
} // namespace

int compactTier(const NumberToken& tok, double magnitude) {
    const int top = tok.compact ? static_cast<int>(tok.compact->ladder.size()) - 1 : 0;
    int tier = 0;
    while (tier < top && magnitude >= compactBoundary(tok, tier)) tier++;
    return tier;
}

double compactBoundary(const NumberToken& tok, int tier) {
    const int d = tier == 0 ? 0 : tok.decimals;
    return pow1000i(tier) * (1000 - 0.5 / pow10i(d));
}

namespace {
inline bool sameNumber(double a, double b) {   // Object.is
    return a == b && std::signbit(a) == std::signbit(b);
}
} // namespace

std::string formatBody(const NumberToken& tok, double value, bool scaled) {
    if (tok.precisionLost && tok.source && sameNumber(value, tok.value)) return *tok.source;
    const double mag = std::abs(value);
    if (tok.isTime && tok.time) {
        const int fields = tok.time->fields, lead = tok.time->lead, frac = tok.time->frac;
        const std::string& fracSep = tok.time->fracSep;
        const double units = jsRound(mag * pow10i(frac));
        const double whole = std::floor(units / pow10i(frac));
        const std::string f = frac ? fracSep + padStart(jsIntegerString(std::fmod(units, pow10i(frac))), frac) : "";
        auto two = [](double n) { return padStart(jsIntegerString(n), 2); };
        if (fields == 3) {
            return padStart(jsIntegerString(std::floor(whole / 3600)), lead) + ":" + two(std::fmod(std::floor(whole / 60), 60)) +
                   ":" + two(std::fmod(whole, 60)) + f;
        }
        return padStart(jsIntegerString(std::floor(whole / 60)), lead) + ":" + two(std::fmod(whole, 60)) + f;
    }
    int tier = 0;
    int decimals = tok.decimals;
    if (tok.compact) {
        tier = scaled ? compactTier(tok, mag) : tok.compact->tier;
        if (tier == 0) decimals = 0;
    }
    const double units = jsRound((mag / pow1000i(tier)) * pow10i(decimals));
    if (!std::isfinite(units)) return jsIntegerString(mag);
    const std::string digits = jsIntegerString(units);
    const std::string all = padStart(digits, decimals + 1);
    const std::string intPart = padStart(all.substr(0, all.size() - decimals), tok.minInt);
    const std::string fr = decimals ? tok.decimalSep + all.substr(all.size() - decimals) : "";
    std::string out = groupDigits(intPart, tok.groupSep) + fr;
    if (tok.compact) out += tok.compact->ladder[tier];
    if (tok.ordinal) out += ordinalSuffix(units);
    return out;
}

std::string rewriteToken(const std::string& text, const NumberToken& tok, double value) {
    if (tok.precisionLost && sameNumber(value, tok.value)) return text;
    NumberToken next = tok;
    double v = value;
    if (next.compact && v != 0) next.compact->tier = compactTier(next, std::abs(v));
    if (next.isTime) v = std::abs(v);
    const std::string body = formatBody(next, v, false);
    const Text t = toCodepoints(text);
    Text out = t.substr(0, tok.start) + toCodepoints(body) + t.substr(tok.end);
    const bool neg = v < 0 && jsRound(std::abs(v) * pow10i(next.decimals)) > 0;
    if (tok.signAt >= 0) {
        const std::string keep = neg ? tok.minusChar : tok.sign == '+' ? "+" : "";
        out = out.substr(0, tok.signAt) + toCodepoints(keep) + out.substr(tok.signAt + 1);
    } else if (neg) {
        const long a = tok.start > 0 && isCurrency(at(t, tok.start - 1)) ? tok.start - 1 : tok.start;
        out = out.substr(0, a) + Text(1, 0x2212) + out.substr(a);
    }
    return fromCodepoints(out);
}

} // namespace counter
} // namespace text
} // namespace openshot
