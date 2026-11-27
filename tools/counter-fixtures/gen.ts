// Parity fixtures for the C++ counter engine (src/text/counter), generated from the TS engine.
// Run through generate.sh, which supplies ./engine. Output: JSON on stdout.
//
// Measurer: fixed per-character widths ('1' = 6, spaces = 4, everything else 10, per codepoint)
// and metrics as fractions of the font size, the stub engine.test.ts uses. The C++ test builds
// the same stub, so any difference is the engine's.
import {
  DEFAULT_FX, PRESETS, TEMPLATES, applyPreset, compileLayer, evaluateLayer, layerFromTemplate, makeLayer,
  parseText, formatBody, rewriteToken, EASINGS,
  type CounterFx, type Layer, type Measurer, type TextStyle,
} from './engine/index.ts';

const measure: Measurer = {
  version: 0,
  width: (_s, text) => [...text].reduce((w, c) => w + (c === '1' ? 6 : c === ' ' || c === ' ' || c === ' ' ? 4 : 10), 0),
  metrics: (s) => ({ ascent: s.fontSize * 0.9, descent: s.fontSize * 0.25, digit: s.fontSize * 0.72 }),
};

const NONE = { type: 'none' as const, duration: 0 };
// 1e-9 absolute is far inside the 1e-6 the C++ test allows, and keeps the file small.
const round = (v: number) => (Number.isFinite(v) ? Math.round(v * 1e9) / 1e9 : v > 0 ? 1e300 : -1e300);

interface Case { name: string; layer: Layer }
const cases: Case[] = [];
const withLayer = (name: string, text: string, fx: CounterFx, style: Partial<TextStyle> = {}, duration = 4) =>
  cases.push({ name, layer: makeLayer({ text, duration, counter: fx, enter: NONE, exit: NONE, style: { ...makeLayer({ text }).style, ...style } }) });

// Every preset on a money amount and on a signed percentage crossing zero.
for (const p of PRESETS) {
  withLayer(`preset:${p.id}:money`, '$1,250,000', { ...applyPreset(null, p), from: {} });
  withLayer(`preset:${p.id}:delta`, '+12.4%', { ...applyPreset(null, p), from: { 0: -3.2 } }, { letterSpacing: 0, align: 'left' });
}
// Every template, as the library adds it.
for (const tpl of TEMPLATES) {
  const layer = layerFromTemplate(tpl, { duration: 4, enter: NONE, exit: NONE });
  cases.push({ name: `template:${tpl.id}`, layer });
}
// Formats, alignment, spacing, multi-line, several targets, odd settings.
const fx = (patch: Partial<CounterFx>): CounterFx => ({ ...DEFAULT_FX, from: {}, ...patch });
withLayer('align:right', 'Total $1,250,000', fx({}), { align: 'right', letterSpacing: 0 });
withLayer('align:left:runs', 'Total $1,250,000 raised', fx({}), { align: 'left', letterSpacing: 0 });
withLayer('multiline:two-targets', 'Revenue $1,250,000\nup 87.5% in 2026', fx({ targets: [0, 1, 2], from: { 2: 1990 } }), { letterSpacing: 0.05 });
withLayer('compact:fixed', '1.2M', fx({ compactScale: false }));
withLayer('compact:ru', '1,2 млн', fx({}));
withLayer('compact:bn', '3.4bn users', fx({ style: 'slide' }));
withLayer('proportional', '$1,250,000', fx({ tabular: false }));
withLayer('stepfps', '9,876', fx({ stepFps: 8, style: 'none' }));
withLayer('clock:hms', '1:02:03.5', fx({ style: 'flip' }));
withLayer('countdown:negative', '-250', fx({ from: { 0: 1200 }, style: 'roll', direction: 'alternate' }));
withLayer('slot:negative', '-4,815', fx({ mode: 'slot', clip: true, order: 'random' }));
withLayer('scramble:ordinal', '42nd', fx({ mode: 'scramble', rate: 10, order: 'rtl' }));
withLayer('leading-zeros', '007', fx({ from: { 0: 123 }, style: 'drum', soft: true }));
withLayer('short-window', '$99,999', fx({ range: [0.4, 0.45], cascade: 0.08 }));
withLayer('zero-window', '512', fx({ range: [0.5, 0.5] }));
withLayer('static-amount', '−$9,007,199,254,740,993.50 · 42', fx({ targets: [0, 1], from: { 0: -9007199254740990 } }));
withLayer('grow-landpop', '850K', fx({ grow: 0.7, landPop: 1, style: 'pop', bounce: 1 }));
withLayer('fast-maxspeed', '$1,250,000', fx({ maxSpeed: 4, easing: EASINGS.linear }), {}, 6);
withLayer('targets:second', '3/4', fx({ targets: [1], from: { 1: 9 } }));

// Times: a regular grid over the clip plus both edges of the count window and the clip end.
function timesFor(layer: Layer): number[][] {
  const out: number[][] = [];
  const d = layer.duration;
  const n = 32;
  for (let i = 0; i <= n; i++) out.push([(i / n) * d, (i / n) * d]);
  const r = layer.counter!.range;
  for (const edge of [r[0] * d, r[1] * d]) for (const e of [-1e-3, 0, 1e-3]) out.push([Math.min(Math.max(edge + e, 0), d), Math.min(Math.max(edge + e, 0), d)]);
  // motion-blur samples: t differs from the frame time
  out.push([1.004, 1.0], [0.996, 1.0], [2.5 + 1 / 120, 2.5]);
  return out;
}

const glyphRow = (g: ReturnType<typeof evaluateLayer>['glyphs'][number]) => [
  g.text, round(g.x), round(g.y), round(g.w), g.run ? 1 : 0, round(g.dy), round(g.sx), round(g.sy), round(g.pivot),
  round(g.opacity), round(g.blur), g.clip ? round(g.clip[0]) : null, g.clip ? round(g.clip[1]) : null, round(g.feather),
];

const out: Record<string, unknown> = { measure: "'1' = 6, U+0020/U+00A0/U+202F = 4, else 10 per codepoint; ascent 0.9, descent 0.25, digit 0.72 x fontSize", cases: [] };
for (const { name, layer } of cases) {
  const c = compileLayer(layer, measure);
  const samples = timesFor(layer).map(([t, ft]) => {
    const f = evaluateLayer(c, t, ft);
    return {
      t, ft,
      g: f.glyphs.map(glyphRow),
      b: [f.bounds.x, f.bounds.y, f.bounds.w, f.bounds.h].map(round),
      xf: [f.xf.dx, f.xf.dy, f.xf.scale, f.xf.px, f.xf.py, f.xf.opacity, f.xf.blur].map(round),
      pose: f.pose.map(round),
    };
  });
  (out.cases as unknown[]).push({
    name,
    text: layer.text,
    style: { fontSize: layer.style.fontSize, letterSpacing: layer.style.letterSpacing, lineHeight: layer.style.lineHeight, align: layer.style.align },
    duration: layer.duration,
    counter: layer.counter,
    tracks: c.tracks.length,
    settles: c.tracks.map((t) => round(t.settlesAt)),
    samples,
  });
}

// Parser and formatter: tokens of a corpus, and values written back in each token's format.
const corpus = [
  '$1,250,000', '1 250 000 ₽', '1 250 000 €', '1.250.000,50 €', '87,5%', '42.195 km', '1,250', '0,250', '007', '1.250',
  '+12.4%', '−$300', 'Top-10', '2020-2026', '1.2M', '850K views', '1,2 млн', '3 тыс. руб', '5 km', '1st place', '22nd', '01:30',
  '1:02:03.5', '4.8/5', '12:34:56,789', '1234:56', '1:23:456', '3.4bn', '2B', 'Bnot 4Bn', '9,007,199,254,740,993', '0.12345678901234567890%',
  '1 000 000', "1'000'000", '1’000', '×10', '-5', 'a-5', '$-5', '−€12.50', '100K+', '9.99kg', '4Kids', '10.5.2026',
];
const values = [0, 1, -1, 7.25, 999, 999.95, 1000, 12345.678, -3.2, 850000, 999949, 999950, 2500000, 1.5e9, 90, 3723.5];
const parse = corpus.map((text) => ({
  text,
  tokens: parseText(text).map((t) => ({
    index: t.index, kind: t.kind, start: t.start, end: t.end, signAt: t.signAt, sign: t.sign, minusChar: t.minusChar,
    value: t.value, precisionLost: !!t.precisionLost, decimals: t.decimals, decimalSep: t.decimalSep, groupSep: t.groupSep,
    minInt: t.minInt, compact: t.compact, ordinal: t.ordinal, time: t.time, source: t.source,
    body: values.map((v) => formatBody(t, v)),
    fixed: values.map((v) => formatBody(t, v, false)),
    rewrite: values.map((v) => rewriteToken(text, t, v)),
  })),
}));
out.values = values;
out.parse = parse;

process.stdout.write(JSON.stringify(out));
