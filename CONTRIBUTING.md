# Contributing

Thanks for considering a contribution. The project is an open-source, cross-platform creative suite; contributions should preserve its modular application boundaries and keep documented behavior aligned with the implementation.

## Before changing files

- Read [AGENTS.md](AGENTS.md) for repository structure, platform, documentation, and change requirements.
- Read the affected application's scope, architecture, roadmap, and regression guide under `docs/` before changing that application.
- Check `git status` and preserve changes already present in your working tree.

## Implementation and verification

- Keep application-owned workflows and document formats in their application; put reusable capabilities in `libs/` only when their contracts and consumers are genuinely shared.
- Add or update regression coverage for changed behavior and public boundaries. Follow [Regression Policy](docs/REGRESSION_POLICY.md) and the affected application's feature-to-verification index.
- Run focused builds and checks for the affected application and its consumers. Document manual checks when full-application behavior cannot be automated reliably.
- Keep project documentation in English. Update affected scope, architecture, format, validation, and user guides in the same change. Document user-facing keyboard shortcut changes in the application's shortcut guide.
- Track third-party dependencies, licenses, codecs, and assets. Do not contribute proprietary code or assets without authorization.

## Pull requests and releases

Keep changes focused and reviewable. Do not commit, push, publish, or prepare release artifacts without maintainer authorization. Before proposing a remote change, review all staged, unstaged, untracked, and ignored files that would be included, and remove secrets, credentials, local configuration, generated output, and private media from the proposed submission.

Application version metadata is maintained by the project maintainers. Follow the version-specific changelog rules in `AGENTS.md` when implementation work is being prepared for a release.