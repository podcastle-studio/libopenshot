#!/bin/bash
# Regenerates the deterministic test media used by the golden-frame suite.
# Only run this when you intend to re-baseline EVERY golden: a different ffmpeg build
# produces different (but equally valid) encodes, which changes the goldens.
set -euo pipefail
cd "$(dirname "$0")"
X264="-c:v libx264 -preset veryfast -crf 22 -pix_fmt yuv420p -g 30 -movflags +faststart -an"

# main clip: moving test pattern, 30 fps, 6 s, 640x360
ffmpeg -y -hide_banner -v error -f lavfi -i "testsrc2=size=640x360:rate=30" -t 6 $X264 clip_a_640x360_30.mp4
# second clip, different fps/size (frame mapping, speed, scale tests)
ffmpeg -y -hide_banner -v error -f lavfi -i "testsrc=size=854x480:rate=24" -t 6 $X264 clip_b_854x480_24.mp4
# larger source (pre-scale path), 1280x720 25 fps
ffmpeg -y -hide_banner -v error -f lavfi -i "testsrc2=size=1280x720:rate=25" -t 3 $X264 clip_c_1280x720_25.mp4
# green-screen clip: red square + white circle moving over pure green (chroma key)
ffmpeg -y -hide_banner -v error -f lavfi -i "color=c=0x00FF00:size=640x360:rate=30" -f lavfi -i "testsrc2=size=220x220:rate=30" -filter_complex "[0][1]overlay=x='60+t*60':y=70" -t 6 $X264 clip_green_640x360_30.mp4
# luminance matte: soft horizontal wipe moving left->right over 4 s (mask / transitions)
ffmpeg -y -hide_banner -v error -f lavfi -i "nullsrc=size=640x360:rate=30,geq=lum='clip(255*((X/W)-(T/4))*4+128,0,255)':cb=128:cr=128" -t 6 $X264 matte_wipe_640x360_30.mp4
# overlay clip (light-leak stand-in): drifting soft gradients on black (additive blend / displacement)
ffmpeg -y -hide_banner -v error -f lavfi -i "gradients=size=640x360:rate=30:speed=0.05:nb_colors=3:c0=0x000000:c1=0xFFB040:c2=0x000000" -t 6 $X264 overlay_gradients_640x360_30.mp4
# PNG with alpha (image clip, premultiplication check): a transparent border, a 280x160
# blue box at 85% alpha and an opaque 200x80 yellow box inside it. Built with geq rather
# than drawbox because drawbox blends RGB only and never writes the alpha plane, so the
# drawbox version of this file was fully transparent -- every scenario using it rendered
# nothing but its background, which is how the clip shadow went untested until W14.
ffmpeg -y -hide_banner -v error -f lavfi -i "nullsrc=size=320x200,format=rgba,geq=\
r='if(between(X,60,259)*between(Y,60,139),255,if(between(X,20,299)*between(Y,20,179),48,0))':\
g='if(between(X,60,259)*between(Y,60,139),208,if(between(X,20,299)*between(Y,20,179),96,0))':\
b='if(between(X,60,259)*between(Y,60,139),64,if(between(X,20,299)*between(Y,20,179),255,0))':\
a='if(between(X,60,259)*between(Y,60,139),255,if(between(X,20,299)*between(Y,20,179),217,0))'" -frames:v 1 image_alpha_320x200.png
# opaque JPEG
ffmpeg -y -hide_banner -v error -f lavfi -i "rgbtestsrc=size=400x300" -frames:v 1 -q:v 2 image_rgb_400x300.jpg
# static luminance matte image
ffmpeg -y -hide_banner -v error -f lavfi -i "nullsrc=size=640x360,geq=lum='255*(1-hypot(X-320,Y-180)/220)':cb=128:cr=128,format=gray" -frames:v 1 matte_radial_640x360.png
# background image
ffmpeg -y -hide_banner -v error -f lavfi -i "gradients=size=960x540:c0=0x102040:c1=0x40A0C0:nb_colors=2:x0=0:y0=0:x1=960:y1=540" -frames:v 1 background_960x540.png
ls -la *.mp4 *.png *.jpg | awk '{print $5, $9}'
# Hand-authored, not generated: shape_arrow.svg, shape_dashed_line.svg (mimic the service's
# ShapeRenderer output), lut_example.cube (copy of examples/example-lut.cube), fonts/ (Noto Sans, OFL).
