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
| `canvas_frames.patch` | Canvas2D records and bounded local/out-of-process child-frame composition. |
| `video_commands.patch` | Video paint-order markers without embedding source-decoded frames. |
| `paint_chunks.patch` | Incremental drawing-command paint chunks. |
| `frame_requests.patch` | On-demand command-capture visual update notifications, also waking document-scoped animation demand in Blink glue before any WebGL context exists. |
| `command_presentation.patch` | Successful command-capture presentation/timing completion. |
| `capture_throttling.patch` | Capture-scoped painting of required cross-site frames outside the viewport; background throttling, loading, display locks and view transitions remain intact. |
| `media_journal.patch` | Ordered same-thread MediaSource operations with bounded local and renderer-wide retention. |
| `webgl_commands.patch` | Converted WebGL commands, resources, retirement and client composition. |
| `canvas_video.patch` | Explicit opt-in per-canvas owned RGBA snapshots and paint markers. |
| `canvas_video_all.patch` | Apply that transport selection to painted Canvas2D as well as WebGL, restoring native raster providers only for canvas video. Preflight the complete painted inventory before snapshots; hidden source buffers stay native without streams. Uniformly downsample oversized painted inventories within the unchanged transport pixel, snapshot and codec budgets; keep native canvas dimensions and page semantics unchanged. HTML remains command replay; unsupported source dimensions or snapshots fail explicitly. |
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

WebGL whole reads also carry a required `sources` map alongside their command
batch; sliced reads carry it in the first command header and an empty map in
later headers. The same source resolver matches only journal-admitted originals,
including loaded raw fetch buffers when available. The agent verifies the
original bytes before admitting source/document context and publishing the
optional URL to the client. Rebuild this glue and use the matched agent/protocol
client together; old WebGL envelopes are rejected, not assumed source-free.

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
| `skia_command_export.patch` | Previously qualified full modified-client delta against Skia `a02d4982d7658aa18fbbdc8f136776a76e6d771f`. | `31115c7a290fb259bb9927fe54e83b93b4869f664ac4a4d270c987e25d6c0587` |
| `skia_webgpu_graphite.patch` | Client-only Graphite/WebGPU delta layered after `skia_command_export.patch` on that same Skia baseline. | `db8cc2fb1eee0367fc13c1c4882fa5ba5b76497f15d2fbd98cc84491b303ca29` |

The frozen Chromium revision is
`9ce0cc67aed57702fcfc011b9e49b0b02d7d5e3a`; client builds use Emscripten
`4.0.7`. The CEF snapshot reverses exactly against its recorded native commit.
For a new native snapshot, generate the full diff from the recorded CEF
baseline through the intended native commit and check reconstruction/reversal.
For a new client snapshot, generate the full diff from the recorded clean Skia
baseline, keeping the layered Graphite delta separate. Do not regenerate these
frozen files merely to rename them.

This release source pairs all-canvas capture with the previously qualified
CanvasKit snapshot. It does not promote the guarded picture-shader candidate
from `0cba64fc9388248113ff5f56ccb00ae5a82ecf24`; that source remains preserved
on `master`. Registered server-side Skia patches are unchanged from the parent
all-canvas candidate. The new native revision still requires its own matched
build and qualification.

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

Animation delivery now arms a refresh deadline only while discovery reports
`animationPending`. This required native discovery field is not compatible with
older artifacts. The registered `frame_requests.patch` hook also wakes animation
delivery independently of picture-notification coalescing. Discovery does not
consume demand; servicing clears it before script so nested RAF/CSS scheduling
is retained. Drawing capture services the same demand without enabling raster
or compositor begin frames. Supplement ownership isolates replacement documents.
Picture-notification coalescing uses the same document lifetime, independently
of animation demand; capture clears the local root document's picture flag
before servicing script. No retired frame addresses remain in a global set.

The incremental chunk cache enforces its serialized-byte and entry limits at
insertion, including when a later capture step fails. Successful-capture aging
is not required to keep retained entries within those limits.

Media journals compact eligible old appends in one pass, preserving control
operations and each buffer's latest initialization segment. Repeated type
changes arm one initialization expectation per buffer, not a queue of them.
If protected content alone exceeds a journal or renderer-wide budget, that
journal reports unsupported and releases its retention instead of exceeding
the bound. Native page playback is unchanged.

Idle discovery also wakes on WebGL journal collection. While its canvas is live,
each journal obtains a retirement notifier shared by its document. Finalization
touches only non-GC notification state, never the canvas, document, frame, or weak
registry. One deferred renderer task coalesces collected journals per document;
only that task resolves the weak document and checks current attached ownership
before waking authoritative stream discovery. This does not arm animation demand
or polling. Retired/replaced documents cannot notify replacement targets.
Mutation/discovery rebinds an adopted canvas to its current document and wakes the
old owner to remove its stale inventory, without retaining either document.
Live rebinding pins the context, canvas, and document across supplement allocation;
the deferred retirement task keeps only a weak document identity.
The updated `webgl_commands.patch` and Blink glue must be built together; existing
artifact images are not evidence of this retirement behavior.
