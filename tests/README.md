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
the production header from `reach_webgl_commands.patch`; it does not build CEF.
It covers 150,000 commands with applied-prefix retirement, non-destructive
reads, duplicate/delayed/future/unread/unsent/stale-generation acknowledgements,
original-byte delivery budgeting and the shared 256 MiB pending-memory bound.
Marking a sent prefix is separate from application and does not release memory.
It also slices a 20 MiB command and 12 MiB original into at most 96 KiB
pieces, rejecting byte gaps/stale generations/future cursors and preserving
active read progress across retries of older commands. Neither sent nor applied
may advance until the entire command **and** original are read. To reproduce
the pre-slice failure, pass `--baseline 01f14d554`.

These are queue tests, not browser pixel, source-cookie isolation, renderer IPC
or reload/context-loss qualification. Full CEF builds remain on the canonical
build VM. Pending delivery is not a reconstruction checkpoint.

## Reach encoded ImageBitmap originals

From a Visual Studio developer shell:

```powershell
python tests\run_reach_image_bitmap_test.py --compiler cl --fixture <patched-chromium>
```

The fixture must be the Chromium revision pinned by
`CHROMIUM_BUILD_COMPATIBILITY.txt`, with `reach_webgl_commands.patch` applied.
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
