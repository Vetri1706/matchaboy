#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <string>

// Optional host output. Queueing never blocks the emulator or link service.
class LinuxAudio {
  public:
    LinuxAudio();
    ~LinuxAudio();
    LinuxAudio(const LinuxAudio &) = delete;
    LinuxAudio &operator=(const LinuxAudio &) = delete;
    void push(std::span<const std::int16_t> samples);
    void clear();
    std::string status() const;
    bool available() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
