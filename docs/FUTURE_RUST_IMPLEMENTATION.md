# Future Rust Implementation Across the Suite

Status: **proposed**. Rust is a possible future implementation language for
focused modules, not a second application that must be developed in parallel
with the main C++ direction.

Assessment updated: **2026-10-21**. This document preserves a provisional
recommendation for future consideration; it does not authorize a migration,
introduce a dependency, or change the current development baseline. Its scope
covers Hub (`hub`), Video Editor (`video-editor`), Image Editor (`image-editor`),
Motion Studio (`motion-editor`), and relevant shared libraries under `libs/`.

## Current position

The current development baseline is C++20, Qt 6, CMake, and FFmpeg where media
processing requires it. Keeping this foundation is the recommendation for the
current phase, but it does not permanently exclude Rust.

Rust should be introduced only when a module has a clear technical reason to
use it and the boundary with the existing C++ code can remain small and easy
to test.

## Benefits, costs, and limits

The strongest reason to evaluate Rust is to prevent certain memory-safety and
concurrency errors in safe Rust code. It is not an automatic performance
upgrade. Rust cannot verify that external C/C++ code obeys the memory contracts
required by a safe wrapper; the integration boundary still needs explicit
ownership rules and review. See the official [Unsafe Rust documentation](https://doc.rust-lang.org/stable/book/ch20-01-unsafe-rust.html).

Maintaining two languages adds Cargo/CMake integration, toolchain and dependency
maintenance, debugging across languages, and build/test work on all three target
operating systems. Poor boundary design can add allocations and buffer copies.
Replacing a language does not by itself resolve codec, GPU synchronization, or
frame-transfer bottlenecks.

The [archived Video Editor comparison](video-editor/decisions/TECHNICAL_PROTOTYPE_COMPARISON.md) did not establish
a performance winner. GPU runtime limitations prevented valid preview/rendering
measurements in the recorded environment. There is no project evidence from
that comparison that Rust would make the applications faster or slower.

An alternative is to keep a module in C++ while improving ownership boundaries,
using sanitizers, and fuzzing relevant parsers. Compare these options against
the actual problem before introducing another language. Rewriting stable code
solely to change languages is not recommended.

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

For Hub, possible experiments include isolated backup/archive validation or
file indexing. For Image Editor, a bounded image-format parser could be a
candidate. Video Editor and Motion Studio could evaluate metadata validation
or background indexing outside their rendering paths. These examples are not
implementation commitments. Share a module only when multiple applications
need the same stable contract; retain application-specific workflows locally.

## Modules that should remain C++ unless evidence changes

The following areas are expected to remain in C++ initially:

- each application's lifecycle orchestration and desktop UI;
- GPU backend integration and render scheduling;
- FFmpeg and native media integration;
- plugin loading and platform-specific system code;
- application-owned project/document models and timeline interaction.

SDL3 belongs to the historical prototypes, not the current Qt application
baseline. Project/document ownership remains with each application; introducing
Rust does not justify centralizing these models into a shared document core.

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

1. Keep the current C++ application foundation stable.
2. Select one small, isolated Rust experiment, preferably a parser or file
   validator.
3. Define and test its C ABI without introducing a shared Rust/C++ core.
4. Compare its reliability, build cost, performance, and contributor impact.
5. Expand Rust only if the evidence justifies another module.

Before an experiment, record the concrete problem, a representative workload,
and acceptance criteria. Compare correctness and malformed-input handling,
memory use, startup or task latency where relevant, throughput, boundary copies,
build cost, and maintenance effort against the C++ alternative. Use repeatable
measurements and keep unvalidated platform results explicitly pending. Safety
or maintenance improvements may justify adoption without a speed increase,
provided their value and the integration costs are documented.

The archived Rust prototype at `prototypes/archive/rust/` remains a technical
reference. It is not a requirement to maintain two complete application
implementations.
