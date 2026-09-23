# Logging Boundary

Status: **provisional**.

The Main Editor owns the first application-local logger under
`apps/video-editor/src/logging/`. It uses only the C++ standard library and does
not expose Qt types.

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
