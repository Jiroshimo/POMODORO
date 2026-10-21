#include "audio.hpp"
#include "audio_samples.hpp"
#include <chrono>
#include <cstring>
#include <future>
#include <iostream>
#include <stdexcept>
#include <vector>

void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
void finish(quiet::Audio& audio) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (audio.playing() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(!audio.playing(), "audio worker finishes");
}
int main(int argc, char** argv) {
    if (argc == 2 && std::strncmp(argv[1], "--wsl-", 6) == 0) {
        const bool preferWindows = std::strcmp(argv[1], "--wsl-preferred-windows") == 0;
        const bool preferLinux = std::strcmp(argv[1], "--wsl-preferred-linux") == 0;
        quiet::Audio audio(quiet::AudioOutput::Automatic, preferWindows || preferLinux);
        const bool forced = std::strcmp(argv[1], "--wsl-failure") == 0 || std::strcmp(argv[1], "--wsl-no-ack") == 0 ||
            std::strcmp(argv[1], "--wsl-early-failure") == 0;
        if (forced) audio.setOutput(quiet::AudioOutput::Windows);
        if (preferWindows) audio.setOutput(quiet::AudioOutput::Windows);
        if (preferLinux) audio.setOutput(quiet::AudioOutput::Linux);
        audio.play(std::strcmp(argv[1], "--wsl-warning") != 0);
        finish(audio);
        const auto result = audio.result();
        if (std::strcmp(argv[1], "--wsl-route") == 0 || std::strcmp(argv[1], "--wsl-warning") == 0 || preferLinux)
            check(result.ok() && result.backend == "Windows audio (WSL)",
                  "default WSL output uses Windows even when PulseAudio accepts sound");
        else if (std::strcmp(argv[1], "--wsl-failure") == 0)
            check(!result.ok() && result.error.find("Windows test output refused audio") != std::string::npos,
                  "explicit Windows selection reports a Windows failure");
        else if (std::strcmp(argv[1], "--wsl-no-ack") == 0)
            check(!result.ok() && result.error.find("without a playback acknowledgment") != std::string::npos,
                  "successful process exit alone cannot claim successful Windows playback");
        else if (std::strcmp(argv[1], "--wsl-early-failure") == 0)
            check(!result.ok() && result.error.find("Windows interop unavailable") != std::string::npos &&
                  result.error.find("Broken pipe") == std::string::npos, "early helper exit retains the actual startup error");
        else if (std::strcmp(argv[1], "--wsl-fallback") == 0 || std::strcmp(argv[1], "--wsl-no-ack-fallback") == 0 || preferWindows)
            check(result.ok() && result.backend == "WSLg / PulseAudio",
                  "automatic output recovers through WSLg when the Windows bridge fails");
        else if (std::strcmp(argv[1], "--wsl-both-fail") == 0)
            check(!result.ok() && result.error.find("Windows test output refused audio") != std::string::npos &&
                  result.error.find("PulseAudio") != std::string::npos, "all output errors are retained when recovery fails");
        else check(false, "known WSL test case");
        if (forced) {
            audio.setOutput(quiet::AudioOutput::Linux);
            audio.play(true);
            finish(audio);
            check(!audio.failed() && audio.result().backend == "WSLg / PulseAudio", "changing output recovers after failure");
        }
        std::cout << "WSL output routing check passed\n";
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--native-route") == 0) {
        quiet::Audio audio(quiet::AudioOutput::Linux);
        audio.play(true);
        finish(audio);
        const auto result = audio.result();
        check(result.ok() && (result.backend == "PulseAudio" || result.backend == "WSLg / PulseAudio"),
              "Linux route writes and drains PCM through PulseAudio");
        std::cout << "Native PulseAudio routing check passed\n";
        return 0;
    }
    for (const bool completion : {false, true}) {
        const auto pcm = quiet::sound::samples(completion);
        check(pcm.size() == (completion ? 59535 : 28665), "cue duration and sample count");
        int peak = 0;
        double power = 0;
        for (const auto sample : pcm) { peak = std::max(peak, std::abs(static_cast<int>(sample))); power += double(sample) * sample; }
        check(peak > 7000 && peak < 30000 && std::sqrt(power / pcm.size()) > 1000, "audible non-silent signal without clipping");
        const auto wav = quiet::sound::wav(pcm);
        auto word = [&](int offset, int size) {
            std::uint32_t value = 0;
            for (int i = 0; i < size; ++i) value |= std::uint32_t(wav[offset + i]) << (8 * i);
            return value;
        };
        check(wav.size() == 44 + pcm.size() * 2 && std::memcmp(wav.data(), "RIFF", 4) == 0 &&
              std::memcmp(wav.data() + 8, "WAVEfmt ", 8) == 0 && std::memcmp(wav.data() + 36, "data", 4) == 0,
              "valid WAV container");
        check(word(4, 4) == wav.size() - 8 && word(40, 4) == pcm.size() * 2 && word(22, 2) == 1 &&
              word(24, 4) == 44100 && word(34, 2) == 16, "WAV lengths and 16-bit mono format");
        for (std::size_t i = 0; i < pcm.size(); ++i)
            check(word(44 + static_cast<int>(i) * 2, 2) == static_cast<std::uint16_t>(pcm[i]), "little-endian PCM data");
    }
    std::promise<void> entered, release;
    auto released = release.get_future().share();
    std::vector<bool> played;
    quiet::Audio queued([&](bool complete) {
        played.push_back(complete);
        if (played.size() == 1) { entered.set_value(); released.wait(); }
        return quiet::PlaybackResult{"test output", {}};
    });
    queued.play(false);
    check(entered.get_future().wait_for(std::chrono::seconds(1)) == std::future_status::ready, "playback runs off the caller thread");
    queued.play(true);
    queued.play(true);
    queued.play(true);
    queued.play(false);
    release.set_value();
    finish(queued);
    check(played.size() == 2 && !played[0] && played[1], "completion survives a busy worker and duplicate clicks coalesce");
    check(queued.result().backend == "test output" && !queued.failed(), "successful playback result");
    int attempts = 0;
    quiet::Audio retry([&](bool) {
        if (++attempts == 1) return quiet::PlaybackResult{{}, "No output device"};
        return quiet::PlaybackResult{"recovered output", {}};
    });
    retry.play(true);
    finish(retry);
    check(retry.failed() && retry.result().error == "No output device", "backend failure is surfaced");
    retry.play(true);
    finish(retry);
    check(!retry.failed() && retry.result().error.empty(), "successful retry clears old error");
    quiet::Audio exception([](bool) -> quiet::PlaybackResult { throw std::runtime_error("Device disconnected"); });
    exception.play(true);
    finish(exception);
    check(exception.failed() && exception.result().error == "Device disconnected", "backend exception cannot terminate the app");
    std::cout << "Audio checks passed\n";
}
