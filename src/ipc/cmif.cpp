#include "lucid/ipc/cmif.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <utility>

namespace lucid::ipc {
namespace {

void set_error(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
}

std::uint32_t read_u32(
    const std::span<const std::uint8_t> bytes, const std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

void write_u32(std::vector<std::uint8_t>& bytes,
               const std::size_t offset, const std::uint32_t value) noexcept {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    bytes[offset + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    bytes[offset + 3U] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

bool valid_service_name(const std::string_view name) noexcept {
    if (name.empty() || name.size() > 8U) return false;
    return std::all_of(name.begin(), name.end(), [](const unsigned char ch) {
        return std::isalnum(ch) != 0 || ch == '-' || ch == '_' || ch == ':';
    });
}

} // namespace

bool parse_raw_cmif_request(
    const std::span<const std::uint8_t> message,
    ParsedRequest& output, std::string* error) {
    if (message.size() < HipcHeaderSize) {
        set_error(error, "The HIPC message is shorter than its 8-byte header.");
        return false;
    }

    const std::uint32_t header0 = read_u32(message, 0U);
    const std::uint32_t header1 = read_u32(message, 4U);
    const auto type = static_cast<CommandType>(header0 & 0xFFFFU);
    const unsigned pointer_count = (header0 >> 16U) & 0xFU;
    const unsigned send_count = (header0 >> 20U) & 0xFU;
    const unsigned receive_count = (header0 >> 24U) & 0xFU;
    const unsigned exchange_count = (header0 >> 28U) & 0xFU;
    const std::uint32_t raw_word_count = header1 & 0x3FFU;
    const unsigned receive_list_count = (header1 >> 10U) & 0xFU;
    const unsigned reserved = (header1 >> 14U) & 0x3FU;
    const unsigned receive_list_offset = (header1 >> 20U) & 0x7FFU;
    const bool has_special_header = (header1 & 0x80000000U) != 0U;

    if (type != CommandType::Request &&
        type != CommandType::RequestWithContext) {
        set_error(error, "Only CMIF Request and RequestWithContext messages are supported.");
        return false;
    }
    if (pointer_count != 0U || send_count != 0U ||
        receive_count != 0U || exchange_count != 0U ||
        receive_list_count != 0U || receive_list_offset != 0U ||
        reserved != 0U || has_special_header) {
        set_error(error, "This parser accepts raw-data-only HIPC frames without descriptors or handles.");
        return false;
    }

    const std::uint64_t raw_size =
        static_cast<std::uint64_t>(raw_word_count) * 4U;
    const std::uint64_t raw_end =
        static_cast<std::uint64_t>(HipcHeaderSize) + raw_size;
    if (raw_word_count == 0U || raw_end > message.size()) {
        set_error(error, "The HIPC raw-data section is empty or extends beyond the supplied message.");
        return false;
    }

    constexpr std::size_t Alignment = 16U;
    const std::size_t raw_begin = HipcHeaderSize;
    const std::size_t cmif_offset =
        (raw_begin + (Alignment - 1U)) & ~(Alignment - 1U);
    if (raw_end < cmif_offset ||
        raw_end - cmif_offset < CmifHeaderSize) {
        set_error(error, "The HIPC raw-data section cannot contain an aligned CMIF header.");
        return false;
    }

    const std::span<const std::uint8_t> view = message;
    if (read_u32(view, cmif_offset) != CmifInputMagic) {
        set_error(error, "The aligned CMIF input header does not contain the SFCI signature.");
        return false;
    }
    const std::uint32_t version = read_u32(view, cmif_offset + 4U);
    if (version > 1U ||
        (type == CommandType::Request && version != 0U) ||
        (type == CommandType::RequestWithContext && version != 1U)) {
        set_error(error, "The CMIF version does not match the request command type.");
        return false;
    }

    ParsedRequest candidate{};
    candidate.type = type;
    candidate.version = version;
    candidate.method_id = read_u32(view, cmif_offset + 8U);
    candidate.token = read_u32(view, cmif_offset + 12U);
    const std::size_t payload_offset = cmif_offset + CmifHeaderSize;
    const std::size_t payload_size =
        static_cast<std::size_t>(raw_end) - payload_offset;
    if (payload_size > MaximumCmifPayloadSize) {
        set_error(error, "The CMIF payload exceeds the 1 MiB safety limit.");
        return false;
    }
    candidate.payload = view.subspan(payload_offset, payload_size);
    output = candidate;
    return true;
}

bool encode_cmif_response(
    const Response& response, std::vector<std::uint8_t>& output,
    std::string* error) {
    if (response.version > 1U ||
        response.payload.size() > MaximumCmifPayloadSize ||
        response.payload.size() >
            std::numeric_limits<std::size_t>::max() - CmifHeaderSize) {
        set_error(error, "The CMIF response version or payload size is unsupported.");
        return false;
    }
    try {
        std::vector<std::uint8_t> candidate(CmifHeaderSize + response.payload.size(), 0U);
        write_u32(candidate, 0U, CmifOutputMagic);
        write_u32(candidate, 4U, response.version);
        write_u32(candidate, 8U, response.result);
        write_u32(candidate, 12U, response.token);
        std::copy(response.payload.begin(), response.payload.end(),
                  candidate.begin() + static_cast<std::ptrdiff_t>(CmifHeaderSize));
        output = std::move(candidate);
    } catch (const std::bad_alloc&) {
        set_error(error, "Not enough host memory to encode the CMIF response.");
        return false;
    }
    return true;
}

bool ServiceManager::register_service(
    const std::string_view service_name, std::string* error) {
    if (!valid_service_name(service_name)) {
        set_error(error, "Service names must contain 1-8 ASCII letters, digits, '-', '_' or ':'.");
        return false;
    }
    const auto [iterator, inserted] =
        services_.try_emplace(std::string(service_name));
    (void)iterator;
    if (!inserted) {
        set_error(error, "The service name is already registered.");
        return false;
    }
    return true;
}

bool ServiceManager::register_command(
    const std::string_view service_name, const std::uint32_t method_id,
    Handler handler, std::string* error) {
    const auto service = services_.find(service_name);
    if (service == services_.end()) {
        set_error(error, "Register the service before adding its commands.");
        return false;
    }
    if (!handler) {
        set_error(error, "A CMIF command requires a valid handler.");
        return false;
    }
    const auto [iterator, inserted] =
        service->second.emplace(method_id, std::move(handler));
    (void)iterator;
    if (!inserted) {
        set_error(error, "The method identifier is already registered for this service.");
        return false;
    }
    return true;
}

bool ServiceManager::dispatch(
    const std::string_view service_name, const ParsedRequest& request,
    Response& response, std::string* error) const {
    if (request.type != CommandType::Request &&
        request.type != CommandType::RequestWithContext) {
        set_error(error, "The request command type is not dispatchable.");
        return false;
    }
    const auto service = services_.find(service_name);
    if (service == services_.end()) {
        set_error(error, "The requested service is not registered.");
        return false;
    }
    const auto command = service->second.find(request.method_id);
    if (command == service->second.end()) {
        set_error(error, "The requested method is not registered by this service.");
        return false;
    }

    Response candidate{};
    candidate.version = request.version;
    candidate.token = request.token;
    if (!command->second(request.payload, candidate.payload,
                         candidate.result, error)) {
        if (error != nullptr && error->empty())
            *error = "The CMIF command handler rejected the request.";
        return false;
    }
    response = std::move(candidate);
    return true;
}

std::size_t ServiceManager::service_count() const noexcept {
    return services_.size();
}

std::size_t ServiceManager::command_count(
    const std::string_view service_name) const noexcept {
    const auto service = services_.find(service_name);
    return service == services_.end() ? 0U : service->second.size();
}

} // namespace lucid::ipc
