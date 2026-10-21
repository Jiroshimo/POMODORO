#pragma once

namespace quiet::sound {
inline constexpr const char* windowsAudioAck = "POMODORO_WINDOWS_AUDIO_OK";
// The caller passes a base64-encoded Windows path, never shell-interpolated text.
// WinMM's actual return value is checked before acknowledging playback.
inline constexpr const char* windowsAudioScript = R"POWERSHELL(
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
try {
    [byte[]]$wav = [IO.File]::ReadAllBytes($wavPath)
    if ($wav.Length -lt 44) { throw 'No WAV audio received from POMODORO.' }
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class PomodoroWindowsAudio {
    [DllImport("winmm.dll", EntryPoint="PlaySoundW", ExactSpelling=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool PlaySound([In] byte[] sound, IntPtr module, uint flags);
}
'@
    // SND_MEMORY | SND_NODEFAULT; synchronous playback keeps the buffer alive.
    if (-not [PomodoroWindowsAudio]::PlaySound($wav, [IntPtr]::Zero, [uint32]6)) {
        throw 'Windows rejected audio playback. Check the Windows output device and Volume mixer.'
    }
    [Console]::Out.WriteLine('POMODORO_WINDOWS_AUDIO_OK')
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
)POWERSHELL";
} // namespace quiet::sound
