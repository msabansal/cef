# Experimental pre-raster command capture

This fork adds the private `Reach.CaptureDrawingCommands.v1` renderer IPC.
It is a bounded local evaluator, not a public CEF API or universal browser
remoting implementation. Original fonts/images and native drawing operations
are serialized in unchanged SKP version 109. Source-page screenshots, video
frames and raster tiles are never fallback resources.

The base is CEF `cf60f42d20a8c68c74bc790cf5ade0de8acbbcd3`, Chromium
`17daf28c361f82916ecd9df505a7a656be1e77e0`, and Skia
`9c7b2dffb2433f5a0cc2b77f06025a09126807ed`. Apply the fork's registered
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
currently supported. MediaSource, MediaStream, DRM, canvas, embedded frames,
unsupported effects/resources and budget violations fail the entire capture.
Live/range streaming, detached Audio objects, WebAudio graphs, external
subtitles, GPU/WebGL capture, production transport and full-browser fidelity
are not qualified. Do not remove rejection checks to claim compatibility.
