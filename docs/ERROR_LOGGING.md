# Error Logging Policy

[Português (pt-BR)](ERROR_LOGGING.pt-BR.md) · English is authoritative if the versions differ.

This policy is referenced from section 8 of [AGENTS.md](../AGENTS.md).

## Requirement

Every application and every failure-prone module must maintain an actionable error log so problems can be diagnosed and corrected.

## What must be logged

- Every unexpected, technical, or operational error must be logged before or while it is reported to the user.
- Intentional control-flow outcomes and expected user actions are not errors and must not be written as error entries. Examples: cancelling a dialog, importing a duplicate, or trying to add a clip while the timeline is occupied.

## Entry contents

Error entries should include:

- timestamp;
- severity;
- subsystem;
- operation;
- human-readable cause;
- relevant error code;
- useful context such as a file path or identifier, when available.

## User-facing messages

User-facing error messages may remain concise, but the detailed cause must be written to the local log before or while the error is reported.

## Sensitive data and size

- Logs must never contain passwords, tokens, private keys, or unnecessary sensitive personal data.
- Persistent logs should support bounded size or rotation.

## Related

- Handle media errors, corrupted files, and resource shortages without unexpectedly terminating the application (section 8 of [AGENTS.md](../AGENTS.md)).
- Consider automatic project recovery and autosave from an early stage.
