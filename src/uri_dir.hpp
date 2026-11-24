#pragma once

#include <string>
#include <vector>

namespace faset {

bool has_stl_extension(const std::string& path);
std::string parse_xdnd_uri_list(const std::string& uri_list);
std::vector<std::string> list_sibling_stls(const std::string& file_path, int* out_current_idx = nullptr);
std::string step_sibling_stl(const std::string& file_path, int delta);

}  // namespace faset
