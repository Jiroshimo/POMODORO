param([string]$InstallPath = (Join-Path $env:LOCALAPPDATA 'Programs\Stash'))
$ErrorActionPreference = 'Stop'
$exe = Join-Path $InstallPath 'stash.exe'
if (!(Test-Path -LiteralPath $exe)) { throw 'Install Stash with install-windows.bat first.' }
$sourceIcon = Join-Path (Split-Path $PSScriptRoot -Parent) 'assets\stash.ico'
$hash = (Get-FileHash -LiteralPath $sourceIcon -Algorithm SHA256).Hash
$iconPath = Join-Path $InstallPath ("stash-" + $hash.Substring(0, 12).ToLowerInvariant() + '.ico')
if (!(Test-Path -LiteralPath $iconPath) -or (Get-FileHash -LiteralPath $iconPath).Hash -ne $hash) {
    Copy-Item -LiteralPath $sourceIcon -Destination $iconPath -Force
}

$shell = New-Object -ComObject WScript.Shell
$desktop = [Environment]::GetFolderPath('Desktop')
$programs = [Environment]::GetFolderPath('Programs')
$paths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($folder in @($desktop, $programs)) {
    $null = $paths.Add((Join-Path $folder 'Stash.lnk'))
}
$legacyExe = Join-Path $env:LOCALAPPDATA 'Programs\QuietPomodoro\pomodoro.exe'
$pinned = Join-Path $env:APPDATA 'Microsoft\Internet Explorer\Quick Launch\User Pinned'
foreach ($folder in @($desktop, $programs, $pinned)) {
    if (!(Test-Path -LiteralPath $folder)) { continue }
    $links = if ($folder -eq $pinned) {
        Get-ChildItem -LiteralPath $folder -Filter '*.lnk' -File -Recurse
    } else {
        Get-ChildItem -LiteralPath $folder -Filter '*.lnk' -File
    }
    foreach ($link in $links) {
        $shortcut = $shell.CreateShortcut($link.FullName)
        if ($shortcut.TargetPath -eq $exe -or $shortcut.TargetPath -eq $legacyExe) {
            $null = $paths.Add($link.FullName)
        }
    }
}

# Notify Explorer using the supported shell API; keep running apps open.
# https://learn.microsoft.com/windows/win32/api/shlobj_core/nf-shlobj_core-shchangenotify
Add-Type @'
using System;
using System.Runtime.InteropServices;
namespace Stash {
    public static class IconRefresh {
        [DllImport("shell32.dll", EntryPoint = "SHChangeNotify", CharSet = CharSet.Unicode, ExactSpelling = true)]
        public static extern void NotifyPath(int eventId, uint flags, string path, IntPtr unused);
        [DllImport("shell32.dll", EntryPoint = "SHChangeNotify", ExactSpelling = true)]
        public static extern void NotifyCache(int eventId, uint flags, IntPtr item1, IntPtr item2);
    }
}
'@
foreach ($path in $paths) {
    $exists = Test-Path -LiteralPath $path
    $shortcut = $shell.CreateShortcut($path)
    $shortcut.TargetPath = $exe
    $shortcut.WorkingDirectory = $InstallPath
    $shortcut.IconLocation = "$iconPath,0"
    $shortcut.Description = 'Stash - focus timer and session tasks'
    $shortcut.Save()
    $saved = $shell.CreateShortcut($path)
    if ($saved.TargetPath -ne $exe -or $saved.IconLocation -ne "$iconPath,0") {
        throw "Could not update the Windows icon: $path"
    }
    # SHCNE_UPDATEITEM or SHCNE_CREATE; SHCNF_PATHW | SHCNF_FLUSH.
    $eventId = if ($exists) { 0x2000 } else { 0x2 }
    [Stash.IconRefresh]::NotifyPath($eventId, 0x1005, $path, [IntPtr]::Zero)
    Write-Host "Updated hourglass icon: $path"
}
# SHCNE_ASSOCCHANGED | SHCNF_FLUSH invalidates Explorer's cached icon images.
[Stash.IconRefresh]::NotifyCache(0x08000000, 0x1000, [IntPtr]::Zero, [IntPtr]::Zero)
Write-Host 'Refreshed Windows shell icons with the current hourglass artwork.'
