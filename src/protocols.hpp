#pragma once
#include <cstdint>
#include <optional>
#include <vector>
#include <array>

namespace battery::protocol {
using Bytes = std::vector<uint8_t>;
struct Value { int percent; std::optional<bool> charging; bool full = false; };
inline std::optional<Value> sora(const Bytes& b) {
    if (b.size() < 13 || b[0] != 5 || b[1] != 0x15 || b[4] != 1 || b[9] > 100 || b[12] != 1)
        return {};
    return Value{b[9], b[10] != 0, b[11] != 0};
}
inline std::optional<Value> hyperx(const Bytes& b) {
    if (b.size() < 5 || b[0] != 0x66 || (b[1] != 0x89 && b[1] != 0x0d) ||
        (b[2] == 0 && b[3] == 0) || b[4] > 100) return {};
    return Value{b[4], {}, false};
}
inline std::optional<int> hyperx_charge(const Bytes& b) {
    if (b.size() < 3 || b[0] != 0x66 || (b[1] != 0x8a && b[1] != 0x0c) || b[2] > 2) return {};
    return b[2];
}
inline Bytes compx_query(uint8_t command) {
    Bytes b(20); b[0] = 0x13; b[1] = command; b[2] = 1;
    b[19] = static_cast<uint8_t>(b[0] + b[1] + b[2]); return b;
}
inline bool compx_valid(const Bytes& b, uint8_t command) {
    if (b.size() != 20 || b[0] != 0x13 || b[1] != command || b[2] != 1 || b[3] != 0) return false;
    uint8_t sum = 0; for (size_t i = 0; i < 19; ++i) sum += b[i];
    return sum == b[19];
}
inline std::optional<std::array<uint8_t, 6>> compx_identity(const Bytes& b) {
    if (!compx_valid(b, 5) || b[4] != 6) return {};
    std::array<uint8_t, 6> id{}; for (size_t i = 0; i < 6; ++i) id[i] = b[i+5]; return id;
}
inline std::optional<Value> compx(const Bytes& b) {
    if (!compx_valid(b, 0x4a) || b[4] != 2 || b[5] > 100 || (b[6] & ~0x11) != 0) return {};
    return Value{b[5], (b[6] & 0x10) != 0, (b[6] & 1) != 0};
}
}
