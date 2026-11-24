#pragma once

#include "math3d.hpp"
#include "stl_loader.hpp"

#include <cstdint>
#include <vector>

namespace faset {

struct DrawSpan {
    int32_t first_vert = 0;
    int32_t vert_count = 0;
};

struct CullStats {
    uint32_t total_chunks = 0;
    uint32_t visible_chunks = 0;
    uint32_t frustum_culled = 0;
    uint32_t cone_culled = 0;
    uint32_t draw_calls = 0;
    uint32_t visible_tris = 0;
};

struct CullOptions {
    bool backface_cone_cull = true;
    bool is_ortho = false;
    Vec3 eye{};
    Vec3 cam_forward{};
};

void cull_chunks(const std::vector<RenderChunk>& chunks,
                 const Frustum& frustum,
                 const CullOptions& opts,
                 std::vector<DrawSpan>& out_spans,
                 CullStats* out_stats = nullptr);

}  // namespace faset
