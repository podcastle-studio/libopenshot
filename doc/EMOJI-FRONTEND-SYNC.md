# Colour emoji — front-end sync spec

The backend (libopenshot `src/text` + `src/subtitle`) now renders colour emoji in text clips. The
editor has to mirror four things or the preview and the exported video will disagree — not just on
how emoji look, but on **line wrapping and which character animates when**, because an emoji
sequence counts as one letter.

This document is the contract. Everything in it was measured against the shipped fonts, not
inferred.

---

## 0. Checklist

| # | What | Why it can't be skipped |
|---|---|---|
| 1 | Load the **same emoji font file**, byte for byte | Emoji artwork and advances come from the font |
| 2 | Mirror the **cluster segmentation** rule | Letter count drives wrapping + the char-stagger index space |
| 3 | Mirror the **font-resolution order** (emoji font wins for emoji-presentation codepoints) | Several text fonts ship monochrome emoji glyphs and would win otherwise |
| 4 | Shape each cluster to **one glyph** | Emoji sequences are GSUB ligatures; a cmap lookup returns the pieces |
| 5 | Give each **draw pass** explicit emoji behaviour | Skia ignores paint colour/shader/stroke/mask-filter on colour glyphs |

Item 5 applies to CanvasKit exactly as it applies to native Skia — it is the same rasteriser.

---

## 1. The font

**`NotoColorEmoji-COLRv1.ttf`**, pinned to noto-emoji **v2.047**, COLRv1 flavour.

```
source: https://github.com/googlefonts/noto-emoji/raw/v2.047/fonts/Noto-COLRv1.ttf
sha256: 23549f29b5ad741fcb4c025b8dc44652ff0f459892467ebcccec1e6bbe839b44
size:   4,813,824 bytes
```

Already vendored in `video-rendering-service/fonts/` and `text-metrics/fonts/`, installed by both
Dockerfiles to `/usr/share/fonts/truetype/custom/`. The backend also accepts an override via the
`OPENSHOT_EMOJI_FONT` env var, and falls back to a distro CBDT build on dev machines.

### Why COLRv1 rather than the CBDT build

| | CBDT (distro `fonts-noto-color-emoji`) | **COLRv1 (pinned)** |
|---|---|---|
| size | 10.8 MB | **5.0 MB** |
| glyph data | 136 px PNG bitmap strikes | **vector** |
| at export resolution | visibly soft above ~140 px | **crisp at any size** (verified at 320 px and in a 4K render) |
| advance | 1.245117 em | **1.245117 em** (identical) |
| CanvasKit support | yes (its freetype config defines `FT_CONFIG_OPTION_USE_PNG`) | yes (`TT_CONFIG_OPTION_COLOR_LAYERS` / `TT_SUPPORT_COLRV1`) |

### Loading it in CanvasKit

```ts
const emojiData = await fetch(EMOJI_FONT_URL).then(r => r.arrayBuffer());
const emojiTypeface = CanvasKit.Typeface.MakeTypefaceFromData(emojiData);
// (MakeFreeTypeFaceFromData is the same call under its legacy name)
// Keep one instance for the session and build a Font per size from it.
```

Do **not** apply synthetic bold or italic (`embolden` / `skewX`) to the emoji face — a colour glyph
carries its own artwork and faux styling smears it. The backend deliberately skips both.

### Two traps

- **Glyph IDs are per-file.** The same emoji is glyph 1780 in the COLRv1 file and glyph 883 in the
  CBDT file. Any glyph-ID cache must be keyed on the font file, and the two sides must load the
  same file for such a cache to be shareable at all.
- **If you subset for download size**, subset from this exact file and keep the full file as the
  fallback. A subset that drops a cluster the export can render produces a visible divergence.

### Metrics (font units)

| | upem | ascent | descent | line spacing | emoji advance |
|---|---|---|---|---|---|
| COLRv1 | 1024 | 950 (0.927734 em) | 250 (0.244141 em) | 1200 | 1275 (**1.245117 em**) |
| CBDT | 2048 | 1898 (0.926758 em) | 507 (0.247559 em) | 2405 | 2550 (**1.245117 em**) |

**Every emoji cluster in this font advances exactly 1.245117 em** — grinning face, ZWJ family, flag,
keycap, skin-toned thumb, all of them. That is a useful invariant for layout (see §4), but read it
from the font rather than hardcoding it, so a future font bump can't silently break layout.

Note the ascent/descent differ by ~0.1–0.3 % between the two flavours. Irrelevant as long as both
sides load the same file; it is why they must.

---

## 2. Segmentation: what counts as one letter

### Why this matters beyond emoji looking right

No emoji beyond the simplest one is a single codepoint:

| sequence | codepoints |
|---|---|
| ❤️ | U+2764 U+FE0F |
| 👍🏽 | U+1F44D U+1F3FD |
| 👨‍👩‍👧 | U+1F468 U+200D U+1F469 U+200D U+1F467 |
| 🇺🇸 | U+1F1FA U+1F1F8 |
| 1️⃣ | U+0031 U+FE0F U+20E3 |

Iterating codepoints splits all of these. That breaks three things at once: the glyphs are wrong,
the measured width is wrong (so lines wrap differently), and the **per-character animation index
space shifts** — every letter after an emoji gets a different stagger slot, so the two engines
animate different characters at different times.

### The rule

This is deliberately **UTS #51 emoji sequences only, not full UAX #29 grapheme breaking**. It was
chosen because it is small enough to implement bit-identically on both sides. Do not substitute
`Intl.Segmenter` — its grapheme rules are broader (and ICU-version-dependent), so it will disagree
with the backend on non-emoji text.

Backend source of truth: `src/subtitle/EmojiFont.cpp`, `emojiClusterLen()`.

Plain text yields exactly one codepoint per letter, so existing behaviour is unchanged for
everything that isn't emoji.

### TypeScript port

```ts
const ZWJ = 0x200d, VS15 = 0xfe0e, VS16 = 0xfe0f, KEYCAP = 0x20e3;
const SKIN_FIRST = 0x1f3fb, SKIN_LAST = 0x1f3ff;
const TAG_FIRST = 0xe0020, TAG_LAST = 0xe007f;

const isRegionalIndicator = (cp: number) => cp >= 0x1f1e6 && cp <= 0x1f1ff;

const isEmojiExtender = (cp: number) =>
  cp === VS15 || cp === VS16 || cp === KEYCAP ||
  (cp >= SKIN_FIRST && cp <= SKIN_LAST) ||
  (cp >= TAG_FIRST && cp <= TAG_LAST);

const cpLen = (cp: number) => (cp > 0xffff ? 2 : 1);   // UTF-16 code units

/** Length in UTF-16 code units of the cluster starting at text[i]. */
export function clusterLength(text: string, i: number): number {
  const base = text.codePointAt(i)!;
  let end = i + cpLen(base);

  // A flag is exactly two regional indicators; a third starts a new flag.
  if (isRegionalIndicator(base)) {
    if (end < text.length) {
      const next = text.codePointAt(end)!;
      if (isRegionalIndicator(next)) end += cpLen(next);
    }
    return end - i;
  }

  const pictographicBase = isEmojiPictographic(base);

  while (end < text.length) {
    const cp = text.codePointAt(end)!;

    // Variation selectors / skin tones / keycaps / tags always belong to the base before them,
    // whatever that base is (a digit takes VS16 + keycap: 1️⃣).
    if (isEmojiExtender(cp)) { end += cpLen(cp); continue; }

    // A ZWJ joins the next pictographic codepoint into the same emoji. A trailing or unjoinable
    // ZWJ is left outside the cluster rather than swallowing unrelated text.
    if (cp === ZWJ && pictographicBase) {
      const after = end + cpLen(cp);
      if (after >= text.length) break;
      const member = text.codePointAt(after)!;
      if (!isEmojiPictographic(member)) break;
      end = after + cpLen(member);
      continue;
    }
    break;
  }
  return end - i;
}

export function* clusters(text: string): Generator<string> {
  let i = 0;
  while (i < text.length) {
    const len = clusterLength(text, i);
    if (len === 0) break;
    yield text.slice(i, i + len);
    i += len;
  }
}

export const clusterCount = (text: string): number => [...clusters(text)].length;
```

**Porting trap:** the C++ works in UTF-8 byte offsets, this works in UTF-16 code units. The cluster
*boundaries* are identical; only the index arithmetic differs. Iterate with the generator rather
than passing indices between the two representations.

---

## 3. Which letters go to the emoji font

### `isEmojiCluster`

```ts
export function isEmojiCluster(cluster: string): boolean {
  const cps = Array.from(cluster, c => c.codePointAt(0)!);   // iterates by code point
  if (cps.length === 0) return false;
  if (cps.length === 1) return isEmojiPresentation(cps[0]);

  // VS15 asks for text presentation explicitly (❤︎) — honour it and stay with the text font.
  if (cps.includes(VS15)) return false;
  // Keycaps are the one sequence whose base is not pictographic (1️⃣ #️⃣ *️⃣).
  if (cps.includes(KEYCAP)) return true;
  if (isRegionalIndicator(cps[0])) return true;
  if (!isEmojiPictographic(cps[0])) return false;   // e.g. a letter with a combining mark

  for (const cp of cps) {
    if (cp === VS16 || cp === ZWJ) return true;                 // ❤️  👨‍👩‍👧
    if (cp >= SKIN_FIRST && cp <= SKIN_LAST) return true;       // 👍🏽
    if (cp >= TAG_FIRST && cp <= TAG_LAST) return true;         // 🏴󠁧󠁢󠁳󠁣󠁴󠁿
  }
  return isEmojiPresentation(cps[0]);
}
```

A bare ❤ (U+2764, no VS16) stays with the **text** font — that is what browsers do, and the
backend matches it. Add VS16 and it becomes emoji.

### Font-resolution order

The backend's per-character fallback chain (`SkiaRenderer::getTypefaceForCharacter`) is now:

1. **colour emoji face** — if `isEmojiPresentation(cp) || isRegionalIndicator(cp)`
2. the requested family / font file, at the requested weight + slant
3. `Noto Sans Arabic`
4. `FreeSans`
5. colour emoji face again — anything the text fonts couldn't draw
6. fontconfig's best match for the style

**Tier 1 is first on purpose.** Plenty of text fonts ship monochrome outlines for emoji codepoints —
DejaVu Sans has one for U+1F600, FreeSerif for the skin-tone modifiers — so any later position lets
a text font win and silently render black-and-white emoji. Measured on the backend host: fontconfig's
own character fallback returns *DejaVu Sans* for U+1F600 and *FreeSerif* for U+1F3FD, so
`matchFamilyStyleCharacter`-style APIs are not trustworthy here.

In practice the editor loads specific webfonts (Poppins, Roboto, Playfair…) that don't cover emoji
at all, so tiers 1 and 2 rarely compete — but mirror the order anyway so a font that *does* cover
them can't cause a divergence.

### `isEmojiPresentation` / `isEmojiPictographic` tables

Port these verbatim from `src/subtitle/EmojiFont.cpp` (`kPresentation`, `kPictographic`). They are
sorted `[first, last]` ranges; a linear scan is fine at these sizes.

- `kPresentation` ≈ Emoji_Presentation=Yes (Unicode 15.1), with the 1FAxx blocks widened to absorb
  later additions. Over-inclusion there is harmless: the backend verifies the candidate face
  actually covers the codepoint before using it, and tier 5 catches anything the table misses.
- `kPictographic` ≈ Extended_Pictographic coarsened to whole blocks. It is only consulted for
  cluster continuation after a ZWJ and for "could this base start an emoji cluster", so a wide net
  costs nothing.

If you'd rather generate them than copy them, generate from the same Unicode version (15.1) and
diff against the C++ table before shipping.

---

## 4. Shaping: a sequence must resolve to one glyph

Emoji sequences are **GSUB ligatures** in the font. A cmap-only lookup (`Font.getGlyphIDs`,
`SkFont::textToGlyphs`, `measureText`) cannot compose them. Measured against NotoColorEmoji with
cmap only:

| sequence | cmap-only result | correct |
|---|---|---|
| ❤️ | glyphs `[168, 0]` — heart + **.notdef** | 1 glyph |
| 👍🏽 | `[569, 489]` — yellow thumb + colour swatch | 1 glyph |
| 👨‍👩‍👧 | `[596, 18, 597, 18, 595]` — three people + two ZWJ boxes | 1 glyph |
| 🇺🇸 | `[225, 223]` — the letters "U", "S" | 1 glyph |
| 1️⃣ | digit + .notdef + enclosing mark | 1 glyph |

So shaping is mandatory, not an optimisation.

**How the backend does it:** HarfBuzz, which is already compiled inside `libskia` and exports its
symbols, so only the headers were needed — no new dependency. It shapes the cluster in font units
and caches the result per `(cluster, size)`. Result: exactly one glyph per cluster in all seven
sequence classes tested.

**For CanvasKit** the requirement is the same but the route is yours to pick, and I don't have your
renderer in front of me. Two things that should help:

- **Layout doesn't need the shaper.** Every emoji cluster in this font advances exactly
  1.245117 em (§1), and the backend gets the same number from HarfBuzz. So width measurement,
  wrapping and stagger can be done from the constant alone; only *drawing* needs the composed
  glyph.
- **Drawing does.** If your per-letter draw path uses `Font.getGlyphIDs` + `drawGlyphs`, sequences
  will come out as pieces. A one-cluster `ParagraphBuilder`/`Paragraph` painted at the pen position
  goes through CanvasKit's shaper and composes correctly; a pinned-font glyph-ID lookup table is
  the cheaper-but-more-fragile alternative.

Please confirm which route you take — if the editor ends up drawing base-codepoint-only fallbacks
while the export composes properly, that's a visible divergence on skin tones and ZWJ families
specifically. The backend degrades to the base codepoint too when HarfBuzz headers are missing at
build time (👍🏽 → 👍), so if you see that shape of mismatch, check both sides' build config first.

---

## 5. Draw passes: Skia ignores paint on colour glyphs

When Skia draws a colour glyph (CBDT bitmap or COLRv1 paint graph) it uses the glyph's own artwork
and **ignores**:

| paint property | effect on a colour glyph |
|---|---|
| colour | ignored |
| shader (e.g. a gradient) | ignored |
| stroke style + width | ignored — draws the same filled glyph |
| **mask filter** (blur) | **ignored** |
| colour filter | applied ✅ |
| image filter | applied ✅ |
| alpha (`setAlphaf`) | applied ✅ |

The two "applied" rows are the escape hatch. This is identical in CanvasKit — same rasteriser.

Left alone, this makes every pass that isn't the plain fill do the wrong thing: a **shadow pass**
drops a full-colour, unblurred duplicate of the emoji behind the text, and a **stroke pass** draws
the emoji a second time on top of itself.

The backend therefore tags every draw with a pass descriptor (`src/subtitle/EmojiPass.h`), and the
text layer passes one at every call site (flat, curved, glow, animated × shadow/stroke/fill):

| pass | kind | emoji behaviour |
|---|---|---|
| fill | `Colour` | draw in its own colours; carry the pass alpha; re-apply the pass blur as an **image** filter |
| stroke | `Skip` | draw nothing — no outline to trace, and the fill already covers it |
| drop shadow | `Silhouette` | flatten to the shadow colour, blurred by the same sigma |
| glow silhouette | `Silhouette` | flatten to the glow colour, so the emoji lights the bloom instead of leaking its artwork into it |

### The recipe

```ts
// Silhouette (shadow / glow): rgb = colour.rgb, a = colour.a * glyph.a
paint.setColorFilter(CanvasKit.ColorFilter.MakeBlend(passColour, CanvasKit.BlendMode.SrcIn));

// Blur that actually applies to a colour glyph — same sigma the mask filter would have used
paint.setImageFilter(CanvasKit.ImageFilter.MakeBlur(sigma, sigma, CanvasKit.TileMode.Decal, null));

// Fill: keep the emoji's colours, follow the pass opacity
paint.setAlphaf(passAlpha);
```

Verified: this produces a correctly tinted, correctly blurred emoji shadow in a single draw, on both
font flavours.

### Gradient text

The backend's flat path puts a gradient on the paint as a **shader**, which colour glyphs ignore —
so emoji keep their own colours inside gradient-filled text. Its per-glyph animated/curved paths
instead composite coverage in a layer and mask the ramp with `SrcIn`, where emoji *do* pick up the
gradient. That inconsistency predates this work and mirrors how the two paths were originally
ported. Match whichever your renderer already does per path; flag it if your behaviour differs and
we'll align both sides deliberately.

### ⚠️ Backend-only: do NOT copy the channel swap

The backend applies an R↔B colour-matrix swap to the emoji fill paint. That is **not** a colour
convention of the format — it compensates for libopenshot handing a Skia N32 buffer to Qt as
`Format_RGBA8888` (see `TextClipReader::renderToQImage`), which transposes red and blue. Every
colour the styles specify is pre-swapped for that by `parseColorString`, but font artwork never
passes through it.

The front end has no such reinterpretation. **Copying this swap would make every emoji in the editor
cyan-faced and blue-hearted** — which is exactly what the backend looked like before the swap was
added.

---

## 6. Test vectors

These are the cases the backend asserts (`tests/EmojiFont.cpp`). Port them; they're the cheapest way
to prove the two segmenters agree.

### Segmentation

| input | expected clusters |
|---|---|
| `abc` | `a`, `b`, `c` |
| `بت` | `ب`, `ت` (one codepoint each) |
| `😀` | `😀` |
| `❤` + VS16 | one cluster |
| `👍` + U+1F3FD | one cluster |
| `👨` ZWJ `👩` ZWJ `👧` | one cluster |
| U+1F1FA U+1F1F8 | one cluster |
| `1` VS16 U+20E3 | one cluster |
| `a😀b` | `a`, `😀`, `b` |
| U+1F1FA U+1F1F8 U+1F1FA U+1F1F8 | **two** clusters (a third RI starts a new flag) |
| `😀` + ZWJ (trailing) | **two** clusters (dangling ZWJ stays separate) |
| `😀` ZWJ `a` | **three** clusters |
| `😀👍` | **two** clusters |

### Presentation

| input | `isEmojiCluster` |
|---|---|
| `😀` | true |
| `❤` (bare) | **false** — text font |
| `❤` + VS16 | true |
| `❤` + VS15 | **false** — explicit text presentation |
| `👍` + skin tone | true |
| ZWJ family | true |
| flag | true |
| keycap | true |
| `a`, `ب`, `""` | false |

### Advances

At any font size, every emoji cluster measures `1.245117 × fontSize`. A quick cross-check that
caught a real bug in the sibling measurement service: **8 grinning faces in a 4 em wide box wrap to
3 lines** (3 fit per line at 1.245 em), and **4 skin-toned thumbs wrap to 2 lines**. If you get 3
lines for the thumbs, the skin-tone modifier is being counted as its own letter.

---

## 7. Backend reference

| file | what's in it |
|---|---|
| `src/subtitle/EmojiPass.h` | the pass descriptor (`Colour` / `Skip` / `Silhouette` + blur sigma) |
| `src/subtitle/EmojiFont.h` / `.cpp` | classification tables, `emojiClusterLen`, `isEmojiCluster`, the pinned face, HarfBuzz shaping cache |
| `src/subtitle/SkiaRenderer.cpp` | the fallback tier order, `drawEmojiCluster`, `measureEmojiCluster`, `emojiClusterBounds` |
| `src/text/TextDrawShared.h` | `forEachCluster` / `clusterCount` / `measureLetter` / `drawLetter` — the single choke point where text is split into letters |
| `tests/EmojiFont.cpp` | the segmentation test vectors above |
| `examples/TextPayloadExample.cpp` | payload **10** (static: shadow + stroke + gradient) and **11** (char animation + glow) render harnesses |
| `video-rendering-service/fonts/`, `text-metrics/fonts/` | the pinned font + Dockerfile install |

`text-metrics` vendors `src/text` and `src/subtitle` verbatim and is already in sync, so its
measurements match the renderer.

---

## 8. Open questions for the front end

1. **What does the editor load for emoji today** — nothing, this file, the CBDT build, or Twemoji?
   If it's already on a different file, tell us and we'll align on that one instead; the shared
   bytes matter more than which file we pick.
2. **How does your per-letter path get a composed glyph** for a ZWJ/skin-tone/flag sequence (§4)?
3. **Do subtitles share the text-clip renderer** on your side? On the backend they don't:
   `src/subtitle/WordRenderer` still walks codepoints with its own UTF-8 loop, so it gets
   single-codepoint emoji from the new fallback tier but renders sequences as pieces. Happy to
   route it through the cluster path too — say if you need it.
