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
