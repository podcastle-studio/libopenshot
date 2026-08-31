#pragma once

// How a colour-emoji cluster takes part in the text pass being drawn.
//
// A colour glyph carries its own artwork, so Skia ignores the paint's colour, its shader, its
// stroke style AND its mask filter when drawing one. Every pass that is not the plain fill
// therefore has to say what it wants instead, or emoji quietly do the wrong thing: a shadow pass
// would drop a full-colour, unblurred duplicate of the emoji behind the text, and a stroke pass
// would draw the emoji a second time on top of itself.
//
// Deliberately dependency-free so both the subtitle and text layers can describe a pass without
// pulling in Skia headers.

namespace openshot {
namespace subtitle {

struct EmojiPass {
    enum class Kind {
        Colour,      // fill pass: draw the emoji in its own colours
        Skip,        // stroke pass: a colour glyph has no outline, so this pass has nothing to add
        Silhouette,  // shadow / glow pass: flatten the emoji to the paint's colour and alpha
    };
    Kind kind = Kind::Colour;

    // Mask-filter sigma the pass would have used. Re-applied as an image filter, which is the
    // one blur that does affect colour glyphs.
    double blurSigma = 0.0;
};

} // namespace subtitle
} // namespace openshot
