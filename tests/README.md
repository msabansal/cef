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

## Reach canvas VIDEO inside command-rendered pages

`--reach-drawing-commands` always selects bounded per-canvas VIDEO for painted
Canvas2D, WebGL1 and WebGL2 regions. Ordinary HTML/Skia commands, ordinary media
and full-page VIDEO retain their existing paths. There is no canvas command
journal, client WebGL replay, texture-limit negotiation or Canvas2D record
retention. Legacy canvas-selection environment variables/switches have no
effect. Native page APIs still use Chromium's raster providers, pixel writes,
readbacks, textures and ImageBitmap behavior.

The registered `reach/canvas_video.patch` contains the complete native producer,
not an overlay on the removed WebGL or all-canvas patches. Preflight selects
viewport-affecting painted markers using the ordinary drawing visibility walk,
including transforms, clips, layer effects and closed shadow roots. Hidden
source canvases remain native without an active stream.

Source dimensions are bounded to 8192 per edge and 16M pixels. Uniform
resampling preserves native canvas dimensions and page semantics while fitting
4,194,304 aggregate even-padded output pixels, 16 MiB owned RGBA snapshots and
320 MiB conservative codec reservations. Inventory length derives from the
minimum possible reservation; there is no separate fixed stream-count cap.
Temporary source readback is bounded to 64 MiB. Retained ownership and IPC-copy
accounting remain independently bounded; this is not a bound on browser RSS.

Dirty revisions precede Blink's ordinary dirty coalescing, including alpha-only
changes. User-framebuffer changes do not dirty the default-buffer snapshot.
Unchanged captures reuse owned pixels. Pre-discard and early-export hooks never
admit/read new pixels without complete-inventory preflight. Loss, resize,
adoption, removal and document/context destruction retire ownership; a restored
producer has a fresh identity. Off-viewport streams release admission and
re-enter with a new identity. Worker OffscreenCanvas, out-of-process canvas,
HDR/F16 and unsupported snapshots fail explicitly.

Every successful drawing capture includes a typed LIST at IPC argument 9,
including an authoritative empty list. Each DICTIONARY contains integer
`id,width,height`, string `document,stream,generation,sequence`, and binary
`rgba`. Full replies reserve NULL at 6–8; incremental replies retain refs/chunks
and NULL-or-BINARY scroll at 8. Only painted markers have the `canvas-video`
media-state annotation. The root document token binds the inventory to the
capture transaction.

Compatibility server raster fails closed when canvas markers affect the
viewport; it cannot embed canvas pixels in a page PNG. The same conservative
visibility proof permits unrelated hidden/off-viewport canvases. Ordinary
HTML/media fallback and full-page VIDEO remain separate supported paths.

```powershell
python tests\run_reach_canvas_video_patch_test.py --fixture <unmodified-chromium>
python tests\run_reach_canvas_video_test.py --compiler cl --fixture <printed-patched-checkout>
```

The patch runner verifies 34 immutable inputs at Chromium
`9ce0cc67aed57702fcfc011b9e49b0b02d7d5e3a`, the production registration order,
exact blobs, strict forward/reverse/reapply, already-applied detection,
production reset/patcher and incremental timestamps. Twelve complete native
Canvas2D/provider/WebGL methods must remain byte-identical to upstream,
including pixel writes, readbacks, resource creation and texture uploads.
`--migration-baseline <previous-cef-commit>` first applies the old five-patch
canvas stack, then verifies complete removal of retired source modifications
and introduced files. The fixture is never changed or downloaded by the test.

The portable C++ runner compiles the production ownership header, actual typed
IPC packing, mutable DOM identity collection, raster exclusion,
dirty/compositor/early-export hooks, context-loss
lifecycle and mode-selection methods with substituted Chromium boundaries. It
covers alpha,
epochs, aggregate budgets, preflight, cached identities, retirement, stale
legacy flags and full-page VIDEO isolation. Omit `--fixture` for ownership and
IPC alone. Linux compilers are supported with `--compiler c++`.

These are focused portable regressions, **not** native browser qualification.
The canonical Azure build and sandboxed browser validation must still verify
moving Canvas2D/WebGL pixels, alpha/HTML overlap, transforms/clips/opacity,
trusted input, native readback/texture semantics, independent/hidden streams,
loss/restore/resize/navigation and mode switching.

## Reach animation-only demand

```powershell
python tests\run_reach_animation_test.py --compiler cl
```

The runner compiles actual document-scoped demand notification, state and tick
code with substituted Blink boundaries. It covers an idle ordinary page's first
RAF without a canvas, coalescing, independent picture/animation wakeups, script
and CSS/lifecycle rearming, cancellation, stale-wire rejection, persistent
frame/document/page ownership and replacement during either callback—even
when replacement documents deliberately share the same token.

The bounded IPC contract is:

- `Reach.DrawingAnimationState.v1` and `Reach.DrawingAnimationTick.v1`:
  `[positiveRequestId, documentString]`, at most 128 document bytes. State
  permits an empty document; tick requires the exact current token.
- `Reach.DrawingAnimationResult.v1`: `[requestId, error, binaryJsonOrNull]`.
  Successful JSON contains only `{document, animationPending}`.
- `Reach.DrawingAnimationChanged.v1`: argument-free notification when the
  current document first acquires animation demand. This is independent of
  an outstanding coalesced `Reach.FrameRequested.v1` picture/canvas wakeup.

State inspection never consumes demand. Tick and capture clear demand before
script so nested scheduling rearms it. Native raster/compositor begin frames
remain held. The caller must retain shared target-lock exclusion between
animation ticks and drawing captures. There are no live WebGL stream reads,
originals, slices or sent/applied acknowledgements.

## Reach bounded capture retention

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
