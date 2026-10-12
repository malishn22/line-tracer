# One-command release: build -> unit tests -> package dist\LineTracer-Setup.exe
# Usage:  .\make.ps1 [-SkipTests] [-Clean]      (or double-click make.bat)
# Stops at the first failing step with a non-zero exit code.
param(
    [switch]$SkipTests,
    [switch]$Clean
)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

function Step([string]$msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Run-Cmd([string]$cmdline, [string]$what) {
    & cmd.exe /c $cmdline
    if ($LASTEXITCODE -ne 0) { throw "$what failed (exit code $LASTEXITCODE)." }
}

try {
    $dist  = Join-Path $PSScriptRoot 'dist'
    $stage = Join-Path $dist 'stage'
    $setup = Join-Path $dist 'LineTracer-Setup.exe'

    if ($Clean) {
        Step 'Cleaning build\ and dist\'
        Remove-Item (Join-Path $PSScriptRoot 'build'), $dist -Recurse -Force -ErrorAction SilentlyContinue
    }

    $env:NOPAUSE = '1'   # build.bat must not wait for a keypress when called from here
    Step 'Building LineTracer.exe'
    Run-Cmd '.\build.bat app' 'Build'
    if ($SkipTests) { Write-Host '(tests skipped)' -ForegroundColor Yellow }
    else {
        Step 'Running unit tests'
        Run-Cmd '.\build.bat test' 'Unit tests'
    }

    $exe = Join-Path $PSScriptRoot 'build\LineTracer.exe'
    if (-not (Test-Path $exe)) { throw 'build\LineTracer.exe was not produced.' }

    Step 'Staging installer files'
    Remove-Item $dist -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $stage | Out-Null
    Copy-Item $exe $stage
    Copy-Item (Join-Path $PSScriptRoot 'installer\install.ps1') $stage

    Step 'Generating IExpress configuration'
    $sedText = (Get-Content (Join-Path $PSScriptRoot 'installer\LineTracer.sed.template') -Raw) `
        -replace '\{\{TARGET\}\}', $setup.Replace('$', '$$') `
        -replace '\{\{STAGE\}\}',  ($stage + '\').Replace('$', '$$') `
        -replace "`r?`n", "`r`n"   # IExpress's INI parser wants CRLF
    $sedPath = Join-Path $dist 'LineTracer.sed'
    Set-Content -Path $sedPath -Value $sedText -Encoding Ascii

    Step 'Packaging with IExpress'
    $iexpress = Join-Path $env:WINDIR 'System32\iexpress.exe'
    if (-not (Test-Path $iexpress)) { throw 'iexpress.exe not found (it ships with Windows).' }
    # IExpress is a GUI-subsystem program and its exit code is unreliable, so the output file is the check.
    # Do not pass -WindowStyle Hidden: IExpress silently fails to build when launched hidden.
    $p = Start-Process -FilePath $iexpress -ArgumentList '/N', '/Q', $sedPath -PassThru
    if (-not $p.WaitForExit(120000)) { $p.Kill(); throw 'IExpress timed out after 120 s.' }
    if (-not (Test-Path $setup)) { throw 'IExpress did not produce LineTracer-Setup.exe (check dist\LineTracer.sed).' }

    $size = [math]::Round((Get-Item $setup).Length / 1KB)
    $hash = (Get-FileHash $setup -Algorithm SHA256).Hash
    Write-Host ''
    Write-Host 'SUCCESS' -ForegroundColor Green
    Write-Host "  Installer : $setup ($size KB)"
    Write-Host "  SHA-256   : $hash"
    exit 0
}
catch {
    Write-Host ''
    Write-Host "FAILED: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
