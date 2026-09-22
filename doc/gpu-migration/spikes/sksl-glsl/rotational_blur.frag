// GENERATED — do not edit. Source: rotational_blur.rts
// Regenerate with:  ./sksl2glsl.py rotational_blur.rts rotational_blur.frag
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

uniform vec2 uSize;
uniform float uBlurAmount;
const float kPi_0 = 3.14159274;
const int kMaxIterations_0 = 30;
float reflectIndex_0(float i, float n);
vec4 texelAt_0(float x, float y, float useReflect);
float reflectIndex_0(float i, float n)
{
	float period = 2.0 * n;
	float t = mod(i, period);
	if (t < 0.0) 
	{
		t += period;
	}
	return t < n ? t : (period - 1.0) - t;
}
vec4 texelAt_0(float x, float y, float useReflect)
{
	float px = useReflect > 0.5 ? reflectIndex_0(x, uSize.x) : clamp(x, 0.0, uSize.x - 1.0);
	float py = useReflect > 0.5 ? reflectIndex_0(y, uSize.y) : clamp(y, 0.0, uSize.y - 1.0);
	vec4 c = vec4(texture2D(uTexture, (vec2(px, py) + 0.5) * uInvSize));
	return c.w > 0.0 ? vec4(c.xyz / c.w, c.w) : vec4(0.0);
}
vec4 skMain(vec2 _coords)
{
	float absBlur = abs(uBlurAmount);
	if (absBlur < 0.1) 
	{
		return vec4(texture2D(uTexture, (_coords) * uInvSize));
	}
	float iters = absBlur < 15.0 ? clamp(floor(absBlur * 1.2), 8.0, 25.0) : clamp(floor(absBlur * 0.6), 3.0, 30.0);
	float useReflect = float(absBlur < 10.0 ? 0.0 : 1.0);
	float maxAngle = (uBlurAmount * kPi_0) * 0.00555555569;
	vec2 center = uSize * 0.5;
	vec2 d = (_coords - 0.5) - center;
	vec4 acc = vec4(0.0);
	for (int i = 0;i < kMaxIterations_0; ++i) 
	{
		float fi = float(i);
		if (fi >= iters) 
		{
			break;
		}
		float a;
		if (absBlur < 15.0) 
		{
			a = ((fi + 0.01) / iters - 0.5) * maxAngle;
		}
		else if (iters == 1.0) 
		{
			a = -maxAngle * 0.5;
		}
		else 
		{
			a = (fi / (iters - 1.0) - 0.5) * maxAngle;
		}
		float s = sin(a);
		float c = cos(a);
		vec2 src = vec2(c * d.x - s * d.y, s * d.x + c * d.y) + center;
		float _0_x0 = floor(src.x);
		float _1_y0 = floor(src.y);
		float _2_ax = src.x - _0_x0;
		float _3_ay = src.y - _1_y0;
		vec4 _4_c00 = texelAt_0(_0_x0, _1_y0, useReflect);
		vec4 _5_c10 = texelAt_0(_0_x0 + 1.0, _1_y0, useReflect);
		vec4 _6_c01 = texelAt_0(_0_x0, _1_y0 + 1.0, useReflect);
		vec4 _7_c11 = texelAt_0(_0_x0 + 1.0, _1_y0 + 1.0, useReflect);
		acc += mix(mix(_4_c00, _5_c10, _2_ax), mix(_6_c01, _7_c11, _2_ax), _3_ay);
	}
	vec4 outColor = acc / iters;
	return vec4(vec4(vec4(outColor.xyz * outColor.w, outColor.w)));
}

void main() {
    // Pixel-space coordinate with fragment centres at integer + 0.5.
    //
    // When the filter's framebuffer is 1:1 with the input texture, `gl_FragCoord.xy` is exactly
    // that and is the form to prefer for a bit-exactness test. `vTextureCoord / uInvSize` is the
    // portable equivalent when Pixi is rendering into a padded or scaled filter target.
    gl_FragColor = skMain(gl_FragCoord.xy);
}
