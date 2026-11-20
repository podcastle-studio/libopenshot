#!/bin/bash
# Bench-only media (large, generated on demand, git-ignored). Deterministic ffmpeg lavfi sources.
set -euo pipefail
cd "$(dirname "$0")"
X264="-c:v libx264 -preset veryfast -crf 22 -pix_fmt yuv420p -g 30 -movflags +faststart -an"
gen() { [ -f "$1" ] && return 0; echo "generating $1"; shift; ffmpeg -y -hide_banner -v error "$@"; }
gen v1080_a.mp4 -f lavfi -i "testsrc2=size=1920x1080:rate=30" -t 8 $X264 v1080_a.mp4
gen v1080_b.mp4 -f lavfi -i "testsrc=size=1920x1080:rate=30" -t 8 $X264 v1080_b.mp4
gen v1080_c.mp4 -f lavfi -i "gradients=size=1920x1080:rate=30:speed=0.1:nb_colors=4" -t 8 $X264 v1080_c.mp4
gen v1080_green.mp4 -f lavfi -i "color=c=0x00FF00:size=1920x1080:rate=30" -f lavfi -i "testsrc2=size=720x720:rate=30" -filter_complex "[0][1]overlay=x='200+t*120':y=180" -t 8 $X264 v1080_green.mp4
gen v1080_overlay.mp4 -f lavfi -i "gradients=size=1920x1080:rate=30:speed=0.05:nb_colors=3:c0=0x000000:c1=0xFFB040:c2=0x000000" -t 8 $X264 v1080_overlay.mp4
gen v2160_a.mp4 -f lavfi -i "testsrc2=size=3840x2160:rate=30" -t 6 $X264 v2160_a.mp4
gen bg_3840x2160.png -f lavfi -i "gradients=size=3840x2160:c0=0x102040:c1=0x40A0C0:nb_colors=2:x0=0:y0=0:x1=3840:y1=2160" -frames:v 1 bg_3840x2160.png
gen logo_512.png -f lavfi -i "color=c=0x00000000@0.0:size=512x512,format=rgba,drawbox=x=32:y=32:w=448:h=448:color=0xFFFFFF@0.9:t=fill,drawbox=x=96:y=96:w=320:h=320:color=0x3060FF@1.0:t=fill" -frames:v 1 logo_512.png
du -ch *.mp4 *.png | tail -1
