# LineTracer per-user installer (no admin rights needed).
# Run by the IExpress setup exe after it extracts LineTracer.exe next to this script.
# Test switches let it run without touching the real profile:  -InstallDir X -NoShortcuts -NoRegister -Silent
param(
    [string]$InstallDir = (Join-Path $env:LOCALAPPDATA 'Programs\LineTracer'),
    [switch]$NoShortcuts,
    [switch]$NoRegister,
    [switch]$Silent
)
$ErrorActionPreference = 'Stop'
$AppName = 'LineTracer'
$Version = '1.0.0'
$RegKey  = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$AppName"
$StartMenu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs'
$Shortcut  = Join-Path $StartMenu "$AppName.lnk"

function Show([string]$text, [string]$icon = 'Information', [string]$buttons = 'OK') {
    if ($Silent) { Write-Host $text; return 'OK' }
    Add-Type -AssemblyName System.Windows.Forms
    return [System.Windows.Forms.MessageBox]::Show($text, "$AppName Setup", $buttons, $icon).ToString()
}

# The uninstaller written next to the installed exe.
$UninstallScript = @'
param([switch]$Silent)
$ErrorActionPreference = 'Stop'
$dir = $PSScriptRoot
$exe = Join-Path $dir 'LineTracer.exe'
Get-Process LineTracer -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe } | Stop-Process -Force
Remove-Item (Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\LineTracer.lnk') -ErrorAction SilentlyContinue
Remove-Item 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\LineTracer' -Recurse -ErrorAction SilentlyContinue
# Safety: only delete a folder that really is an install dir.
if ((Split-Path $dir -Leaf) -eq 'LineTracer' -and (Test-Path $exe)) {
    Start-Process cmd.exe -WindowStyle Hidden -ArgumentList '/c', "ping 127.0.0.1 -n 3 >nul & rmdir /s /q `"$dir`""
}
if (-not $Silent) {
    Add-Type -AssemblyName System.Windows.Forms
    [void][System.Windows.Forms.MessageBox]::Show("LineTracer was removed.`nYour recordings in Documents\LineTrace were kept.", 'LineTracer Uninstall')
}
'@

try {
    $srcExe = Join-Path $PSScriptRoot 'LineTracer.exe'
    if (-not (Test-Path $srcExe)) { throw "LineTracer.exe not found next to the installer ($PSScriptRoot)." }

    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    $dstExe = Join-Path $InstallDir 'LineTracer.exe'

    # Close a running copy from the install dir so the file can be replaced (upgrade case).
    Get-Process LineTracer -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $dstExe } | Stop-Process -Force
    Start-Sleep -Milliseconds 300

    Copy-Item $srcExe $dstExe -Force
    Set-Content -Path (Join-Path $InstallDir 'uninstall.ps1') -Value $UninstallScript -Encoding UTF8

    if (-not $NoShortcuts) {
        $ws = New-Object -ComObject WScript.Shell
        $lnk = $ws.CreateShortcut($Shortcut)
        $lnk.TargetPath = $dstExe
        $lnk.WorkingDirectory = $InstallDir
        $lnk.Description = 'Record mouse movement as glowing line art'
        $lnk.Save()
    }

    if (-not $NoRegister) {
        New-Item -Path $RegKey -Force | Out-Null
        $uninst = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$(Join-Path $InstallDir 'uninstall.ps1')`""
        $sizeKb = [int]((Get-Item $dstExe).Length / 1KB)
        $props = @{
            DisplayName = $AppName; DisplayVersion = $Version; Publisher = 'Personal'
            InstallLocation = $InstallDir; DisplayIcon = $dstExe; UninstallString = $uninst
        }
        foreach ($k in $props.Keys) { New-ItemProperty -Path $RegKey -Name $k -Value $props[$k] -Force | Out-Null }
        New-ItemProperty -Path $RegKey -Name NoModify -Value 1 -PropertyType DWord -Force | Out-Null
        New-ItemProperty -Path $RegKey -Name NoRepair -Value 1 -PropertyType DWord -Force | Out-Null
        New-ItemProperty -Path $RegKey -Name EstimatedSize -Value $sizeKb -PropertyType DWord -Force | Out-Null
    }

    $where = if ($NoShortcuts) { $InstallDir } else { "$InstallDir`n`nFind it in the Start menu as '$AppName'." }
    $answer = Show "$AppName $Version installed to:`n$where`n`nLaunch it now?" 'Information' 'YesNo'
    if ($answer -eq 'Yes') { Start-Process $dstExe -WorkingDirectory $InstallDir }
    exit 0
}
catch {
    [void](Show "Installation failed:`n$($_.Exception.Message)" 'Error')
    exit 1
}
