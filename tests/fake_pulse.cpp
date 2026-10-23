// A hardware-free PulseAudio server boundary for the real dynamic playback
// path. It validates the PCM contract and requires both writing and draining.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>

namespace {
struct SampleSpec { int format; std::uint32_t rate; std::uint8_t channels; };
struct Device { bool written = false; } device;
}
extern "C" {
int pa_parse_sample_format(const char* text) { return std::strcmp(text, "s16le") == 0 ? 3 : 4; }
const char* pa_strerror(int) { return "Invalid PulseAudio playback contract"; }
void* pa_simple_new(const char* server, const char* name, int direction, const char*, const char*,
                    const SampleSpec* spec, const void*, const void*, int* error) {
    const char* mode = std::getenv("FAKE_WINDOWS_AUDIO_RESULT");
    const bool valid = !(mode && (std::strcmp(mode, "both-fail") == 0 || std::strcmp(mode, "preferred-linux") == 0)) &&
        server && std::strcmp(server, "unix:/tmp/stash-pulse-test") == 0 && name &&
        direction == 1 && spec && (spec->format == 3 || spec->format == 4) && spec->rate == 44100 && spec->channels == 1;
    *error = valid ? 0 : 1;
    device.written = false;
    return valid ? &device : nullptr;
}
int pa_simple_write(void* handle, const void* data, std::size_t bytes, int* error) {
    const auto* samples = static_cast<const std::int16_t*>(data);
    bool audible = false;
    if (data) for (std::size_t i = 0; i < bytes / 2; ++i) if (samples[i] > 7000 || samples[i] < -7000) audible = true;
    const bool valid = handle == &device && bytes == 59535 * 2 && audible;
    device.written = valid;
    *error = valid ? 0 : 1;
    return valid ? 0 : -1;
}
int pa_simple_drain(void* handle, int* error) {
    const bool valid = handle == &device && device.written;
    *error = valid ? 0 : 1;
    return valid ? 0 : -1;
}
void pa_simple_free(void*) {}
}
