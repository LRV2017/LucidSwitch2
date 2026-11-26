#include "lucid/devices/interrupt_controller.hpp"
namespace lucid::devices {
bool InterruptController::set_enabled(unsigned id,bool on) noexcept {if(id>=64U)return false;auto b=std::uint64_t{1}<<id;if(on)enabled_|=b;else enabled_&=~b;return true;}
bool InterruptController::raise(unsigned id) noexcept {if(id>=64U)return false;pending_|=std::uint64_t{1}<<id;return true;}
bool InterruptController::clear(unsigned id) noexcept {if(id>=64U)return false;pending_&=~(std::uint64_t{1}<<id);return true;}
bool InterruptController::pending(unsigned id) const noexcept{return id<64U&&(pending_&enabled_&(std::uint64_t{1}<<id))!=0U;}
bool InterruptController::acknowledge(unsigned& id) noexcept {auto active=pending_&enabled_;if(!active)return false;for(unsigned i=0;i<64U;++i)if(active&(std::uint64_t{1}<<i)){id=i;pending_&=~(std::uint64_t{1}<<i);return true;}return false;}
std::uint64_t InterruptController::pending_mask() const noexcept{return pending_&enabled_;}
}