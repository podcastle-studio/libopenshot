## l4 — NVIDIA_L4 — $0.854/h — Vulkan compositor: yes

| case | par | fps (agg) | p95 ms | cores | GPU % | enc % | dec % | VRAM MB | $/video-h |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| smoke | 1 | 149.5 |  | 0.91 |  |  |  | 602.0 | 0.171 |
| gpu_nvenc_1080p_single_video | 1 | 273.6 |  | 0.84 | 10.0 | 16.7 | 5.0 | 602.0 | 0.094 |
| gpu_nvenc_1080p_source_4k | 1 | 144.7 |  | 0.71 | 6.8 | 5.2 | 9.0 | 1109.0 | 0.177 |
| gpu_nvenc_1080p_heavy_effects | 1 | 148.9 |  | 0.9 | 30.8 | 12.0 | 3.8 | 730.0 | 0.172 |
| gpu_nvenc_1080p_chroma_key_green | 1 | 246.0 |  | 0.98 | 4.0 |  | 1.0 | 796.0 | 0.104 |
| gpu_nvenc_1080p_transitions_chain | 1 | 71.6 |  | 0.81 | 10.1 | 7.3 | 9.3 | 979.0 | 0.358 |
| gpu_nvenc_1080p_grid_2x2 | 1 | 219.9 |  | 0.99 | 9.8 | 12.5 | 9.0 | 1105.0 | 0.117 |
| gpu_nvenc_1080p_grid_3x3 | 1 | 132.1 |  | 1.11 | 35.2 | 14.4 | 39.8 | 2159.0 | 0.194 |
| gpu_nvenc_1080p_blend_stack_5 | 1 | 197.5 |  | 1.08 | 19.8 | 14.8 | 16.5 | 1379.0 | 0.13 |
| gpu_nvenc_1080p_podcast_pip | 1 | 164.1 |  | 0.94 | 12.2 | 11.5 | 6.0 | 832.0 | 0.156 |
| gpu_nvenc_1080p_text_static_4 | 1 | 183.8 |  | 0.89 | 9.0 | 16.7 | 5.7 | 699.0 | 0.139 |
| gpu_nvenc_1080p_text_animated_glow_3 | 1 | 97.5 |  | 0.75 | 23.5 | 9.5 |  | 809.0 | 0.263 |
| gpu_nvenc_1080p_subtitles_words | 1 | 286.8 |  | 0.88 | 18.3 | 16.3 | 7.0 | 606.0 | 0.089 |
| gpu_nvenc_1080p_everything | 1 | 113.3 |  | 0.91 | 16.5 | 6.2 | 7.3 | 1456.0 | 0.226 |
| gpu_render_1080p_single_video | 1 | 330.4 | 2.431 | 0.91 | 17.5 |  | 7.5 | 473.0 | 0.078 |
| gpu_render_1080p_source_4k | 1 | 146.8 | 10.655 | 0.68 | 13.4 |  | 10.0 | 980.0 | 0.175 |
| gpu_render_1080p_heavy_effects | 1 | 162.0 | 4.516 | 0.74 | 14.8 |  | 2.0 | 601.0 | 0.158 |
| gpu_render_1080p_chroma_key_green | 1 | 264.6 | 3.234 | 0.87 | 19.3 |  | 14.3 | 648.0 | 0.097 |
| gpu_render_1080p_transitions_chain | 1 | 74.3 | 103.47 | 0.79 | 11.4 |  | 7.9 | 894.0 | 0.345 |
| gpu_render_1080p_grid_2x2 | 1 | 185.2 | 4.619 | 0.95 | 15.5 |  | 11.8 | 1056.0 | 0.138 |
| gpu_render_1080p_grid_3x3 | 1 | 112.4 | 7.079 | 0.97 | 27.6 |  | 31.8 | 2028.0 | 0.228 |
| gpu_render_1080p_blend_stack_5 | 1 | 150.0 | 5.781 | 0.92 | 18.8 |  | 9.6 | 1250.0 | 0.171 |
| gpu_render_1080p_podcast_pip | 1 | 212.1 | 3.894 | 0.98 | 16.3 |  | 9.7 | 703.0 | 0.121 |
| gpu_render_1080p_text_static_4 | 1 | 296.1 | 2.72 | 0.8 | 16.7 |  | 6.7 | 627.0 | 0.087 |
| gpu_render_1080p_text_animated_glow_3 | 1 | 119.8 | 8.203 | 0.75 | 27.2 |  |  | 483.0 | 0.214 |
| gpu_render_1080p_subtitles_words | 1 | 281.0 | 3.363 | 0.82 | 14.7 |  | 7.0 | 478.0 | 0.091 |
| gpu_render_1080p_everything | 1 | 103.1 | 9.456 | 0.89 | 22.0 |  | 9.4 | 1322.0 | 0.248 |
| gpu_nvenc_2160p_single_video | 1 | 95.4 |  | 0.52 | 9.4 | 19.4 | 2.6 | 977.0 | 0.269 |
| gpu_nvenc_2160p_source_4k | 1 | 88.0 |  | 0.59 | 12.0 | 20.0 | 8.0 | 1422.0 | 0.291 |
| gpu_nvenc_2160p_heavy_effects | 1 | 88.3 |  | 0.57 | 11.5 | 25.0 | 1.8 | 1177.0 | 0.29 |
| gpu_nvenc_2160p_transitions_chain | 1 | 94.7 |  | 0.72 | 9.2 | 20.0 | 2.4 | 1395.0 | 0.271 |
| gpu_nvenc_2160p_text_animated_glow_3 | 1 | 53.6 |  | 0.43 | 40.1 | 21.1 |  | 1956.0 | 0.478 |
| gpu_nvenc_2160p_everything | 1 | 81.2 |  | 0.8 | 33.7 | 21.3 | 9.1 | 2145.0 | 0.316 |
| gpu_density_1080p_single_video_x1 | 1 | 294.2 |  | 0.81 | 0.7 |  |  | 602.0 | 0.087 |
| gpu_density_1080p_single_video_x2 | 2 | 488.9 |  | 1.62 | 24.3 | 33.0 | 11.7 | 1201.0 | 0.052 |
| gpu_density_1080p_single_video_x4 | 4 | 604.5 |  | 2.26 | 16.3 | 20.2 | 6.0 | 2399.0 | 0.042 |
| gpu_density_1080p_single_video_x8 | 8 | 632.4 |  | 2.97 | 22.6 | 27.0 | 8.5 | 4795.0 | 0.041 |
| gpu_density_1080p_transitions_chain_x1 | 1 | 78.5 |  | 0.81 | 10.8 | 4.7 | 11.3 | 979.0 | 0.327 |
| gpu_density_1080p_transitions_chain_x2 | 2 | 123.6 |  | 1.65 | 23.1 | 12.0 | 16.9 | 2035.0 | 0.207 |
| gpu_density_1080p_transitions_chain_x4 | 4 | 135.0 |  | 2.57 | 23.5 | 12.7 | 17.9 | 3913.0 | 0.19 |
| gpu_density_1080p_transitions_chain_x8 | 8 | 135.0 |  | 3.15 | 24.8 | 14.9 | 19.9 | 7811.0 | 0.19 |
| gpu_density_1080p_everything_x1 | 1 | 114.0 |  | 0.88 | 18.2 | 5.8 | 7.2 | 1456.0 | 0.225 |
| gpu_density_1080p_everything_x2 | 2 | 128.1 |  | 1.47 | 26.9 | 9.2 | 11.4 | 2909.0 | 0.2 |
| gpu_density_1080p_everything_x4 | 4 | 138.0 |  | 2.06 | 28.1 | 10.4 | 13.9 | 5815.0 | 0.186 |
| gpu_density_1080p_everything_x8 | 8 | 140.2 |  | 2.62 | 31.6 | 11.9 | 15.0 | 11628.0 | 0.183 |
| cpu_x264_1080p_single_video | 1 | 36.1 |  | 4.29 |  |  |  |  | 0.709 |
| cpu_x264_1080p_transitions_chain | 1 | 9.3 |  | 4.68 |  |  |  |  | 2.754 |
| cpu_x264_1080p_podcast_pip | 1 | 7.5 |  | 4.16 |  |  |  |  | 3.414 |
| cpu_x264_1080p_grid_2x2 | 1 | 20.6 |  | 3.01 |  |  |  |  | 1.241 |

## pro6000mig24 — NVIDIA_RTX_PRO_6000_Blackwell_Server_Edition — $0.59/h — Vulkan compositor: NO

| case | par | fps (agg) | p95 ms | cores | GPU % | enc % | dec % | VRAM MB | $/video-h |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| smoke | 1 | 35.8 |  | 1.07 |  |  |  |  | 0.494 |
| gpu_nvenc_1080p_single_video | 1 | 72.9 |  | 1.36 |  |  |  |  | 0.243 |
| gpu_nvenc_1080p_source_4k | 1 | 60.9 |  | 1.37 |  |  |  |  | 0.291 |
| gpu_nvenc_1080p_heavy_effects | 1 | 0.4 |  | 6.76 |  |  |  |  | 48.923 |
| gpu_nvenc_1080p_chroma_key_green | 1 | 2.1 |  | 6.53 |  |  |  |  | 8.551 |
| gpu_nvenc_1080p_transitions_chain | 1 | 2.8 |  | 5.54 |  |  |  |  | 6.276 |
| gpu_nvenc_1080p_grid_2x2 | 1 | 36.8 |  | 1.78 |  |  |  |  | 0.481 |
| gpu_nvenc_1080p_grid_3x3 | 1 | 23.2 |  | 1.83 |  |  |  |  | 0.762 |
| gpu_nvenc_1080p_blend_stack_5 | 1 | 0.6 |  | 6.67 |  |  |  |  | 31.14 |
| gpu_nvenc_1080p_podcast_pip | 1 | 0.8 |  | 6.71 |  |  |  |  | 22.095 |
| gpu_nvenc_1080p_text_static_4 | 1 | 62.6 |  | 1.57 |  |  |  |  | 0.283 |
| gpu_nvenc_1080p_text_animated_glow_3 | 1 | 0.0 |  | 1.0 |  |  |  |  | 435.061 |
| gpu_nvenc_1080p_subtitles_words | 1 | 92.2 |  | 1.54 |  |  |  |  | 0.192 |
| gpu_nvenc_1080p_everything | 1 | 0.1 |  | 1.29 |  |  |  |  | 323.881 |
| gpu_render_1080p_single_video | 1 | 99.9 | 15.062 | 1.24 |  |  |  |  | 0.177 |
| gpu_render_1080p_source_4k | 1 | 75.8 | 28.964 | 1.27 |  |  |  |  | 0.234 |
| gpu_render_1080p_heavy_effects | 1 | 0.4 | 3099.399 | 6.77 |  |  |  |  | 46.158 |
| gpu_render_1080p_chroma_key_green | 1 | 2.2 | 695.78 | 6.58 |  |  |  |  | 8.003 |
| gpu_render_1080p_transitions_chain | 1 | 2.7 | 1593.101 | 5.6 |  |  |  |  | 6.516 |
| gpu_render_1080p_grid_2x2 | 1 | 39.8 | 20.973 | 1.56 |  |  |  |  | 0.445 |
| gpu_render_1080p_grid_3x3 | 1 | 21.3 | 29.761 | 1.65 |  |  |  |  | 0.83 |
| gpu_render_1080p_blend_stack_5 | 1 | 0.6 | 1997.809 | 6.67 |  |  |  |  | 29.262 |
| gpu_render_1080p_podcast_pip | 1 | 0.9 | 1224.056 | 6.71 |  |  |  |  | 20.041 |
| gpu_render_1080p_text_static_4 | 1 | 63.4 | 16.676 | 1.16 |  |  |  |  | 0.279 |
| gpu_render_1080p_text_animated_glow_3 | 1 | 0.0 | 26700.115 | 1.0 |  |  |  |  | 424.162 |
| gpu_render_1080p_subtitles_words | 1 | 109.6 | 9.899 | 1.19 |  |  |  |  | 0.161 |
| gpu_render_1080p_everything | 1 | 0.1 | 20706.901 | 1.3 |  |  |  |  | 325.821 |
| gpu_nvenc_2160p_single_video | 1 | 17.9 |  | 1.5 |  |  |  |  | 0.988 |
| gpu_nvenc_2160p_source_4k | 1 | 28.3 |  | 1.87 |  |  |  |  | 0.625 |
| gpu_nvenc_2160p_heavy_effects | 1 | 0.3 |  | 6.66 |  |  |  |  | 55.335 |
| gpu_nvenc_2160p_transitions_chain | 1 | 3.4 |  | 5.25 |  |  |  |  | 5.207 |
| gpu_nvenc_2160p_text_animated_glow_3 | 1 | 0.0 |  | 1.0 |  |  |  |  | 1246.074 |
| gpu_nvenc_2160p_everything | 1 | 0.0 |  | 1.12 |  |  |  |  | 852.336 |
| gpu_density_1080p_single_video | 1 | 88.2 |  | 1.55 |  |  |  |  | 0.201 |
| gpu_density_1080p_transitions_chain | 1 | 2.5 |  | 5.44 |  |  |  |  | 7.121 |
| gpu_density_1080p_everything | 1 | 0.1 |  | 1.3 |  |  |  |  | 324.023 |

## rtx3090-badhost-hf6he2id2t0sys — NVIDIA_GeForce_RTX_3090 — $None/h — Vulkan compositor: NO

| case | par | fps (agg) | p95 ms | cores | GPU % | enc % | dec % | VRAM MB | $/video-h |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| smoke | 1 | 0 |  | 0 | 1.0 |  |  | 49.0 |  |
| gpu_nvenc_1080p_single_video | 1 | 0 |  | 0 | 0.7 |  |  | 31.0 |  |
| gpu_nvenc_1080p_source_4k | 1 | 0 |  | 0 |  |  |  | 296.0 |  |
| gpu_nvenc_1080p_heavy_effects | 1 | 0 |  | 0 | 7.0 |  |  | 32.0 |  |
| gpu_nvenc_1080p_chroma_key_green | 1 | 0 |  | 0 | 3.0 |  |  | 266.0 |  |
| gpu_nvenc_1080p_transitions_chain | 1 | 0 |  | 0 | 2.5 |  |  | 62.0 |  |
| gpu_nvenc_1080p_grid_2x2 | 1 | 0 |  | 0 | 0.7 |  |  | 296.0 |  |
| gpu_nvenc_1080p_grid_3x3 | 1 | 0 |  | 0 | 4.7 |  |  | 295.0 |  |
| gpu_nvenc_1080p_blend_stack_5 | 1 | 0 |  | 0 | 0.7 |  |  | 376.0 |  |
| gpu_nvenc_1080p_podcast_pip | 1 | 0 |  | 0 |  |  |  | 266.0 |  |
| gpu_nvenc_1080p_text_static_4 | 1 | 0 |  | 0 | 1.0 |  |  | 296.0 |  |
| gpu_nvenc_1080p_text_animated_glow_3 | 1 | 0 |  | 0 | 0.7 |  |  | 281.0 |  |
| gpu_nvenc_1080p_subtitles_words | 1 | 0 |  | 0 |  |  |  | 50.0 |  |
| gpu_nvenc_1080p_everything | 1 | 0 |  | 0 | 1.0 |  |  | 266.0 |  |
| gpu_render_1080p_single_video | 1 | 85.2 | 4.448 | 1.08 | 8.0 |  |  | 336.0 |  |
| gpu_render_1080p_source_4k | 1 | 77.8 | 6.239 | 1.04 | 13.5 |  |  | 358.0 |  |
| gpu_render_1080p_heavy_effects | 1 | 55.4 | 8.927 | 0.93 | 6.8 |  |  | 503.0 |  |
| gpu_render_1080p_chroma_key_green | 1 | 49.5 | 6.728 | 0.98 | 7.7 |  |  | 386.0 |  |
| gpu_render_1080p_transitions_chain | 1 | 30.0 | 17.33 | 1.13 | 8.8 |  |  | 590.0 |  |
| gpu_render_1080p_grid_2x2 | 1 | 23.1 | 16.184 | 0.9 | 16.7 |  |  | 366.0 |  |
| gpu_render_1080p_grid_3x3 | 1 | 11.6 | 17.306 | 0.93 | 7.3 |  |  | 416.0 |  |
| gpu_render_1080p_blend_stack_5 | 1 | 22.3 | 7.136 | 0.97 | 4.2 |  |  | 376.0 |  |
| gpu_render_1080p_podcast_pip | 1 | 43.3 | 5.746 | 1.09 | 3.4 |  |  | 461.0 |  |
| gpu_render_1080p_text_static_4 | 1 | 44.2 | 14.879 | 0.84 | 25.9 |  |  | 432.0 |  |
| gpu_render_1080p_text_animated_glow_3 | 1 | 22.4 | 61.015 | 0.98 | 36.0 |  |  | 390.0 |  |
| gpu_render_1080p_subtitles_words | 1 | 45.7 | 14.887 | 0.81 | 26.9 |  |  | 340.0 |  |
| gpu_render_1080p_everything | 1 | 4.8 | 42.585 | 1.01 | 4.1 |  |  | 573.0 |  |
| gpu_nvenc_2160p_single_video | 1 | 0 |  | 0 | 21.0 |  |  | 32.0 |  |
| gpu_nvenc_2160p_source_4k | 1 | 0 |  | 0 | 0.7 |  |  | 266.0 |  |
| gpu_nvenc_2160p_heavy_effects | 1 | 0 |  | 0 | 1.0 |  |  | 52.0 |  |
| gpu_nvenc_2160p_transitions_chain | 1 | 0 |  | 0 |  |  |  | 279.0 |  |
| gpu_nvenc_2160p_text_animated_glow_3 | 1 | 0 |  | 0 | 0.7 |  |  | 576.0 |  |
| gpu_nvenc_2160p_everything | 1 | 0 |  | 0 |  |  |  | 266.0 |  |
| gpu_density_1080p_single_video | 1 | 0 |  | 0 | 8.0 |  |  | 296.0 |  |
| gpu_density_1080p_transitions_chain | 1 | 0 |  | 0 |  |  |  | 49.0 |  |
| gpu_density_1080p_everything | 1 | 0 |  | 0 | 1.0 |  |  | 296.0 |  |

## rtx3090 — NVIDIA_GeForce_RTX_3090 — $0.5/h — Vulkan compositor: yes

| case | par | fps (agg) | p95 ms | cores | GPU % | enc % | dec % | VRAM MB | $/video-h |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| smoke | 1 | 108.7 |  | 1.06 |  |  |  | 704.0 | 0.138 |
| gpu_nvenc_1080p_single_video | 1 | 235.0 |  | 0.99 | 11.5 | 50.0 | 34.5 | 704.0 | 0.064 |
| gpu_nvenc_1080p_source_4k | 1 | 134.1 |  | 0.97 | 2.4 |  | 21.2 | 1276.0 | 0.112 |
| gpu_nvenc_1080p_heavy_effects | 1 | 171.5 |  | 1.04 | 23.8 | 25.0 | 15.8 | 797.0 | 0.087 |
| gpu_nvenc_1080p_chroma_key_green | 1 | 191.9 |  | 1.35 | 9.5 | 25.5 | 30.5 | 929.0 | 0.078 |
| gpu_nvenc_1080p_transitions_chain | 1 | 34.6 |  | 0.91 | 8.5 | 0.6 | 39.8 | 1267.0 | 0.434 |
| gpu_nvenc_1080p_grid_2x2 | 1 | 104.4 |  | 1.01 | 11.2 | 14.0 | 33.5 | 1380.0 | 0.144 |
| gpu_nvenc_1080p_grid_3x3 | 1 | 51.7 |  | 1.02 | 10.0 | 7.4 | 45.0 | 2512.0 | 0.29 |
| gpu_nvenc_1080p_blend_stack_5 | 1 | 88.1 |  | 1.03 | 11.7 | 10.0 | 36.9 | 1606.0 | 0.17 |
| gpu_nvenc_1080p_podcast_pip | 1 | 164.3 |  | 1.18 |  |  | 8.4 | 967.0 | 0.091 |
| gpu_nvenc_1080p_text_static_4 | 1 | 183.9 |  | 1.18 | 5.8 | 25.0 | 14.8 | 802.0 | 0.082 |
| gpu_nvenc_1080p_text_animated_glow_3 | 1 | 102.4 |  | 1.2 | 22.0 | 50.0 | 14.8 | 862.0 | 0.146 |
| gpu_nvenc_1080p_subtitles_words | 1 | 235.9 |  | 1.21 | 22.2 | 50.0 | 15.0 | 648.0 | 0.064 |
| gpu_nvenc_1080p_everything | 1 | 100.2 |  | 1.21 | 13.7 | 24.6 | 43.1 | 1651.0 | 0.15 |
| gpu_render_1080p_single_video | 1 | 267.4 | 3.439 | 0.92 | 32.5 |  | 50.0 | 575.0 | 0.056 |
| gpu_render_1080p_source_4k | 1 | 126.0 | 11.099 | 0.93 | 7.6 |  | 34.4 | 1147.0 | 0.119 |
| gpu_render_1080p_heavy_effects | 1 | 169.1 | 4.923 | 1.04 | 23.8 |  | 32.2 | 715.0 | 0.089 |
| gpu_render_1080p_chroma_key_green | 1 | 178.9 | 5.964 | 1.02 | 31.4 |  | 45.8 | 801.0 | 0.084 |
| gpu_render_1080p_transitions_chain | 1 | 35.7 | 257.202 | 0.81 | 21.3 |  | 74.3 | 1130.0 | 0.42 |
| gpu_render_1080p_grid_2x2 | 1 | 102.1 | 13.807 | 1.04 | 12.0 |  | 33.3 | 1252.0 | 0.147 |
| gpu_render_1080p_grid_3x3 | 1 | 51.1 | 26.319 | 0.97 | 15.6 |  | 41.5 | 2421.0 | 0.294 |
| gpu_render_1080p_blend_stack_5 | 1 | 83.6 | 14.234 | 1.17 | 14.9 |  | 35.7 | 1477.0 | 0.179 |
| gpu_render_1080p_podcast_pip | 1 | 163.8 | 5.843 | 1.1 | 10.8 |  | 13.4 | 836.0 | 0.092 |
| gpu_render_1080p_text_static_4 | 1 | 254.6 | 2.758 | 1.08 | 11.0 |  | 16.8 | 671.0 | 0.059 |
| gpu_render_1080p_text_animated_glow_3 | 1 | 128.4 | 7.253 | 1.02 | 3.3 |  |  | 475.0 | 0.117 |
| gpu_render_1080p_subtitles_words | 1 | 245.6 | 3.639 | 1.02 | 3.0 |  |  | 601.0 | 0.061 |
| gpu_render_1080p_everything | 1 | 46.6 | 23.769 | 0.84 | 38.0 |  | 17.5 | 1523.0 | 0.322 |
| gpu_nvenc_2160p_single_video | 1 | 88.4 |  | 0.88 | 4.0 | 40.0 | 6.8 | 1081.0 | 0.17 |
| gpu_nvenc_2160p_source_4k | 1 | 79.5 |  | 0.9 | 3.7 | 33.3 | 20.3 | 1590.0 | 0.189 |
| gpu_nvenc_2160p_heavy_effects | 1 | 85.9 |  | 0.87 | 1.0 | 20.0 | 9.2 | 1284.0 | 0.175 |
| gpu_nvenc_2160p_transitions_chain | 1 | 65.1 |  | 1.04 | 7.3 | 66.7 | 18.7 | 1654.0 | 0.23 |
| gpu_nvenc_2160p_text_animated_glow_3 | 1 | 50.5 |  | 0.67 | 15.0 | 42.2 | 3.5 | 2292.0 | 0.297 |
| gpu_nvenc_2160p_everything | 1 | 73.0 |  | 0.94 | 18.0 | 49.5 | 27.6 | 2340.0 | 0.206 |
| gpu_density_1080p_single_video_x1 | 1 | 255.8 |  | 1.15 | 11.2 | 50.0 | 23.0 | 704.0 | 0.059 |
| gpu_density_1080p_single_video_x2 | 2 | 280.0 |  | 1.6 | 5.2 | 29.5 | 11.0 | 1403.0 | 0.054 |
| gpu_density_1080p_single_video_x4 | 4 | 331.9 |  | 2.54 | 12.6 | 53.0 | 33.9 | 2804.0 | 0.045 |
| gpu_density_1080p_single_video_x8 | 8 | 396.8 |  | 3.26 | 15.0 | 40.0 | 30.3 | 5603.0 | 0.038 |
| gpu_density_1080p_transitions_chain_x1 | 1 | 35.2 |  | 0.94 | 9.0 | 0.3 | 45.9 | 1267.0 | 0.426 |
| gpu_density_1080p_transitions_chain_x2 | 2 | 43.9 |  | 1.26 | 15.0 | 13.7 | 65.9 | 2535.0 | 0.342 |
| gpu_density_1080p_transitions_chain_x4 | 4 | 52.1 |  | 1.88 | 14.8 | 8.5 | 75.7 | 5065.0 | 0.288 |
| gpu_density_1080p_transitions_chain_x8 | 8 | 53.1 |  | 2.37 | 19.9 | 12.0 | 83.5 | 10126.0 | 0.282 |
| gpu_density_1080p_everything_x1 | 1 | 105.6 |  | 1.01 | 11.3 | 11.7 | 42.3 | 1651.0 | 0.142 |
| gpu_density_1080p_everything_x2 | 2 | 132.5 |  | 1.8 | 15.6 | 14.0 | 37.8 | 3298.0 | 0.113 |
| gpu_density_1080p_everything_x4 | 4 | 149.1 |  | 2.73 | 25.6 | 20.9 | 54.4 | 6592.0 | 0.101 |
| gpu_density_1080p_everything_x8 | 8 | 145.6 |  | 3.24 | 25.0 | 21.0 | 55.9 | 13179.0 | 0.103 |
| cpu_x264_1080p_single_video | 1 | 61.4 |  | 4.4 | 7.5 |  |  | 1.0 | 0.244 |
| cpu_x264_1080p_transitions_chain | 1 | 10.6 |  | 24.54 |  |  |  | 1.0 | 1.419 |
| cpu_x264_1080p_podcast_pip | 1 | 3.9 |  | 30.08 |  |  |  | 1.0 | 3.806 |
| cpu_x264_1080p_grid_2x2 | 1 | 28.8 |  | 3.39 |  |  |  | 1.0 | 0.521 |

## rtx4000ada-worst — NVIDIA_RTX_4000_Ada_Generation — $0.28/h — Vulkan compositor: NO

| case | par | fps (agg) | p95 ms | cores | GPU % | enc % | dec % | VRAM MB | $/video-h |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| worst_1080p_x1 | 1 | 18.1 |  | 0.97 | 21.4 | 1.4 | 28.1 | 8449.0 | 0.465 |
| worst_1080p_x2 | 2 | 24.1 |  | 1.44 | 33.5 | 1.9 | 51.0 | 16889.0 | 0.348 |
| worst_1080p_x4 | 4 | 0 |  | 0 | 1.1 |  | 0.9 | 20028.0 |  |
| worst_2160p_x1 | 1 | 11.5 |  | 0.93 | 33.3 | 3.1 | 20.4 | 9402.0 | 0.731 |
| worst_2160p_x2 | 2 | 14.6 |  | 1.31 | 40.5 | 4.3 | 27.5 | 19126.0 | 0.574 |
| worst_2160p_x3 | 3 | 0 |  | 0 | 2.4 | 0.1 | 1.0 | 20021.0 |  |
| long_1080p_x1 | 1 | 203.2 |  | 1.0 | 24.3 | 16.9 | 25.8 | 2291.0 | 0.041 |
| long_1080p_x4 | 4 | 388.3 |  | 2.86 | 52.4 | 37.5 | 60.7 | 8808.0 | 0.022 |

## rtx4000ada — NVIDIA_RTX_4000_Ada_Generation — $0.28/h — Vulkan compositor: yes

| case | par | fps (agg) | p95 ms | cores | GPU % | enc % | dec % | VRAM MB | $/video-h |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| smoke | 1 | 110.1 |  | 1.01 |  |  | 2.0 | 508.0 | 0.076 |
| gpu_nvenc_1080p_single_video | 1 | 235.2 |  | 1.0 |  |  | 1.7 | 608.0 | 0.036 |
| gpu_nvenc_1080p_source_4k | 1 | 136.9 |  | 0.94 | 2.4 | 3.0 | 7.1 | 1178.0 | 0.061 |
| gpu_nvenc_1080p_heavy_effects | 1 | 150.0 |  | 1.12 | 3.4 | 4.2 | 10.0 | 735.0 | 0.056 |
| gpu_nvenc_1080p_chroma_key_green | 1 | 190.1 |  | 1.04 | 1.3 |  |  | 833.0 | 0.044 |
| gpu_nvenc_1080p_transitions_chain | 1 | 31.8 |  | 0.93 | 12.4 | 3.7 | 17.7 | 1172.0 | 0.264 |
| gpu_nvenc_1080p_grid_2x2 | 1 | 138.7 |  | 1.11 | 4.3 |  | 2.0 | 1285.0 | 0.061 |
| gpu_nvenc_1080p_grid_3x3 | 1 | 65.4 |  | 1.14 | 2.6 | 1.1 | 9.6 | 2417.0 | 0.128 |
| gpu_nvenc_1080p_blend_stack_5 | 1 | 108.5 |  | 1.06 | 15.3 | 7.3 | 24.2 | 1510.0 | 0.077 |
| gpu_nvenc_1080p_podcast_pip | 1 | 148.8 |  | 1.07 | 1.4 | 2.6 | 0.2 | 838.0 | 0.056 |
| gpu_nvenc_1080p_text_static_4 | 1 | 163.8 |  | 1.04 | 5.3 | 5.9 | 3.9 | 703.0 | 0.051 |
| gpu_nvenc_1080p_text_animated_glow_3 | 1 | 74.1 |  | 0.89 | 15.3 | 4.0 |  | 783.0 | 0.113 |
| gpu_nvenc_1080p_subtitles_words | 1 | 252.9 |  | 1.19 | 12.3 | 16.3 | 13.7 | 584.0 | 0.033 |
| gpu_nvenc_1080p_everything | 1 | 94.4 |  | 1.11 | 17.0 | 12.1 | 21.3 | 1551.0 | 0.089 |
| gpu_render_1080p_single_video | 1 | 168.8 | 5.095 | 0.96 | 0.5 |  | 4.5 | 479.0 | 0.05 |
| gpu_render_1080p_source_4k | 1 | 115.5 | 10.716 | 0.86 | 16.3 |  | 16.7 | 1050.0 | 0.073 |
| gpu_render_1080p_heavy_effects | 1 | 121.2 | 7.82 | 0.87 | 12.8 |  | 1.8 | 606.0 | 0.069 |
| gpu_render_1080p_chroma_key_green | 1 | 144.4 | 6.262 | 0.97 | 16.2 |  | 5.8 | 704.0 | 0.058 |
| gpu_render_1080p_transitions_chain | 1 | 31.3 | 255.452 | 0.85 | 18.7 |  | 22.4 | 1043.0 | 0.268 |
| gpu_render_1080p_grid_2x2 | 1 | 99.4 | 8.2 | 1.04 | 17.5 |  | 17.3 | 1156.0 | 0.084 |
| gpu_render_1080p_grid_3x3 | 1 | 61.1 | 11.434 | 1.0 | 18.1 |  | 27.9 | 2286.0 | 0.137 |
| gpu_render_1080p_blend_stack_5 | 1 | 84.7 | 8.773 | 1.04 | 19.9 |  | 18.6 | 1381.0 | 0.099 |
| gpu_render_1080p_podcast_pip | 1 | 126.7 | 6.316 | 0.93 | 11.6 |  | 8.8 | 740.0 | 0.066 |
| gpu_render_1080p_text_static_4 | 1 | 176.8 | 5.027 | 0.97 | 30.4 |  | 17.6 | 574.0 | 0.048 |
| gpu_render_1080p_text_animated_glow_3 | 1 | 83.2 | 12.608 | 0.79 | 28.4 |  |  | 479.0 | 0.101 |
| gpu_render_1080p_subtitles_words | 1 | 167.2 | 6.346 | 1.0 | 30.4 |  | 8.4 | 483.0 | 0.05 |
| gpu_render_1080p_everything | 1 | 72.1 | 14.699 | 0.97 | 20.9 |  | 14.4 | 1423.0 | 0.116 |
| gpu_nvenc_2160p_single_video | 1 | 88.9 |  | 0.77 | 12.7 | 16.7 | 6.8 | 982.0 | 0.094 |
| gpu_nvenc_2160p_source_4k | 1 | 81.0 |  | 0.8 | 11.5 | 16.7 | 13.3 | 1491.0 | 0.104 |
| gpu_nvenc_2160p_heavy_effects | 1 | 85.3 |  | 0.82 | 23.0 | 20.0 | 3.2 | 1182.0 | 0.099 |
| gpu_nvenc_2160p_transitions_chain | 1 | 72.2 |  | 0.98 | 2.7 | 16.7 | 4.0 | 1556.0 | 0.116 |
| gpu_nvenc_2160p_text_animated_glow_3 | 1 | 50.8 |  | 0.65 | 33.0 | 16.2 |  | 1886.0 | 0.165 |
| gpu_nvenc_2160p_everything | 1 | 69.8 |  | 0.99 | 17.0 | 14.0 | 9.0 | 2237.0 | 0.12 |
| gpu_density_1080p_single_video_x1 | 1 | 258.8 |  | 1.12 | 0.5 |  |  | 508.0 | 0.032 |
| gpu_density_1080p_single_video_x2 | 2 | 436.4 |  | 1.8 | 5.2 |  | 0.2 | 1211.0 | 0.019 |
| gpu_density_1080p_single_video_x4 | 4 | 536.5 |  | 2.75 | 25.2 | 25.0 | 23.6 | 2417.0 | 0.016 |
| gpu_density_1080p_single_video_x8 | 8 | 504.3 |  | 3.42 | 18.5 | 26.1 | 16.3 | 4829.0 | 0.017 |
| gpu_density_1080p_transitions_chain_x1 | 1 | 34.7 |  | 0.9 | 7.0 | 1.9 | 18.7 | 1172.0 | 0.242 |
| gpu_density_1080p_transitions_chain_x2 | 2 | 59.2 |  | 1.67 | 22.5 | 11.7 | 44.3 | 2339.0 | 0.142 |
| gpu_density_1080p_transitions_chain_x4 | 4 | 70.9 |  | 2.55 | 23.0 | 8.1 | 41.5 | 4673.0 | 0.118 |
| gpu_density_1080p_transitions_chain_x8 | 8 | 85.6 |  | 3.61 | 26.9 | 6.0 | 52.2 | 9216.0 | 0.098 |
| gpu_density_1080p_everything_x1 | 1 | 88.0 |  | 1.03 | 12.4 | 4.6 | 10.9 | 1551.0 | 0.095 |
| gpu_density_1080p_everything_x2 | 2 | 115.2 |  | 1.76 | 16.8 | 7.0 | 18.2 | 3102.0 | 0.073 |
| gpu_density_1080p_everything_x4 | 4 | 120.8 |  | 2.42 | 24.4 | 8.1 | 17.3 | 6205.0 | 0.07 |
| gpu_density_1080p_everything_x8 | 8 | 131.2 |  | 2.99 | 21.6 | 7.9 | 19.5 | 12413.0 | 0.064 |
| cpu_x264_1080p_single_video | 1 | 38.1 |  | 4.54 | 1.3 |  |  | 2.0 | 0.22 |
| cpu_x264_1080p_transitions_chain | 1 | 13.9 |  | 10.03 |  |  |  | 2.0 | 0.604 |
| cpu_x264_1080p_podcast_pip | 1 | 3.0 |  | 13.17 |  |  |  | 2.0 | 2.801 |
| cpu_x264_1080p_grid_2x2 | 1 | 22.7 |  | 3.87 |  |  |  | 2.0 | 0.37 |

## rtxpro4000-badhost-idleclocks — NVIDIA_RTX_PRO_4000_Blackwell — $None/h — Vulkan compositor: yes

| case | par | fps (agg) | p95 ms | cores | GPU % | enc % | dec % | VRAM MB | $/video-h |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| smoke | 1 | 75.6 |  | 0.8 | 42.5 | 22.5 | 13.0 | 679.0 |  |
| gpu_nvenc_1080p_single_video | 1 | 132.7 |  | 0.67 | 16.4 | 14.8 | 10.4 | 679.0 |  |
| gpu_nvenc_1080p_source_4k | 1 | 116.3 |  | 0.69 | 34.3 | 33.4 | 14.9 | 1258.0 |  |
| gpu_nvenc_1080p_heavy_effects | 1 | 44.4 |  | 0.45 | 60.2 | 8.4 | 2.4 | 807.0 |  |
| gpu_nvenc_1080p_chroma_key_green | 1 | 118.3 |  | 0.71 | 28.0 | 16.7 | 7.7 | 907.0 |  |
| gpu_nvenc_1080p_transitions_chain | 1 | 34.3 |  | 0.67 | 46.7 | 8.0 | 14.9 | 1249.0 |  |
| gpu_nvenc_1080p_grid_2x2 | 1 | 100.7 |  | 0.81 | 25.4 | 14.3 | 13.1 | 1364.0 |  |
| gpu_nvenc_1080p_grid_3x3 | 1 | 49.8 |  | 0.74 | 42.5 | 8.7 | 22.0 | 2506.0 |  |
| gpu_nvenc_1080p_blend_stack_5 | 1 | 54.3 |  | 0.59 | 56.4 | 13.0 | 15.4 | 1592.0 |  |
| gpu_nvenc_1080p_podcast_pip | 1 | 87.5 |  | 0.7 | 31.4 | 15.7 | 5.7 | 944.0 |  |
| gpu_nvenc_1080p_text_static_4 | 1 | 100.0 |  | 0.7 | 19.7 | 21.4 | 2.9 | 781.0 |  |
| gpu_nvenc_1080p_text_animated_glow_3 | 1 | 24.3 |  | 0.29 | 52.9 | 6.5 |  | 857.0 |  |
| gpu_nvenc_1080p_subtitles_words | 1 | 129.0 |  | 0.72 | 35.8 | 24.8 | 10.4 | 683.0 |  |
| gpu_nvenc_1080p_everything | 1 | 39.0 |  | 0.51 | 49.8 | 12.0 | 8.8 | 1640.0 |  |
| gpu_render_1080p_single_video | 1 | 230.0 | 4.636 | 0.92 | 33.5 |  | 12.0 | 548.0 |  |
| gpu_render_1080p_source_4k | 1 | 131.4 | 11.748 | 0.78 | 13.2 |  | 10.0 | 1127.0 |  |
| gpu_render_1080p_heavy_effects | 1 | 56.5 | 17.796 | 0.43 | 69.6 |  | 8.9 | 686.0 |  |
| gpu_render_1080p_chroma_key_green | 1 | 165.1 | 7.183 | 0.81 | 15.8 |  | 6.2 | 776.0 |  |
| gpu_render_1080p_transitions_chain | 1 | 38.3 | 198.9 | 0.69 | 53.9 |  | 22.7 | 1116.0 |  |
| gpu_render_1080p_grid_2x2 | 1 | 117.8 | 10.818 | 0.85 | 26.7 |  | 16.7 | 1235.0 |  |
| gpu_render_1080p_grid_3x3 | 1 | 62.5 | 20.999 | 0.84 | 36.2 |  | 29.4 | 2376.0 |  |
| gpu_render_1080p_blend_stack_5 | 1 | 63.8 | 18.175 | 0.67 | 48.8 |  | 14.4 | 1463.0 |  |
| gpu_render_1080p_podcast_pip | 1 | 122.0 | 8.971 | 0.78 | 44.8 |  | 10.3 | 813.0 |  |
| gpu_render_1080p_text_static_4 | 1 | 214.4 | 4.726 | 0.83 | 44.0 |  | 12.8 | 670.0 |  |
| gpu_render_1080p_text_animated_glow_3 | 1 | 42.1 | 28.623 | 0.33 | 66.7 |  |  | 491.0 |  |
| gpu_render_1080p_subtitles_words | 1 | 216.6 | 4.769 | 0.85 | 40.2 |  | 5.5 | 572.0 |  |
| gpu_render_1080p_everything | 1 | 50.0 | 22.834 | 0.6 | 55.4 |  | 10.9 | 1506.0 |  |
| gpu_nvenc_2160p_single_video | 1 | 38.9 |  | 0.41 | 11.6 | 27.8 | 1.6 | 1054.0 |  |
| gpu_nvenc_2160p_source_4k | 1 | 37.0 |  | 0.44 | 25.6 | 33.8 | 12.0 | 1571.0 |  |
| gpu_nvenc_2160p_heavy_effects | 1 | 29.6 |  | 0.36 | 60.3 | 25.2 | 1.2 | 1258.0 |  |
| gpu_nvenc_2160p_transitions_chain | 1 | 37.2 |  | 0.61 | 23.3 | 27.8 | 1.3 | 1634.0 |  |
| gpu_nvenc_2160p_text_animated_glow_3 | 1 | 13.2 |  | 0.16 | 69.0 | 16.1 |  | 1978.0 |  |
| gpu_nvenc_2160p_everything | 1 | 22.5 |  | 0.37 | 73.6 | 19.5 | 5.5 | 2335.0 |  |
| gpu_density_1080p_single_video | 1 | 131.9 |  | 0.67 | 31.0 | 20.0 | 5.8 | 679.0 |  |
| gpu_density_1080p_transitions_chain | 1 | 34.8 |  | 0.67 | 42.6 | 5.8 | 15.2 | 1249.0 |  |
| gpu_density_1080p_everything | 1 | 38.9 |  | 0.5 | 59.1 | 11.3 | 11.4 | 1640.0 |  |
| cpu_x264_1080p_single_video | 1 | 64.3 |  | 4.39 |  |  |  | 2.0 |  |
| cpu_x264_1080p_transitions_chain | 1 | 20.4 |  | 7.3 |  |  |  | 2.0 |  |
| cpu_x264_1080p_podcast_pip | 1 | 7.4 |  | 9.74 |  |  |  | 2.0 |  |
| cpu_x264_1080p_grid_2x2 | 1 | 29.2 |  | 2.97 |  |  |  | 2.0 |  |

## rtxpro4000-density — NVIDIA_RTX_PRO_4000_Blackwell — $0.57/h — Vulkan compositor: yes

| case | par | fps (agg) | p95 ms | cores | GPU % | enc % | dec % | VRAM MB | $/video-h |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| smoke | 1 | 128.1 |  | 1.12 | 0.7 |  |  | 377.0 | 0.133 |
| gpu_density_1080p_single_video_x2 | 2 | 553.5 |  | 1.89 | 2.0 |  |  | 1210.0 | 0.031 |
| gpu_density_1080p_single_video_x4 | 4 | 702.3 |  | 2.81 | 13.3 | 19.0 | 9.7 | 2691.0 | 0.024 |
| gpu_density_1080p_single_video_x8 | 8 | 653.9 |  | 3.76 | 22.4 | 28.9 | 7.1 | 5402.0 | 0.026 |
| gpu_density_1080p_transitions_chain_x2 | 2 | 93.8 |  | 2.01 | 10.7 | 2.3 | 12.4 | 2478.0 | 0.182 |
| gpu_density_1080p_transitions_chain_x4 | 4 | 116.7 |  | 3.06 | 13.5 | 5.2 | 12.5 | 4811.0 | 0.147 |
| gpu_density_1080p_transitions_chain_x8 | 8 | 115.5 |  | 3.6 | 16.4 | 2.8 | 17.2 | 9782.0 | 0.148 |
| gpu_density_1080p_everything_x2 | 2 | 112.5 |  | 1.8 | 15.3 | 6.6 | 7.4 | 3276.0 | 0.152 |
| gpu_density_1080p_everything_x4 | 4 | 150.9 |  | 2.6 | 15.9 | 6.5 | 7.7 | 6548.0 | 0.113 |
| gpu_density_1080p_everything_x8 | 8 | 158.5 |  | 3.39 | 20.4 | 8.6 | 8.6 | 13092.0 | 0.108 |

## rtxpro4000 — NVIDIA_RTX_PRO_4000_Blackwell — $0.57/h — Vulkan compositor: yes

| case | par | fps (agg) | p95 ms | cores | GPU % | enc % | dec % | VRAM MB | $/video-h |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| smoke | 1 | 108.5 |  | 0.97 | 0.5 |  |  | 679.0 | 0.158 |
| gpu_nvenc_1080p_single_video | 1 | 315.6 |  | 1.03 |  |  |  | 659.0 | 0.054 |
| gpu_nvenc_1080p_source_4k | 1 | 233.9 |  | 0.98 |  |  | 4.5 | 1058.0 | 0.073 |
| gpu_nvenc_1080p_heavy_effects | 1 | 166.8 |  | 1.04 | 0.6 |  |  | 807.0 | 0.103 |
| gpu_nvenc_1080p_chroma_key_green | 1 | 247.9 |  | 1.04 | 0.5 |  |  | 907.0 | 0.069 |
| gpu_nvenc_1080p_transitions_chain | 1 | 62.9 |  | 1.11 | 4.2 | 0.2 | 4.8 | 1249.0 | 0.272 |
| gpu_nvenc_1080p_grid_2x2 | 1 | 180.4 |  | 1.12 | 25.8 | 20.0 | 21.2 | 1364.0 | 0.095 |
| gpu_nvenc_1080p_grid_3x3 | 1 | 94.9 |  | 1.18 | 2.9 | 1.1 | 3.4 | 2505.0 | 0.18 |
| gpu_nvenc_1080p_blend_stack_5 | 1 | 150.5 |  | 1.12 | 21.2 | 11.2 | 13.2 | 1592.0 | 0.114 |
| gpu_nvenc_1080p_podcast_pip | 1 | 184.2 |  | 1.05 | 0.4 |  | 1.2 | 907.0 | 0.093 |
| gpu_nvenc_1080p_text_static_4 | 1 | 189.7 |  | 1.06 |  |  |  | 781.0 | 0.09 |
| gpu_nvenc_1080p_text_animated_glow_3 | 1 | 107.4 |  | 0.97 | 9.8 | 3.2 |  | 837.0 | 0.159 |
| gpu_nvenc_1080p_subtitles_words | 1 | 288.2 |  | 1.11 | 14.3 | 4.3 |  | 683.0 | 0.059 |
| gpu_nvenc_1080p_everything | 1 | 117.7 |  | 1.11 | 13.3 | 6.3 | 5.7 | 1640.0 | 0.145 |
| gpu_render_1080p_single_video | 1 | 370.5 | 2.048 | 1.08 | 10.3 |  | 5.0 | 628.0 | 0.046 |
| gpu_render_1080p_source_4k | 1 | 217.3 | 6.167 | 1.09 | 17.2 |  | 14.0 | 889.0 | 0.079 |
| gpu_render_1080p_heavy_effects | 1 | 211.3 | 3.833 | 0.91 | 20.5 |  | 11.8 | 686.0 | 0.081 |
| gpu_render_1080p_chroma_key_green | 1 | 249.5 | 2.986 | 1.03 | 21.0 |  | 6.2 | 696.0 | 0.069 |
| gpu_render_1080p_transitions_chain | 1 | 66.1 | 113.209 | 1.07 | 17.2 |  | 9.8 | 1116.0 | 0.259 |
| gpu_render_1080p_grid_2x2 | 1 | 160.9 | 4.455 | 1.05 | 16.4 |  | 10.6 | 1235.0 | 0.106 |
| gpu_render_1080p_grid_3x3 | 1 | 90.7 | 6.854 | 1.14 | 3.4 |  | 2.3 | 2376.0 | 0.189 |
| gpu_render_1080p_blend_stack_5 | 1 | 126.2 | 5.4 | 1.08 | 4.0 |  | 2.3 | 1463.0 | 0.135 |
| gpu_render_1080p_podcast_pip | 1 | 223.0 | 2.861 | 1.12 | 2.2 |  |  | 813.0 | 0.077 |
| gpu_render_1080p_text_static_4 | 1 | 337.5 | 1.966 | 1.11 | 1.7 |  |  | 488.0 | 0.051 |
| gpu_render_1080p_text_animated_glow_3 | 1 | 125.4 | 7.505 | 0.91 | 14.0 |  |  | 471.0 | 0.136 |
| gpu_render_1080p_subtitles_words | 1 | 307.1 | 2.829 | 1.15 | 5.3 |  | 1.7 | 552.0 | 0.056 |
| gpu_render_1080p_everything | 1 | 100.6 | 10.156 | 1.06 | 14.5 |  | 3.8 | 1506.0 | 0.17 |
| gpu_nvenc_2160p_single_video | 1 | 111.6 |  | 0.76 | 2.8 | 12.5 |  | 1054.0 | 0.153 |
| gpu_nvenc_2160p_source_4k | 1 | 96.7 |  | 0.79 | 5.6 | 30.0 | 5.6 | 1571.0 | 0.177 |
| gpu_nvenc_2160p_heavy_effects | 1 | 103.7 |  | 0.85 | 0.8 | 0.6 |  | 1258.0 | 0.165 |
| gpu_nvenc_2160p_transitions_chain | 1 | 89.2 |  | 0.99 | 4.8 | 20.6 | 0.4 | 1504.0 | 0.192 |
| gpu_nvenc_2160p_text_animated_glow_3 | 1 | 74.0 |  | 0.78 | 42.0 | 25.3 |  | 2018.0 | 0.231 |
| gpu_nvenc_2160p_everything | 1 | 88.2 |  | 1.03 | 14.6 | 14.3 | 4.6 | 2333.0 | 0.194 |
| gpu_density_1080p_single_video_x1 | 1 | 326.3 |  | 1.04 | 0.7 |  |  | 659.0 | 0.052 |
| gpu_density_1080p_single_video_x2 | 2 | 518.8 |  | 1.93 | 0.5 |  |  | 1147.0 | 0.033 |
| gpu_density_1080p_single_video_x4 | 4 | 650.1 |  | 2.81 | 10.3 | 12.6 | 5.4 | 2703.0 | 0.026 |
| gpu_density_1080p_single_video_x8 | 8 | 645.3 |  | 3.65 | 16.0 | 18.0 | 4.5 | 5402.0 | 0.026 |
| gpu_density_1080p_transitions_chain_x1 | 1 | 60.3 |  | 1.0 | 9.6 | 7.6 | 7.1 | 1241.0 | 0.284 |
| gpu_density_1080p_transitions_chain_x2 | 2 | 98.1 |  | 2.01 | 10.0 | 5.0 | 8.6 | 2494.0 | 0.174 |
| gpu_density_1080p_transitions_chain_x4 | 4 | 123.4 |  | 3.2 | 14.8 | 2.0 | 15.0 | 4867.0 | 0.139 |
| gpu_density_1080p_transitions_chain_x8 | 8 | 115.8 |  | 3.58 | 22.2 | 8.6 | 17.5 | 9740.0 | 0.148 |
| gpu_density_1080p_everything_x1 | 1 | 119.2 |  | 1.11 | 1.0 |  | 1.6 | 1640.0 | 0.143 |
| gpu_density_1080p_everything_x2 | 2 | 151.1 |  | 1.94 | 11.0 | 4.5 | 5.8 | 3059.0 | 0.113 |
| gpu_density_1080p_everything_x4 | 4 | 165.2 |  | 2.87 | 21.7 | 9.6 | 10.3 | 6548.0 | 0.104 |
| gpu_density_1080p_everything_x8 | 8 | 159.5 |  | 3.34 | 16.6 | 7.1 | 8.1 | 13092.0 | 0.107 |
| cpu_x264_1080p_single_video | 1 | 48.5 |  | 5.35 | 0.4 |  |  | 2.0 | 0.352 |
| cpu_x264_1080p_transitions_chain | 1 | 14.5 |  | 7.92 |  |  |  | 2.0 | 1.176 |
| cpu_x264_1080p_podcast_pip | 1 | 5.9 |  | 9.76 |  |  |  | 2.0 | 2.92 |
| cpu_x264_1080p_grid_2x2 | 1 | 24.5 |  | 4.09 |  |  |  | 2.0 | 0.697 |
