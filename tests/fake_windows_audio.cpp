// Validates the real subprocess protocol without Windows or speaker access.
#include "audio_samples.hpp"
#include <gio/gio.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    const char* mode = g_getenv("FAKE_WINDOWS_AUDIO_RESULT");
    if (mode && std::strcmp(mode, "early-failure") == 0) {
        std::cerr << "Windows interop unavailable\n";
        return 7;
    }
    const char* encoded = nullptr;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "-EncodedCommand") == 0) encoded = argv[i + 1];
    if (!encoded) return 2;
    gsize length = 0;
    guchar* utf16 = g_base64_decode(encoded, &length);
    gchar* decoded = g_convert(reinterpret_cast<const char*>(utf16), length, "UTF-8", "UTF-16LE", nullptr, nullptr, nullptr);
    g_free(utf16);
    if (!decoded) return 3;
    const std::string script = decoded;
    g_free(decoded);
    const auto nativeCall = script.find("[PomodoroWindowsAudio]::PlaySound");
    const auto ack = script.find("[Console]::Out.WriteLine('POMODORO_WINDOWS_AUDIO_OK')");
    if (script.find("ReadAllBytes($wavPath)") == std::string::npos || script.find("OpenStandardInput()") != std::string::npos ||
        script.find("EntryPoint=\"PlaySoundW\"") == std::string::npos ||
        nativeCall == std::string::npos || ack == std::string::npos || nativeCall >= ack ||
        script.find("[uint32]6") == std::string::npos || script.find("exit 1") == std::string::npos) return 4;
    const auto start = script.find("FromBase64String('");
    if (start == std::string::npos) return 5;
    const auto end = script.find("'", start + 18);
    if (end == std::string::npos) return 5;
    const auto encodedPath = script.substr(start + 18, end - start - 18);
    guchar* pathBytes = g_base64_decode(encodedPath.c_str(), &length);
    std::string path(reinterpret_cast<const char*>(pathBytes), length);
    g_free(pathBytes);
    const std::string prefix = std::string("\\\\wsl.localhost\\") + g_getenv("WSL_DISTRO_NAME");
    if (path.compare(0, prefix.size(), prefix) != 0) return 5;
    path.erase(0, prefix.size());
    std::replace(path.begin(), path.end(), '\\', '/');
    gchar* data = nullptr;
    if (!g_file_get_contents(path.c_str(), &data, &length, nullptr)) return 5;
    const std::vector<unsigned char> received(data, data + length);
    g_free(data);
    if (received != quiet::sound::wav(quiet::sound::samples(!(mode && std::strcmp(mode, "warning") == 0)))) return 5;
    if (mode && (std::strcmp(mode, "failure") == 0 || std::strcmp(mode, "fallback") == 0 ||
                 std::strcmp(mode, "both-fail") == 0 || std::strcmp(mode, "preferred-windows") == 0)) {
        std::cerr << "Windows test output refused audio\n";
        return 7;
    }
    if (!(mode && (std::strcmp(mode, "no-ack") == 0 || std::strcmp(mode, "no-ack-fallback") == 0)))
        std::cout << "POMODORO_WINDOWS_AUDIO_OK\n";
    return 0;
}
