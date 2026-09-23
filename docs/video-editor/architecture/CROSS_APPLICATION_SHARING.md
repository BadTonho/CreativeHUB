# Cross-Application Sharing Proposal

**Status:** Provisional proposal. This document describes a direction to validate; it does not establish a final implementation or technology decision.

## Goal

Share stable domain behavior between the Video Editor, Motion Editor, and Photo Editor while allowing each application to keep its own interface and workflows. Treat internal code reuse and transferring work between applications as separate capabilities.

## Recommended Boundaries

Applications should depend on reusable libraries through documented APIs. Shared libraries must not depend on application UI code. Each application can adapt its own controls and workflows to the shared operations.

| Capability | Suggested ownership |
| --- | --- |
| Layers, transforms, masks, and composition | Shared composition library, once there is a second real consumer |
| Keyframe evaluation and curves | Shared by the Video Editor and Motion Editor; include the Photo Editor only if animation becomes an approved use case |
| Timeline editing and audio workflows | Application-specific, with reusable lower-level services where a real second consumer exists |
| Painting, selection, and retouching tools | Photo Editor |
| Advanced motion-design workflows | Motion Editor |
| Panels, controls, shortcuts, and application workflows | Each application |
| Media metadata and resource references | Shared where their semantics match; application-specific import and export workflows may remain separate |

Color management and common effect definitions are possible shared capabilities. Define their contracts after the project establishes the relevant color pipeline and confirms that the same behavior is needed by multiple applications.

## First Shared Capability to Validate

Start with a small composition operation: a layer with a source, transform, opacity, optional mask, and a defined compositing result. The Video Editor already has composition-related code, including a backend-neutral composition stage and a CPU frame compositor. Use those existing boundaries to prototype a stable API before moving code into a shared library.

The API contract should document:

- coordinate systems, units, and transform conventions;
- pixel formats, color assumptions, and alpha behavior;
- resource ownership, lifetime, and thread requirements;
- errors and the context that must be logged;
- serialization and compatibility expectations, if the data is persisted.

Keep keyframe and curve evaluation as a separate capability so applications can adopt it according to their needs.

## Extraction Criteria and Build Shape

Keep a capability application-local while the Video Editor is its only consumer. Extract it into a focused library under `libs/` when a second application has a real use case and the API can serve both without application-specific conditions.

Use CMake library targets and link each consuming application to the required targets. Prefer focused modules such as `composition` or `animation` over a catch-all core target. Choose static or shared linkage based on deployment and plugin requirements; runtime-loaded libraries are not required merely to share code among applications in one build.

Before extraction, confirm that:

1. one implementation owns the behavior;
2. both consumers use the same documented contract;
3. regression coverage exercises the shared behavior and both application boundaries;
4. application UI and workflow decisions remain outside the library.

## Work Transfer Between Applications

Treat opening or continuing a document in another application as a later, separate feature. Begin with an explicit handoff, such as opening a supported composition in the Motion Editor, backed by a versioned document format and stable resource references.

Before enabling edits across applications, define how the format represents revisions, unsaved changes, linked assets, missing resources, and unsupported features. Avoid live synchronization of all editor state until a concrete workflow requires it.

OpenFX may be evaluated later for third-party effect-plugin interoperability. A plugin host API is distinct from the internal API used by the project's own applications.

## Suggested Sequence

1. Create a capability ownership matrix for the three applications.
2. Specify the composition contract and validate it inside the Video Editor.
3. Build a Motion Editor prototype that consumes the same operation.
4. Extract the proven capability into a focused library and link both applications to it.
5. Evaluate Photo Editor adoption based on its actual image-composition requirements.
6. Design versioned document handoff after shared data structures and workflows are understood.

This sequence keeps the shared core aligned with the repository architecture: shared libraries should have a real second consumer, and the applications should not duplicate media, rendering, or animation engines without a documented technical reason.
