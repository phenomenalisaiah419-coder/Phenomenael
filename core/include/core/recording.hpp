#pragma once

#include "transport.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace bedrockai {

// Sanitised transport recorder for deterministic replay/debugging. It deliberately
// stores protocol frames rather than credentials or raw socket metadata.
struct RecordedFrame {
    std::uint64_t timestamp_ms = 0;
    TransportFrame frame{};
};

class TransportRecorder {
public:
    explicit TransportRecorder(std::size_t max_frames = 4096) : max_frames_(max_frames) {}

    bool record(const TransportFrame& frame, std::uint64_t timestamp_ms, std::string* error = nullptr);
    void clear();
    const std::vector<RecordedFrame>& frames() const { return frames_; }

    // Binary format is intentionally versioned and local to this project.
    bool write_file(const std::string& path, std::string* error = nullptr) const;
    static bool read_file(const std::string& path,
                          std::vector<RecordedFrame>& out,
                          std::size_t max_frames = 4096,
                          std::string* error = nullptr);

private:
    std::size_t max_frames_ = 4096;
    std::vector<RecordedFrame> frames_;
};

} // namespace bedrockai
