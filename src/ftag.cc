#include <algorithm>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <sstream>
#include <unordered_map>
#include <vector>

#include <cstring>

#include <sys/stat.h>
#include <sys/xattr.h>
/* setxattr("reconnect.mjs", "ftag.fid", "103", 3, 0) */
/* listxattr */
/* getxattr("reconnect.mjs", "user.ftag", NULL, 0) */


#define ERR_EXIT(A, ...) { \
    std::fprintf(stderr, "error: file " __FILE__ ":%i in %s(): ", __LINE__, __func__); \
    std::fprintf(stderr, __VA_ARGS__); \
    std::fputc('\n', stderr); \
    std::exit(static_cast<int>(A)); \
}

#define WARN(...) { \
    std::fprintf(stderr, "warning: file " __FILE__ ":%i in %s(): ", __LINE__, __func__); \
    std::fprintf(stderr, __VA_ARGS__); \
    std::fputc('\n', stderr); \
}


template <typename T>
T get_random_int() {
    static std::random_device device{};
    static std::default_random_engine engine(device());
    return std::uniform_int_distribution<T>()(engine);
}

bool file_exists(const std::string &filename) {
    struct stat buffer{};
    return !stat(filename.c_str(), &buffer);
}


std::uint64_t generate_unique_tid();

struct color_t {
    std::uint16_t r = 0, g = 0, b = 0;
};


static constexpr std::string esc = "\033[";
static constexpr std::string reset = esc + "0m";

std::string color_out(const color_t &color, bool is_fg) {
    return esc + (is_fg ? "38;2;" : "48;2;") + std::to_string(color.r) + ';' + std::to_string(color.g) + ';' + std::to_string(color.b) + 'm';
}

void string_color_fg(const color_t &color, const std::string &str, std::ostream &out = std::cout) {
    out << color_out(color, true) << str << reset;
}

/* for both, 0 is an invalid value */
using tid_t = std::uint64_t; /* temporary, changes every run */
using fid_t = std::uint64_t; /* permanent, uses xattrs to store in file, name is "user.ftag.id" */

struct tag_t {
    std::uint64_t id = 0;
    std::string name; /* can't have spaces, parens, colons, and cannot start with a dash, encourages plain naming style something-like-this */
    std::optional<color_t> color;
    std::vector<tid_t> sub;
    std::vector<tid_t> super;
    std::vector<fid_t> files; /* file ids */
};

std::unordered_map<tid_t, tag_t> tags; /* NOLINT */

std::uint64_t generate_unique_tid() {
    std::uint64_t id = 0;
    bool in = false;
    do {
        in = false;
        id = get_random_int<std::uint64_t>();
        for (const auto &[tagid, _] : tags) {
            in = in || (tagid == id);
        }
    } while (in || id == 0);
    return id;
}

void split(const std::string &s, const std::string &delim, std::vector<std::string> &outs, std::uint32_t n = 0) {
    std::size_t last = 0, next = 0;
    while ((next = s.find(delim, last)) != std::string::npos) {
        outs.push_back(s.substr(last, next - last));
        if (n > 0 && outs.size() >= n) { break; }
        last = next + delim.size();
    }
    if (last != s.size()) { outs.push_back(s.substr(last, s.size())); }
}

/* same output for "a,,b,c" as normal split on "a,b,c" both with delim "," */
void split_no_rep_delims(const std::string &s, const std::string &delim, std::vector<std::string> &outs, std::uint32_t n = 0) {
    std::size_t last = 0, next = 0;
    while ((next = s.find(delim, last)) != std::string::npos) {
        outs.push_back(s.substr(last, next - last));
        if (n > 0 && outs.size() >= n) { break; }
        last = next + delim.size();
        for (; last < s.size(); last += delim.size()) {
            if (s.substr(last, delim.size()) != delim) { break; }
        }
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

inline void remove_whitespace(std::string &str) {
    str.erase(std::remove_if(str.begin(), str.end(), [](const char &c) { return std::isspace(c); }), str.end());
}

inline void trim_whitespace(std::string &str) {
    for (std::uint32_t i = 0; i < str.size(); i++) {
        if (std::isspace(str[i])) {
            str.erase(str.begin() + i);
            i--;
        } else {
            break;
        }
    }
    for (auto i = static_cast<std::int32_t>(str.size() - 1); i >= 0; i--) {
        if (std::isspace(str[i])) {
            str.erase(str.begin() + i);
        } else {
            break;
        }
    }
}

static constexpr std::string tags_filename = "save.tags";

std::int32_t hex_to_rgb(const std::string &s, color_t &color) {
    return sscanf(s.c_str(), "%2hx%2hx%2hx", &color.r, &color.g, &color.b); /* NOLINT */
}

std::string rgb_to_hex(const color_t &color) {
    std::stringstream s;
    s << std::setfill('0') << std::setw(6) << std::hex;
    s << (color.r << 16 | color.g << 8 | color.b);
    return s.str();
}

/* loops in the tag graph are discouraged but are allowed, including a tag having a supertag be itself */

/* --- tag file structure ---
 *
 * tag-name: super-tag other-super-tag
 * -[file id]
 * -[file id]
 * other-tag-name (FF0000): blah-super-tag super-tag
 * blah-tag-name (#FF7F7F)
 */
void read_saved_tags() {
    if (!file_exists(tags_filename)) {
        std::fprintf(stderr, "tag file \"%s\" not found", tags_filename.c_str());
        return;
    }
    std::string tags_file_content = get_file_content(tags_filename);
    std::vector<std::string> lines;
    split(tags_file_content, "\n", lines);
    lines.erase(std::remove(lines.begin(), lines.end(), ""), lines.end());

    std::optional<tag_t> current_tag;
    std::unordered_map<tid_t, std::vector<std::string>> unresolved_stags; /* main tag id, supertag names */
    for (std::uint32_t i = 0; i < lines.size(); i++) {
        const std::string &line = lines[i];
        std::string no_whitespace_line = line;
        remove_whitespace(no_whitespace_line);
        if (no_whitespace_line.empty()) { continue; }
        std::string supertags;

#define FINISH_START_TAG { \
    if (current_tag.has_value()) { \
        tags[current_tag.value().id] = current_tag.value(); \
    } \
    current_tag = tag_t(); \
    current_tag.value().id = generate_unique_tid(); \
}

        /* is a file id line */
        if (no_whitespace_line[0] == '-') {
            if (!current_tag.has_value()) {
                ERR_EXIT(1, "tag file \"%s\" line %i had \"-[file id]\" under no active tag", tags_filename.c_str(), i + 1);
            }
            std::string file_id_str = no_whitespace_line.substr(1);
            fid_t file_id = std::strtoul(file_id_str.c_str(), nullptr, 0);
            if (file_id == 0) {
                ERR_EXIT(1, "tag file \"%s\" line %i had bad file id: \"%s\"", tags_filename.c_str(), i + 1, file_id_str.c_str());
            }
            current_tag.value().files.push_back(file_id);
            continue;
        }
        /* is a declaring tag line */
        else {
            FINISH_START_TAG;
            std::string ttag;
            std::string tname;
            bool has_colon = line.find(':') != std::string::npos;

            /* is still a declaring tag line, just without any supertags */
            if (!has_colon) {
                ttag = no_whitespace_line;
            } else {
                std::vector<std::string> sections;
                sections.reserve(2);
                split(line, ":", sections, 2);
                ttag = sections[0];
                remove_whitespace(ttag);
                supertags = sections[1];
            }
            if (ttag.empty()) {
                ERR_EXIT(1, "tag file \"%s\" line %i had empty tag name", tags_filename.c_str(), i + 1);
            }
            if (ttag.find(')') != std::string::npos && ttag.find('(') != std::string::npos) {
                current_tag.value().color = color_t();
                if (ttag.find('(') > ttag.find(')')) {
                    ERR_EXIT(1, "tag file \"%s\" line %i color had ')' before '('", tags_filename.c_str(), i + 1);
                }
                std::string hexstr = ttag.substr(ttag.find('(') + 1, ttag.find(')') - ttag.find('(') - 1);
                if (hexstr[0] == '#') { hexstr.erase(hexstr.begin()); }
                if (hex_to_rgb(hexstr, current_tag.value().color.value()) != 3) {
                    ERR_EXIT(1, "tag file \"%s\" line %i had bad hex color: \"%s\"", tags_filename.c_str(), i + 1, hexstr.c_str());
                }
                tname = ttag.substr(0, ttag.find('('));
            } else {
                tname = ttag;
            }
            if (tname.empty()) {
                ERR_EXIT(1, "tag file \"%s\" line %i had empty tag name", tags_filename.c_str(), i + 1);
            }
            /* check if tname is good */
            bool name_bad = (tname[0] == '-');
            for (const char &c : tname) {
                name_bad = name_bad || (c == ' ' || c == '(' || c == ')' || c == ':');
            }
            if (name_bad) {
                ERR_EXIT(1, "tag file \"%s\" line %i had bad tag name: \"%s\"", tags_filename.c_str(), i + 1, tname.c_str());
            }
            current_tag.value().name = tname;

            for (const auto &[_, tag] : tags) {
                if (tag.name == tname) {
                    ERR_EXIT(1, "tag file \"%s\" line %i redefined tag \"%s\"", tags_filename.c_str(), i + 1, tname.c_str());
                }
            }

            /* no supertags */
            if (!has_colon) { continue; }

            trim_whitespace(supertags);
            if (supertags.empty()) {
                WARN("tag file \"%s\" line %i tag name \"%s\" had empty supertags, expected supertags due to ':'", tags_filename.c_str(), i + 1, tname.c_str());
                continue;
            }
            std::vector<std::string> tstags;
            split_no_rep_delims(supertags, " ", tstags);
            for (const std::string &stag_name : tstags) {
                tid_t stag_id = 0;
                for (auto &[tagid, tag] : tags) {
                    if (tag.name == stag_name) {
                        stag_id = tagid;
                        tag.sub.push_back(current_tag.value().id);
                        break;
                    }
                }
                if (stag_id == 0) {
                    unresolved_stags[current_tag.value().id].push_back(stag_name);
                } else {
                    current_tag.value().super.push_back(stag_id);
                }
            }
            continue;
        }
    }

    /* residual finishing tag */
    if (current_tag.has_value()) {
        tags[current_tag.value().id] = current_tag.value();
    }

#undef FINISH_START_TAG

    for (const auto &[utag, stags] : unresolved_stags) {
        std::vector<tid_t> resolved_stag_ids;
        for (const std::string &stag_name : stags) {
            bool found = false;
            for (auto &[tagid, tag] : tags) {
                if (tag.name == stag_name) {
                    found = true;
                    tag.sub.push_back(utag);
                    resolved_stag_ids.push_back(tagid);
                }
            }
            if (!found) {
                ERR_EXIT(1, "tag file \"%s\" tag \"%s\" referenced unresolved supertag \"%s\" which was never declared after", tags_filename.c_str(), tags[utag].name.c_str(), stag_name.c_str());
            }
        }
        tags[utag].super.insert(tags[utag].super.end(), resolved_stag_ids.begin(), resolved_stag_ids.end());
    }
}

/* overwrites the file */
void dump_saved_tags() {
    std::ofstream file(tags_filename);
    for (const auto &[id, tag] : tags) {
        file << tag.name;
        if (tag.color.has_value()) {
            file << " (#" << rgb_to_hex(tag.color.value()) << ")"; /* NOLINT */
        }
        if (!tag.super.empty()) {
            file << ':';
            for (const tid_t &id : tag.super) {
                file << ' ' << tags[id].name;
            }
        }
        file << '\n';
        for (const fid_t &file_id : tag.files) {
            file << '-' << file_id << '\n';
        }
    }
}



int main(int argc, char **argv) {
    read_saved_tags();
    if (argc <= 1) {
        std::cout << "error: no action provided\ntry " << argv[0] << " --help for more information\n";
        return 1;
    }

    for (std::uint32_t i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--help")) {
            std::cout << "usage: " << argv[0] << R"( [flags]


)";
        }
    }
}
