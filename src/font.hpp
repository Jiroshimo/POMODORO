#pragma once
#include <string>
#include <vector>

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
    std::vector<std::string> paths_;
};
} // namespace quiet
