# Repository Structure

Status: **provisional**.

The project uses one repository organized by applications, modules, and
documentation. It does not create separate repositories or an unnecessary
monorepo structure.

## Current layout

```text
apps/
  main-editor/       # User-facing audiovisual editor
libs/                # Shared libraries added only when multiple apps need them
platform/            # Platform adapters when a shared abstraction requires them
prototypes/          # Isolated technical experiments and references
docs/                # Project and architecture documentation
```

The first real application is located at `apps/main-editor/`. The archived
technical prototypes remain under `prototypes/` and are not application
dependencies.

## Module boundaries

The Main Editor and Motion Studio are expected to share core responsibilities
without duplicating media, rendering, or animation engines. A shared library is
created only when it has a second real consumer and a documented responsibility.

The initial application keeps media, timeline, playback, logging, project
persistence, and UI code application-local. This avoids premature abstractions
while the product boundaries are still being validated.
