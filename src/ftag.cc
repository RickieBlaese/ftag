#include <algorithm>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <sstream>
#include <unordered_map>
#include <vector>

#include <sys/xattr.h>
/* setxattr("reconnect.mjs", "ftag.fid", "103", 3, 0) */


template <typename T>
T get_random_int() {
    static std::random_device device{};
    static std::default_random_engine engine(device());
    return std::uniform_int_distribution<T>()(engine);
}


std::uint64_t generate_unique_tid();

struct color_t {
    std::uint16_t r = 0, g = 0, b = 0;
};

struct tag_t {
    std::uint64_t id;
    std::string name;
    color_t color;
    std::vector<std::uint64_t> sub;
    std::vector<std::uint64_t> super;
    std::vector<std::uint64_t> files; /* file ids */

    tag_t() : id(generate_unique_tid()) {}
    tag_t(std::string name, const color_t &color) : id(generate_unique_tid()), name(std::move(name)), color(color) {}
};

std::vector<tag_t> tags; /* NOLINT */

std::uint64_t generate_unique_tid() {
    std::uint64_t id = 0;
    bool in = false;
    do {
        id = get_random_int<std::uint64_t>();
        for (const tag_t &tag : tags) {
            in = in || (tag.id == id);
        }
    } while (in);
    return id;
}

void split(const std::string &s, const std::string &delim, std::vector<std::string> &outs) {
    std::size_t last = 0, next = 0;
    while ((next = s.find(delim, last)) != std::string::npos) {
        outs.push_back(s.substr(last, next - last));
        last = next + 1;
    }
    if (last != s.size()) { outs.push_back(s.substr(last, s.size())); }
}

std::string get_file_content(const std::string &filename) {
    std::ifstream file(filename);
    std::stringstream ss;
    ss << file.rdbuf();
    std::string text = ss.str();
    file.close();
    return text;
}

static constexpr std::string tags_filename = "tags.dash";

void read_saved_tags() {
    std::string tags_file_content = get_file_content(tags_filename);
    std::vector<std::string> lines;
    split(tags_file_content, "\n", lines);
    lines.erase(std::remove(lines.begin(), lines.end(), ""), lines.end());

    for (const std::string &line : lines) {

    }
}



int main() {}
