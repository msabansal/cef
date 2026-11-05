# Reach CEF, Chromium and Skia patches

This fork owns Reach's native integration and matched-client patch sources.
Reach-maintained patches live directly in `patches/reach/`, without filename
prefixes. Upstream CEF patches keep their upstream names and locations.
Preserve upstream Chromium/Skia C++ style and licenses. Registered patches and
frozen snapshots share this folder: do not apply every `.patch`.

## Registered native patches

`patch.cfg` selects these files from `patches/reach/` in its explicit application
order. Paths inside each patch are relative to Chromium `src`, except
`canvaskit_picture_images.patch`, which targets `src/third_party/skia`.
CEF adapter changes live directly in `include/`, `libcef/` and `libcef_dll/`;
they are not an extra patch to apply to an already integrated fork.

| File in `patches/reach/` | Reach integration |
| --- | --- |
| `wire_media.patch` | Browser-context original encoded responses, headers, cookies and cancellation. |
| `canvas_frames.patch` | Canvas2D records and bounded local/out-of-process child-frame composition. |
| `video_commands.patch` | Video paint-order markers without embedding source-decoded frames. |
| `paint_chunks.patch` | Incremental drawing-command paint chunks. |
| `frame_requests.patch` | On-demand command-capture visual update notifications. |
| `command_presentation.patch` | Successful command-capture presentation/timing completion. |
| `media_journal.patch` | Ordered same-thread MediaSource operations. |
| `webgl_commands.patch` | Converted WebGL commands, resources, retirement and client composition. |
| `canvas_video.patch` | Explicit opt-in per-canvas owned RGBA snapshots and paint markers. |
| `display_lock_unlock.patch` | Reach-maintained backport of upstream `491571b360` (crbug.com/40063752), not original Reach authorship. |
| `simplified_layout_lock.patch` | Preserve descendant state across display-locked simplified layout. |
| `canvaskit_picture_images.patch` | Skia/CanvasKit original-image resources and ordered media substitution. |
| `async_find_display_locks.patch` | Activatable display-lock scope for asynchronous text finding. |
| `osr_popup_context.patch` | Target-owned popup widget/context lifecycle; not a VIDEO capture fallback. |
| `video_sink.patch` | Target-specific direct Viz compositor output and release/backpressure. |

Use CEF's normal `tools/patcher.py` build hook and `tools/patch_updater.py`
maintenance workflow, described in [README.txt](README.txt). Their existing
relative-name handling supports names such as `reach/video_sink`, including
explicit `--patch-file` and updater `--patch` selection; no tool changes are
needed. Generate/review
patches against the pinned Chromium/Skia checkout, not a random upstream HEAD.
Register new Reach patches as `reach/<name>` in `patch.cfg`, preserve ordering
and `path`, and
document purpose and upstream provenance here. Validate exact application and
reverse application against that baseline before native compilation and
sandboxed runtime qualification. A successful patch check is not pixel proof.

The canonical build tooling is this fork's `docker/`; matched CanvasKit tooling
is `tools/reach_canvaskit.py`. Reach invokes it through
`Deployment\build-vm\Invoke-ReleaseBuild.ps1` on its existing Azure build VM,
with automatic deallocation. Moving these files does not rebuild or qualify
any native/client artifact.

## Retired CEF-root overlays

The old optional cursor, wire-media and video-sink overlays are retired.
Their functionality is already integrated in this fork; Git history preserves
the old deltas. There is no `overlays/` directory and Reach's
`Build-CefRuntime.ps1` no longer accepts `-NativeCursor`, `-WireMedia` or
`-VideoSink`. Build the integrated fork rather than reapplying those deltas.
Reach's integration notes now live in `docs/rbi/native-cursor.md`,
`docs/rbi/wire-media.md` and `docs/rbi/video-sink.md` in the Reach repository.

## Frozen source and matched-client snapshots

The same flat `patches/reach/` folder preserves these source snapshots without
changing one byte. They are **not registered** in `patch.cfg` and must not be
applied in addition to the registered patches. There is no separate provenance
source folder.

| File in `patches/reach/` | Generation/baseline | SHA256 |
| --- | --- | --- |
| `cef_command_export.patch` | Complete 33-section native delta from CEF `e537569ecda2e3f23ffdac559ecf7f80b8e59467` through `3ab5aef4270f83afdc4eb8b19fc001e18ccc5d08`; not an extra overlay or a snapshot of later documentation moves. | `123f5678331bd2d3f1ddfdd7ccd007454ee0bf69bfb3140ad246eb3141390d32` |
| `skia_command_export.patch` | Full modified-client delta against Skia `a02d4982d7658aa18fbbdc8f136776a76e6d771f`. | `31115c7a290fb259bb9927fe54e83b93b4869f664ac4a4d270c987e25d6c0587` |
| `skia_webgpu_graphite.patch` | Client-only Graphite/WebGPU delta layered after `skia_command_export.patch` on that same Skia baseline. | `db8cc2fb1eee0367fc13c1c4882fa5ba5b76497f15d2fbd98cc84491b303ca29` |

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
