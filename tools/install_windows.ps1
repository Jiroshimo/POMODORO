param(
    [string]$Msys2Root = $(if ($env:MSYS2_ROOT) { $env:MSYS2_ROOT } else { 'C:\msys64' }),
    [string]$StateFile,
    [switch]$InstallDependencies,
    [switch]$Launch
)
$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path $PSScriptRoot -Parent
$installPath = Join-Path $env:LOCALAPPDATA 'Programs\QuietPomodoro'
$bash = Join-Path $Msys2Root 'usr\bin\bash.exe'
if (!(Test-Path $bash)) { throw 'Install MSYS2 from https://www.msys2.org/ first, then rerun this installer.' }
$env:MSYSTEM = 'UCRT64'
$env:CHERE_INVOKING = '1'

function Invoke-Msys([string]$Command) {
    & $bash --login -c $Command
    if ($LASTEXITCODE -ne 0) { throw "MSYS2 command failed ($LASTEXITCODE): $Command" }
}
if ($InstallDependencies) {
    # A core MSYS2 update may end the first shell. Start a new shell for the second pass.
    Invoke-Msys 'pacman -Syu --noconfirm'
    Invoke-Msys 'pacman -Syu --noconfirm'
    Invoke-Msys 'pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-gtk3'
}
foreach ($tool in @('cmake.exe', 'ninja.exe', 'pkgconf.exe', 'libgtk-3-0.dll')) {
    if (!(Test-Path (Join-Path $Msys2Root "ucrt64\bin\$tool"))) {
        throw "Missing $tool. Rerun install-windows.bat to install the build dependencies."
    }
}
$running = Get-Process pomodoro -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq "$installPath\pomodoro.exe" }
if ($running) { throw 'Close the Windows Pomodoro app before reinstalling.' }

$work = Join-Path ([IO.Path]::GetTempPath()) ('QuietPomodoro-' + [Guid]::NewGuid().ToString('N'))
$nativeSource = Join-Path $work 'source'
$stage = Join-Path $work 'app'
New-Item -ItemType Directory -Path $nativeSource -Force | Out-Null
try {
    # Build on the Windows filesystem, including when the source is accessed over WSL's UNC share.
    foreach ($item in @('CMakeLists.txt', 'src', 'assets', 'tests', 'tools')) {
        Copy-Item -LiteralPath (Join-Path $sourceRoot $item) -Destination $nativeSource -Recurse
    }
    $cygpath = Join-Path $Msys2Root 'usr\bin\cygpath.exe'
    $unixSource = (& $cygpath -u $nativeSource).Trim()
    $unixStage = (& $cygpath -u $stage).Trim()
    & $bash --login "$unixSource/tools/build_windows.sh" $unixSource $unixStage
    if ($LASTEXITCODE -ne 0) { throw "Windows build or tests failed ($LASTEXITCODE). Build files: $work" }

    New-Item -ItemType Directory -Path (Split-Path $installPath -Parent) -Force | Out-Null
    $backup = Join-Path $work 'previous-install'
    if (Test-Path $installPath) { Move-Item -LiteralPath $installPath -Destination $backup }
    try { Move-Item -LiteralPath $stage -Destination $installPath }
    catch {
        if (Test-Path $backup) { Move-Item -LiteralPath $backup -Destination $installPath }
        throw
    }
    # Image loader caches contain absolute filenames: generate after moving to the final path.
    $loaderDir = Join-Path $installPath 'lib\gdk-pixbuf-2.0\2.10.0\loaders'
    if (Test-Path $loaderDir) {
        $oldModuleDir = $env:GDK_PIXBUF_MODULEDIR
        try {
            $env:GDK_PIXBUF_MODULEDIR = $loaderDir
            $cache = & (Join-Path $Msys2Root 'ucrt64\bin\gdk-pixbuf-query-loaders.exe')
            if ($LASTEXITCODE -ne 0) { throw 'Unable to generate the installed image loader cache.' }
            [IO.File]::WriteAllLines((Join-Path (Split-Path $loaderDir -Parent) 'loaders.cache'), [string[]]$cache, (New-Object Text.UTF8Encoding $false))
        } finally { $env:GDK_PIXBUF_MODULEDIR = $oldModuleDir }
    }
    $statePath = Join-Path $env:LOCALAPPDATA 'quiet-pomodoro\state.ini'
    if ($StateFile -and !(Test-Path $statePath)) {
        New-Item -ItemType Directory -Path (Split-Path $statePath -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $StateFile -Destination $statePath
        Write-Host 'Copied existing Pomodoro tasks and settings to Windows.'
    }
    $shell = New-Object -ComObject WScript.Shell
    foreach ($folder in @([Environment]::GetFolderPath('Desktop'), [Environment]::GetFolderPath('Programs'))) {
        $shortcut = $shell.CreateShortcut((Join-Path $folder 'Quiet Pomodoro.lnk'))
        $shortcut.TargetPath = Join-Path $installPath 'pomodoro.exe'
        $shortcut.WorkingDirectory = $installPath
        $shortcut.IconLocation = "$installPath\pomodoro.exe,0"
        $shortcut.Description = 'Quiet Pomodoro - focus timer and session tasks'
        $shortcut.Save()
    }
    Write-Host "Installed: $installPath\pomodoro.exe"
    Write-Host 'Created Quiet Pomodoro shortcuts on the Desktop and in the Start Menu.'
    if ($Launch) { Start-Process -FilePath (Join-Path $installPath 'pomodoro.exe') -WorkingDirectory $installPath }
} catch {
    Write-Host "Build and diagnostic files retained at: $work"
    throw
}
# Remove only this run's temporary build and previous installation, after success.
Remove-Item -LiteralPath $work -Recurse -Force
