#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace quiet {
enum class AudioOutput { Automatic, Windows, Linux };
struct PlaybackResult {
    std::string backend;
    std::string error;
    bool ok() const { return error.empty(); }
};
class Audio {
public:
    using Playback = std::function<PlaybackResult(bool)>;
    explicit Audio(Playback playback = {});
    explicit Audio(AudioOutput output, bool allowFallback = true);
    ~Audio();
    void play(bool completion);
    void setOutput(AudioOutput output) { selected_ = output; }
    bool failed() const { return failed_; }
    bool playing() const { return busy_; }
    PlaybackResult result() const;
private:
    void run();
    Playback playback_;
    std::atomic<AudioOutput> selected_{AudioOutput::Automatic};
    bool allowFallback_ = true;
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<bool> pending_;
    PlaybackResult result_;
    bool stopping_ = false;
    std::atomic<bool> busy_{false};
    std::atomic<bool> failed_{false};
    std::thread worker_;
};
} // namespace quiet
