# Experimental pre-raster command capture

This fork adds the private `Reach.CaptureDrawingCommands.v1` renderer IPC.
It is a bounded local evaluator, not a public CEF API or universal browser
remoting implementation. Original fonts/images and native drawing operations
are serialized in native SKP version 110. Command capture never silently
substitutes source pixels. Explicit, separately identified raster capture is
available when the application authorizes and traces a rendering fallback.

The base is CEF `e537569ecda2e3f23ffdac559ecf7f80b8e59467`, Chromium
`9ce0cc67aed57702fcfc011b9e49b0b02d7d5e3a` (156.0.8078.0), and Skia
`a02d4982d7658aa18fbbdc8f136776a76e6d771f`. Apply the fork's registered
patches through the normal CEF patch workflow after dependency synchronization.
Do not synchronize over an edited source checkout.

From the prepared Linux Chromium source root, build using every available CPU:

```sh
export PATH="/path/to/depot_tools:$PATH"
third_party/ninja/ninja -C out/Release_GN_x64 -j"$(nproc)" libcef chrome_sandbox
python3 cef/tools/reach_canvaskit.py
```

The client helper synchronizes only its standalone dependencies at revisions
from the pinned Skia DEPS and activates the pinned Emscripten SDK. For an
already prepared checkout, `--skip-sync` builds without dependency setup.
The helper retains existing Ninja outputs and archives; `--jobs` controls
parallel compilation and defaults to all available CPUs.
Stage both `third_party/skia/out/canvaskit_wasm/canvaskit.js` and
`canvaskit.wasm` together. The npm CanvasKit bundle is not interchangeable:
the matching client exposes `MakePictureWithMedia` and original-image decode
callbacks, full runtime effects, and native-compatible picture serialization.
Never edit a picture's version bytes to bypass compatibility checks.

Capture validates the complete recording before serialization. It bounds
viewport pixels, operation counts, original-image bytes, shader recursion,
filter graphs and picture bytes. Bounded blur, color, shadow, composition and
backdrop filters and nested shader recordings retain native rendering order.
Encoded animated/HDR/multipart images remain original resources; the browser
client owns decoding, animation and sRGB/SDR output, not HDR display headroom.

`Reach.DrawingFrame.v1` accepts an optional sixth integer: 0 collects original
resources for legacy callers, 1 streams originals, and 2 explicitly captures
raster fallback (or requests its frame metadata). Streaming emits
`Reach.DrawingOriginal.v1` events containing request ID, `image:`/`font:` SHA-256
identity, and original binary bytes directly from the Skia serialization callback,
before the final picture reply. There is no fixed resource-count ceiling.
The 4 MiB image, 32 MiB aggregate/font, individual-resource and decoded-pixel
budgets remain. Identity/event overhead also counts toward the native transaction
budget. The renderer does not retain an additional originals map in stream mode.
It never waits for a browser acknowledgement on its synchronous renderer thread.
The browser drains the byte-bounded IPC queue into a bounded asynchronous sink;
transport backpressure can retain at most the transaction budget, not an
unbounded number of resource bytes. Original bytes needed to deserialize a child
SKP are temporarily retained until its parent is composed. Streaming root capture
does not collect originals into its final result. Early resources are cache
candidates only: no partial command picture is presented on capture failure.
The matched CanvasKit reader likewise has no original-typeface count ceiling:
it bounds unique original font bytes and accumulated typeface-reference metadata
to 32 MiB each, retaining the 4 KiB individual descriptor limit. Deploy the rebuilt
matched reader, not an earlier client with a fixed 64-face limit.

Video painting is intercepted only during the owned paint-preview capture.
The registered Blink patch records a native DOM-node marker in place of
source-decoded video pixels; posters keep ordinary original-image behavior.
Native clips, transforms, layers, controls and occlusion remain in paint order.
The picture contains bounded `Reach.MediaState.v1` metadata and
`Reach.Video.v1` markers, not media source URLs or encoded video. The matched
client substitutes a client-decoded image at each video marker. Audio has
state metadata without a video marker. Original media bytes are retrieved
separately by native element ID and SHA-256 source-identity fingerprint.

The application must host-mute command-mode CEF audio, authenticate resource
retrieval, retain source credentials only in the native browser, enforce
resource budgets, handle browser autoplay permission, and clean up playback
on navigation/rejection/shutdown. Source media decoding for timing/events is
not eliminated. Qualified codecs are VP8 WebM and PCM WAV, not every platform's
proprietary codecs.

Only attached, main-document, finite progressive HTTP(S)/data audio/video is
currently supported. Empty media elements need no playback transport; original
poster images retain ordinary image painting, including inside child frames.
Active child-frame media still requires frame-scoped resource routing.

Command mode retains bounded Canvas2D histories without allocating source raster
surfaces. Resets clear history; pixel readback, raw pixel writes, GPU and
OffscreenCanvas contexts reject explicitly. Attached local and isolated remote
frames compose bottom-up using original embedded fonts and encoded images.
Flattened command playback avoids duplicate nested resource tables without
raising the 4 MiB original-resource budget. Document/embedding/viewport checks
reject inconsistent frame trees; this is not an atomic compositor snapshot.
Local documents that have not yet received an embedding token use their existing
document token consistently in metadata, composition and input routing. Remote
frames without a token remain unavailable and are retried within the bounded
capture deadline; no painted child is omitted or given a fabricated identity.
Capture bounds include 16 frames, depth 8, 32 MiB aggregate picture input and
16 Mi pixels across frame viewports. Native focus and hit testing route input
across isolated frames without changing same-origin or site-isolation policy.

Authored HTTP frame scenarios qualify software/WebGL pixels, original resources,
mouse/keyboard input, scrolling, navigation and removal. A separate native trace
qualifies retained Canvas2D and child original resources without matched source
raster/output events. Opaque `data:` parent navigation into an unsandboxed
`srcdoc` child still hits a Chromium origin-equality CHECK in the isolated native
probe; it is unresolved, not worked around by weakening origin enforcement.
MediaSource, MediaStream, DRM, unsupported effects/resources and budget violations
fail the entire command capture.

Explicit raster capture independently records original native paint, composes
child PNGs at the original native markers/clips/transforms, and encodes the whole
viewport as PNG. It does not rasterize a partially serialized command picture,
recreate the browser, enable normal OSR delivery, or change origin/site-isolation
policy. Raster composition permits at most 256 discovered frames, depth 8,
16 Mi aggregate viewport/decoded-child pixels, and the same byte budgets.
Texture-backed/deferred images and unknown custom paint fail explicitly rather
than producing an incomplete screenshot. This path is not a GPU/compositor or
universal live-media capture API. The application must send a separate raster
packet and structured fallback reason; raster success is not command success.
Live/range streaming, detached Audio objects, WebAudio graphs, external
subtitles, GPU/WebGL capture, production transport and full-browser fidelity
are not qualified. Do not remove rejection checks to claim compatibility.
