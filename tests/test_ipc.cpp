#include "lucid/ipc/cmif.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;
void expect(const bool condition, const char* message) {
    if (condition) std::cout << "[PASS] " << message << "\n";
    else { ++failures; std::cerr << "[FAIL] " << message << "\n"; }
}
void write32(std::vector<std::uint8_t>& bytes, const std::size_t offset,
             const std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    bytes[offset + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    bytes[offset + 3U] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}
std::vector<std::uint8_t> make_request(
    const std::uint32_t type = 4U, const std::uint32_t version = 0U) {
    // 8-byte HIPC header, 8 bytes of alignment padding, then CMIF header+payload.
    std::vector<std::uint8_t> message(40U, 0U);
    write32(message, 0U, type);
    write32(message, 4U, 8U); // RawData is 8 words (32 bytes).
    write32(message, 16U, lucid::ipc::CmifInputMagic);
    write32(message, 20U, version);
    write32(message, 24U, 7U);
    write32(message, 28U, 0x12345678U);
    write32(message, 32U, 0xAABBCCDDU);
    write32(message, 36U, 0x01020304U);
    return message;
}

void test_parse_and_validate() {
    using namespace lucid::ipc;
    auto message = make_request();
    ParsedRequest request{};
    std::string error;
    expect(parse_raw_cmif_request(message, request, &error),
           "parse a raw-data-only HIPC CMIF request");
    expect(request.type == CommandType::Request &&
           request.version == 0U && request.method_id == 7U &&
           request.token == 0x12345678U && request.payload.size() == 8U,
           "extract command ID, token, version and bounded payload");
    expect(request.payload[0] == 0xDDU && request.payload[1] == 0xCCU &&
           request.payload[4] == 0x04U,
           "preserve raw CMIF parameter bytes");

    expect(parse_raw_cmif_request(make_request(6U, 1U), request, &error) &&
           request.type == CommandType::RequestWithContext && request.version == 1U,
           "accept the context-bearing CMIF request form");

    expect(!parse_raw_cmif_request(std::span<const std::uint8_t>(message.data(), 7U),
                                   request, &error),
           "reject a truncated HIPC header");
    auto damaged = make_request();
    write32(damaged, 0U, 4U | (1U << 16U));
    expect(!parse_raw_cmif_request(damaged, request, &error),
           "reject unsupported HIPC pointer descriptors");
    damaged = make_request();
    write32(damaged, 4U, 0x80000008U);
    expect(!parse_raw_cmif_request(damaged, request, &error),
           "reject unsupported HIPC special headers and handle sections");
    damaged = make_request();
    write32(damaged, 4U, 7U);
    expect(!parse_raw_cmif_request(damaged, request, &error),
           "reject a raw-data section truncated before the CMIF payload");
    damaged = make_request();
    write32(damaged, 16U, 0U);
    expect(!parse_raw_cmif_request(damaged, request, &error),
           "reject a CMIF message without the SFCI signature");
    damaged = make_request(4U, 2U);
    expect(!parse_raw_cmif_request(damaged, request, &error),
           "reject an unsupported CMIF version");
}

void test_service_dispatch_and_response() {
    using namespace lucid::ipc;
    ServiceManager manager;
    std::string error;
    expect(manager.register_service("echo", &error),
           "register a named CMIF service");
    expect(!manager.register_service("echo", &error),
           "reject duplicate service registrations");
    expect(!manager.register_command("missing", 1U,
        [](std::span<const std::uint8_t>, std::vector<std::uint8_t>&,
           std::uint32_t&, std::string*) { return true; }, &error),
        "require a service to exist before adding commands");

    expect(manager.register_command("echo", 7U,
        [](std::span<const std::uint8_t> input,
           std::vector<std::uint8_t>& output,
           std::uint32_t& result, std::string*) {
            output.assign(input.begin(), input.end());
            result = 0U;
            return true;
        }, &error), "register a test command handler");
    expect(!manager.register_command("echo", 7U,
        [](std::span<const std::uint8_t>, std::vector<std::uint8_t>&,
           std::uint32_t&, std::string*) { return true; }, &error),
        "reject duplicate command identifiers");

    auto message = make_request();
    ParsedRequest request{};
    Response response{};
    expect(parse_raw_cmif_request(message, request, &error),
           "parse the request before service dispatch");
    expect(manager.dispatch("echo", request, response, &error),
           "dispatch a CMIF method through the service registry");
    expect(response.version == 0U && response.result == 0U &&
           response.token == 0x12345678U &&
           response.payload.size() == request.payload.size() &&
           response.payload[0] == request.payload[0],
           "preserve request context and echo the response payload");

    std::vector<std::uint8_t> encoded;
    expect(encode_cmif_response(response, encoded, &error),
           "encode a bounded CMIF output header and payload");
    expect(encoded.size() == 24U &&
           encoded[0] == 0x53U && encoded[1] == 0x46U &&
           encoded[2] == 0x43U && encoded[3] == 0x4FU,
           "write the SFCO response signature in little-endian form");
    expect(encoded[8] == 0U && encoded[12] == 0x78U &&
           encoded[15] == 0x12U,
           "serialize result code and response token");
    expect(manager.service_count() == 1U &&
           manager.command_count("echo") == 1U,
           "report registered services and methods");
    expect(!manager.dispatch("missing", request, response, &error),
           "reject dispatch to an unregistered service");
    request.method_id = 99U;
    expect(!manager.dispatch("echo", request, response, &error),
           "reject dispatch to an unregistered method");
}
} // namespace

int main() {
    test_parse_and_validate();
    test_service_dispatch_and_response();
    if (failures != 0) {
        std::cerr << failures << " HIPC/CMIF tests failed.\n";
        return 1;
    }
    std::cout << "All HIPC/CMIF tests passed.\n";
    return 0;
}
