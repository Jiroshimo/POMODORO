#include "audio.hpp"
#include "audio_samples.hpp"
#include "windows_audio_script.hpp"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <exception>
#include <utility>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#else
#include <dlfcn.h>
#endif

namespace {
using quiet::PlaybackResult;
#ifndef _WIN32
class LocalAudioRuntime {
public:
    LocalAudioRuntime() {
#ifdef QUIET_AUDIO_LIBRARY_ARCH
        gchar* executable = g_file_read_link("/proc/self/exe", nullptr);
        if (!executable) return;
        gchar* directory = g_path_get_dirname(executable);
        const std::string base = directory;
        g_free(directory);
        g_free(executable);
        std::vector<std::string> pending;
        for (const auto& root : {base + "/audio-runtime", base + "/../lib/stash/audio-runtime"}) {
            const auto libraries = root + "/usr/lib/" QUIET_AUDIO_LIBRARY_ARCH;
            for (const auto& path : {libraries, libraries + "/pulseaudio"}) {
                GDir* dir = g_dir_open(path.c_str(), 0, nullptr);
                if (!dir) continue;
                while (const char* name = g_dir_read_name(dir)) {
                    if (g_strstr_len(name, -1, ".so")) pending.push_back(path + "/" + name);
                }
                g_dir_close(dir);
            }
        }
        // Distribution libraries can carry their own RUNPATH, which prevents
        // inheriting the executable's paths. Load local dependencies in order
        // of readiness; ELF SONAMEs then resolve them without changing the
        // process environment or depending on a particular library version.
        bool progress = true;
        while (progress && !pending.empty()) {
            progress = false;
            for (auto it = pending.begin(); it != pending.end();) {
                if (void* handle = dlopen(it->c_str(), RTLD_NOW | RTLD_LOCAL)) {
                    handles_.push_back(handle);
                    it = pending.erase(it);
                    progress = true;
                } else ++it;
            }
        }
#endif
    }
    ~LocalAudioRuntime() { for (auto it = handles_.rbegin(); it != handles_.rend(); ++it) dlclose(*it); }
private:
    std::vector<void*> handles_;
};

class Library {
public:
    explicit Library(const char* name) : handle_(dlopen(name, RTLD_NOW | RTLD_LOCAL)) {
        if (!handle_ && g_str_equal(name, "libpulse-simple.so.0")) {
            static LocalAudioRuntime runtime;
            handle_ = dlopen(name, RTLD_NOW | RTLD_LOCAL);
        }
        if (!handle_) { const char* detail = dlerror(); error_ = detail ? detail : "unknown loader error"; }
    }
    ~Library() { if (handle_) dlclose(handle_); }
    template<class T> T get(const char* name) const { return handle_ ? reinterpret_cast<T>(dlsym(handle_, name)) : nullptr; }
    explicit operator bool() const { return handle_ != nullptr; }
    const std::string& error() const { return error_; }
private:
    void* handle_;
    std::string error_;
};

PlaybackResult pulse(const std::vector<std::int16_t>& samples) {
    PlaybackResult result{"PulseAudio", {}};
    Library library("libpulse-simple.so.0");
    if (!library) {
        result.error = "PulseAudio could not load: " + library.error() +
            ". Launch with ./run.sh to set up the local audio runtime, or install libpulse0.";
        return result;
    }
    // Stable public PulseAudio ABI, loaded at runtime so ALSA-only systems work.
    struct SampleSpec { int format; std::uint32_t rate; std::uint8_t channels; };
    auto open = library.get<void* (*)(const char*, const char*, int, const char*, const char*, const SampleSpec*, const void*, const void*, int*)>("pa_simple_new");
    auto write = library.get<int (*)(void*, const void*, std::size_t, int*)>("pa_simple_write");
    auto drain = library.get<int (*)(void*, int*)>("pa_simple_drain");
    auto close = library.get<void (*)(void*)>("pa_simple_free");
    auto format = library.get<int (*)(const char*)>("pa_parse_sample_format");
    auto describe = library.get<const char* (*)(int)>("pa_strerror");
    if (!(open && write && drain && close && format && describe)) {
        result.error = "PulseAudio runtime is incomplete."; return result;
    }
    const std::uint16_t endian = 1;
    SampleSpec spec{format(*reinterpret_cast<const unsigned char*>(&endian) ? "s16le" : "s16be"), quiet::sound::rate, 1};
    int error = 0;
    void* device = open(g_getenv("PULSE_SERVER"), "Stash", 1 /* playback */, nullptr, "Timer chime", &spec, nullptr, nullptr, &error);
    if (!device) { result.error = std::string("PulseAudio: ") + describe(error); return result; }
    if (write(device, samples.data(), samples.size() * sizeof(samples[0]), &error) < 0 || drain(device, &error) < 0)
        result.error = std::string("PulseAudio: ") + describe(error);
    close(device);
    return result;
}

PlaybackResult alsa(const std::vector<std::int16_t>& samples) {
    PlaybackResult result{"ALSA", {}};
    Library library("libasound.so.2");
    if (!library) { result.error = "ALSA runtime missing."; return result; }
    auto open = library.get<int (*)(void**, const char*, int, int)>("snd_pcm_open");
    auto configure = library.get<int (*)(void*, int, int, unsigned, unsigned, int, unsigned)>("snd_pcm_set_params");
    auto write = library.get<long (*)(void*, const void*, unsigned long)>("snd_pcm_writei");
    auto drain = library.get<int (*)(void*)>("snd_pcm_drain");
    auto close = library.get<int (*)(void*)>("snd_pcm_close");
    auto recover = library.get<int (*)(void*, int, int)>("snd_pcm_recover");
    auto format = library.get<int (*)(const char*)>("snd_pcm_format_value");
    auto describe = library.get<const char* (*)(int)>("snd_strerror");
    if (!(open && configure && write && drain && close && recover && format && describe)) {
        result.error = "ALSA runtime is incomplete."; return result;
    }
    void* device = nullptr;
    int error = open(&device, "default", 0, 0);
    if (error < 0) { result.error = std::string("ALSA: ") + describe(error); return result; }
    const std::uint16_t endian = 1;
    error = configure(device, format(*reinterpret_cast<const unsigned char*>(&endian) ? "S16_LE" : "S16_BE"),
                      3 /* RW_INTERLEAVED */, 1, quiet::sound::rate, 1, 100000);
    std::size_t offset = 0;
    int retries = 0;
    while (error >= 0 && offset < samples.size()) {
        const long written = write(device, samples.data() + offset, samples.size() - offset);
        if (written > 0) { offset += static_cast<std::size_t>(written); retries = 0; }
        else if (written < 0 && retries++ < 3) error = recover(device, static_cast<int>(written), 1);
        else { error = written < 0 ? static_cast<int>(written) : -5; break; }
    }
    if (error >= 0) error = drain(device);
    if (error < 0) result.error = std::string("ALSA: ") + describe(error);
    close(device);
    return result;
}

// WSL interop is optional. Each helper has a deadline so a stuck Windows
// process cannot keep the sound worker (or app shutdown) waiting indefinitely.
bool command(const char* const* argv, std::string& output, std::string& error) {
    GError* failure = nullptr;
    GSubprocess* process = g_subprocess_newv(argv,
        static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE), &failure);
    if (!process) { error = failure->message; g_error_free(failure); return false; }
    struct Capture {
        GMainLoop* loop;
        GSubprocess* process;
        GBytes* out = nullptr;
        GBytes* err = nullptr;
        GError* failure = nullptr;
        bool timeout = false;
    };
    GMainContext* context = g_main_context_new();
    g_main_context_push_thread_default(context);
    Capture capture{g_main_loop_new(context, FALSE), process};
    GSource* deadline = g_timeout_source_new(8000);
    g_source_set_callback(deadline, +[](gpointer data) -> gboolean {
        auto* c = static_cast<Capture*>(data);
        c->timeout = true;
        g_subprocess_force_exit(c->process);
        return G_SOURCE_REMOVE;
    }, &capture, nullptr);
    g_source_attach(deadline, context);
    g_subprocess_communicate_async(process, nullptr, nullptr, +[](GObject* object, GAsyncResult* ready, gpointer data) {
        auto* c = static_cast<Capture*>(data);
        g_subprocess_communicate_finish(G_SUBPROCESS(object), ready, &c->out, &c->err, &c->failure);
        g_main_loop_quit(c->loop);
    }, &capture);
    g_main_loop_run(capture.loop);
    g_source_destroy(deadline);
    g_source_unref(deadline);
    const bool ok = !capture.timeout && !capture.failure && g_subprocess_get_successful(process);
    auto readText = [](GBytes* bytes) -> std::string {
        if (!bytes) return {};
        gsize size = 0;
        const auto* data = static_cast<const char*>(g_bytes_get_data(bytes, &size));
        if (!size) return {};
        gchar* valid = g_utf8_make_valid(data, static_cast<gssize>(size));
        std::string result = g_strstrip(valid);
        g_free(valid);
        return result;
    };
    output = readText(capture.out);
    const std::string detail = readText(capture.err);
    if (!ok) {
        if (capture.timeout) error = "audio helper timed out";
        else if (!detail.empty()) error = detail;
        else if (capture.failure) error = capture.failure->message;
        else error = "Windows audio helper failed";
    }
    g_clear_error(&capture.failure);
    g_clear_pointer(&capture.out, g_bytes_unref);
    g_clear_pointer(&capture.err, g_bytes_unref);
    g_main_loop_unref(capture.loop);
    g_main_context_pop_thread_default(context);
    g_main_context_unref(context);
    g_object_unref(process);
    return ok;
}

PlaybackResult windowsFromWsl(const std::vector<std::int16_t>& samples) {
    PlaybackResult result{"Windows audio (WSL)", {}};
    gchar* powershell = g_find_program_in_path("powershell.exe");
    if (!powershell) {
        result.error = "Windows audio requires powershell.exe on WSL's PATH. Launch from a normal WSL terminal with Windows interop enabled.";
        return result;
    }
    // Windows interop can close redirected stdin before a large WAV has been
    // written. Use a private WAV and a Windows-readable path instead, retaining
    // stderr so startup errors are reported rather than hidden by Broken pipe.
    struct TemporaryWav {
        gchar* path = nullptr;
        ~TemporaryWav() { if (path) { g_unlink(path); g_free(path); } }
    } file;
    GError* failure = nullptr;
    const int fd = g_file_open_tmp("quiet-pomodoro-XXXXXX.wav", &file.path, &failure);
    if (fd < 0) {
        result.error = std::string("Could not create the chime: ") + failure->message;
        g_error_free(failure);
        g_free(powershell);
        return result;
    }
    g_close(fd, nullptr);
    const auto bytes = quiet::sound::wav(samples);
    if (!g_file_set_contents(file.path, reinterpret_cast<const char*>(bytes.data()), bytes.size(), &failure)) {
        result.error = std::string("Could not write the chime: ") + failure->message;
        g_error_free(failure);
        g_free(powershell);
        return result;
    }
    std::string windowsPath;
    const char* distro = g_getenv("WSL_DISTRO_NAME");
    if (distro && *distro) {
        windowsPath = std::string("\\\\wsl.localhost\\") + distro + file.path;
        std::replace(windowsPath.begin(), windowsPath.end(), '/', '\\');
    } else {
        const char* args[] = {"wslpath", "-w", file.path, nullptr};
        std::string detail;
        if (!command(args, windowsPath, detail)) {
            result.error = "Could not locate the Windows chime path: " + detail;
            g_free(powershell);
            return result;
        }
    }
    gchar* path64 = g_base64_encode(reinterpret_cast<const guchar*>(windowsPath.data()), windowsPath.size());
    const std::string script = std::string("$wavPath = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('") +
        path64 + "'))\n" + quiet::sound::windowsAudioScript;
    g_free(path64);
    std::string output, detail;
    gsize length = 0;
    gchar* utf16 = g_convert(script.c_str(), -1, "UTF-16LE", "UTF-8", nullptr, &length, nullptr);
    gchar* encoded = utf16 ? g_base64_encode(reinterpret_cast<const guchar*>(utf16), length) : nullptr;
    if (encoded) {
        const char* args[] = {powershell, "-NoLogo", "-NoProfile", "-NonInteractive", "-EncodedCommand", encoded, nullptr};
        if (!command(args, output, detail)) result.error = "Windows audio bridge: " + detail;
        else if (output != quiet::sound::windowsAudioAck) result.error = "Windows did not confirm playback. The audio helper exited without a playback acknowledgment.";
    } else result.error = "WSL: could not encode the Windows audio command.";
    g_free(encoded);
    g_free(utf16);
    g_free(powershell);
    return result;
}
bool isWsl() {
    if (g_getenv("WSL_DISTRO_NAME") || g_getenv("WSL_INTEROP")) return true;
    gchar* release = nullptr;
    bool wsl = false;
    if (g_file_get_contents("/proc/sys/kernel/osrelease", &release, nullptr, nullptr)) {
        gchar* lower = g_ascii_strdown(release, -1);
        wsl = g_strstr_len(lower, -1, "microsoft") != nullptr;
        g_free(lower);
    }
    g_free(release);
    return wsl;
}
#endif

PlaybackResult output(bool complete, quiet::AudioOutput selected, bool allowFallback) {
    const auto samples = quiet::sound::samples(complete);
#ifdef _WIN32
    (void)allowFallback;
    if (selected == quiet::AudioOutput::Linux) return {{}, "Linux audio is unavailable on Windows."};
    const auto bytes = quiet::sound::wav(samples);
    const bool ok = PlaySoundW(reinterpret_cast<LPCWSTR>(bytes.data()), nullptr, SND_MEMORY | SND_SYNC | SND_NODEFAULT) != FALSE;
    return {"Windows audio", ok ? "" : "Windows could not play the chime. Check the default output device and app volume."};
#else
    std::string errors;
    const bool wsl = isWsl();
    const bool windowsFirst = selected == quiet::AudioOutput::Windows || (selected == quiet::AudioOutput::Automatic && wsl);
    if (windowsFirst) {
        auto result = windowsFromWsl(samples);
        if (result.ok()) return result;
        if (selected == quiet::AudioOutput::Windows && !allowFallback) return result;
        errors = result.error + "\n";
    }
    auto result = pulse(samples);
    if (result.ok()) {
        if (wsl) result.backend = "WSLg / PulseAudio";
        return result;
    }
    errors += result.error;
    if (wsl && selected == quiet::AudioOutput::Linux && allowFallback) {
        result = windowsFromWsl(samples);
        if (result.ok()) return result;
        errors += "\n" + result.error;
    }
    if (wsl) return {{}, errors}; // WSL has no ALSA hardware device.
    result = alsa(samples);
    if (result.ok()) return result;
    return {{}, errors + "\n" + result.error};
#endif
}
} // namespace

namespace quiet {
Audio::Audio(Playback playback) : playback_(playback ? std::move(playback) : Playback([this](bool complete) {
    return output(complete, selected_.load(), allowFallback_);
})), worker_([this] { run(); }) {}
Audio::Audio(AudioOutput selected, bool allowFallback) : Audio() {
    selected_ = selected;
    allowFallback_ = allowFallback;
}
Audio::~Audio() {
    { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; pending_.clear(); }
    ready_.notify_one();
    worker_.join();
}
void Audio::play(bool completion) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Coalesce repeated clicks, but keep one completion behind an active
        // preview/warning so the session boundary cannot silently lose its cue.
        if (completion) pending_.erase(std::remove(pending_.begin(), pending_.end(), false), pending_.end());
        if (!completion && std::find(pending_.begin(), pending_.end(), true) != pending_.end()) return;
        if (std::find(pending_.begin(), pending_.end(), completion) == pending_.end())
            pending_.push_back(completion);
        busy_ = true;
    }
    ready_.notify_one();
}
PlaybackResult Audio::result() const { std::lock_guard<std::mutex> lock(mutex_); return result_; }
void Audio::run() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (true) {
        ready_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
        if (stopping_) return;
        const bool completion = pending_.front();
        pending_.pop_front();
        lock.unlock();
        PlaybackResult result;
        try { result = playback_(completion); }
        catch (const std::exception& e) { result.error = e.what(); }
        catch (...) { result.error = "Unexpected audio playback error."; }
        lock.lock();
        result_ = std::move(result);
        failed_ = !result_.ok();
        busy_ = !pending_.empty();
    }
}
} // namespace quiet
