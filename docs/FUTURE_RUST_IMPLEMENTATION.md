# Future Rust Implementation

Status: **proposed**. Rust is a possible future implementation language for
focused modules, not a second application that must be developed in parallel
with the main C++ direction.

## Current position

The main product may use C++ as its primary language for the UI, GPU, media
integration, and desktop platform layers. This is a practical direction for
the current phase, but it does not permanently exclude Rust.

Rust should be introduced only when a module has a clear technical reason to
use it and the boundary with the existing C++ code can remain small and easy
to test.

## Suitable future modules

Potential Rust candidates include:

- parsers for PSD, project, plugin, and other complex binary formats;
- validation of untrusted files before they reach the C++ core;
- compression, decompression, and archive handling;
- background jobs with strict ownership and cancellation rules;
- isolated media metadata or indexing services;
- security-sensitive utilities where memory safety has a measurable benefit.

These are candidates only. A module should not be rewritten in Rust merely to
use a second language.

## Modules that should remain C++ unless evidence changes

The following areas are expected to remain in C++ initially:

- the main application and desktop UI;
- GPU backend integration and render scheduling;
- FFmpeg and SDL3 integration;
- plugin loading and platform-specific system code;
- the first version of the shared project and document core.

Keeping these boundaries stable reduces build complexity and allows most
contributors to work without learning both languages and their FFI details.

## Integration boundary

Rust modules should be compiled as independent Cargo packages and exposed to
the C++ side through a small C-compatible API. The boundary should use opaque
handles, fixed-width integers, explicit byte slices, and result codes or
structured error values.

Example shape:

```c
typedef struct RustDocument RustDocument;

RustDocument* rust_document_open(const uint8_t* data, size_t length);
void rust_document_free(RustDocument* document);
int32_t rust_document_layer_count(const RustDocument* document);
```

The API must document which side allocates and releases every object. C++
classes, Rust-owned strings, standard-library containers, exceptions, and Rust
panics must not cross the boundary directly.

## Required build and test rules

Before adding a Rust module:

1. Define the module responsibility and its ownership boundary.
2. Prove that the module has a meaningful safety, correctness, or maintenance
   benefit over an equivalent C++ implementation.
3. Keep the public interface small and C-compatible.
4. Add C++/Rust integration tests and malformed-input tests where relevant.
5. Build the module on Windows, macOS, and Linux in continuous integration.
6. Record dependencies, licenses, generated bindings, and distribution impact.
7. Measure the cost of the FFI boundary before moving large buffers or video
   frames between languages.

Large video frames, GPU resources, and high-frequency render data should not
cross the boundary repeatedly unless profiling demonstrates that the design is
worth the cost.

## Adoption path

The recommended order is:

1. Keep the current C++ prototype and product foundation focused.
2. Select one small, isolated Rust experiment, preferably a parser or file
   validator.
3. Define and test its C ABI without introducing a shared Rust/C++ core.
4. Compare its reliability, build cost, performance, and contributor impact.
5. Expand Rust only if the evidence justifies another module.

The archived Rust prototype at `prototypes/archive/rust/` remains a technical
reference. It is not a requirement to maintain two complete application
implementations.
