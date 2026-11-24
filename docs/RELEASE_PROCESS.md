# Release Process Policy

[Português (pt-BR)](RELEASE_PROCESS.pt-BR.md) · English is authoritative if the versions differ.

This policy is referenced from [AGENTS.md](../AGENTS.md).

## Authorization

- Do not create or push release tags (`v<year>.<minor>.<patch>`) or publish a GitHub Release without explicit maintainer authorization.
- Do not generate application installers or release packages unless the user explicitly requests them.
- A release tag still requires a successful manual CI run for its commit.

## What a tag triggers

A tag push triggers `.github/workflows/release.yml`, which builds the Windows installers and Linux packages and attaches them to a draft release. The maintainer reviews and publishes the draft manually.

## Keeping documentation aligned

When the release flow changes, keep `release.yml` and the documentation in `packaging/windows/` and `packaging/linux/` aligned in the same change.

## Related policies

- Version metadata and changelog rules: [Changelog and Versioning Policy](CHANGELOG_AND_VERSIONING.md).
- Update continuity and migration requirements: section 7 of [AGENTS.md](../AGENTS.md).
