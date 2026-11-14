# BC7 mode-0 hard-edge optimization — benchmark harness

This directory holds the evaluation harness used to measure the mode-0 (3-subset)
hard-edge encoding improvement in `bc7enc.cpp`.

## Build

```sh
g++ -O3 -fopenmp -fno-strict-aliasing -std=c++17 -I. \
  bench/bench.cpp bc7enc.cpp bc7decomp.cpp lodepng.cpp -lm -o build/bench
g++ -O3 -fopenmp -fno-strict-aliasing -std=c++17 -I. \
  bench/crosscheck.cpp bc7enc.cpp bc7decomp.cpp -lm -o build/crosscheck
```

## What each program does

- `bench.cpp` — builds four synthetic 256x254 textures (`hard_edge`, `gradient`,
  `mixed`, `alpha_edge`), encodes every 4x4 block, decodes with the BC7 decoder and
  reports RGB/RGBA PSNR, max error, selected mode histogram and timing. Any extra
  arguments are treated as PNG paths and measured the same way.
- `crosscheck.cpp` — encodes 20k random multi-region blocks and compares the two
  independent decoders (`bc7decomp` and `bc7decomp_ref`) byte-for-byte. It must
  print `decoder_mismatches=0`.
- `gen_textures.cpp` — writes realistic PNG textures (photo-like gradient, UI/text,
  checkerboard, alpha sprite) into `bench/png/`.

## Reliable timing

Wall-clock runs in `bench.cpp` are noisy under a loaded machine. For speed claims use
the pinned micro-benchmark, which takes the median over many repetitions on one core:

```sh
g++ -O3 -fopenmp -fno-strict-aliasing -std=c++17 -I. \
  /tmp/microbench.cpp bc7enc.cpp bc7decomp.cpp -lm -o build/mb
taskset -c 2 ./build/mb
```

## Measured results (median, linear RGB, uber level 0)

`mixed` (50% hard-edge / 50% gradient blocks), 256x256:

| Metric | Baseline (modes 1/6) | New (modes 0/1/6) |
| --- | --- | --- |
| RGB PSNR | 22.260 dB | 24.459 dB (+2.20 dB) |
| RGBA PSNR | 23.508 dB | 25.707 dB (+2.20 dB) |
| encode time | 3.24 us/block | 3.47 us/block (+7.0%) |

Gradient-only and already-clean textures show identical PSNR and timing because the
hard-edge gate skips mode 0 for them.
