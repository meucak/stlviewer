#pragma once

#include "matcap.hpp"
#include "math3d.hpp"

#include <string>

namespace faset {

struct ViewerState {
    bool dark_theme = true;
    MatcapKind matcap = MatcapKind::Clay;
    bool wireframe = false;
    bool xray = false;
    bool two_sided = false;
    bool show_grid = true;
    bool show_hud = true;
    bool show_help = false;

    bool slice_enabled = false;
    int slice_axis = 2;  // 0=X, 1=Y, 2=Z
    bool slice_flip = false;
    float slice_t = 0.5f;  // [0, 1] along bbox axis

    bool measure_mode = false;
    bool measure_has_a = false;
    bool measure_has_b = false;
    Vec3 measure_a{};
    Vec3 measure_b{};

    int sibling_index = -1;
    int sibling_count = 0;

    std::string status_msg;
};

}  // namespace faset
