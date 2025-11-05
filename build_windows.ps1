param(
    [ValidateSet("Debug","Release","RelWithDebInfo","MinSizeRel")]
    [string]$Configuration = "RelWithDebInfo",

    [string]$Generator
)

$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $scriptRoot

$buildRoot = Join-Path $scriptRoot "build\windows\$Configuration"

Write-Host "================================"
Write-Host "Building fbpp (Windows) with configuration: $Configuration"
Write-Host "================================"

if (Test-Path $buildRoot) {
    Write-Host "Cleaning build directory..."
    Remove-Item $buildRoot -Recurse -Force
}

if (Test-Path "$scriptRoot\CMakeUserPresets.json") {
    Remove-Item "$scriptRoot\CMakeUserPresets.json" -Force
}

if (-not (Test-Path "C:\fb50")) {
    throw "Firebird runtime not found in C:\fb50 - update the path in build_windows.ps1 if it is installed elsewhere."
}

# Ensure test configuration is available where test binaries expect it.
$buildWindowsRoot = Join-Path $scriptRoot "build\windows"
if (-not (Test-Path $buildWindowsRoot)) {
    New-Item -ItemType Directory -Path $buildWindowsRoot -Force | Out-Null
}
if (Test-Path (Join-Path $scriptRoot "config\test_config.json")) {
    $configSource = Join-Path $scriptRoot "config\test_config.json"
    $configDest = Join-Path $buildWindowsRoot "config"
    if (-not (Test-Path $configDest)) {
        New-Item -ItemType Directory -Path $configDest -Force | Out-Null
    }
    Copy-Item -Path $configSource -Destination (Join-Path $configDest "test_config.json") -Force

    $configDestConfig = Join-Path $buildRoot "config"
    if (-not (Test-Path $configDestConfig)) {
        New-Item -ItemType Directory -Path $configDestConfig -Force | Out-Null
    }
    Copy-Item -Path $configSource -Destination (Join-Path $configDestConfig "test_config.json") -Force
}

$env:FIREBIRD = "C:/fb50"

if (-not ($env:PATH -split ";" | Where-Object { $_ -ieq "C:\fb50\bin" })) {
    $env:PATH = "C:\fb50\bin;$env:PATH"
}

Write-Host "Installing Conan dependencies..."
conan install . `
    --output-folder="$buildRoot" `
    --build=missing `
    -s build_type=$Configuration `
    -c tools.cmake.cmaketoolchain:generator="Ninja"

$toolchainCandidates = @(
    (Join-Path $buildRoot "generators\conan_toolchain.cmake"),
    (Join-Path $buildRoot "build\$Configuration\generators\conan_toolchain.cmake"),
    (Join-Path $buildRoot "conan_toolchain.cmake")
)
$toolchain = $toolchainCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $toolchain) {
    throw "Unable to locate conan_toolchain.cmake under $buildRoot"
}

Write-Host "Configuring CMake..."
$cmakeArgs = @(
    "-S", $scriptRoot,
    "-B", $buildRoot,
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
    "-DBUILD_TESTING=ON",
    "-DBUILD_EXAMPLES=ON",
    "-DFBPP_BUILD_LIBS=ON",
    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
    "-DCMAKE_BUILD_TYPE=$Configuration"
)

if ($Generator) {
    $cmakeArgs += @("-G", $Generator)
}

cmake @cmakeArgs

$cacheFile = Join-Path $buildRoot "CMakeCache.txt"
$multiConfig = $false
if (Test-Path $cacheFile) {
    $multiConfig = Select-String -Path $cacheFile -Pattern "CMAKE_CONFIGURATION_TYPES" -Quiet
}

Write-Host "Building project..."
$buildArgs = @("--build", $buildRoot)
if ($multiConfig) {
    $buildArgs += @("--config", $Configuration)
}

cmake @buildArgs

Write-Host "Building examples..."
$exampleTargets = @(
    "01_basic_operations",
    "02_json_operations",
    "03_batch_simple",
    "04_batch_advanced",
    "06_named_parameters",
    "07_cancel_operation",
    "07_cancel_test_variants",
    "test_prepare_transaction",
    "test_statement_free",
    "test_statement_refcount"
)
foreach ($target in $exampleTargets) {
    $exampleArgs = @("--build", $buildRoot, "--target", $target)
    if ($multiConfig) {
        $exampleArgs += @("--config", $Configuration)
    }
    cmake @exampleArgs
}

Write-Host "Running tests..."
$ctestArgs = @("--test-dir", $buildRoot, "--output-on-failure")
if ($multiConfig) {
    $ctestArgs += @("--build-config", $Configuration)
}

ctest @ctestArgs

Write-Host "================================"
Write-Host "Windows build complete!"
Write-Host "================================"
