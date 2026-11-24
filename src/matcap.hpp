#pragma once

#include <cstdint>
#include <vector>

namespace faset {

enum class MatcapKind : int {
    Clay = 0,
    Aluminum = 1,
    Slate = 2,
    Normals = 3,
    Count = 4,
};

constexpr int MATCAP_SIZE = 256;

const char* matcap_name(MatcapKind kind);
std::vector<uint8_t> generate_matcap_rgb8(MatcapKind kind);

}  // namespace faset
