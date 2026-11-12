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

MediaSource journals are replayable from painted local roots, including
out-of-process child frames. `Reach.DrawingMedia.v2` reads are bound to the
exporting frame and its exact current document; journals of other roots or
detached documents remain inaccessible. Media paint markers include the local
root's document identity, and MSE resource hashes also include that identity,
so renderer-local DOM and journal integers cannot collide during composition.
Worker MediaSource, encrypted media and child-frame original URLs remain
unsupported; this does not add a raster or encoded-video fallback.

Reach's drawing/video root-scrollbar patch suppresses only main-frame viewport
scrollbar creation and reserved space. It does not change scrollbar modes:
`LocalFrameView::SetCanHaveScrollbars(false)` also disables native root wheel
and keyboard scrolling. The client retains its existing root-scroll controls;
nested overflow, shadow-DOM and child-frame scrollbars remain native. Do not
combine this with CDP `Emulation.setScrollbarsHidden(true)`, which hides all
scrollbars. The patch is gated by the existing Reach renderer capture switches
and must be rebuilt and qualified with the agent's removal of that CDP override.

Command capture also holds compositor commits. Its native scrollbar gestures
therefore apply through Blink's owning `ScrollableArea::UserScroll`, retaining
native hit testing, capture, axis/granularity, bounds and release-time snapping
instead of queuing an unserviced compositor gesture. Other capture modes retain
Chromium's original path. This patch targets Chromium `156.0.8078.0`.

| File in `patches/reach/` | Reach integration |
| --- | --- |
| `wire_media.patch` | Browser-context original encoded responses, image/font destination and CSP, headers, cookies and cancellation. |
| `root_scrollbars.patch` | Main-frame viewport scrollbar suppression without disabling native root input. |
| `scrollbar_input.patch` | Native scrollbar input for command capture without compositor commits. |
| `device_pixel_content_box.patch` | Keep `ResizeObserver` device-pixel boxes coherent with target-local emulated DPR in both capture modes. Apply the inspector density override before snapping size and paint offset; preserve native/page/CSS zoom, CSS boxes and ordinary non-Reach rendering. |
| `canvas_frames.patch` | Ordinary paint-record/image drawing and bounded local/out-of-process child-frame composition; no Canvas2D retention. |
| `video_commands.patch` | Video paint-order markers without embedding source-decoded frames. |
| `paint_chunks.patch` | Incremental drawing-command paint chunks. |
| `frame_requests.patch` | On-demand command-capture visual update notifications, also waking document-scoped animation demand in Blink glue before any WebGL context exists. |
| `command_presentation.patch` | Successful command-capture presentation/timing completion. |
| `capture_throttling.patch` | Capture-scoped painting of required cross-site frames outside the viewport; background throttling, loading, display locks and view transitions remain intact. |
| `media_journal.patch` | Ordered same-thread MediaSource operations with bounded local and renderer-wide retention. |
| `canvas_video.patch` | Mandatory per-canvas VIDEO for painted Canvas2D/WebGL regions inside command-rendered HTML. Complete-inventory preflight, owned RGBA snapshots and paint markers; native raster providers preserve page pixel/readback/texture semantics. Hidden sources stay native without streams. Sources use the decoded-image ceiling (8192 per edge, 16M pixels); temporary readback stays within 64 MiB. Uniform downsampling fits the aggregate 4M-pixel/16MiB sample and codec budgets without changing native dimensions. Unsupported sources fail explicitly, never reverting to canvas commands or page-raster pixels. |
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

Drawing originals use the integrated `Reach.DrawingOriginal.v2` event. Its
optional source metadata comes from loaded Blink resource buffers at capture
time, matches the original byte hash, and identifies the exporting frame and
document loader. No DevTools polling/body-size discovery index is required.
Fonts with different sanitized/decompressed bytes or unavailable raw buffers
have no publisher source; their captured streams remain content-addressed.
The updated agent rejects old original events explicitly: rebuild and qualify
the matched native artifact before using this adapter, rather than relabeling
an existing binary.

Canvas/WebGL command journaling, replay-original retention, replay texture-limit
negotiation and Canvas2D record retention are removed, not disabled. The former
`webgl_commands` and `canvas_video_all` registrations are gone; the consolidated
`canvas_video` patch applies directly after the remaining configured
predecessors. `--reach-drawing-commands` always selects canvas VIDEO. Legacy
canvas-selection environment variables/switches do not restore other paths.
Full-page VIDEO is unaffected.

Incremental build reset also consumes `retired_canvas_files.json`. It restores
formerly patched upstream files and removes formerly introduced Reach files,
including binding hooks and journal sources. Without this cleanup, simply
removing a registration leaves stale native code in reused Chromium checkouts.
The portable patch regression checks migration from the old stack as well as
clean application and exact reverse/reapplication.

## Retired CEF-root overlays

The old optional cursor, wire-media and video-sink overlays are retired.
Their functionality is already integrated in this fork; Git history preserves
the old deltas. There is no `overlays/` directory and Reach's
`Build-CefRuntime.ps1` no longer accepts `-NativeCursor`, `-WireMedia` or
`-VideoSink`. Build the integrated fork rather than reapplying those deltas.
Reach's integration notes now live in `docs/rbi/native-cursor.md`,
`docs/rbi/wire-media.md` and `docs/rbi/video-sink.md` in the Reach repository.

## Frozen source and matched-client snapshots

The same flat `patches/reach/` folder preserves these source snapshots against
their recorded baselines. They are **not registered** in `patch.cfg` and must not be
applied in addition to the registered patches. There is no separate provenance
source folder.

| File in `patches/reach/` | Generation/baseline | SHA256 |
| --- | --- | --- |
| `cef_command_export.patch` | Complete 33-section native delta from CEF `e537569ecda2e3f23ffdac559ecf7f80b8e59467` through `3ab5aef4270f83afdc4eb8b19fc001e18ccc5d08`; not an extra overlay or a snapshot of later documentation moves. | `123f5678331bd2d3f1ddfdd7ccd007454ee0bf69bfb3140ad246eb3141390d32` |
| `skia_command_export.patch` | Full modified-client delta against Skia `a02d4982d7658aa18fbbdc8f136776a76e6d771f`. | `057a5170b132d7256f1e4463e8224c0adddc1b872d12c17c8d5a1f2d2e9a67a4` |
| `skia_webgpu_graphite.patch` | Client-only Graphite/WebGPU delta layered after `skia_command_export.patch` on that same Skia baseline. | `db8cc2fb1eee0367fc13c1c4882fa5ba5b76497f15d2fbd98cc84491b303ca29` |

The frozen Chromium revision is
`9ce0cc67aed57702fcfc011b9e49b0b02d7d5e3a`; client builds use Emscripten
`4.0.7`. The CEF snapshot reverses exactly against its recorded native commit.
For a new native snapshot, generate the full diff from the recorded CEF
baseline through the intended native commit and check reconstruction/reversal.
For a new client snapshot, generate the full diff from the recorded clean Skia
baseline, keeping the layered Graphite delta separate. Do not regenerate these
frozen files merely to rename them.

The current client snapshot includes an opt-in Ganesh picture-shader submission
threshold, enabled only by CanvasKit's WebGL context factory at 64 MiB of newly
queued tile bytes. Submission waits for the GPU at completed outermost Device
draws; nested draw/shader construction and flush callbacks cannot re-enter it.
Tile dimensions, transforms, sampling, color and exact cache keys are unchanged.
This is not a hard memory cap: one outermost draw can exceed the threshold.
The registered server patches are unchanged. This client-only candidate requires
new CanvasKit builds and actual panorama/pixel/memory qualification before release.

The canvas-video inventory has no independent fixed stream-count cap, including
for small Canvas2D widgets in closed shadow roots. Aggregate limits remain
4,194,304 even-padded pixels, 16 MiB of native RGBA snapshots and 320 MiB of
conservative codec reservations. Preflight derives its maximum inventory length
from the minimum possible codec reservation, including one padded macroblock
and per-stream overhead; actual geometry must still fit the aggregate budgets.
This is independent of the four-frame presentation window. Native capture,
agent admission and client decoding must use matching reservation accounting.
Only viewport-affecting canvas markers receive snapshots and active streams.
Visibility reuses the drawing damage walk's transforms, clips and conservative
layer-effect bounds, including closed shadow roots and nested scrollers.
Off-viewport canvas markers are omitted from full and prefetched chunk output;
visibility changes invalidate their serialized chunk contents. Leaving view
releases native snapshot/admission state and removes the authoritative stream,
retiring agent encoders and client workers. Re-entry receives a fresh identity.

Reach staging accepts the renamed source inputs but deliberately retains the
artifact-internal paths `provenance/skia-command-export.patch` and
`provenance/skia-webgpu-graphite.patch`. Their hashes, manifest keys, matched
CanvasKit bytes and independently qualified native identities stay unchanged.
New native source revisions require their own build/qualification; never relabel
an existing binary as built from a new documentation/provenance commit.

Animation-only state/tick IPC replaces the retired `Reach.WebGl*.v2` interfaces.
`Reach.DrawingAnimationState.v1` and `Reach.DrawingAnimationTick.v1` accept
`[requestId, document]`; state permits an empty document, tick requires its exact
current token. `Reach.DrawingAnimationResult.v1` returns
`[requestId, error, binaryJsonOrNull]`, with only `{document, animationPending}`
on success. No originals, streams, slices or acknowledgements remain.
`Reach.DrawingAnimationChanged.v1` wakes animation delivery independently of
the coalesced `Reach.FrameRequested.v1` picture/canvas notification.

Discovery does not consume demand. Servicing clears demand before script so
nested RAF/CSS scheduling rearms it. Drawing capture services the same demand
without enabling raster or compositor begin frames. Callers must preserve
target-lock exclusion between ticks and captures. Supplement ownership isolates
replacement documents; capture clears the local root's picture flag before
script. No retired frame addresses remain in a global set.

The incremental chunk cache enforces its serialized-byte and entry limits at
insertion, including when a later capture step fails. Successful-capture aging
is not required to keep retained entries within those limits.

Media journals compact eligible old appends in one pass, preserving control
operations and each buffer's latest initialization segment. Repeated type
changes arm one initialization expectation per buffer, not a queue of them.
If protected content alone exceeds a journal or renderer-wide budget, that
journal reports unsupported and releases its retention instead of exceeding
the bound. Native page playback is unchanged.

Canvas mutations and retirement notify through the drawing frame-request path,
not a WebGL journal callback or GC retirement task. Typed canvas snapshot IPC
remains at argument 9, including an authoritative empty inventory. The media
marker kind is `canvas-video`. Compatibility page raster rejects
viewport-affecting canvas markers rather than embedding their pixels; the
ordinary visibility walk avoids blocking unrelated hidden sources. Rebuild
this glue with the matched agent/client; old artifacts do not implement this
contract.
