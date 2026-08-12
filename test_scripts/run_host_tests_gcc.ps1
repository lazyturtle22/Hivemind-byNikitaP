# Local host-test runner: compiles hive_core + Unity tests with native gcc.
# Fast red/green loop for machines without Docker; CI runs the same tests on
# the ESP-IDF linux target via run_host_tests.sh.
#
# Requires: gcc on PATH (MSYS2 works) and an ESP-IDF checkout for the vendored
# Unity sources (IDF_PATH, default C:\esp\esp-idf).
$ErrorActionPreference = "Stop"

$Root  = Split-Path $PSScriptRoot -Parent
$Idf   = if ($env:IDF_PATH) { $env:IDF_PATH } else { "C:\esp\esp-idf" }
$Unity = Join-Path $Idf "components\unity\unity\src"
if (-not (Test-Path (Join-Path $Unity "unity.c"))) {
    Write-Error "Unity sources not found at $Unity (set IDF_PATH)"
    exit 69
}

$OutDir = Join-Path $Root "host_test\build_gcc"
New-Item -ItemType Directory -Force $OutDir | Out-Null
$Exe = Join-Path $OutDir "host_test.exe"

$Sources = @(
    Join-Path $Unity "unity.c"
    Get-ChildItem (Join-Path $Root "firmware\components\hive_core") -Filter *.c | ForEach-Object FullName
    Get-ChildItem (Join-Path $Root "host_test\main") -Filter *.c | ForEach-Object FullName
)

Write-Host "gcc: building host tests -> $Exe"
& gcc -std=c11 -Wall -Wextra -O1 `
    -I (Join-Path $Root "firmware\components\hive_core\include") `
    -I $Unity `
    $Sources -o $Exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $Exe
exit $LASTEXITCODE
