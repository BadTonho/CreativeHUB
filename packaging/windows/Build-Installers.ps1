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
            & cmake --install $resolvedBuildDirectory --config Release --prefix $stageDirectory --component $appId
            if ($LASTEXITCODE -ne 0) { throw "CMake staging failed for '$appId' with exit code $LASTEXITCODE." }
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

            $compilerCommand = Get-Command $InnoCompiler -ErrorAction SilentlyContinue
            if (-not $compilerCommand) { throw "Inno Setup Compiler '$InnoCompiler' is required to build the changed '$appId' installer." }
            & $compilerCommand.Source `
                "/DAppId=$appId" `
                "/DAppName=$($application.name)" `
                "/DAppVersion=$($application.version)" `
                "/DAppExecutable=$($application.executable)" `
                "/DAppOutputName=$([System.IO.Path]::GetFileNameWithoutExtension($application.installer_asset))" `
                "/DSourceDir=$stageDirectory" `
                "/DOutputDir=$resolvedOutputDirectory" `
                $scriptTemplate
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
