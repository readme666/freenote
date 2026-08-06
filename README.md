# freenote

Xournal++ with **Google Ink Stroke Modeler** handwriting prediction and
adaptive input stabilization.

This repository is a fork of [Xournal++](https://github.com/xournalpp/xournalpp)
(upstream master `c24f69614`) that adds low-latency pen-input smoothing and a
non-persistent "wet ink" stroke preview. The upstream application is otherwise
unmodified.

## What this adds

| Feature | Description |
|---|---|
| **Stroke prediction with wet-ink preview** | The [Google Ink Stroke Modeler](https://github.com/google/ink-stroke-modeler) (spring/drag model + Kalman predictor) estimates where the pen tip is heading. A translucent preview tail renders the prediction while you draw. Prediction is **transient**: it is never written to the document, so `.xopp` files stay byte-compatible with upstream. |
| **1€ adaptive stabilization** | Replaces the previous averaging stabilizers with a [1 Euro filter](https://dl.acm.org/doi/10.1145/2207676.2208639): strong smoothing for slow/jittery input, minimal latency for fast strokes. Pressure is filtered independently. A direction-invariant 2D variant removes diagonal-stroke lag. |
| **Robust input handling** | Out-of-order timestamps, GDK millisecond wraparound, devices reporting faster than 1 kHz, mid-stroke pauses and zoom changes are all handled as sub-stroke boundaries instead of silently disabling the model. Stylus tip-up detection consults the raw device pressure and ignores touch input. |

The prediction pipeline is wired into `StrokeHandler` and rendered by a
dedicated `PredictiveStrokeToolView` overlay. It is gated to the plain pen tool
(no fill, default drawing mode) so it never interferes with shape tools,
highlighters, or the eraser. The Kalman pass is recomputed at the display
cadence (8 ms) rather than on every input event, keeping the CPU cost bounded
on high-rate styluses.

## Building

Install the regular [Xournal++ build dependencies](https://github.com/xournalpp/xournalpp/blob/master/readme/LinuxBuild.md)
(gtk3, poppler-glib, libxml2, portaudio, libsndfile, lua, libzip, qpdf,
gtksourceview4, and `abseil-cpp` for the modeler), then:

```sh
cmake -S . -B build -G Ninja -DENABLE_INK_STROKE_MODELER=ON
cmake --build build
```

`ENABLE_INK_STROKE_MODELER` defaults to **OFF**. When enabled, the modeler is
taken from `third_party/ink-stroke-modeler` (vendored, pinned) by default; a
system checkout can be used instead with `-DINK_STROKE_MODELER_SOURCE_DIR=/path/to/ink-stroke-modeler`.
Without the option, this builds as vanilla Xournal++ plus the 1€ stabilizer.

To run the unit tests:

```sh
cmake -S . -B build -G Ninja -DENABLE_GTEST=ON -DENABLE_INK_STROKE_MODELER=ON
cmake --build build --target test-units
./build/test/test-units
```

## Usage

1. Start freenote and open **Settings → Input stabilization**.
2. Select **Google Ink** as the averaging method (the default for new
   configurations). Choose the prediction interval (4–20 ms).
3. Draw with the pen tool. The wet-ink tail ahead of the pen tip is the
   prediction preview; only the confirmed stroke is stored.

Known caveats:

- Prediction only applies to the plain **pen** tool.
- The model works in screen-physical centimetres normalized to 72 dpi (the
  same convention as the rest of Xournal++), so its tuning constants scale by
  `display_dpi / 72` on other displays.
- On very fast flicks the stored stroke can end a sample-spacing short of the
  lift point; this deliberately avoids the terminal "hook" caused by drifting
  zero-pressure release coordinates.

## Repository layout

```
src/core/control/tools/InkStrokeModelerAdapter.{h,cpp}   # Google modeler bridge (PIMPL)
src/core/control/tools/OneEuroFilter.{h,cpp}             # 1€ filter (scalar + 2D)
src/core/view/overlays/PredictiveStrokeToolView.{h,cpp}  # wet-ink preview overlay
third_party/ink-stroke-modeler/                          # vendored modeler (Apache-2.0)
third_party/ink-stroke-modeler-disable-unused-gtest.patch  # modeler build guard
```

The vendored modeler is upstream commit `8974a7ce` plus a single downstream
patch (`third_party/ink-stroke-modeler-disable-unused-gtest.patch`) that
stops it from fetching GoogleTest when its own tests are disabled.

## License and source

freenote is a derivative work of
[Xournal++](https://github.com/xournalpp/xournalpp) and is distributed under
the same **GPL-2.0-or-later** license (see `LICENSE`). The copyright and
attribution notices of the original authors are preserved in the source
headers. The document format (`.xopp`/`.xoj`) is byte-compatible with
Xournal++.

The [Google Ink Stroke Modeler](https://github.com/google/ink-stroke-modeler)
is **Apache-2.0** and is vendored at `third_party/ink-stroke-modeler/` with its
license intact (compatible with GPL-2.0).

## Relationship to upstream

Based on upstream Xournal++ master `c24f69614` with the history squashed for a
clean start; see the commits for the changes relative to upstream. To merge
newer upstream changes:

```sh
git remote add upstream https://github.com/xournalpp/xournalpp.git
git fetch upstream
git merge upstream/master
```

## Arch Linux

An Arch package (`freenote`) is maintained separately in the AUR; the PKGBUILD
is not part of this repository.
