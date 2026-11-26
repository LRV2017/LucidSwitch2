#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
namespace lucid::mmu {
enum class AccessType { Read,Write,Execute };
enum class TranslationFault { None,Unmapped,PermissionDenied,AddressOverflow,InvalidDescriptor,AccessFlagFault,TableWalkFault,UnsupportedConfiguration };
struct Translation { TranslationFault fault{TranslationFault::Unmapped}; std::uint64_t physical_address{}; explicit operator bool() const noexcept{return fault==TranslationFault::None;} };
class AddressSpace final {
public:
 static constexpr std::uint64_t PageSize=4096U;
 bool map_page(std::uint64_t va,std::uint64_t pa,bool r,bool w,bool x);
 bool unmap_page(std::uint64_t va);
 Translation translate(std::uint64_t va,AccessType access) const noexcept;
 std::size_t mapped_pages() const noexcept;
private:
 struct Page { std::uint64_t pa{}; bool r{},w{},x{}; };
 std::map<std::uint64_t,Page> pages_;
};
}