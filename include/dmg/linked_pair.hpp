#pragma once
#include <cstdint>
#include <memory>
namespace dmg {
class Bus;
class Cpu;
class LinkedPair {
public:
    LinkedPair(Bus &, Cpu &, Bus &, Cpu &);
    ~LinkedPair();
    void run_frame();
    std::uint64_t edges() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
