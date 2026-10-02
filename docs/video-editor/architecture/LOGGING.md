# Logging Boundary

Status: **provisional**.

The logger implementation is shared in `libs/diagnostics/` as
`creative_suite::diagnostics::Logger`. The Video Editor's
`apps/video-editor/src/logging/` files provide compatibility aliases to that
implementation. Motion Studio also uses the shared logger while keeping its
own metric schemas and application context. Logging policy and configuration
remain application-specific.

## Policy

The logger writes structured text entries with UTC timestamps, severity,
subsystem, operation, message, optional error code, and useful context. Every
unexpected, technical, or operational failure must produce a detailed log
entry before or while it is reported to the user.

Expected control-flow outcomes, such as cancelling a dialog, duplicate
imports, or a drop outside the active track, are intentionally excluded from
error logging.

Logs are stored in the platform's user log directory and limited to three files
of up to 5 MB each. The Help menu provides an `Open Log Folder` action. Native
crash dumps are not implemented in this phase.

Passwords, tokens, private keys, unnecessary personal data, and media contents
must never be written to the log.

## Experimental GPU composition

The playback adapter logs `gpu-composition` failures before emitting its nonmodal
fallback warning. Entries include operation/cause/code, canvas dimensions,
Timeline frame, generation, track/clip indexes, layer count and `fallback=cpu`.
Request limits or missing precision capabilities report a warning once per
activation; a technical failure is logged and latches CPU until the option is
switched off/on. Cancellation is ordinary control flow and adds no error entry.
Presentation retains its separate `rendering/gpu_preview` diagnostics. Composition
and presentation costs are separated in [metrics schema 8](RENDERING.md#gpu-composition-metrics-schema-8).
