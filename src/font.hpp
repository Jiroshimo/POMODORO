#pragma once
#include <string>

namespace quiet {
// The font is private to this process. It is never installed system-wide.
class PixelFont {
public:
    PixelFont();
    ~PixelFont();
    PixelFont(const PixelFont&) = delete;
    PixelFont& operator=(const PixelFont&) = delete;
private:
    std::string directory_;
    std::string path_;
    bool registered_ = false;
};
} // namespace quiet
