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
ownership, pending commands and pending originals. A read remains at most
8 MiB including original bytes; originals are at most 8 MiB minus 1024 bytes.
Larger otherwise-valid GL inputs are **not generally supported**: copied spans
above 3 MiB, serialized commands above 4 MiB and individual strings above
1 MiB fail explicitly. Base64/JSON overhead can reject a copied span at the
nominal 3 MiB boundary. No native fragmentation or silent splitting changes
WebGL calls; transport fragments do not remove these native limits.

Client crash/reload is a fresh-target operation: revoke/close the old target
and load a fresh page from zero. GPU-state reconstruction is not required.
Actual fresh-client pixels/input, sibling isolation and source semantics still
require native-build/browser acceptance before activation.

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
