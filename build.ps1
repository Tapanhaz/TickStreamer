param(
    [Parameter(Position=0)]
    [ValidateSet("x64", "arm64")]
    [string]$TargetArch = "x64",
    
    [Parameter(Position=1)]
    [ValidateSet("Release", "Debug")]
    [string]$BuildType = "Release"
)

$ErrorActionPreference = "Stop"

Write-Host "Building for: $TargetArch ($BuildType)" -ForegroundColor Cyan

$VersionsFile = Join-Path $PSScriptRoot "versions.env"

if (-not (Test-Path $VersionsFile)) {
    Write-Error "versions.env not found. Cannot continue."
}

Get-Content $VersionsFile | ForEach-Object {
    if ($_ -match '^\s*([^#=]+)\s*=\s*(.+)\s*$') {
        Set-Item -Path env:$($matches[1]) -Value $matches[2]
    }
}


$boostVersion = $env:BOOST_VERSION
$boostVersionUnderscore = $boostVersion -replace '\.', '_'
$boostDir = Join-Path $PSScriptRoot "external\boost_$boostVersionUnderscore"
$boostUrl = "https://archives.boost.io/release/$boostVersion/source/boost_$boostVersionUnderscore.zip"


if (-not (Test-Path $boostDir)) {
    Write-Host "Downloading Boost $boostVersion (headers only, ~218MB)..." -ForegroundColor Yellow
    $boostZip = "$env:TEMP\boost.zip"
    
    Invoke-WebRequest -Uri $boostUrl -OutFile $boostZip -UseBasicParsing
    
    Write-Host "Extracting Boost..." -ForegroundColor Yellow
    Expand-Archive -Path $boostZip -DestinationPath "external" -Force
    Remove-Item $boostZip
    
    Write-Host "Boost headers ready (Beast/ASIO are header-only)" -ForegroundColor Green
}

$cmakeArch = if ($TargetArch -eq "x64") { "x64" } else { "ARM64" }
$buildSuffix = if ($TargetArch -eq "x64") { "win-x64" } else { "win-arm64" }

$msvcRuntime = if ($BuildType -eq "Debug") { "MultiThreadedDebug" } else { "MultiThreaded" }

Write-Host "Building zstd for $TargetArch..." -ForegroundColor Yellow
Push-Location external\zstd\build\VS2010
if (Test-Path "zstd-$buildSuffix") {
    Remove-Item -Recurse -Force "zstd-$buildSuffix"
}

Pop-Location
Push-Location external\zstd
if (Test-Path "build-$buildSuffix") {
    Remove-Item -Recurse -Force "build-$buildSuffix"
}
New-Item -ItemType Directory -Force -Path "build-$buildSuffix" | Out-Null
Push-Location "build-$buildSuffix"

cmake ..\build\cmake `
    -DCMAKE_BUILD_TYPE=$BuildType `
    -DBUILD_SHARED_LIBS=OFF `
    -DZSTD_BUILD_PROGRAMS=OFF `
    -DZSTD_BUILD_TESTS=OFF `
    -DCMAKE_MSVC_RUNTIME_LIBRARY=$msvcRuntime `
    -G "Visual Studio 17 2022" -A $cmakeArch

cmake --build . --config $BuildType --target libzstd_static

if (-not (Test-Path "lib\$BuildType\zstd_static.lib")) {
    Write-Host "ERROR: zstd build failed!" -ForegroundColor Red
    exit 1
}

Pop-Location
Pop-Location

Write-Host "Building simdjson for $TargetArch..." -ForegroundColor Yellow
Push-Location external\simdjson
if (Test-Path "build-$buildSuffix") {
    Remove-Item -Recurse -Force "build-$buildSuffix"
}
New-Item -ItemType Directory -Force -Path "build-$buildSuffix" | Out-Null
Push-Location "build-$buildSuffix"

cmake .. `
    -DCMAKE_POLICY_DEFAULT_CMP0091=NEW `
    -DCMAKE_BUILD_TYPE=$BuildType `
    -DBUILD_SHARED_LIBS=OFF `
    -DSIMDJSON_JUST_LIBRARY=ON `
    -DCMAKE_MSVC_RUNTIME_LIBRARY="$msvcRuntime" `
    -G "Visual Studio 17 2022" -A $cmakeArch

cmake --build . --config $BuildType

if (-not (Test-Path "$BuildType\simdjson.lib")) {
    Write-Host "ERROR: simdjson build failed!" -ForegroundColor Red
    exit 1
}

Pop-Location
Pop-Location

Write-Host "Building stream_ticks for $TargetArch..." -ForegroundColor Yellow

if (Test-Path "build-$buildSuffix") {
    Remove-Item -Recurse -Force "build-$buildSuffix"
}
New-Item -ItemType Directory -Force -Path "build-$buildSuffix" | Out-Null
Push-Location "build-$buildSuffix"

cmake .. `
    -DCMAKE_BUILD_TYPE=$BuildType `
    -DBoost_ROOT="$boostDir" `
    -DBoost_USE_STATIC_LIBS=ON `
    -DBUILD_STATIC=ON `
    -G "Visual Studio 17 2022" -A $cmakeArch

cmake --build . --config $BuildType

cpack -G ZIP -C $BuildType

Pop-Location

Write-Host ""
Write-Host "===================================" -ForegroundColor Green
Write-Host "Build complete!" -ForegroundColor Green
Write-Host "Binary: build-$buildSuffix\bin\$BuildType\stream_ticks.exe" -ForegroundColor Green
Write-Host "Package: build-$buildSuffix\*.zip" -ForegroundColor Green
Write-Host "===================================" -ForegroundColor Green

if (Test-Path "build-$buildSuffix\bin\$BuildType\stream_ticks.exe") {
    $exePath = "build-$buildSuffix\bin\$BuildType\stream_ticks.exe"
    Write-Host ""
    Write-Host "Binary info:" -ForegroundColor Cyan
    
    $size = (Get-Item $exePath).Length / 1MB
    Write-Host "Size: $([math]::Round($size, 2)) MB" -ForegroundColor Cyan
    
    Write-Host "`nDependencies:" -ForegroundColor Cyan
    
    if (Get-Command dumpbin -ErrorAction SilentlyContinue) {
        & dumpbin /dependents $exePath | Select-String "\.dll"
    } else {
        Write-Host "dumpbin not in PATH (skipping dependency check)" -ForegroundColor Yellow
    }
}