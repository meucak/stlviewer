#pragma once

#include "math3d.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace faset {

struct QuantizedVertex {
    uint16_t x;
    uint16_t y;
    uint16_t z;
    uint16_t attr;
};
static_assert(sizeof(QuantizedVertex) == 8, "QuantizedVertex must be 8 bytes");

constexpr uint32_t CHUNK_TRIS = 4096;

struct RenderChunk {
    uint32_t first_vert = 0;
    uint32_t vert_count = 0;
    uint32_t tri_count = 0;
    Vec3 center{};
    float radius = 0.0f;
    Vec3 cone_axis{};
    float cone_cutoff = 1.0f;  // sin(half_angle)
    bool cone_cullable = false;
};

struct MeshData {
    bool ok = false;
    std::string error;
    std::string path;
    bool is_binary = true;
    uint64_t file_bytes = 0;
    uint32_t tri_count = 0;
    uint64_t vram_bytes = 0;
    double load_ms = 0.0;

    Vec3 bbox_min{};
    Vec3 bbox_max{};
    Vec3 bbox_extent{};
    Vec3 quant_scale{};
    double volume_mm3 = 0.0;
    double area_mm2 = 0.0;

    std::vector<QuantizedVertex> vertices;
    std::vector<RenderChunk> chunks;

    Vec3 dequantize(const QuantizedVertex& v) const {
        constexpr float INV_65535 = 1.0f / 65535.0f;
        return {
            bbox_min.x + (static_cast<float>(v.x) * INV_65535) * quant_scale.x,
            bbox_min.y + (static_cast<float>(v.y) * INV_65535) * quant_scale.y,
            bbox_min.z + (static_cast<float>(v.z) * INV_65535) * quant_scale.z,
        };
    }
};

MeshData load_stl(const std::string& path);

}  // namespace faset
