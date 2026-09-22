#!/usr/bin/env python3
"""Emit a WebGL 1 (GLSL ES 1.00) fragment shader from an SkSL runtime effect.

This is not a transpiler. Skia's own `skslc` does the parsing, inlining and constant folding,
writing a normalised SkSL body to a `.stage` file; everything here is a mechanical rename of
types plus a host prologue. That split matters: the arithmetic, including how `/180.0` gets
folded into a float32 multiply, is decided once by Skia and is therefore identical on both
sides.

  ./sksl2glsl.py rotational_blur.rts rotational_blur.frag

Requires skslc (Skia GN target `skslc`, needs `skia_compile_sksl_tests=true`, and the
`src/sksl/*.sksl` module files copied next to the binary). Override its path with SKSLC=.
"""
import os
import re
import subprocess
import sys
import tempfile

SKSLC = os.environ.get("SKSLC", os.path.expanduser("~/skia-stable/skia/out/skslc/skslc"))

# Longest first: float4x4 before float4, half4 before half.
TYPES = [
    ("float2x2", "mat2"), ("float3x3", "mat3"), ("float4x4", "mat4"),
    ("half2x2", "mat2"),  ("half3x3", "mat3"),  ("half4x4", "mat4"),
    ("float2", "vec2"), ("float3", "vec3"), ("float4", "vec4"),
    ("half2", "vec2"),  ("half3", "vec3"),  ("half4", "vec4"),
    ("int2", "ivec2"),  ("int3", "ivec3"),  ("int4", "ivec4"),
    ("bool2", "bvec2"), ("bool3", "bvec3"), ("bool4", "bvec4"),
    ("half", "float"),
]

PROLOGUE = """// GENERATED — do not edit. Source: {src}
// Regenerate with:  ./sksl2glsl.py {src} {dst}
//
// WebGL 1 / GLSL ES 1.00 fragment shader, emitted from the SkSL that the server runs through
// SkRuntimeEffect. The body below is Skia's own normalised output, so both stacks evaluate the
// same expression tree with the same folded constants.
//
// The host MUST satisfy all of these or the two stacks will not agree:
//   * uTexture bound with NEAREST filtering, no mipmaps, no premultiply-on-upload changes.
//     The shader does its own bilinear; a LINEAR sampler would silently replace it with weights
//     the GL spec does not pin to bit precision.
//   * highp float. mediump is fp16 on mobile and will not match.
//   * The coordinate passed to skMain is in PIXELS, with fragment centres at integer + 0.5.
//   * Alpha convention must match whatever the server feeds SkRuntimeEffect (see README).
precision highp float;
precision highp sampler2D;

uniform sampler2D uTexture;
uniform vec2      uInvSize;       // 1.0 / texture size in pixels

varying vec2 vTextureCoord;       // Pixi: normalised, texel centres

"""

EPILOGUE = """
void main() {
    // Pixel-space coordinate with fragment centres at integer + 0.5.
    //
    // When the filter's framebuffer is 1:1 with the input texture, `gl_FragCoord.xy` is exactly
    // that and is the form to prefer for a bit-exactness test. `vTextureCoord / uInvSize` is the
    // portable equivalent when Pixi is rendering into a padded or scaled filter target.
    gl_FragColor = skMain(gl_FragCoord.xy);
}
"""


def find_matching_paren(s, open_idx):
    depth = 0
    for i in range(open_idx, len(s)):
        if s[i] == "(":
            depth += 1
        elif s[i] == ")":
            depth -= 1
            if depth == 0:
                return i
    raise ValueError("unbalanced parentheses")


def rewrite_child_eval(src):
    """child_N.eval(EXPR) -> texture2D(uTexture, (EXPR) * uInvSize), nesting-aware."""
    pattern = re.compile(r"child_(\d+)\.eval\(")
    while True:
        m = pattern.search(src)
        if not m:
            return src
        open_idx = m.end() - 1
        close_idx = find_matching_paren(src, open_idx)
        expr = src[open_idx + 1:close_idx]
        src = src[:m.start()] + "texture2D(uTexture, (" + expr + ") * uInvSize)" + src[close_idx + 1:]


def rewrite_types(src):
    for sksl, glsl in TYPES:
        src = re.sub(r"\b" + sksl + r"\b", glsl, src)
    return src


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src_path, dst_path = sys.argv[1], sys.argv[2]

    with tempfile.NamedTemporaryFile(suffix=".stage", delete=False) as tmp:
        stage_path = tmp.name
    try:
        subprocess.run([SKSLC, src_path, stage_path], check=True)
        body = open(stage_path).read()
    finally:
        os.unlink(stage_path)

    body = rewrite_child_eval(body)
    body = rewrite_types(body)

    # The generated entry point keeps its SkSL shape as a plain function, so no `return` inside
    # it has to be rewritten into a gl_FragColor assignment. The host prologue calls it.
    # skslc rewrites *references* to the main coords as `_coords` but leaves the parameter with
    # its authored name, so the declaration is renamed to match the body rather than the source.
    body, n = re.subn(r"\bvec4 main\(vec2 \w+\)", "vec4 skMain(vec2 _coords)", body)
    if n != 1:
        sys.exit("expected exactly one `half4 main(float2 ...)` entry point, found %d" % n)

    # Uniforms Skia declares implicitly; the sampler and its reciprocal size are ours.
    out = PROLOGUE.format(src=os.path.basename(src_path), dst=os.path.basename(dst_path))
    out += body.rstrip() + "\n" + EPILOGUE
    open(dst_path, "w").write(out)
    print("wrote %s" % dst_path)


if __name__ == "__main__":
    main()
