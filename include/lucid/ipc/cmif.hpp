#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lucid::ipc {

inline constexpr std::uint32_t CmifInputMagic = 0x49434653U;  // "SFCI"
inline constexpr std::uint32_t CmifOutputMagic = 0x4F434653U; // "SFCO"
inline constexpr std::size_t HipcHeaderSize = 8U;
inline constexpr std::size_t CmifHeaderSize = 16U;
inline constexpr std::size_t MaximumCmifPayloadSize = 1024U * 1024U;

enum class CommandType : std::uint16_t {
    Invalid = 0U,
    LegacyRequest = 1U,
    Close = 2U,
    LegacyControl = 3U,
    Request = 4U,
    Control = 5U,
    RequestWithContext = 6U,
    ControlWithContext = 7U
};

struct ParsedRequest {
    CommandType type{CommandType::Invalid};
    std::uint32_t version{};
    std::uint32_t method_id{};
    std::uint32_t token{};
    std::span<const std::uint8_t> payload;
};

struct Response {
    std::uint32_t version{};
    std::uint32_t result{};
    std::uint32_t token{};
    std::vector<std::uint8_t> payload;
};

// Parses only HIPC messages with no PID, handles, pointer/static buffers,
// send/receive/exchange descriptors, or receive-list descriptors. CMIF data
// is aligned to 16 bytes inside the HIPC raw-data section.
[[nodiscard]] bool parse_raw_cmif_request(
    std::span<const std::uint8_t> message,
    ParsedRequest& output,
    std::string* error = nullptr);

// Encodes the 16-byte CMIF output header followed by its raw payload.
// HIPC framing and descriptor construction are separate operations.
[[nodiscard]] bool encode_cmif_response(
    const Response& response,
    std::vector<std::uint8_t>& output,
    std::string* error = nullptr);

class ServiceManager final {
public:
    using Handler = std::function<bool(
        std::span<const std::uint8_t> request_payload,
        std::vector<std::uint8_t>& response_payload,
        std::uint32_t& result,
        std::string* error)>;

    [[nodiscard]] bool register_service(
        std::string_view service_name, std::string* error = nullptr);
    [[nodiscard]] bool register_command(
        std::string_view service_name, std::uint32_t method_id,
        Handler handler, std::string* error = nullptr);
    [[nodiscard]] bool dispatch(
        std::string_view service_name, const ParsedRequest& request,
        Response& response, std::string* error = nullptr) const;
    [[nodiscard]] std::size_t service_count() const noexcept;
    [[nodiscard]] std::size_t command_count(std::string_view service_name) const noexcept;

private:
    using CommandTable = std::map<std::uint32_t, Handler>;
    std::map<std::string, CommandTable, std::less<>> services_;
};

} // namespace lucid::ipc
