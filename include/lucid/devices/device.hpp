#pragma once
#include <cstdint>
#include <string_view>
namespace lucid::devices {
enum class DeviceAccess { Read,Write };
struct DeviceResult { bool success{}; std::uint64_t value{}; };
class MmioDevice {
public: virtual ~MmioDevice()=default;
 virtual DeviceResult access(std::uint64_t offset,DeviceAccess op,std::uint64_t value,unsigned width) noexcept=0;
 virtual std::string_view name() const noexcept=0;
};
}