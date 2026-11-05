# Reach CEF, Chromium and Skia patches

This fork owns Reach's native integration and matched-client patch sources.
Reach-maintained patch filenames begin with `reach_`; upstream CEF patches keep
their upstream names. Preserve upstream Chromium/Skia C++ style and licenses.
The directories below have different purposes: do not apply every `.patch`.

## Registered native patches

`patch.cfg` selects these files from `patches/` in its explicit application
order. Paths inside each patch are relative to Chromium `src`, except
`reach_canvaskit_picture_images.patch`, which targets `src/third_party/skia`.
CEF adapter changes live directly in `include/`, `libcef/` and `libcef_dll/`;
they are not an extra patch to apply to an already integrated fork.

| File in `patches/` | Reach integration |
| --- | --- |
| `reach_wire_media.patch` | Browser-context original encoded responses, headers, cookies and cancellation. |
| `reach_canvas_frames.patch` | Canvas2D records and bounded local/out-of-process child-frame composition. |
| `reach_video_commands.patch` | Video paint-order markers without embedding source-decoded frames. |
| `reach_paint_chunks.patch` | Incremental drawing-command paint chunks. |
| `reach_frame_requests.patch` | On-demand command-capture visual update notifications. |
| `reach_command_presentation.patch` | Successful command-capture presentation/timing completion. |
| `reach_media_journal.patch` | Ordered same-thread MediaSource operations. |
| `reach_webgl_commands.patch` | Converted WebGL commands, resources, retirement and client composition. |
| `reach_canvas_video.patch` | Explicit opt-in per-canvas owned RGBA snapshots and paint markers. |
| `reach_display_lock_unlock.patch` | Reach-maintained backport of upstream `491571b360` (crbug.com/40063752), not original Reach authorship. |
| `reach_simplified_layout_lock.patch` | Preserve descendant state across display-locked simplified layout. |
| `reach_canvaskit_picture_images.patch` | Skia/CanvasKit original-image resources and ordered media substitution. |
| `reach_async_find_display_locks.patch` | Activatable display-lock scope for asynchronous text finding. |
| `reach_osr_popup_context.patch` | Target-owned popup widget/context lifecycle; not a VIDEO capture fallback. |
| `reach_video_sink.patch` | Target-specific direct Viz compositor output and release/backpressure. |

Use CEF's normal `tools/patcher.py` build hook and `tools/patch_updater.py`
maintenance workflow, described in [README.txt](README.txt). Generate/review
patches against the pinned Chromium/Skia checkout, not a random upstream HEAD.
Register new Reach patches in `patch.cfg`, preserve ordering and `path`, and
document purpose and upstream provenance here. Validate exact application and
reverse application against that baseline before native compilation and
sandboxed runtime qualification. A successful patch check is not pixel proof.

The canonical build tooling is this fork's `docker/`; matched CanvasKit tooling
is `tools/reach_canvaskit.py`. Reach invokes it through
`Deployment\build-vm\Invoke-ReleaseBuild.ps1` on its existing Azure build VM,
with automatic deallocation. Moving these files does not rebuild or qualify
any native/client artifact.

## Historical CEF-root overlays

`overlays/` contains byte-exact snapshots moved from Reach's `external/`.
They are optional inputs to Reach's `Build-CefRuntime.ps1`, not entries in
`patch.cfg`. They describe the older
`e829384deed07d05af5cc3923fb292ea644266f4` integration era and do **not** pass
exact reverse application against the integrated native source
`3ab5aef4270f83afdc4eb8b19fc001e18ccc5d08`. Do not reapply them to this fork
or claim current-baseline applicability without checking the selected checkout.

| File in `overlays/` | Purpose | SHA256 |
| --- | --- | --- |
| `reach_cef_native_cursor.patch` | CEF effective native cursor ownership. | `33863d2479974678edad3226bc3e8056958b8633003cdc90d38eb7927de97d67` |
| `reach_cef_video_sink.patch` | Historical CEF adapter and registered Chromium VIDEO sink. | `d7f95460a94f49cd95f66956cc35e8574a9dfd859159525f89a38e990d9394e5` |
| `reach_cef_wire_media.patch` | Historical embedded Chromium wire-media patch file; the snapshot itself does not add a `patch.cfg` entry. | `fdb34a369d40a0081c624fcde1ff95c671a9767581cdaed3a2329dce3032932b` |

These are preserved historical generated deltas, not the current editable
implementation. Their original baselines must be checked before generation or
use; integration-era attribution alone is not an exact application guarantee.
Reach's integration notes now live in `docs/rbi/native-cursor.md`,
`docs/rbi/wire-media.md` and `docs/rbi/video-sink.md` in the Reach repository.

## Frozen source and matched-client provenance

`provenance/` preserves the qualified source snapshots below without changing
one byte. They are **not** automatically registered native patches.

| File in `provenance/` | Generation/baseline | SHA256 |
| --- | --- | --- |
| `reach_cef_command_export.patch` | Complete 33-section native delta from CEF `e537569ecda2e3f23ffdac559ecf7f80b8e59467` through `3ab5aef4270f83afdc4eb8b19fc001e18ccc5d08`; not an extra overlay or a snapshot of later documentation moves. | `123f5678331bd2d3f1ddfdd7ccd007454ee0bf69bfb3140ad246eb3141390d32` |
| `reach_skia_command_export.patch` | Full modified-client delta against Skia `a02d4982d7658aa18fbbdc8f136776a76e6d771f`. | `31115c7a290fb259bb9927fe54e83b93b4869f664ac4a4d270c987e25d6c0587` |
| `reach_skia_webgpu_graphite.patch` | Client-only Graphite/WebGPU delta layered after `reach_skia_command_export.patch` on that same Skia baseline. | `db8cc2fb1eee0367fc13c1c4882fa5ba5b76497f15d2fbd98cc84491b303ca29` |

The frozen Chromium revision is
`9ce0cc67aed57702fcfc011b9e49b0b02d7d5e3a`; client builds use Emscripten
`4.0.7`. The CEF snapshot reverses exactly against its recorded native commit.
For a new native snapshot, generate the full diff from the recorded CEF
baseline through the intended native commit and check reconstruction/reversal.
For a new client snapshot, generate the full diff from the recorded clean Skia
baseline, keeping the layered Graphite delta separate. Do not regenerate these
frozen files merely to rename them.

Reach staging accepts the renamed source inputs but deliberately retains the
artifact-internal paths `provenance/skia-command-export.patch` and
`provenance/skia-webgpu-graphite.patch`. Their hashes, manifest keys, matched
CanvasKit bytes and independently qualified native identities stay unchanged.
New native source revisions require their own build/qualification; never relabel
an existing binary as built from a new documentation/provenance commit.
