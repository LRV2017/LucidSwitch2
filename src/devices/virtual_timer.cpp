#include "lucid/devices/virtual_timer.hpp"
#include <limits>
namespace lucid::devices {
void VirtualTimer::set_compare(std::uint64_t v) noexcept{compare_=v;if(enabled_&&counter_>=compare_)pending_=true;}
void VirtualTimer::set_enabled(bool v) noexcept{enabled_=v;if(!v)pending_=false;else if(counter_>=compare_)pending_=true;}
void VirtualTimer::advance(std::uint64_t t) noexcept{counter_=t>std::numeric_limits<std::uint64_t>::max()-counter_?std::numeric_limits<std::uint64_t>::max():counter_+t;if(enabled_&&counter_>=compare_)pending_=true;}
std::uint64_t VirtualTimer::counter() const noexcept{return counter_;}
std::uint64_t VirtualTimer::compare() const noexcept{return compare_;}
bool VirtualTimer::interrupt_pending() const noexcept{return pending_;}
void VirtualTimer::acknowledge() noexcept{pending_=false;}
}