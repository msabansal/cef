# CEF Test Applications

This directory contains sample applications and test suites demonstrating the Chromium Embedded Framework (CEF).

## Sample Applications

### [cefsimple](cefsimple/)

Minimal C++ browser application - the recommended starting point for learning CEF.

- **Purpose:** Learning CEF basics
- **Complexity:** ~10 source files
- **Best for:** Understanding CEF architecture and fundamental APIs
- **Tutorial:** [CEF Tutorial](https://chromiumembedded.github.io/cef/tutorial) provides detailed walkthrough

### [cefsimple_capi](cefsimple_capi/)

Pure C implementation of cefsimple using the CEF C API.

- **Purpose:** C API reference implementation
- **Language:** Pure C (C11), no C++
- **Best for:** Embedding CEF in C projects or when C++ is unavailable
- **Documentation:** [Using the CAPI](https://chromiumembedded.github.io/cef/using_the_capi)

### [cefclient](cefclient/)

Comprehensive sample application demonstrating advanced CEF features.

- **Purpose:** Feature demonstration and manual testing
- **Complexity:** ~100+ source files
- **Features:** Full CEF API coverage, built-in test framework, multiple UI modes
- **Best for:** Reference implementation, exploring advanced features

## Test Suite

### [ceftests](ceftests/)

Automated unit test suite for CEF API validation.

- **Purpose:** Automated regression testing
- **Framework:** Google Test (gtest)
- **Coverage:** Comprehensive CEF API testing across all platforms
- **Best for:** CI/CD integration, validating CEF releases

## Reach live WebGL pending delivery

`python tests\run_reach_webgl_pending_test.py --compiler cl` runs the portable
queue regression from a Visual Studio developer shell. On Linux, use
`python3 tests/run_reach_webgl_pending_test.py --compiler c++`. The test extracts
the production header from `reach/webgl_commands.patch`; it does not build CEF.
It covers 150,000 commands with applied-prefix retirement, non-destructive
reads, duplicate/delayed/future/unread/unsent/stale-generation acknowledgements,
original-byte delivery budgeting and the shared 256 MiB pending-memory bound.
Marking a sent prefix is separate from application and does not release memory.
The UTF-16 bound test instantiates both Blink-equivalent 32-bit length/index
signatures and the standard library's native-width string, including escaped
text, surrogate pairs and unpaired surrogates. Add
`--clang-tidy <clang-tidy>` to run Clang's `-Wshorten-64-to-32` diagnostics as
errors before compilation. With the current test, `--baseline dd13d76c5`
reproduces the native Blink-index narrowing failure.
It also slices a 20 MiB command and 12 MiB original into at most 96 KiB
pieces, rejecting byte gaps/stale generations/future cursors and preserving
active read progress across retries of older commands. Neither sent nor applied
may advance until the entire command **and** original are read. To reproduce
the pre-slice failure, pass `--baseline 01f14d554`.

These are queue tests, not browser pixel, source-cookie isolation, renderer IPC
or reload/context-loss qualification. Full CEF builds remain on the canonical
build VM. Pending delivery is not a reconstruction checkpoint.

## Reach canvas-only AVC444 snapshots

The matched reach-cef runtime selects this opt-in drawing mode with
`Runtime::start_commands_with_canvas_video(config)`, after projecting
`REACH_WEBGL_VIDEO=1` and `REACH_WEBGL_COMMAND_REPLAY=0`. Both browser and
children receive `--reach-drawing-commands --reach-webgl-video`. Values other
than `"1"` do not enable video. Ordinary commands and full VIDEO are unchanged;
the video switch excludes the WebGL journal even if its legacy flag is present.

`reach/canvas_video.patch` snapshots the actual logical drawing buffer before
`MarkLayerComposited`, the canvas marker paint, `PostFinalizeFrame`'s early
resource export, and canvas-listener export. Unchanged paints reuse owned
pixels, not a reread of a discarded backbuffer. Default-buffer dirty revisions
precede Blink's ordinary dirty coalescing, including alpha-only changes;
user-framebuffer changes do not dirty that snapshot. Loss, resize, adoption and
removal advance epochs, and document/context destruction releases ownership.
Notifications use the existing coalesced frame-request path independently of
the command journal.

The implementation uses the pinned Chromium public
`GetRGBAUnacceleratedStaticBitmapImage(kBackBuffer)` and
`StaticBitmapImage::CopyImageData` APIs. DrawingBuffer's RGBA snapshot already
has top-left origin; the copy requests RGBA8888, unpremultiplied alpha and sRGB.
Worker OffscreenCanvas, out-of-process WebGL, HDR and F16 fail explicitly.
HTML and Canvas2D still use their command records; no viewport crop or encoded
image substitute is produced.

Every successful video capture has a typed LIST at IPC argument 9, including
an authoritative empty list. Each exact DICTIONARY contains integer
`id,width,height`, string `document,stream,generation,sequence`, and binary
`rgba`. Full replies reserve NULL at 6–8; incremental replies retain refs/chunks
and NULL-or-BINARY scroll at 8. The complete inventory includes hidden canvases;
only painted markers receive the matching `webgl-video` media-state annotation.
The root's native document token binds all records to the capture transaction.

Admission precedes readback: four streams, dimensions 1–3840 by 1–2160,
4,194,304 aggregate even-padded pixels, and at most 16 MiB per raw inventory.
Target-shared admission includes unchanged retained snapshots. During readback,
current ownership plus prior response records and the bitmap/copy/new-vector
scratch remain within 64 MiB. During IPC, the temporary raw response inventory
is released before serialization, leaving room for retained pixels, CEF values,
wire serialization and the receiver copy (four inventories). This bounds owned
pixel representations, not total browser RSS. Codec reservation is separately
bounded by 64 bytes per 16-aligned pixel plus 4 MiB per stream, at most 320 MiB.

```powershell
python tests\run_reach_canvas_video_patch_test.py --fixture <unmodified-chromium>
python tests\run_reach_canvas_video_test.py --compiler cl --fixture <printed-patched-checkout>
```

The first runner independently verifies ten input paths against Chromium
`9ce0cc67aed57702fcfc011b9e49b0b02d7d5e3a`, applies the relevant configured
predecessor hunks, and checks exact new-patch blobs and strict forward, reverse
and reapplication. It creates its own `.artifacts` checkout; the input is never
changed. No source download occurs during a test. The second compiles the
production ownership header, actual typed IPC packing, and actual dirty,
compositor and early-export method bodies with substituted Chromium boundaries.
It also runs the actual mode-selection functions in isolated processes, covering
command defaults, exact `"1"` enablement, conflicting legacy flags and full
VIDEO isolation.
Omit `--fixture` to run ownership and IPC tests alone. `--baseline-hook dirty`,
`composited` or `finalize` substitutes that pre-video method and must fail its
pixel/alpha assertion. These are portable behavioral regressions, **not**
native capture qualification.

Actual CEF/Linux compilation belongs to the canonical Azure build. Activation
still requires moving native WebGL1/2 pixels, alpha/HTML/Canvas2D overlap,
transforms/clips/opacity, trusted input, independent/hidden streams,
loss/restore/resize/navigation isolation and command/video mode switching.
Neither these tests nor separately decoded synthetic metadata prove native
browser capture or qualify the combined deployment.

## Reach encoded ImageBitmap originals

From a Visual Studio developer shell:

```powershell
python tests\run_reach_image_bitmap_test.py --compiler cl --fixture <patched-chromium>
```

The fixture must be the Chromium revision pinned by
`CHROMIUM_BUILD_COMPATIBILITY.txt`, with `reach/webgl_commands.patch` applied.
The runner compiles the actual native ImageBitmap upload method, original
capture methods, append/retirement methods and production ownership/queue
headers. Chromium/Skia boundary objects are substituted; no CEF build or
browser runs. `--baseline-upload` substitutes the fixture index's pre-fix upload
method and must fail the original-byte assertion.
The same harness compiles production byte encoding and slicing: a 9 MiB copied
upload becomes one 12 MiB base64-bearing GL operation, and a 12 MiB original
is delivered only through its referencing command. It verifies pre-copy budget
reservation, source ownership shared with pending references, and retirement.
Direct HTML-image copies also reserve before allocation, transfer the charge
to pending original ownership once, and release it only on applied retirement.
`--baseline-limits 01f14d554` reproduces the old copied-upload rejection.
The base64/JSON, Chromium and Skia boundaries are substituted; these tests
qualify native sizing/ownership/slice logic, not the underlying codec.

The regression covers 128 combinations of creation/application unpack policy
and texture/subimage uploads: original references, temporary creation-policy
commands, exact application-state restoration, source closure before delivery,
sent/applied retirement, current-source reupload, bounded aggregate ownership
and explicit unsupported-source/transform errors. It is not decoder, worker
transfer, cookie-route, native GPU or pixel-equivalence qualification.

Native provenance is attached to a live `StaticBitmapImage` for supported
encoded HTML images and Blob decoding. Exact bytes stay with that current
source and outstanding delivery references, not a global cache or delivered
history; closing/collecting the source and applying its final reference releases
them. Ordinary transfer and exact transfer copies preserve provenance.
Creation-time flip, premultiply and default/none color conversion use ordered
`pixelStorei` overrides around the original reference, then restore application
unpack state without modifying native GL. Cropping/resizing, oriented or
animated sources, HDR HTML images, 3D uploads, transformed copies without proven
provenance and wide-gamut unpack fail explicitly. Original dimensions remain
at most 4096 each.

The shared 256 MiB budget conservatively includes current-source encoded
ownership, construction reservations, pending serialized capacity/metadata and
pending originals. The same ImageBitmap encoded allocation is charged once
when shared with its pending SkData. UTF-8/JSON string expansion, base64 output
and encoded-source copies are preflighted/reserved; allocation capacity and
queue metadata are charged before commitment. Captured value-tree storage and
the JSON destination have separate simultaneous reservations while serializing.
Only after releasing the value tree does the JSON reservation transfer to
pending queue ownership. A 120 MiB raw upload (approximately 160 MiB captured
base64 plus 160 MiB JSON) is rejected before serialization; a 90 MiB raw upload
with approximately 240 MiB simultaneous owned representations is admitted.
The compiled production-method harness checks the actual destination capacity
against concurrent captured ownership. The accounting regression is reproduced
with `--baseline-limits 6aa434f7256314b9f55d4eefa251098ebf406760`.
This bounds owned capture/pending data, not the entire browser heap or all
IPC/Rust read scratch buffers.

The former 1/3/4/8 MiB individual string/span/command/original limits are removed.
Native responses remain at most 8 MiB; oversized individual operations use
96 KiB byte-slice IPCs. The existing Rust `read_webgl_commands` facade preserves
small bounded batches and reassembles an oversized operation without changing
its GL semantics. `MAX_WEBGL_READ_BYTES` is now the complete 256 MiB read budget,
including original admission bytes; caller budgets remain enforced. Runtime
and client complete-batch limits must match; their control-frame bounds do not
change. Dimensions and client GPU/resource validation remain independent.

Client crash/reload is a fresh-target operation: revoke/close the old target
and load a fresh page from zero. GPU-state reconstruction is not required.
Actual fresh-client pixels/input, sibling isolation and source semantics still
require native-build/browser acceptance before activation.

`$discard` is an implicit backbuffer-clear operation, not context retirement.
Native context destruction emits no final batch or change notification;
weak-owner discovery can subsequently omit that stream. Client disposal needs
coordinator-authoritative lifetime/inventory integration, never picture absence.

## Reach native patch application

```powershell
python tests\run_reach_webgl_patch_test.py --fixture <unmodified-chromium>
```

The fixture must contain the unmodified files from Chromium
`156.0.8078.0`, commit `9ce0cc67aed57702fcfc011b9e49b0b02d7d5e3a`.
`reach_webgl_patch_baseline.json` pins SHA-256 identities for 26 upstream files
and requires six Reach-introduced files to be absent, not empty placeholders.
The identities were verified against that immutable Chromium commit; no
network or fixture auto-update runs during testing.

The runner copies verified inputs into its own `.artifacts` checkout, then
executes the actual production reset script and patcher. It preserves the
configured Canvas/Video/WebGL order and verifies omitted preceding patches do
not touch these 32 paths. It checks clean application, creation of all six new
files, exact final WebGL blob identities, strict forward/reverse application,
already-applied detection, and incremental reset/reapplication with unchanged
file timestamps. The input fixture is never changed; no CEF build runs.
Passing `--baseline 4b3275a79064727003c59cc5958894e16d0c8f99` reproduces the
four missing-file failures caused by treating introduced files as modifications
of pre-existing empty files. Cached-index-only checks against such placeholders
do not establish application on the real Chromium baseline.

## Reach animation tick glue

```powershell
python tests\run_reach_webgl_tick_test.py --compiler cl --clang-tidy <clang-tidy>
```

The runner extracts the actual `WebGlRequest` prefix through animation servicing
and both document-identity checks, preserving its public parameters. Substituted
Blink boundaries verify persistent frame/document/page ownership during script
and lifecycle callbacks, identity rejection after either callback replaces the
document (even with the same token), and unchanged stale-wire rejection.
Clang runs with fatal `-Wshadow` diagnostics before compiling/executing the
portable harness. `--baseline 01af7fa1f` reproduces the local-document shadow.
This does not build CEF or qualify actual Blink GC/native browser behavior.

`python tests\run_reach_animation_test.py` also checks notification coalescing
and animation demand independently, including a replacement document sharing
the same frame address. `--baseline 5fd152cc36f06594a50b0c6dc835eafdd49a49cc`
reproduces the missing first picture notification after replacement.

## Reach bounded capture retention

From a Visual Studio developer shell:

```powershell
python tests\run_reach_chunk_cache_test.py
python tests\run_reach_media_journal_test.py
python tests\run_reach_media_journal_patch_test.py --fixture <unmodified-chromium>
```

The first two compile actual cache/journal code with substituted Chromium
boundaries. The cache regression checks byte and entry limits without ever
finishing a capture, then verifies ordinary successful-capture aging. The
journal regression checks renderer-wide admission with protected initialization
segments, repeated type changes, initialization retention, cursor resets, and
large-append compaction. It prints compaction time without a flaky timing gate.
Their `--baseline 5fd152cc36f06594a50b0c6dc835eafdd49a49cc` options reproduce
the old failures; journal `--case global`, `init`, or `compact` selects one case.

The patch runner verifies immutable Chromium inputs against
`reach_media_journal_patch_baseline.json`, applies configured predecessors, and
checks exact final blobs plus strict forward/reverse/reapplication. It never
changes its input fixture or downloads source. These are portable regressions,
not native browser qualification.

## Reach program metadata capture

```powershell
python tests\run_reach_webgl_program_test.py --compiler cl --fixture <unmodified-chromium>
```

The fixture must include `gpu/command_buffer/client/gles2_implementation.cc`
and `gles2_implementation_impl_autogen.h` at pinned Chromium commit
`9ce0cc67aed57702fcfc011b9e49b0b02d7d5e3a`; the runner verifies their hashes.
It compiles the complete production `ReachWebGLJournal::Program` method
from the native patch and the exact upstream output-initialization macro
invoked by `GetProgramiv`. GLES data and value dictionaries are substituted.
The regression checks nontrivial WebGL2 attributes plus uniform blocks,
unchanged WebGL1 metadata, initialized outputs when a lost context leaves
uniform-block destinations untouched, and existing metadata bounds.
`--baseline 999e2c1e2` reproduces the fatal reused-output contract violation.
This is not native GPU, browser-pixel or full CEF qualification.

## Shared Code

### [shared](shared/)

Common utilities and helpers used by cefclient and ceftests.

- Delegate pattern framework for application customization
- Message loop implementations (standard, external pump, multi-threaded)
- Cross-platform resource loading and file utilities
- **Not used by** cefsimple or cefsimple_capi (self-contained examples)

## Learning Path

**New to CEF?** Follow this recommended path:

1. **Start with [cefsimple](cefsimple/)** and the [CEF Tutorial](https://chromiumembedded.github.io/cef/tutorial) to understand CEF architecture
2. **Explore [cefclient](cefclient/)** to see advanced features and integration patterns
3. **Review [ceftests](ceftests/)** to understand API usage and testing patterns
4. **Study [shared](shared/)** code for reusable patterns and best practices

For C API development, substitute [cefsimple_capi](cefsimple_capi/) for cefsimple and consult [Using the CAPI](https://chromiumembedded.github.io/cef/using_the_capi).

## References

- [CEF Project](https://github.com/chromiumembedded/cef) - Main repository
- [CEF Tutorial](https://chromiumembedded.github.io/cef/tutorial) - Detailed architecture walkthrough
- [CEF General Usage](https://chromiumembedded.github.io/cef/general_usage) - Complete API documentation
- [CEF Forum](https://magpcss.org/ceforum/) - Community support and discussions
