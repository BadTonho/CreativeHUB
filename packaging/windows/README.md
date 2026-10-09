# Windows installer release assets

`app-versions.json` is the single version source used by CMake to embed the
application versions and by the release script to label installer metadata.
`Build-Installers.ps1` stages each CMake install component, compiles a complete
Inno Setup installer, and writes `updates.json` with each installer size and
SHA-256 digest. The output directory contains the four `.exe` installers and
the catalog to upload to one GitHub Release.

Each setup executable embeds the matching application icon from
`docs/assets/app-icons` using Inno Setup's `SetupIconFile`. Start Menu and
uninstall entries continue to use the icon embedded in the installed app.

CMake places each executable and runtime DLL in the staged `bin` directory and
Qt plugins in `plugins`. The release script adjusts the staged `qt.conf` for
the flattened layout. The setup maps runtime files into the selected app
directory root and preserves `plugins` as a child directory. This keeps the
registered install path, executable path, Qt plugin path, and shortcut
consistent.

Use a Release-configured Windows build that includes all four applications.
For the first published suite release, pass `-FirstRelease`. For every later
release, provide the downloaded assets from the previous release through
`-PreviousReleaseAssetsDirectory`. When an app's version is unchanged, the
script verifies and reuses that exact installer file. It refuses to publish
without a valid prior catalog containing all four app entries. An app whose
version is unchanged must also have its previous installer available and
matching that catalog; it will not be silently rebuilt under the same version.
Application versions cannot move backwards. Inno Setup is required to build
any app whose version advanced.

```powershell
./packaging/windows/Build-Installers.ps1 `
  -BuildDirectory ./build `
  -OutputDirectory ./release-assets `
  -SuiteVersion 2026.10.0 `
  -FirstRelease
```

For a later suite release, replace `-FirstRelease` with:

```powershell
  -PreviousReleaseAssetsDirectory ./previous-release-assets
```

Example command shape for a later release:

```powershell
./packaging/windows/Build-Installers.ps1 `
  -BuildDirectory ./build `
  -OutputDirectory ./release-assets `
  -SuiteVersion 2026.11.0 `
  -PreviousReleaseAssetsDirectory ./previous-release-assets
```

The setup is per-user and uses the same executable for a first installation
and an update. On first install, the directory page lets the user choose a
location. Later runs reuse the registered application directory and run
silently when started by the updater. Before replacing an existing installation,
the setup keeps a file backup in the user's local updater data directory and
writes a transaction state to the per-user registry. A setup failure attempts
to copy the prior files back. If the app has not confirmed a successful first
launch, the Hub can offer that retained backup for restoration.

The generated installer should be validated on Windows in a disposable user
profile before publication, including checking that the setup executable and
wizard display the matching app icon, a clean install, an update to a chosen
directory, cancellation/failure recovery, and Hub restoration. Also confirm the
Start Menu shortcut uses the installed app icon. The repository
does not include the Inno Setup compiler, so the packaging machine must have
Inno Setup installed and `ISCC.exe` available on `PATH` or passed with
`-InnoCompiler`. The release script locates the Visual Studio C++ runtime,
copies its redistributable DLLs into each app's directory, and removes the
machine-wide redistributable installer from the per-user package. Microsoft
supports local deployment for installs that do not have administrator rights,
but notes that the app owner must service those files; see the
[deployment-method guidance](https://learn.microsoft.com/en-us/cpp/windows/choosing-a-deployment-method?view=msvc-170).
