# faset

Bare-metal, hardware-accelerated native 3D STL viewer (`C++20` / `X11-GLX` / `OpenGL 3.3 Core`), engineered for minimal memory footprint, instant load times, and 0% idle CPU/GPU usage on NVIDIA GT218/GT218M (Mesa Nouveau) and modern Linux desktops.

## Architecture Highlights

- **Zero-copy `mmap` + multi-threaded STL ingestion:** Maps binary and ASCII STLs directly from OS page cache (`MADV_SEQUENTIAL | MADV_WILLNEED`) and `munmap`s immediately after quantization; distinguishes binary files even when headers start with `solid`.
- **8-byte UNORM16 Vertex Quantization:** Compresses 3D coordinates into hardware-aligned `uint16_t[4]` (`8 bytes/vertex`) and discards STL normals completely.
- **0-byte Surface Normals:** Computes razor-sharp flat shading normals per-fragment via `normalize(cross(dFdx(vViewPos), dFdy(vViewPos)))`.
- **Conservative Chunk + Normal-Cone Culling:** Splits geometry into 4,096-triangle chunks with bounding spheres and conservative normal cones; merges adjacent visible chunks into minimal draw spans.
- **Progressive 16× Halton SSAA:** Interactive frames render immediately without jitter; once camera stops, accumulates 16 Halton-jittered sub-pixel samples into an `RGBA16F` FBO and sleeps in `poll()` (`timeout = -1`) at **0.0% CPU / 0.0% GPU**.
- **0.01 ms GPU Depth Picking:** Reads 1×1 pixel from the scene depth buffer for instant double-click orbit pivot and 3D point-to-point distance measurement.
- **Zero Host Dependencies:** Dynamically loads `libGL.so.1` via `dlopen` only when a GL mode runs; CLI `--info` / `--bench` work headlessly.

## Build (Docker Debian Trixie Toolchain)

```bash
./build.sh
```

Produces `/home/kasa/apps/faset/build/faset` and runs `ctest --output-on-failure`.

## CLI Usage

```bash
faset [file.stl]                          # Interactive viewer (or empty drop-zone)
faset --info <file.stl>                   # Print geometry, bbox, volume, load ms & VRAM
faset --bench <file.stl> [--runs 5]       # Benchmark mmap/parse/quantize/cull throughput
faset --self-test-gl <file.stl>           # Live OpenGL 3.3 Core + shader + depth-pick test
faset --screenshot <out.ppm> <file.stl>   # Render 16x SSAA frame to PPM
```

## Controls

- **Orbit / Pan / Zoom:** `Left Drag` / `Shift+Left` or `Right` or `Middle Drag` / `Scroll`
- **Pivot / Fit:** `Double-Click` surface to set orbit pivot; `Double-Click` background or `R`/`F` to reset
- **CAD Views:** `1` Front, `2` Back, `3` Right, `4` Left, `5` Top, `6` Bottom, `0` Iso, `P` Persp/Ortho, `U` Z/Y-Up, `G` Grid
- **Shading & Tools:** `M` MatCap (Clay/Aluminum/Slate/Normals), `W` Wireframe, `S` Slice (`Tab` axis, `I` flip), `D` Measure, `X` X-Ray, `B` Two-Sided
- **Files & UI:** `O` Open dialog, `Drag & Drop` `.stl` (XDND), `[` / `]` Prev/Next STL in folder, automatic `inotify` hot-reload, `T` Dark/Light theme, `H` Hide HUD, `?` / `F1` Help, `Q` Quit
