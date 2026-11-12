# Linux release packages

`build-packages.sh` turns an already built Release CMake tree into one portable
`.tar.gz` per application, named
`creative-suite-<app-id>-<app-version>-linux-<arch>.tar.gz`, and writes
`SHA256SUMS-linux.txt`. App versions come from `packaging/app-versions.json`,
the same source the Windows release uses.

```bash
./packaging/linux/build-packages.sh ./build ./release-assets 2026.11.0
```

Each archive contains `bin/` (executable and `qt.conf`), optional `lib/` and
plugin directories, and `LICENSE`. The script runs `cmake --install --component
<app-id>`, copies shared libraries that resolve outside the system library
directories (for example vcpkg-provided Qt or FFmpeg) into `lib/`, and sets the
executable RPATH to `$ORIGIN/../lib` with `patchelf`, which is required only
when libraries are bundled. System libraries (glibc, GL/EGL, X11/xcb, and so
on) are never bundled. Archives are reproducible: sorted entries, fixed
ownership and mtime, no gzip timestamp.

## Status and limits

- This is a **provisional** format (a plain tarball, no desktop integration,
  no `.deb`/`.rpm`/AppImage/Flatpak). Distribution-specific packaging remains
  pending and needs its own decision.
- Linux packages are **not** in `updates.json` and the in-app updater does not
  handle them; users download a new archive manually. Adding Linux to the
  update catalog would change the catalog and updater contract and needs a
  migration path plus regression coverage.
- Packages are unsigned and only x86_64 is built by the release workflow.
- Verified locally only by running the script against a system-Qt build tree
  (four archives, correct layout). A package built from the CI vcpkg tree is
  unvalidated until the first release run: check that each app starts on a
  clean machine and that `ldd bin/<executable>` has no unresolved libraries.
