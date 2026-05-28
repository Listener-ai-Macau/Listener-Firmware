param()

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$testRoot = Join-Path $projectRoot ".cache\ota_package_release_rule_tests"
$sourcePackageScript = Join-Path $PSScriptRoot "package_ota_firmware.ps1"
$sourceFactoryScript = Join-Path $PSScriptRoot "package_factory_firmware.ps1"
$manifestCheckScript = Join-Path $PSScriptRoot "check_ota_manifest.ps1"

if (Test-Path -LiteralPath $testRoot) {
    Remove-Item -LiteralPath $testRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $testRoot | Out-Null

$fakeRepo = Join-Path $testRoot "fake_repo"
$fakeTools = Join-Path $fakeRepo "tools"
New-Item -ItemType Directory -Force -Path $fakeTools | Out-Null
Copy-Item -LiteralPath $sourcePackageScript -Destination (Join-Path $fakeTools "package_ota_firmware.ps1") -Force
Copy-Item -LiteralPath $sourceFactoryScript -Destination (Join-Path $fakeTools "package_factory_firmware.ps1") -Force
Set-Content -LiteralPath (Join-Path $fakeRepo ".gitignore") -Value @(".cache/", "build/") -Encoding UTF8
& git -C $fakeRepo init | Out-Null
& git -C $fakeRepo add . | Out-Null
& git -C $fakeRepo -c user.name="ota-test" -c user.email="ota-test@example.invalid" commit -m "init package test repo" | Out-Null

$packageScript = Join-Path $fakeTools "package_ota_firmware.ps1"
$buildRoot = Join-Path $testRoot "builds"

function Get-TestUserProfilePath {
    if ($env:USERPROFILE) {
        return $env:USERPROFILE
    }
    $profile = [Environment]::GetFolderPath([Environment+SpecialFolder]::UserProfile)
    if (-not [string]::IsNullOrWhiteSpace($profile)) {
        return $profile
    }
    throw "Unable to resolve user profile path."
}

function Get-TestPythonExe {
    $candidate = Join-Path (Get-TestUserProfilePath) ".espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe"
    if (Test-Path -LiteralPath $candidate) {
        return $candidate
    }
    return "python"
}

function New-FakePartitionTable {
    param([Parameter(Mandatory = $true)][string]$OutputPath)

    $idf = if ($env:IDF_PATH) { $env:IDF_PATH } elseif ($env:ESP_IDF_PATH) { $env:ESP_IDF_PATH } else { Join-Path (Get-TestUserProfilePath) "esp\esp-idf" }
    $genPart = Join-Path $idf "components\partition_table\gen_esp32part.py"
    if (-not (Test-Path -LiteralPath $genPart)) {
        throw "Missing ESP-IDF partition parser: $genPart"
    }

    $csvPath = [System.IO.Path]::ChangeExtension($OutputPath, ".csv")
    @(
        "# Name, Type, SubType, Offset, Size"
        "nvs,data,nvs,0x9000,0x6000"
        "otadata,data,ota,0xf000,0x2000"
        "phy_init,data,phy,0x11000,0x1000"
        "ota_0,app,ota_0,0x20000,0x1B0000"
        "ota_1,app,ota_1,0x1D0000,0x1B0000"
    ) | Set-Content -LiteralPath $csvPath -Encoding UTF8

    $python = Get-TestPythonExe
    $output = @(& $python $genPart $csvPath $OutputPath 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to generate fake partition table:`n$($output -join "`n")"
    }
}

function New-FakeBuild {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Version,
        [bool]$CompleteFactoryArtifacts = $true
    )

    $buildDir = Join-Path $buildRoot $Name
    New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
    $description = [ordered]@{
        project_name = "voice-keyboard-firmware"
        project_version = $Version
        target = "esp32s3"
    }
    $description | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $buildDir "project_description.json") -Encoding UTF8
    [System.IO.File]::WriteAllBytes((Join-Path $buildDir "voice-keyboard-firmware.bin"), [byte[]](0xE9, 0x4C, 0x49, 0x53, 0x54, 0x45, 0x4E, 0x45, 0x52))

    if ($CompleteFactoryArtifacts) {
        $bootloaderDir = Join-Path $buildDir "bootloader"
        $partitionDir = Join-Path $buildDir "partition_table"
        New-Item -ItemType Directory -Force -Path $bootloaderDir, $partitionDir | Out-Null
        [System.IO.File]::WriteAllBytes((Join-Path $bootloaderDir "bootloader.bin"), [byte[]](0x42, 0x4F, 0x4F, 0x54))
        New-FakePartitionTable -OutputPath (Join-Path $partitionDir "partition-table.bin")
    }

    return $buildDir
}

function Invoke-OtaPackage {
    param(
        [Parameter(Mandatory = $true)][string]$BuildDir,
        [Parameter(Mandatory = $true)][string]$OutputRoot,
        [Parameter(Mandatory = $true)][string]$Channel
    )

    $output = @(& pwsh -NoProfile -File $packageScript -BuildDir $BuildDir -OutputRoot $OutputRoot -Channel $Channel 2>&1)
    return [PSCustomObject]@{
        ExitCode = $LASTEXITCODE
        Output = ($output -join "`n")
    }
}

function Assert-PackageFails {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$BuildDir,
        [Parameter(Mandatory = $true)][string]$Channel,
        [Parameter(Mandatory = $true)][string]$ExpectedText
    )

    $outputRoot = Join-Path $testRoot "out_$Name"
    $result = Invoke-OtaPackage -BuildDir $BuildDir -OutputRoot $outputRoot -Channel $Channel
    if ($result.ExitCode -eq 0) {
        throw "Expected '$Name' to fail but it passed:`n$($result.Output)"
    }
    if ($result.Output -notlike "*$ExpectedText*") {
        throw "Expected '$Name' failure to mention '$ExpectedText' but saw:`n$($result.Output)"
    }
    Write-Host "PASS expected fail: $Name -> $ExpectedText"
}

function Assert-CompletePackage {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$BuildDir,
        [Parameter(Mandatory = $true)][string]$Channel,
        [Parameter(Mandatory = $true)][string]$ExpectedVersion
    )

    $outputRoot = Join-Path $testRoot "out_$Name"
    $result = Invoke-OtaPackage -BuildDir $BuildDir -OutputRoot $outputRoot -Channel $Channel
    if ($result.ExitCode -ne 0) {
        throw "Expected '$Name' to pass but it failed:`n$($result.Output)"
    }

    $packageDir = Get-ChildItem -LiteralPath $outputRoot -Directory | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $packageDir) { throw "Expected '$Name' to create an OTA package directory." }

    $manifestPath = Join-Path $packageDir.FullName "ota_manifest.json"
    $otaBinPath = Join-Path $packageDir.FullName "firmware_ota.bin"
    if (-not (Test-Path -LiteralPath $manifestPath)) { throw "Missing OTA manifest for '$Name'." }
    if (-not (Test-Path -LiteralPath $otaBinPath)) { throw "Missing OTA binary for '$Name'." }

    $checkOutput = @(& pwsh -NoProfile -File $manifestCheckScript -ManifestPath $manifestPath 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "Generated manifest for '$Name' failed validation:`n$($checkOutput -join "`n")"
    }

    $manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
    if ($manifest.channel -ne $Channel) { throw "Expected channel '$Channel' but saw '$($manifest.channel)'." }
    if ($manifest.firmware.version -ne $ExpectedVersion) { throw "Expected version '$ExpectedVersion' but saw '$($manifest.firmware.version)'." }

    $factoryRoot = Join-Path $packageDir.FullName "factory"
    $factoryPackage = Get-ChildItem -LiteralPath $factoryRoot -Directory | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $factoryPackage) { throw "Missing nested factory package for '$Name'." }
    foreach ($required in @("manifest.json", "FLASHING.md", "bootloader.bin", "partition-table.bin", "voice-keyboard-firmware.bin")) {
        if (-not (Test-Path -LiteralPath (Join-Path $factoryPackage.FullName $required))) {
            throw "Nested factory package for '$Name' is missing $required."
        }
    }

    Write-Host "PASS complete package: $Name"
}

$dirtyVersionBuild = New-FakeBuild -Name "dirty-version" -Version "review-dirty" -CompleteFactoryArtifacts $true
Assert-PackageFails -Name "stable_dirty_version" -BuildDir $dirtyVersionBuild -Channel "stable" -ExpectedText "requires a clean"

$devVersionBuild = New-FakeBuild -Name "dev-version" -Version "0.1.0-dev" -CompleteFactoryArtifacts $true
Assert-PackageFails -Name "beta_dev_version" -BuildDir $devVersionBuild -Channel "beta" -ExpectedText "requires a clean"

$missingFactoryBuild = New-FakeBuild -Name "missing-factory" -Version "1.2.3" -CompleteFactoryArtifacts $false
Assert-PackageFails -Name "stable_missing_factory_artifacts" -BuildDir $missingFactoryBuild -Channel "stable" -ExpectedText "Factory package generation failed"

$stableBuild = New-FakeBuild -Name "stable-complete" -Version "1.2.3" -CompleteFactoryArtifacts $true
Assert-CompletePackage -Name "stable_complete" -BuildDir $stableBuild -Channel "stable" -ExpectedVersion "1.2.3"

$betaBuild = New-FakeBuild -Name "beta-complete" -Version "1.2.4-beta.1" -CompleteFactoryArtifacts $true
Assert-CompletePackage -Name "beta_complete" -BuildDir $betaBuild -Channel "beta" -ExpectedVersion "1.2.4-beta.1"

Write-Host "PASS: OTA release package rules completed."
