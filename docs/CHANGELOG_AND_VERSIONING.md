# Changelog and Versioning Policy

[Português (pt-BR)](CHANGELOG_AND_VERSIONING.pt-BR.md) · English is authoritative if the versions differ.

This policy is referenced from [AGENTS.md](../AGENTS.md). The Hub reads the files described here at runtime, so keep the layout exact.

## Changelog files

- Maintain application changelogs only in version-specific files at `Changelog/<application-id>/<version>.md`, using the release catalog IDs: `hub`, `video-editor`, `image-editor`, and `motion-editor`.
- Update the corresponding file as each feature, improvement, fix, removal, compatibility change, or significant build/dependency/distribution maintenance is implemented.
- Include the entry in the same change and, when committing is authorized, in the same commit as the implementation. This is a completion and commit requirement even when the application's version metadata is unchanged.
- Purely editorial documentation corrections do not require changelog entries.
- Write entries in English only. Do not add translated siblings to `Changelog/`: the Hub's changelog reader loads files from that folder by version.

## Accumulating work for the next release

- During development, accumulate changes in the version-specific file for the intended next release, as designated by the maintainer or repository release planning. Reuse that file; for example, changes intended for 0.1.1 belong in `Changelog/<application-id>/0.1.1.md` while the executable may still report 0.1.0.
- Mark the file `Unreleased` until publication.
- Do not create `UNRELEASED.md` or another generic file in place of a version-specific changelog.
- A changelog filename or status does not authorize a release or an application version change.
- When the intended release version changes, consolidate its pending entries into the chosen version's file.
- Preserve published release history when accumulating work for a later version.

## Historical baseline and missing changelogs

- The initial 0.1.0 baseline is an explicit historical exception: no retroactive changelog is required for it.
- Every subsequent version, including prereleases, must have its version-specific changelog when prepared or documented.
- If a later published release lacks one, reconstruct it from available release evidence without changing version metadata merely to add documentation.
- Record changes since that application's previous published version.
- For shared-library changes, update the changelogs of applications whose delivered behavior or relevant maintenance is affected.
- If a release has no user-visible changes, state that and summarize its maintenance or distribution changes.

## Version metadata

- Keep application version metadata unchanged during ordinary development.
- Advance it only as part of maintainer-authorized release preparation, before compiling release artifacts, and verify that each application's executable, installer, and update-catalog entry agree.
- The suite release version remains independent of application versions.
- Document and protocol schema versions are separate and may advance when required by a validated compatibility change.

See also [Release Process](RELEASE_PROCESS.md).
