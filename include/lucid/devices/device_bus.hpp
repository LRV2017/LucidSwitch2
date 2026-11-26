#pragma once
#include "lucid/devices/device.hpp"
#include <cstdint>
#include <vector>
namespace lucid::devices {
class DeviceBus final {
public:
 bool map(std::uint64_t base,std::uint64_t size,MmioDevice& device);
 DeviceResult access(std::uint64_t address,DeviceAccess op,std::uint64_t value,unsigned width) noexcept;
 std::size_t device_count() const noexcept;
private: struct Mapping{std::uint64_t base{},size{};MmioDevice* device{};}; std::vector<Mapping> mappings_;
};
}