/**
 * @file
 * @brief Demo harness for libopenshot's clip blend modes
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * Renders the same two source images composited with every openshot::BlendMode, through a real
 * openshot::Timeline (two clips, one per layer), and writes:
 *
 *   <out>/base.png             the backdrop layer, exactly as libopenshot consumed it
 *   <out>/overlay.png          the source layer, exactly as libopenshot consumed it
 *   <out>/NN-<mode>.png        one full-resolution composite per blend mode
 *   <out>/contact-sheet.png    all modes in a labelled grid
 *   <out>/manifest.json        scenario metadata + the clip JSON used, for the front end
 *
 * Both source images are written at the timeline's full size and composited 1:1 (no scaling,
 * no offset, no rotation), so a front-end render of the same two PNGs differs only by blend
 * math - not by resampling.
 *
 * Usage:
 *   openshot-blend-modes [--out DIR] [--scenario lab|probe] [--size WxH]
 *   openshot-blend-modes [--out DIR] --base BASE.png --overlay OVERLAY.png
 *
 * @ref License
 */

// Copyright (c) 2008-2019 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "BlendModes.h"
#include "Clip.h"
#include "Enums.h"
#include "Frame.h"
#include "Timeline.h"

#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QFont>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QString>
#include <QTextStream>

#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

using openshot::BlendMode;

namespace {

/* ------------------------------- scenario: "lab" -------------------------------- */
/* Recreates the generated test patterns from overlay-lab.html at t = 0, so the images
 * look like what the front end sees in the lab. The overlay keeps the lab's 480x270 size
 * and top-left placement, but is padded out to the full frame with transparent pixels so
 * both layers composite 1:1. */

QImage make_lab_base(int width, int height) {
	QImage image(width, height, QImage::Format_RGBA8888_Premultiplied);
	QPainter p(&image);
	p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing, true);

	QLinearGradient gradient(0, 0, width, height);
	gradient.setColorAt(0.0, QColor("#1a2980"));
	gradient.setColorAt(1.0, QColor("#26d0ce"));
	p.fillRect(0, 0, width, height, gradient);

	// The lab's 20 drifting diagonals, frozen at t = 0
	p.setPen(QPen(QColor(255, 255, 255, 38), 2));
	for (int i = 0; i < 20; ++i) {
		const double offset = std::fmod(i * 50.0, (double) width);
		p.drawLine(QPointF(offset, 0), QPointF(offset - 200, height));
	}

	QFont font("monospace", 0, QFont::Bold);
	font.setPixelSize(40);
	p.setFont(font);
	p.setPen(QColor(255, 255, 255, 217));
	p.drawText(40, 70, "BASE LAYER");
	p.end();
	return image;
}

QImage make_lab_overlay(int width, int height) {
	QImage image(width, height, QImage::Format_RGBA8888_Premultiplied);
	image.fill(Qt::transparent);

	// The lab draws its overlay pattern into a 480x270 canvas
	const int overlay_width = width / 2;
	const int overlay_height = height / 2;

	QPainter p(&image);
	p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing, true);
	p.fillRect(0, 0, overlay_width, overlay_height, QColor("#00ff00"));

	// Circle position at t = 0: (240 + sin(0) * 120, 135 + cos(0) * 60) on a 480x270 canvas
	const double cx = overlay_width * 0.5;
	const double cy = overlay_height * 0.5 + overlay_height * (60.0 / 270.0);
	p.setBrush(QColor("#ff5e8a"));
	p.setPen(Qt::NoPen);
	p.drawEllipse(QPointF(cx, cy), overlay_height * (55.0 / 270.0), overlay_height * (55.0 / 270.0));

	QFont font("monospace", 0, QFont::Bold);
	font.setPixelSize(30);
	p.setFont(font);
	p.setPen(QColor("#d4ff4f"));
	p.drawText(150, 40, "OVERLAY");
	p.end();
	return image;
}

/* ------------------------------ scenario: "probe" ------------------------------- */
/* A parity chart rather than a pretty picture: the backdrop sweeps six hues horizontally
 * across the full 0..1 luminance range, the source sweeps the same six hues vertically,
 * and a bottom strip sweeps source alpha from 0 to 1 over black/grey/white. Between them
 * they cover a wide swath of the (Cb, Cs, as) space every blend formula is defined over,
 * so a numeric diff against a front-end render exercises the whole function - not just the
 * few colours a photo happens to contain. */

// grey, red, yellow, green, cyan, blue
const QColor PROBE_HUES[6] = {
	QColor(255, 255, 255), QColor(255, 0, 0), QColor(255, 255, 0),
	QColor(0, 255, 0),     QColor(0, 255, 255), QColor(0, 0, 255)
};
const int PROBE_HUE_COUNT = 6;
const double PROBE_SWEEP_FRACTION = 0.8;  // top 80% is the hue sweep, bottom 20% the alpha strip

// Scale a hue by a 0..1 luminance factor
QColor probe_shade(const QColor& hue, double level) {
	return QColor((int) std::lround(hue.red() * level),
	              (int) std::lround(hue.green() * level),
	              (int) std::lround(hue.blue() * level));
}

QImage make_probe_base(int width, int height) {
	QImage image(width, height, QImage::Format_RGBA8888_Premultiplied);
	const int sweep_height = (int) std::lround(height * PROBE_SWEEP_FRACTION);

	for (int y = 0; y < height; ++y) {
		unsigned char* row = image.scanLine(y);
		for (int x = 0; x < width; ++x) {
			const double u = (width > 1) ? (double) x / (width - 1) : 0.0;
			QColor color;
			if (y < sweep_height) {
				// Six horizontal rows of hue, each ramping 0 -> 1 left to right
				const int band = std::min(PROBE_HUE_COUNT - 1, y * PROBE_HUE_COUNT / sweep_height);
				color = probe_shade(PROBE_HUES[band], u);
			} else {
				// Backdrop for the alpha strip: a plain black -> white ramp
				color = probe_shade(QColor(255, 255, 255), u);
			}
			unsigned char* pixel = row + x * 4;
			pixel[0] = (unsigned char) color.red();
			pixel[1] = (unsigned char) color.green();
			pixel[2] = (unsigned char) color.blue();
			pixel[3] = 255;
		}
	}
	return image;
}

QImage make_probe_overlay(int width, int height) {
	QImage image(width, height, QImage::Format_RGBA8888_Premultiplied);
	const int sweep_height = (int) std::lround(height * PROBE_SWEEP_FRACTION);
	const int strip_height = height - sweep_height;

	for (int y = 0; y < height; ++y) {
		unsigned char* row = image.scanLine(y);
		for (int x = 0; x < width; ++x) {
			const double u = (width > 1) ? (double) x / (width - 1) : 0.0;
			QColor color;
			double alpha = 1.0;

			if (y < sweep_height) {
				// Six vertical columns of hue, each ramping 0 -> 1 top to bottom
				const int band = std::min(PROBE_HUE_COUNT - 1, x * PROBE_HUE_COUNT / width);
				const double v = (sweep_height > 1) ? (double) y / (sweep_height - 1) : 0.0;
				color = probe_shade(PROBE_HUES[band], v);
			} else {
				// Alpha strip: three rows (black, mid grey, white) with alpha ramping 0 -> 1
				const int strip_row = std::min(2, (y - sweep_height) * 3 / std::max(1, strip_height));
				const int level = (strip_row == 0) ? 0 : (strip_row == 1 ? 128 : 255);
				color = QColor(level, level, level);
				alpha = u;
			}

			// libopenshot frames are premultiplied
			const int a8 = (int) std::lround(alpha * 255.0);
			unsigned char* pixel = row + x * 4;
			pixel[0] = (unsigned char) std::lround(color.red() * a8 / 255.0);
			pixel[1] = (unsigned char) std::lround(color.green() * a8 / 255.0);
			pixel[2] = (unsigned char) std::lround(color.blue() * a8 / 255.0);
			pixel[3] = (unsigned char) a8;
		}
	}
	return image;
}

/* ---------------------------------- contact sheet -------------------------------- */

QImage make_contact_sheet(const std::vector<QImage>& composites,
                          const std::vector<std::string>& labels,
                          int columns, int cell_width) {
	if (composites.empty())
		return QImage();

	const double aspect = (double) composites[0].height() / composites[0].width();
	const int cell_height = (int) std::lround(cell_width * aspect);
	const int label_height = 26;
	const int rows = (int) ((composites.size() + columns - 1) / columns);

	QImage sheet(columns * cell_width, rows * (cell_height + label_height),
	             QImage::Format_RGBA8888_Premultiplied);
	sheet.fill(QColor("#0d0c10"));

	QPainter p(&sheet);
	p.setRenderHints(QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing, true);
	QFont font("monospace", 0, QFont::Bold);
	font.setPixelSize(13);
	p.setFont(font);

	for (size_t i = 0; i < composites.size(); ++i) {
		const int col = (int) (i % columns);
		const int row = (int) (i / columns);
		const int x = col * cell_width;
		const int y = row * (cell_height + label_height);

		p.drawImage(QRect(x, y, cell_width, cell_height), composites[i]);
		p.setPen(QColor("#d4ff4f"));
		p.drawText(QRect(x + 8, y + cell_height, cell_width - 16, label_height),
		           Qt::AlignVCenter | Qt::AlignLeft, QString::fromStdString(labels[i]));
	}
	p.end();
	return sheet;
}

/* ------------------------------- comparison page --------------------------------- */

// Read a file back and wrap it as a data: URI, so the comparison page is a single
// self-contained file that works from file:// - an <img> loaded over file:// would taint the
// canvas and block the getImageData() the diff needs.
QString data_uri(const QString& path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return QString();
	const QByteArray encoded = file.readAll().toBase64();
	return "data:image/png;base64," + QString::fromLatin1(encoded);
}

bool write_comparison_html(const QString& out_file,
                           const QString& base_file,
                           const QString& overlay_file,
                           const QString& out_prefix,
                           const std::vector<std::string>& mode_names,
                           const std::string& scenario,
                           int width, int height) {
	const QString base_uri = data_uri(base_file);
	const QString overlay_uri = data_uri(overlay_file);
	if (base_uri.isEmpty() || overlay_uri.isEmpty())
		return false;

	// One entry per mode: the CSS name plus libopenshot's render, inlined
	QString mode_entries;
	for (size_t i = 0; i < mode_names.size(); ++i) {
		char filename[64];
		std::snprintf(filename, sizeof(filename), "%02d-%s.png", (int) i, mode_names[i].c_str());
		const QString uri = data_uri(out_prefix + QString::fromUtf8(filename));
		if (uri.isEmpty())
			return false;
		mode_entries += QString("{name:\"%1\",img:\"%2\"},\n")
		                    .arg(QString::fromStdString(mode_names[i]), uri);
	}

	QFile file(out_file);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
		return false;

	QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
	out.setCodec("UTF-8");
#endif
	out << R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Blend Mode Parity</title>
<style>
  :root {
    --bg:#0d0c10; --panel:#16151c; --panel-2:#1e1d27; --line:#2a2933;
    --ink:#e8e6f0; --dim:#8a879c; --accent:#d4ff4f; --warn:#ff5e8a; --ok:#4fd48a;
  }
  * { box-sizing:border-box; margin:0; padding:0; }
  body {
    background:var(--bg); color:var(--ink); padding:28px;
    font-family:'IBM Plex Mono','JetBrains Mono',ui-monospace,monospace;
  }
  header { margin-bottom:22px; }
  h1 { font-size:22px; letter-spacing:-0.5px; margin-bottom:8px; }
  h1 span { color:var(--accent); }
  header p { color:var(--dim); font-size:12px; line-height:1.7; max-width:76ch; }
  header code { color:var(--accent); background:var(--panel-2); padding:1px 5px; border-radius:3px; }
  .bar {
    display:flex; gap:18px; align-items:center; flex-wrap:wrap;
    background:var(--panel); border:1px solid var(--line); border-radius:10px;
    padding:14px 18px; margin:18px 0;
  }
  .bar label { font-size:11px; color:var(--dim); text-transform:uppercase; letter-spacing:.5px; }
  .bar .val { color:var(--ink); }
  .summary { font-size:12px; }
  .summary b { color:var(--accent); }
  table { width:100%; border-collapse:collapse; }
  th, td { border-bottom:1px solid var(--line); padding:10px 8px; text-align:left; vertical-align:top; }
  th { font-size:10px; text-transform:uppercase; letter-spacing:1.2px; color:var(--accent); font-weight:600; }
  td.mode { font-size:13px; font-weight:700; white-space:nowrap; }
  td.num { font-variant-numeric:tabular-nums; font-size:12px; white-space:nowrap; }
  .pass { color:var(--ok); }
  .fail { color:var(--warn); }
  canvas, img.render { display:block; width:300px; height:auto; border-radius:4px; background:#111; }
  .caption { font-size:10px; color:var(--dim); margin-top:4px; text-transform:uppercase; letter-spacing:.5px; }
  .sources { display:flex; gap:18px; flex-wrap:wrap; margin-bottom:8px; }
  .sources figure img { width:340px; height:auto; border-radius:6px; border:1px solid var(--line);
    background-image:linear-gradient(45deg,#1a1a1a 25%,transparent 25%),linear-gradient(-45deg,#1a1a1a 25%,transparent 25%),linear-gradient(45deg,transparent 75%,#1a1a1a 75%),linear-gradient(-45deg,transparent 75%,#1a1a1a 75%);
    background-size:20px 20px; background-position:0 0,0 10px,10px -10px,-10px 0; background-color:#111; }
  input[type=range] { width:130px; accent-color:var(--accent); }
  details.contract {
    background:var(--panel); border:1px solid var(--line); border-radius:10px;
    padding:14px 18px; margin-top:18px; font-size:12px; max-width:76ch;
  }
  details.contract summary {
    cursor:pointer; color:var(--accent); font-size:11px;
    text-transform:uppercase; letter-spacing:1.2px; font-weight:600;
  }
  details.contract p { color:var(--dim); line-height:1.7; margin-top:12px; }
  details.contract code { color:var(--accent); background:var(--panel-2); padding:1px 5px; border-radius:3px; }
  details.contract pre {
    background:var(--panel-2); border:1px solid var(--line); border-radius:6px;
    padding:12px 14px; margin-top:12px; overflow-x:auto; color:var(--ink); line-height:1.6;
  }
</style>
</head>
<body>
<header>
  <h1>Blend Mode <span>Parity</span></h1>
  <p>
    Every row composites the <b>same two PNGs</b> &mdash; shown below, inlined in this file &mdash; with one
    blend mode. <b>libopenshot</b> is the render from <code>openshot-blend-modes</code>
    (C++/Qt, <code>openshot::Clip::Blend()</code>); <b>canvas 2D</b> is rendered live in your
    browser via <code>globalCompositeOperation</code>, which is the same
    <a style="color:var(--accent)" href="https://www.w3.org/TR/compositing-1/">W3C Compositing and
    Blending Level 1</a> spec PixiJS implements. The overlay is composited 1:1 at (0,0) with no
    scale, rotation or extra opacity, so nothing but the blend math can differ.
  </p>
</header>

<div class="sources" id="sources"></div>

<details class="contract">
  <summary>How a blend mode is set on a libopenshot clip</summary>
  <p>
    The clip JSON takes the same mode names this page uses. <code>blendMode</code> accepts the
    CSS / canvas / Pixi spelling, and the snake_case and camelCase variants of it; an unknown
    name is ignored rather than silently changing the render. <code>source-over</code> is
    accepted as a synonym for <code>normal</code>.
  </p>
<pre>{
  "clips": [
    { "layer": 1, "reader": { "path": "base.png" } },
    { "layer": 2, "reader": { "path": "overlay.png" },
      "blendMode": "color-dodge" }
  ]
}</pre>
  <p>
    In C++: <code>clip.Blend(openshot::BLEND_COLOR_DODGE)</code>. The mode applies where the clip
    is flattened onto the layers beneath it, so it composites against whatever the lower tracks
    have already produced &mdash; exactly like the overlay in this table composites against
    <code>base.png</code>.
  </p>
</details>

<div class="bar">
  <span class="summary" id="summary">measuring&hellip;</span>
  <span style="flex:1"></span>
  <label>diff gain <span class="val" id="gainVal">8x</span></label>
  <input type="range" id="gain" min="1" max="32" step="1" value="8">
</div>

<table>
  <thead><tr>
    <th>mode</th><th>libopenshot</th><th>canvas 2D</th><th>abs diff</th><th>max &Delta;</th><th>mean &Delta;</th><th>px &Delta;&gt;2</th>
  </tr></thead>
  <tbody id="rows"></tbody>
</table>

<script>
const BASE = ")HTML" << base_uri << R"HTML(";
const OVERLAY = ")HTML" << overlay_uri << R"HTML(";
const MODES = [
)HTML" << mode_entries << R"HTML(];
const W = )HTML" << width << R"HTML(, H = )HTML" << height << R"HTML(;
const SCENARIO = ")HTML" << QString::fromStdString(scenario) << R"HTML(";

const load = src => new Promise((res, rej) => {
  const img = new Image();
  img.onload = () => res(img);
  img.onerror = rej;
  img.src = src;
});

function pixels(img) {
  const c = document.createElement('canvas');
  c.width = W; c.height = H;
  const x = c.getContext('2d', { willReadFrequently: true });
  x.drawImage(img, 0, 0);
  return x.getImageData(0, 0, W, H);
}

(async () => {
  const baseImg = await load(BASE);
  const ovImg = await load(OVERLAY);

  document.getElementById('sources').innerHTML = `
    <figure><img src="${BASE}"><div class="caption">base.png &mdash; backdrop, layer 1 (${W}x${H})</div></figure>
    <figure><img src="${OVERLAY}"><div class="caption">overlay.png &mdash; source, layer 2, blended</div></figure>`;

  const rows = document.getElementById('rows');
  const diffs = [];
  let worstMax = 0, worstMode = '';

  for (const entry of MODES) {
    const ourImg = await load(entry.img);

    // --- canvas 2D reference render ---
    const canvas = document.createElement('canvas');
    canvas.width = W; canvas.height = H;
    const ctx = canvas.getContext('2d', { willReadFrequently: true });
    ctx.drawImage(baseImg, 0, 0);
    ctx.globalCompositeOperation = entry.name === 'normal' ? 'source-over' : entry.name;
    ctx.drawImage(ovImg, 0, 0);
    ctx.globalCompositeOperation = 'source-over';
    const ref = ctx.getImageData(0, 0, W, H);

    // --- diff ---
    const ours = pixels(ourImg);
    const diff = ctx.createImageData(W, H);
    let maxd = 0, sum = 0, n = 0, over2 = 0;
    for (let i = 0; i < ours.data.length; i += 4) {
      let worst = 0;
      for (let k = 0; k < 3; k++) {
        const d = Math.abs(ours.data[i+k] - ref.data[i+k]);
        if (d > worst) worst = d;
        sum += d; n++;
        if (d > 2) over2++;
      }
      if (worst > maxd) maxd = worst;
      diff.data[i] = diff.data[i+1] = diff.data[i+2] = worst;   // gain applied at draw time
      diff.data[i+3] = 255;
    }
    diffs.push({ entry, diff, maxd });
    if (maxd > worstMax) { worstMax = maxd; worstMode = entry.name; }

    const diffCanvas = document.createElement('canvas');
    diffCanvas.width = W; diffCanvas.height = H;

    const row = document.createElement('tr');
    const cell = html => { const td = document.createElement('td'); td.innerHTML = html; return td; };
    const mode = cell(entry.name); mode.className = 'mode'; row.appendChild(mode);
    row.appendChild(cell(`<img class="render" src="${entry.img}"><div class="caption">libopenshot</div>`));

    const caption = text => {
      const div = document.createElement('div');
      div.className = 'caption';
      div.textContent = text;
      return div;
    };

    const refTd = document.createElement('td');
    canvas.className = 'render';
    refTd.appendChild(canvas);
    refTd.appendChild(caption('canvas 2D \u2014 this browser'));
    row.appendChild(refTd);

    const diffTd = document.createElement('td');
    diffCanvas.className = 'render';
    diffTd.appendChild(diffCanvas);
    diffTd.appendChild(caption('|libopenshot - canvas 2D|'));
    row.appendChild(diffTd);

    const cls = maxd <= 2 ? 'pass' : 'fail';
    const md = cell(`<span class="${cls}">${maxd}</span>`); md.className = 'num'; row.appendChild(md);
    const mean = cell((sum / n).toFixed(3)); mean.className = 'num'; row.appendChild(mean);
    const pct = cell((100 * over2 / n).toFixed(3) + '%'); pct.className = 'num'; row.appendChild(pct);
    rows.appendChild(row);

    diffs[diffs.length - 1].canvas = diffCanvas;
  }

  // Redraw the diff heatmaps whenever the gain changes
  const gain = document.getElementById('gain');
  const gainVal = document.getElementById('gainVal');
  const paint = () => {
    const g = +gain.value;
    gainVal.textContent = g + 'x';
    for (const d of diffs) {
      const x = d.canvas.getContext('2d');
      const scaled = x.createImageData(W, H);
      for (let i = 0; i < d.diff.data.length; i += 4) {
        const v = Math.min(255, d.diff.data[i] * g);
        scaled.data[i] = v; scaled.data[i+1] = Math.round(v * 0.35); scaled.data[i+2] = Math.round(v * 0.35);
        scaled.data[i+3] = 255;
      }
      x.putImageData(scaled, 0, 0);
    }
  };
  gain.addEventListener('input', paint);
  paint();

  const verdict = worstMax <= 2
    ? `<b class="pass">MATCH</b> &mdash; worst single-channel deviation across all ${MODES.length} modes is <b>${worstMax}/255</b> (8-bit rounding), on "${worstMode}".`
    : `<b class="fail">MISMATCH</b> &mdash; worst deviation is <b>${worstMax}/255</b>, on "${worstMode}".`;
  document.getElementById('summary').innerHTML =
    `scenario <b>${SCENARIO}</b> &middot; ${W}x${H} &middot; ${verdict}`;
})();
</script>
</body>
</html>
)HTML";
	out.flush();
	file.close();
	return true;
}

/* --------------------------------------- CLI ------------------------------------- */

void print_usage() {
	std::cout <<
		"usage: openshot-blend-modes [options]\n"
		"  --out DIR          output directory (default: ./blend-modes)\n"
		"  --scenario NAME    generated sources: 'lab' (overlay-lab.html patterns) or\n"
		"                     'probe' (hue/alpha parity chart). Default: lab\n"
		"  --size WxH         frame size for generated sources (default: 960x540)\n"
		"  --base PATH        use this image as the backdrop layer instead of a scenario\n"
		"  --overlay PATH     use this image as the source layer instead of a scenario\n";
}

}  // anonymous namespace

int main(int argc, char** argv) {
	// Needed for QFont/QPainter text rendering in the generated source patterns
	QGuiApplication app(argc, argv);

	std::string out_dir = "blend-modes";
	std::string scenario = "lab";
	std::string base_path;
	std::string overlay_path;
	int width = 960;
	int height = 540;

	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		const bool has_value = (i + 1 < argc);
		if (arg == "--help" || arg == "-h") {
			print_usage();
			return 0;
		} else if (arg == "--out" && has_value) {
			out_dir = argv[++i];
		} else if (arg == "--scenario" && has_value) {
			scenario = argv[++i];
		} else if (arg == "--base" && has_value) {
			base_path = argv[++i];
		} else if (arg == "--overlay" && has_value) {
			overlay_path = argv[++i];
		} else if (arg == "--size" && has_value) {
			const std::string size = argv[++i];
			const size_t x_pos = size.find('x');
			if (x_pos == std::string::npos) {
				std::cerr << "error: --size expects WxH, got '" << size << "'\n";
				return 1;
			}
			width = std::stoi(size.substr(0, x_pos));
			height = std::stoi(size.substr(x_pos + 1));
		} else {
			std::cerr << "error: unknown argument '" << arg << "'\n";
			print_usage();
			return 1;
		}
	}

	if (scenario != "lab" && scenario != "probe") {
		std::cerr << "error: --scenario must be 'lab' or 'probe'\n";
		return 1;
	}
	if (base_path.empty() != overlay_path.empty()) {
		std::cerr << "error: --base and --overlay must be given together\n";
		return 1;
	}

	if (!QDir().mkpath(QString::fromStdString(out_dir))) {
		std::cerr << "error: could not create output directory '" << out_dir << "'\n";
		return 1;
	}
	const QString out_prefix = QString::fromStdString(out_dir) + "/";

	// ---- source layers ----
	QImage base_image;
	QImage overlay_image;
	if (!base_path.empty()) {
		if (!base_image.load(QString::fromStdString(base_path))) {
			std::cerr << "error: could not load base image '" << base_path << "'\n";
			return 1;
		}
		if (!overlay_image.load(QString::fromStdString(overlay_path))) {
			std::cerr << "error: could not load overlay image '" << overlay_path << "'\n";
			return 1;
		}
		// The frame is sized from the backdrop; the overlay is composited 1:1 on top of it,
		// so pad or crop it to match rather than letting the clip scale (and resample) it.
		width = base_image.width();
		height = base_image.height();
		if (overlay_image.size() != base_image.size()) {
			QImage padded(width, height, QImage::Format_RGBA8888_Premultiplied);
			padded.fill(Qt::transparent);
			QPainter p(&padded);
			p.drawImage(0, 0, overlay_image);
			p.end();
			overlay_image = padded;
			std::cout << "note: overlay was padded/cropped to " << width << "x" << height
			          << " so both layers composite 1:1\n";
		}
		scenario = "custom";
	} else if (scenario == "lab") {
		base_image = make_lab_base(width, height);
		overlay_image = make_lab_overlay(width, height);
	} else {
		base_image = make_probe_base(width, height);
		overlay_image = make_probe_overlay(width, height);
	}

	// Write the sources first: these are the exact pixels libopenshot composites, and the
	// exact pixels the front end should load to reproduce the renders.
	const QString base_file = out_prefix + "base.png";
	const QString overlay_file = out_prefix + "overlay.png";
	base_image.save(base_file, "PNG");
	overlay_image.save(overlay_file, "PNG");
	std::cout << "Sources: " << base_file.toStdString() << ", "
	          << overlay_file.toStdString() << " (" << width << "x" << height << ")\n";

	// ---- render every blend mode through a real timeline ----
	const openshot::Fraction fps(30, 1);
	std::vector<QImage> composites;
	std::vector<std::string> labels;
	std::string manifest_modes;

	int index = 0;
	for (const auto mode : openshot::BlendModeList()) {
		const std::string mode_name = openshot::BlendModeToString(mode);

		// Fresh timeline per mode: two clips, backdrop on layer 1, blended source on layer 2.
		// Both are stretched to the frame, which is a no-op at 1:1 - it just guarantees the
		// two layers stay pixel-aligned regardless of the frame size.
		openshot::Timeline timeline(width, height, fps, 44100, 2, openshot::LAYOUT_STEREO);

		openshot::Clip base_clip(base_file.toStdString());
		base_clip.Layer(1);
		base_clip.Position(0.0);
		base_clip.End(1.0);
		base_clip.scale = openshot::SCALE_STRETCH;
		base_clip.gravity = openshot::GRAVITY_TOP_LEFT;

		openshot::Clip overlay_clip(overlay_file.toStdString());
		overlay_clip.Layer(2);
		overlay_clip.Position(0.0);
		overlay_clip.End(1.0);
		overlay_clip.scale = openshot::SCALE_STRETCH;
		overlay_clip.gravity = openshot::GRAVITY_TOP_LEFT;
		overlay_clip.Blend(mode);

		timeline.AddClip(&base_clip);
		timeline.AddClip(&overlay_clip);
		timeline.Open();

		auto frame = timeline.GetFrame(1);
		QImage composite = frame->GetImage()->copy();

		char filename[64];
		std::snprintf(filename, sizeof(filename), "%02d-%s.png", index, mode_name.c_str());
		const QString out_file = out_prefix + QString::fromUtf8(filename);
		composite.save(out_file, "PNG");

		timeline.Close();

		composites.push_back(composite);
		labels.push_back(mode_name);

		if (!manifest_modes.empty())
			manifest_modes += ",\n";
		manifest_modes += std::string("    { \"index\": ") + std::to_string(index)
		                + ", \"mode\": \"" + mode_name + "\""
		                + ", \"enum\": " + std::to_string((int) mode)
		                + ", \"file\": \"" + filename + "\" }";

		std::cout << "  [" << (index + 1) << "/" << openshot::BLEND_MODE_COUNT << "] "
		          << out_file.toStdString() << "\n";
		++index;
	}

	// ---- contact sheet ----
	const QImage sheet = make_contact_sheet(composites, labels, 4, 480);
	const QString sheet_file = out_prefix + "contact-sheet.png";
	sheet.save(sheet_file, "PNG");
	std::cout << "Contact sheet: " << sheet_file.toStdString() << "\n";

	// ---- manifest, so the front-end comparison page can drive itself ----
	const QString manifest_file = out_prefix + "manifest.json";
	FILE* manifest = std::fopen(manifest_file.toUtf8().constData(), "w");
	if (manifest) {
		std::fprintf(manifest,
			"{\n"
			"  \"generator\": \"openshot-blend-modes\",\n"
			"  \"spec\": \"W3C Compositing and Blending Level 1\",\n"
			"  \"scenario\": \"%s\",\n"
			"  \"width\": %d,\n"
			"  \"height\": %d,\n"
			"  \"base\": \"base.png\",\n"
			"  \"overlay\": \"overlay.png\",\n"
			"  \"geometry\": \"overlay composited 1:1 at (0,0), no scale/rotation, alpha 1.0\",\n"
			"  \"clip_json\": { \"blendMode\": \"multiply\" },\n"
			"  \"modes\": [\n%s\n  ]\n"
			"}\n",
			scenario.c_str(), width, height, manifest_modes.c_str());
		std::fclose(manifest);
		std::cout << "Manifest: " << manifest_file.toStdString() << "\n";
	}

	// ---- self-contained comparison page: libopenshot vs. a live canvas 2D render ----
	const QString comparison_file = out_prefix + "comparison.html";
	if (write_comparison_html(comparison_file, base_file, overlay_file, out_prefix,
	                          labels, scenario, width, height)) {
		std::cout << "Comparison page: " << comparison_file.toStdString()
		          << "  (open in a browser - all images are inlined, no server needed)\n";
	} else {
		std::cerr << "warning: could not write " << comparison_file.toStdString() << "\n";
	}

	return 0;
}
