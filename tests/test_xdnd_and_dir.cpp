#include "test_framework.hpp"
#include "uri_dir.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace faset;

TEST(xdnd_uri_list_decoding) {
    // UTF-8 percent-encoded Turkish path
    std::string u1 = "file:///home/toshi/Belgeler/3d/%C3%A7amurluk/Izomer_Camurluk.stl\r\n";
    CHECK_EQ(parse_xdnd_uri_list(u1), std::string("/home/toshi/Belgeler/3d/\xc3\xa7" "amurluk/Izomer_Camurluk.stl"));

    // file://localhost and %20 spaces
    std::string u2 = "file://localhost/tmp/My%20Model%20v2.STL\nfile:///tmp/other.stl\n";
    CHECK_EQ(parse_xdnd_uri_list(u2), std::string("/tmp/My Model v2.STL"));

    // Picks first .stl among mixed files and ignores comments
    std::string u3 = "# comment\r\nfile:///tmp/notes.txt\r\nfile:///tmp/part.stl\r\n";
    CHECK_EQ(parse_xdnd_uri_list(u3), std::string("/tmp/part.stl"));

    // Rejects non-file URIs and empty input
    CHECK_EQ(parse_xdnd_uri_list("https://example.com/model.stl\r\n"), std::string(""));
    CHECK_EQ(parse_xdnd_uri_list(""), std::string(""));
    CHECK_EQ(parse_xdnd_uri_list("file:///tmp/image.png\n"), std::string(""));
}

TEST(sibling_stl_natural_sort_and_wrap) {
    namespace fs = std::filesystem;
    std::string dir = "/tmp/faset_dir_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    auto touch = [&](const std::string& name) {
        std::ofstream f(dir + "/" + name);
        f << "x";
    };
    touch("Part_10.stl");
    touch("part_2.STL");
    touch("alpha.stl");
    touch("ignore.txt");
    touch("model.sc");
    fs::create_directories(dir + "/subdir.stl", ec);  // directory named .stl must be ignored

    int idx = -1;
    auto list = list_sibling_stls(dir + "/part_2.STL", &idx);
    CHECK_EQ(list.size(), 3u);
    CHECK_EQ(list[0], dir + "/alpha.stl");
    CHECK_EQ(list[1], dir + "/part_2.STL");
    CHECK_EQ(list[2], dir + "/Part_10.stl");
    CHECK_EQ(idx, 1);

    CHECK_EQ(step_sibling_stl(dir + "/part_2.STL", +1), dir + "/Part_10.stl");
    CHECK_EQ(step_sibling_stl(dir + "/Part_10.stl", +1), dir + "/alpha.stl");  // wrap forward
    CHECK_EQ(step_sibling_stl(dir + "/alpha.stl", -1), dir + "/Part_10.stl");   // wrap backward

    fs::remove_all(dir, ec);
}
