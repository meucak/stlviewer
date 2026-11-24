#include "culling.hpp"

#include <cmath>

namespace faset {

void cull_chunks(const std::vector<RenderChunk>& chunks,
                 const Frustum& frustum,
                 const CullOptions& opts,
                 std::vector<DrawSpan>& out_spans,
                 CullStats* out_stats) {
    out_spans.clear();
    CullStats stats{};
    stats.total_chunks = static_cast<uint32_t>(chunks.size());

    bool has_open = false;
    DrawSpan cur{};

    for (const auto& ch : chunks) {
        if (ch.vert_count == 0) continue;

        if (!frustum.intersects_sphere(ch.center, ch.radius)) {
            ++stats.frustum_culled;
            continue;
        }

        if (opts.backface_cone_cull && ch.cone_cullable) {
            if (opts.is_ortho) {
                if (dot(opts.cam_forward, ch.cone_axis) > ch.cone_cutoff + 1e-4f) {
                    ++stats.cone_culled;
                    continue;
                }
            } else {
                Vec3 d = ch.center - opts.eye;
                float dist = length(d);
                if (dist > ch.radius &&
                    dot(d, ch.cone_axis) > dist * ch.cone_cutoff + 1.001f * ch.radius) {
                    ++stats.cone_culled;
                    continue;
                }
            }
        }

        ++stats.visible_chunks;
        stats.visible_tris += ch.tri_count;

        int32_t first = static_cast<int32_t>(ch.first_vert);
        int32_t count = static_cast<int32_t>(ch.vert_count);
        if (has_open && (cur.first_vert + cur.vert_count) == first) {
            cur.vert_count += count;
        } else {
            if (has_open) out_spans.push_back(cur);
            cur = {first, count};
            has_open = true;
        }
    }

    if (has_open) out_spans.push_back(cur);
    stats.draw_calls = static_cast<uint32_t>(out_spans.size());
    if (out_stats) *out_stats = stats;
}

}  // namespace faset
