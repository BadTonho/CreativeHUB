param(
    [Parameter(Mandatory = $true)]
    [string] $BuildDirectory,

    [Parameter(Mandatory = $true)]
    [string] $OutputDirectory,

    [Parameter(Mandatory = $true)]
    [string] $SuiteVersion,

    [string] $PreviousReleaseAssetsDirectory,
    [switch] $FirstRelease,
    [string] $InnoCompiler = "ISCC.exe"
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$versionFile = Join-Path $repoRoot "packaging\app-versions.json"
$notesFile = Join-Path $repoRoot "packaging\release-notes.json"
$scriptTemplate = Join-Path $PSScriptRoot "creative-suite-app.iss"
$versions = Get-Content -LiteralPath $versionFile -Raw | ConvertFrom-Json
$notes = Get-Content -LiteralPath $notesFile -Raw | ConvertFrom-Json
$appIds = @("hub", "video-editor", "image-editor", "motion-editor")

function ConvertTo-InnoString([string] $Value) {
    return '"' + $Value.Replace('"', '""') + '"'
}

function Get-VisualStudioVcInstallDirectory {
    if (-not [string]::IsNullOrWhiteSpace($env:VCINSTALLDIR)) {
        return $env:VCINSTALLDIR
    }

    $programFilesX86 = [System.Environment]::GetFolderPath("ProgramFilesX86")
    $vswherePath = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswherePath -PathType Leaf)) {
        return $null
    }

    $installationPath = & $vswherePath -latest -products "*" `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace([string]$installationPath)) {
        return $null
    }

    $vcDirectory = Join-Path ([string]$installationPath.Trim()) "VC"
    if (-not (Test-Path -LiteralPath $vcDirectory -PathType Container)) {
        return $null
    }
    return $vcDirectory.TrimEnd('\') + '\'
}

function Get-VisualStudioCrtDirectory([string] $VcInstallDirectory) {
    if ([string]::IsNullOrWhiteSpace($VcInstallDirectory)) { return $null }

    $redistRoot = Join-Path $VcInstallDirectory "Redist\MSVC"
    if (-not (Test-Path -LiteralPath $redistRoot -PathType Container)) { return $null }

    $redistVersions = Get-ChildItem -LiteralPath $redistRoot -Directory |
        Where-Object { $_.Name -match '^\d+\.\d+' } |
        Sort-Object { [version]$_.Name } -Descending
    foreach ($redistVersion in $redistVersions) {
        $x64Directory = Join-Path $redistVersion.FullName "x64"
        $crtDirectory = Get-ChildItem -LiteralPath $x64Directory -Directory `
            -Filter "Microsoft.VC*.CRT" -ErrorAction SilentlyContinue |
            Sort-Object Name | Select-Object -First 1
        if ($crtDirectory) { return $crtDirectory.FullName }
    }

    return $null
}

$resolvedBuildDirectory = (Resolve-Path -LiteralPath $BuildDirectory).Path
$resolvedOutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $resolvedOutputDirectory -Force | Out-Null

try { $null = [version]$SuiteVersion } catch { throw "SuiteVersion must be a numeric major.minor.patch version." }
if ($FirstRelease -and $PreviousReleaseAssetsDirectory) {
    throw "Use either -FirstRelease or -PreviousReleaseAssetsDirectory, not both."
}
if (-not $FirstRelease -and -not $PreviousReleaseAssetsDirectory) {
    throw "Provide -PreviousReleaseAssetsDirectory for every release after the first, or explicitly select -FirstRelease."
}

$previousCatalog = $null
if ($PreviousReleaseAssetsDirectory) {
    $previousDirectory = (Resolve-Path -LiteralPath $PreviousReleaseAssetsDirectory).Path
    if ([System.StringComparer]::OrdinalIgnoreCase.Equals(
            [System.IO.Path]::GetFullPath($previousDirectory), $resolvedOutputDirectory)) {
        throw "OutputDirectory must differ from PreviousReleaseAssetsDirectory."
    }
    $previousCatalogPath = Join-Path $previousDirectory "updates.json"
    if (-not (Test-Path -LiteralPath $previousCatalogPath -PathType Leaf)) {
        throw "The previous release assets must include updates.json."
    }
    $previousCatalog = Get-Content -LiteralPath $previousCatalogPath -Raw | ConvertFrom-Json
    if ($previousCatalog.schema_version -ne 1) {
        throw "Unsupported previous catalog schema; refusing to publish a release without a verified migration path."
    }
    foreach ($appId in $appIds) {
        if (-not $previousCatalog.applications.PSObject.Properties[$appId].Value) {
            throw "The previous catalog is missing '$appId'; refusing to publish an incomplete suite release."
        }
    }
}

$publishedApplications = [ordered]@{}
foreach ($appId in $appIds) {
    $application = $versions.applications.PSObject.Properties[$appId].Value
    if (-not $application) { throw "No version metadata exists for '$appId'." }
    $assetPath = Join-Path $resolvedOutputDirectory $application.installer_asset
    $previousApplication = if ($previousCatalog) {
        $previousCatalog.applications.PSObject.Properties[$appId].Value
    } else { $null }

    $releaseNotes = $notes.PSObject.Properties[$appId].Value
    if ([string]::IsNullOrWhiteSpace([string]$releaseNotes)) {
        throw "Release notes are required for '$appId'."
    }
    if ($previousApplication -and
        ([version]$application.version -lt [version]$previousApplication.version)) {
        throw "The '$appId' version cannot move backwards from $($previousApplication.version) to $($application.version)."
    }

    $reuseInstaller = $previousApplication -and
        ($previousApplication.version -eq $application.version)

    if ($reuseInstaller) {
        $previousAssetPath = Join-Path $previousDirectory $previousApplication.installer_asset
        if (-not (Test-Path -LiteralPath $previousAssetPath -PathType Leaf)) {
            throw "'$appId' kept version $($application.version), but its previous installer is missing. Publish the old asset or advance this app's version."
        }
        $previousHash = (Get-FileHash -LiteralPath $previousAssetPath -Algorithm SHA256).Hash.ToLowerInvariant()
        $previousSize = (Get-Item -LiteralPath $previousAssetPath).Length
        if (($previousHash -ne $previousApplication.sha256) -or
            ($previousSize -ne [int64]$previousApplication.size_bytes)) {
            throw "The previous installer for '$appId' does not match its published catalog."
        }
        Copy-Item -LiteralPath $previousAssetPath -Destination $assetPath -Force
        Write-Host "Reusing the previously published $appId installer byte-for-byte."
    }
    else {
        $stageDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ("creative-suite-stage-" + [guid]::NewGuid().ToString("N"))
        try {
            New-Item -ItemType Directory -Path $stageDirectory | Out-Null
            $originalVcInstallDirectory = $env:VCINSTALLDIR
            try {
                $vcInstallDirectory = Get-VisualStudioVcInstallDirectory
                if (-not [string]::IsNullOrWhiteSpace([string]$vcInstallDirectory)) {
                    $env:VCINSTALLDIR = $vcInstallDirectory
                    Write-Host "Using Visual Studio C++ runtime files from $vcInstallDirectory"
                }
                & cmake --install $resolvedBuildDirectory --config Release --prefix $stageDirectory --component $appId
                if ($LASTEXITCODE -ne 0) { throw "CMake staging failed for '$appId' with exit code $LASTEXITCODE." }
            }
            finally {
                if ($null -eq $originalVcInstallDirectory) {
                    Remove-Item Env:VCINSTALLDIR -ErrorAction SilentlyContinue
                }
                else {
                    $env:VCINSTALLDIR = $originalVcInstallDirectory
                }
            }
            if (-not (Test-Path -LiteralPath (Join-Path $stageDirectory (Join-Path "bin" $application.executable)) -PathType Leaf)) {
                throw "The staged '$appId' executable is missing: $($application.executable)"
            }
            $qtConfigPath = Join-Path $stageDirectory "bin\qt.conf"
            if (Test-Path -LiteralPath $qtConfigPath -PathType Leaf) {
                [System.IO.File]::WriteAllText(
                    $qtConfigPath,
                    "[Paths]`r`nPrefix = .`r`nPlugins = plugins`r`n",
                    (New-Object System.Text.UTF8Encoding($false)))
            }

            $crtDirectory = Get-VisualStudioCrtDirectory $vcInstallDirectory
            if (-not $crtDirectory) {
                throw "The official x64 Visual C++ redistributable files could not be located for '$appId'."
            }
            $crtFiles = Get-ChildItem -LiteralPath $crtDirectory -File |
                Where-Object { $_.Extension -in @(".dll", ".manifest") }
            if (-not $crtFiles) {
                throw "The official x64 Visual C++ runtime folder is empty: $crtDirectory"
            }
            Copy-Item -LiteralPath $crtFiles.FullName -Destination (Join-Path $stageDirectory "bin") -Force

            $redistributableInstaller = Join-Path $stageDirectory "bin\vc_redist.x64.exe"
            if (Test-Path -LiteralPath $redistributableInstaller -PathType Leaf) {
                Remove-Item -LiteralPath $redistributableInstaller -Force
            }

            $compilerCommand = Get-Command $InnoCompiler -ErrorAction SilentlyContinue
            if (-not $compilerCommand) { throw "Inno Setup Compiler '$InnoCompiler' is required to build the changed '$appId' installer." }
            $compileScriptPath = Join-Path $stageDirectory "creative-suite-app-build.iss"
            $compileScriptLines = @(
                "#define AppId $(ConvertTo-InnoString $appId)"
                "#define AppName $(ConvertTo-InnoString ([string]$application.name))"
                "#define AppVersion $(ConvertTo-InnoString ([string]$application.version))"
                "#define AppExecutable $(ConvertTo-InnoString ([string]$application.executable))"
                "#define AppOutputName $(ConvertTo-InnoString ([System.IO.Path]::GetFileNameWithoutExtension($application.installer_asset)))"
                "#define SourceDir $(ConvertTo-InnoString $stageDirectory)"
                "#define OutputDir $(ConvertTo-InnoString $resolvedOutputDirectory)"
                "#include $(ConvertTo-InnoString $scriptTemplate)"
            )
            [System.IO.File]::WriteAllLines(
                $compileScriptPath,
                $compileScriptLines,
                (New-Object System.Text.UTF8Encoding($false)))
            & $compilerCommand.Source $compileScriptPath
            if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed for '$appId' with exit code $LASTEXITCODE." }
            if (-not (Test-Path -LiteralPath $assetPath -PathType Leaf)) {
                throw "Inno Setup did not create the expected asset '$($application.installer_asset)'."
            }
        }
        finally {
            $temporaryRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
            $temporaryPrefix = $temporaryRoot.TrimEnd(
                [System.IO.Path]::DirectorySeparatorChar,
                [System.IO.Path]::AltDirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
            $resolvedStageDirectory = [System.IO.Path]::GetFullPath($stageDirectory)
            $stageLeaf = [System.IO.Path]::GetFileName($resolvedStageDirectory)
            $isOwnedStage = $resolvedStageDirectory.StartsWith(
                $temporaryPrefix, [System.StringComparison]::OrdinalIgnoreCase) -and
                $stageLeaf.StartsWith("creative-suite-stage-", [System.StringComparison]::Ordinal)
            if ($isOwnedStage -and (Test-Path -LiteralPath $resolvedStageDirectory)) {
                Remove-Item -LiteralPath $resolvedStageDirectory -Recurse -Force
            }
        }
    }

    $installerInfo = Get-Item -LiteralPath $assetPath
    $installerHash = (Get-FileHash -LiteralPath $assetPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $publishedApplications[$appId] = [ordered]@{
        version = $application.version
        installer_asset = $application.installer_asset
        size_bytes = [int64]$installerInfo.Length
        sha256 = $installerHash
        release_notes = [string]$releaseNotes
    }
}

$catalog = [ordered]@{
    schema_version = 1
    suite_version = $SuiteVersion
    applications = $publishedApplications
}
$catalogJson = $catalog | ConvertTo-Json -Depth 8
$catalogPath = Join-Path $resolvedOutputDirectory "updates.json"
[System.IO.File]::WriteAllText($catalogPath, $catalogJson, (New-Object System.Text.UTF8Encoding($false)))
Write-Host "Created the four installers and updates.json in $resolvedOutputDirectory"
