#pragma once
#include <cstdint>
namespace lucid::devices {
class VirtualTimer final {
public:
 void set_compare(std::uint64_t value) noexcept;
 void set_enabled(bool enabled) noexcept;
 void advance(std::uint64_t ticks) noexcept;
 std::uint64_t counter() const noexcept;
 std::uint64_t compare() const noexcept;
 bool interrupt_pending() const noexcept;
 void acknowledge() noexcept;
private: std::uint64_t counter_{},compare_{}; bool enabled_{},pending_{};
};
}