#pragma once
#include <cstdint>
namespace lucid::devices {
class InterruptController final {
public:
 static constexpr unsigned InterruptCount=64U;
 bool set_enabled(unsigned id,bool enabled) noexcept;
 bool raise(unsigned id) noexcept;
 bool clear(unsigned id) noexcept;
 bool pending(unsigned id) const noexcept;
 bool acknowledge(unsigned& id) noexcept;
 std::uint64_t pending_mask() const noexcept;
private: std::uint64_t enabled_{},pending_{};
};
}