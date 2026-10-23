param([string]$InstallPath = (Join-Path $env:LOCALAPPDATA 'Programs\Stash'))
$ErrorActionPreference = 'Stop'
$exe = Join-Path $InstallPath 'stash.exe'
$version = (Get-Item -LiteralPath $exe).VersionInfo
if ($version.ProductName -ne 'Stash') { throw 'The installed executable does not identify itself as Stash.' }
$cmake = Get-Content -LiteralPath (Join-Path (Split-Path $PSScriptRoot -Parent) 'CMakeLists.txt') -Raw
$expectedVersion = [regex]::Match($cmake, 'project\(Stash VERSION ([0-9.]+)').Groups[1].Value
if (!$expectedVersion -or $version.ProductVersion -ne $expectedVersion) {
    throw "Expected Stash $expectedVersion; installed version is $($version.ProductVersion)."
}
Write-Host "Verified Stash version $($version.ProductVersion)."
$sourceIcon = Join-Path (Split-Path $PSScriptRoot -Parent) 'assets\stash.ico'
$iconHash = (Get-FileHash -LiteralPath $sourceIcon -Algorithm SHA256).Hash
$shortcutIcon = Join-Path $InstallPath ("stash-" + $iconHash.Substring(0, 12).ToLowerInvariant() + '.ico')
if (!(Test-Path -LiteralPath $shortcutIcon) -or
    (Get-FileHash -LiteralPath $shortcutIcon -Algorithm SHA256).Hash -ne $iconHash) {
    throw 'The installed shortcut icon does not match the current hourglass artwork.'
}
$work = Join-Path ([IO.Path]::GetTempPath()) ('Stash-verify-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $work 'docs') -Force | Out-Null
Add-Type -AssemblyName System.Drawing
$icon = [System.Drawing.Icon]::new($shortcutIcon, 32, 32)
try {
    $bitmap = $icon.ToBitmap()
    try { $bitmap.Save((Join-Path $work 'hourglass.png'), [System.Drawing.Imaging.ImageFormat]::Png) }
    finally { $bitmap.Dispose() }
} finally { $icon.Dispose() }
Write-Host 'Verified the Windows hourglass icon renders correctly.'
$statePath = Join-Path $env:LOCALAPPDATA 'quiet-pomodoro\state.ini'
$stateHash = if (Test-Path $statePath) { (Get-FileHash -LiteralPath $statePath).Hash } else { $null }
# Test the packaged application without access to build tools or external GTK runtimes.
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
foreach ($variable in @('GTK_PATH', 'GTK_EXE_PREFIX', 'GTK_DATA_PREFIX', 'GDK_PIXBUF_MODULEDIR',
                        'GDK_PIXBUF_MODULE_FILE', 'GSETTINGS_SCHEMA_DIR', 'FONTCONFIG_FILE',
                        'FONTCONFIG_PATH', 'WSL_DISTRO_NAME', 'WSL_INTEROP', 'MSYSTEM')) {
    [Environment]::SetEnvironmentVariable($variable, $null, 'Process')
}
$env:QUIET_SMOKE_CONFIG = Join-Path $work 'config'
$env:XDG_CONFIG_HOME = $env:QUIET_SMOKE_CONFIG
function Test-App([string]$Name, [string[]]$Arguments) {
    $stdout = Join-Path $work "$Name.stdout.log"
    $stderr = Join-Path $work "$Name.stderr.log"
    $process = Start-Process -FilePath $exe -ArgumentList $Arguments -WorkingDirectory $work -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    # Keep the process handle open so Windows PowerShell can read the exit code after exit.
    $null = $process.Handle
    if (!$process.WaitForExit(30000)) {
        $process.Kill()
        throw "$Name timed out. Logs: $work"
    }
    Get-Content -LiteralPath $stdout
    Get-Content -LiteralPath $stderr
    if ($process.ExitCode -ne 0) { throw "$Name failed ($($process.ExitCode)). Logs: $work" }
}
Test-App 'sound' @('--test-sound', '--output=windows')
Test-App 'desktop' @('--smoke-test')
if (!(Test-Path (Join-Path $work 'config\quiet-pomodoro\state.ini'))) { throw 'Smoke test did not save in its isolated configuration folder.' }
$afterHash = if (Test-Path $statePath) { (Get-FileHash -LiteralPath $statePath).Hash } else { $null }
if ($stateHash -ne $afterHash) { throw 'The desktop smoke test changed the real Windows saved state.' }
$shell = New-Object -ComObject WScript.Shell
foreach ($folder in @([Environment]::GetFolderPath('Desktop'), [Environment]::GetFolderPath('Programs'))) {
    $shortcutPath = Join-Path $folder 'Stash.lnk'
    if (!(Test-Path $shortcutPath)) { throw "Missing shortcut: $shortcutPath" }
    $shortcut = $shell.CreateShortcut($shortcutPath)
    if ($shortcut.TargetPath -ne $exe -or $shortcut.WorkingDirectory -ne $InstallPath -or
        $shortcut.IconLocation -ne "$shortcutIcon,0") {
        throw "Shortcut does not target the native installed application: $shortcutPath"
    }
    Write-Host "Verified shortcut: $shortcutPath -> $($shortcut.TargetPath)"
}
Write-Host "Native Windows verification passed. Logs and screenshots: $work"
