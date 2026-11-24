#include "uri_dir.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace faset {

namespace {

int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string percent_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int hi = hex_val(s[i + 1]);
            int lo = hex_val(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

bool natural_less(const std::string& a, const std::string& b) {
    size_t ia = 0, ib = 0;
    while (ia < a.size() && ib < b.size()) {
        unsigned char ca = static_cast<unsigned char>(a[ia]);
        unsigned char cb = static_cast<unsigned char>(b[ib]);
        if (std::isdigit(ca) && std::isdigit(cb)) {
            uint64_t va = 0, vb = 0;
            size_t za = ia, zb = ib;
            while (ia < a.size() && std::isdigit(static_cast<unsigned char>(a[ia]))) {
                va = va * 10 + static_cast<uint64_t>(a[ia] - '0');
                ++ia;
            }
            while (ib < b.size() && std::isdigit(static_cast<unsigned char>(b[ib]))) {
                vb = vb * 10 + static_cast<uint64_t>(b[ib] - '0');
                ++ib;
            }
            if (va != vb) return va < vb;
            if ((ia - za) != (ib - zb)) return (ia - za) < (ib - zb);
        } else {
            int la = std::tolower(ca);
            int lb = std::tolower(cb);
            if (la != lb) return la < lb;
            ++ia;
            ++ib;
        }
    }
    if (ia != a.size() || ib != b.size()) {
        return a.size() < b.size();
    }
    return a < b;
}

}  // namespace

bool has_stl_extension(const std::string& path) {
    if (path.size() < 4) return false;
    size_t n = path.size();
    return path[n - 4] == '.' &&
           (path[n - 3] == 's' || path[n - 3] == 'S') &&
           (path[n - 2] == 't' || path[n - 2] == 'T') &&
           (path[n - 1] == 'l' || path[n - 1] == 'L');
}

std::string parse_xdnd_uri_list(const std::string& uri_list) {
    size_t pos = 0;
    while (pos < uri_list.size()) {
        size_t end = uri_list.find('\n', pos);
        if (end == std::string::npos) end = uri_list.size();
        std::string line = uri_list.substr(pos, end - pos);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        size_t start = 0;
        while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) ++start;
        if (start > 0) line.erase(0, start);

        pos = end + 1;
        if (line.empty() || line[0] == '#') continue;

        std::string path;
        if (line.rfind("file://localhost/", 0) == 0) {
            path = percent_decode(line.substr(16));
        } else if (line.rfind("file:///", 0) == 0) {
            path = percent_decode(line.substr(7));
        } else {
            continue;
        }

        if (has_stl_extension(path)) {
            return path;
        }
    }
    return {};
}

std::vector<std::string> list_sibling_stls(const std::string& file_path, int* out_current_idx) {
    namespace fs = std::filesystem;
    if (out_current_idx) *out_current_idx = -1;

    std::error_code ec;
    fs::path fp(file_path);
    fs::path dir = fp.parent_path();
    if (dir.empty()) dir = ".";

    struct Entry {
        std::string name;
        std::string full;
    };
    std::vector<Entry> items;

    for (fs::directory_iterator it(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::error_code ec2;
        if (!it->is_regular_file(ec2) || ec2) continue;
        std::string full = it->path().string();
        if (!has_stl_extension(full)) continue;
        items.push_back({it->path().filename().string(), full});
    }

    std::sort(items.begin(), items.end(), [](const Entry& a, const Entry& b) {
        return natural_less(a.name, b.name);
    });

    std::vector<std::string> result;
    result.reserve(items.size());
    std::string target_name = fp.filename().string();
    fs::path target_canon = fs::weakly_canonical(fp, ec);

    for (size_t i = 0; i < items.size(); ++i) {
        result.push_back(items[i].full);
        if (out_current_idx && *out_current_idx < 0) {
            if (items[i].full == file_path || items[i].name == target_name) {
                *out_current_idx = static_cast<int>(i);
            } else {
                std::error_code ec3;
                fs::path c = fs::weakly_canonical(items[i].full, ec3);
                if (!ec && !ec3 && c == target_canon) {
                    *out_current_idx = static_cast<int>(i);
                }
            }
        }
    }
    return result;
}

std::string step_sibling_stl(const std::string& file_path, int delta) {
    int idx = -1;
    auto list = list_sibling_stls(file_path, &idx);
    if (list.empty()) return {};
    if (idx < 0) return list[0];
    int n = static_cast<int>(list.size());
    int next = ((idx + delta) % n + n) % n;
    return list[static_cast<size_t>(next)];
}

}  // namespace faset
