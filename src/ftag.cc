#include <algorithm>
#include <iomanip>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <random>
#include <regex>
#include <string>
#include <sstream>
#include <unordered_map>
#include <variant>
#include <vector>

#include <cstdlib>
#include <cstring>

#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>


#define STRINGIZE_NX(A) #A

#define STRINGIZE(A) STRINGIZE_NX(A)

#define VERSIONA 0
#define VERSIONB 3
#define VERSIONC 1
#define VERSION STRINGIZE(VERSIONA) "." STRINGIZE(VERSIONB) "." STRINGIZE(VERSIONC)

/* > 0 */
enum struct warn_level_t : std::uint32_t {
    all = 1,
    urgent
};

warn_level_t warn_level = warn_level_t::all; /* NOLINT */

#ifdef DEBUG_BUILD

#define ERR_EXIT(A, ...) { \
    std::fprintf(stderr, "ftag: error: file " __FILE__ ":%i in %s(): ", __LINE__, __func__); \
    std::fprintf(stderr, __VA_ARGS__); \
    std::fputc('\n', stderr); \
    std::exit(static_cast<int>(A)); \
}

#define WARN(...) { \
    if (warn_level <= warn_level_t::all) { \
        std::fprintf(stderr, "ftag: warning: file " __FILE__ ":%i in %s(): ", __LINE__, __func__); \
        std::fprintf(stderr, __VA_ARGS__); \
        std::fputc('\n', stderr); \
    } \
}

#else

#define ERR_EXIT(A, ...) { /* NOLINT */ \
    std::fputs("ftag: error: ", stderr); \
    std::fprintf(stderr, __VA_ARGS__); \
    std::fputc('\n', stderr); \
    std::exit(static_cast<int>(A)); \
}

#define WARN(...) { /* NOLINT */ \
    if (warn_level <= warn_level_t::all) { \
        std::fputs("ftag: warning: ", stderr); \
        std::fprintf(stderr, __VA_ARGS__); \
        std::fputc('\n', stderr); \
    } \
}

#endif


std::uint16_t get_columns() {
    static struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return w.ws_col;
} 

template <typename T>
T get_random_int() {
    static std::random_device device{};
    static std::default_random_engine engine(device());
    return std::uniform_int_distribution<T>()(engine);
}

template <class Key, class Tp, class Compare>
bool map_contains(const std::map<Key, Tp, Compare> &m, const Key &key) {
    return m.find(key) != m.end();
}

template <class Key, class Tp, class Compare>
bool map_contains(const std::unordered_map<Key, Tp, Compare> &m, const Key &key) {
    return m.find(key) != m.end();
}

bool file_exists(const std::string &filename, struct stat *pbuffer = nullptr) {
    struct stat buffer{};
    if (pbuffer == nullptr) {
        pbuffer = &buffer;
    }
    return !stat(filename.c_str(), pbuffer);
}

ino_t path_get_ino(const std::filesystem::path &path) {
    struct stat buffer{};
    stat(path.c_str(), &buffer);
    return buffer.st_ino;
}

std::uint64_t generate_unique_tid();

struct color_t {
    std::uint16_t r = 0, g = 0, b = 0;
};


const std::string esc = "\033["; /* NOLINT */
const std::string reset = esc + "0m"; /* NOLINT */

std::string color_out(const color_t &color, bool is_fg) {
    return esc + (is_fg ? "38;2;" : "48;2;") + std::to_string(color.r) + ';' + std::to_string(color.g) + ';' + std::to_string(color.b) + 'm';
}

void reset_out(std::ostream &out = std::cout) {
    out << reset;
}

void string_color_fg(const color_t &color, const std::string &str, std::ostream &out = std::cout) {
    out << color_out(color, true) << str << reset;
}

void bold_out(std::ostream &out = std::cout) {
    out << esc << "1m";
}

void underline_out(std::ostream &out = std::cout) {
    out << esc << "4m";
}

/* 0 is an invalid value for both tids and inode number numbers */
using tid_t = std::uint64_t; /* temporary, changes every run */

struct tag_t {
    std::uint64_t id = 0;
    std::string name; /* can't have spaces, parens, square brackets, colons, and cannot start with a dash, encourages plain naming style something-like-this */
    std::optional<color_t> color;
    std::vector<tid_t> sub;
    std::vector<tid_t> super;
    std::vector<ino_t> files; /* file inode numbers */
    bool enabled = true;
};

bool path_ok(const std::string &pathstr) {
    try {
        const std::filesystem::path p = std::filesystem::path(pathstr);
    } catch (const std::exception &e) {
        return false;
    }
    return true;
}

struct file_info_t {
    ino_t file_ino;
    std::string pathstr;
    std::vector<tid_t> tags;

    bool unresolved() const {
        return pathstr.empty();
    }
    
    /* caching? why not! clearly checking if pathstr is ok is extremely expensive... */
    bool pathstr_ok() const {
        static std::string last_pathstr;
        static bool last_ok = false;
        if (last_pathstr != pathstr) {
            last_pathstr = pathstr;
            last_ok = path_ok(pathstr);
        }
        return last_ok;
    }

    /* caching? why not! clearly grabbing the filename from pathstr is extremely expensive... */
    std::string filename() const {
        static std::string last_pathstr;
        static std::string last_filename;
        if (last_pathstr != pathstr) {
            last_pathstr = pathstr;
            last_filename = std::filesystem::path(pathstr).filename().string();
        }
        return last_filename;
    }

    std::filesystem::path path() const {
        static std::string last_pathstr;
        static std::filesystem::path last_path;
        if (last_pathstr != pathstr) {
            last_pathstr = pathstr;
            last_path = std::filesystem::path(pathstr);
        }
        return last_path;
    }
};


std::vector<tid_t> tags_parsed_order; /* NOLINT */

/* have to use a cmp struct here instead of normal lambda cmp because of storage/lifetime bs */
struct tagcmp_t {
    bool operator()(const tid_t &a, const tid_t &b) const {
        return std::find(tags_parsed_order.begin(), tags_parsed_order.end(), a) < std::find(tags_parsed_order.begin(), tags_parsed_order.end(), b);
    }
};

std::map<tid_t, tag_t, tagcmp_t> tags; /* NOLINT */

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

 /* NOLINTBEGIN */
std::string config_directory = "/.config/ftag/";
const std::string c_tags_filename = "main.tags";
const std::string c_index_filename = ".fileindex";
std::string tags_file = c_tags_filename;
std::string index_file = c_index_filename;
bool set_tags_file = false;
bool set_index_file = false;
/* NOLINTEND */

std::map<ino_t, file_info_t> file_index; /* NOLINT */

std::int32_t hex_to_rgb(const std::string &s, color_t &color) {
    return sscanf(s.c_str(), "%2hx%2hx%2hx", &color.r, &color.g, &color.b); /* NOLINT */
}

std::string rgb_to_hex(const color_t &color) {
    std::stringstream s;
    s << std::setfill('0') << std::setw(6) << std::hex;
    s << (color.r << 16 | color.g << 8 | color.b);
    return s.str();
}

bool tag_name_bad(const std::string &tname) {
    bool name_bad = (tname[0] == '-');
    for (const char &c : tname) {
        name_bad = name_bad || (c == ' ' || c == '(' || c == ')' || c == '[' || c == ']' || c == ':');
    }
    return name_bad;
}


/* loops in the tag graph are discouraged but are allowed, including a tag having a supertag be itself */

/* *** RUN read_file_index BEFORE THIS ***
 * in order to correctly/efficiently add to file_info_t::tags
 *
 * [] denote the state list, right now only possible members are 'd' for disabled and 'e' for enabled (default, so specifying 'e' is redundant)
 *
 * --- tag file structure ---
 *
 * tag-name: super-tag other-super-tag
 * -[file inode number]
 * -[file inode number]
 * other-tag-name (FF0000): blah-super-tag super-tag
 * blah-tag-name (#FF7F7F)
 * disabled-tag-name [d] (#FF7F7F): enabled-tag-name
 * enabled-tag-name
 * also-enabled-tag-name [e]
 */
void read_saved_tags() {
    std::string tags_file_content = get_file_content(tags_file);
    std::vector<std::string> lines;
    split_no_rep_delims(tags_file_content, "\n", lines);

    std::optional<tag_t> current_tag;
    std::unordered_map<tid_t, std::vector<std::string>> unresolved_stags; /* main tag id, supertag names */
    for (std::uint32_t i = 0; i < lines.size(); i++) {
        const std::string &line = lines[i];
        std::string no_whitespace_line = line;
        remove_whitespace(no_whitespace_line);
        if (no_whitespace_line.empty()) { continue; }
        std::string supertags;

#define FINISH_TAG { \
    if (current_tag.has_value()) { \
        tags_parsed_order.push_back(current_tag.value().id); /* should do before we add it */ \
        tags[current_tag.value().id] = current_tag.value(); \
    } \
}

#define START_TAG { \
    current_tag = tag_t(); \
    current_tag.value().id = generate_unique_tid(); \
}

        /* is a file inode number line */
        if (no_whitespace_line[0] == '-') {
            if (!current_tag.has_value()) {
                ERR_EXIT(1, "tag file \"%s\" line %i had \"-[file inode number]\" under no active tag", tags_file.c_str(), i + 1);
            }
            std::string file_ino_str = no_whitespace_line.substr(1);
            ino_t file_ino = std::strtoul(file_ino_str.c_str(), nullptr, 0);
            if (file_ino == 0) {
                ERR_EXIT(1, "tag file \"%s\" line %i had bad file inode number: \"%s\"", tags_file.c_str(), i + 1, file_ino_str.c_str());
            }
            current_tag.value().files.push_back(file_ino);
            if (map_contains(file_index, file_ino)) {
                file_index[file_ino].tags.push_back(current_tag.value().id);
            }
            continue;
        /* is a declaring tag line */
        } else {
            FINISH_TAG;
            START_TAG;
            std::string ttag;
            std::string tname;
            std::size_t colon_pos = line.find(':');
            bool has_colon = colon_pos != std::string::npos;

            /* is still a declaring tag line, just without any supertags */
            if (!has_colon) {
                ttag = no_whitespace_line;
            } else {
                ttag = line.substr(0, colon_pos);
                remove_whitespace(ttag);
                supertags = line.substr(colon_pos + 1);
            }
            if (ttag.empty()) {
                ERR_EXIT(1, "tag file \"%s\" line %i had empty tag name", tags_file.c_str(), i + 1);
            }
            std::size_t sqbegin = ttag.find('[');
            std::size_t sqend = ttag.find(']');
            std::size_t pbegin = ttag.find('(');
            std::size_t pend = ttag.find(')');
            bool has_states = false;
            bool has_color = false;

            if (sqend != std::string::npos && sqbegin != std::string::npos) {
                if (sqbegin >= sqend) {
                    ERR_EXIT(1, "tag file \"%s\" line %i state list had ']' before '['", tags_file.c_str(), i + 1);
                }
                has_states = true;
                std::string statesstr = ttag.substr(sqbegin + 1, sqend - sqbegin - 1);
                std::vector<std::string> states;
                split(statesstr, ",", states);
                for (const std::string &state : states) {
                    std::string rwstate = state;
                    if (rwstate == "e") {
                        current_tag.value().enabled = true;
                    } else if (rwstate == "d") {
                        current_tag.value().enabled = false;
                    }
                }
            }
            if (pend != std::string::npos && pbegin != std::string::npos) {
                current_tag.value().color = color_t();
                if (pbegin >= pend) {
                    ERR_EXIT(1, "tag file \"%s\" line %i color had ')' before '('", tags_file.c_str(), i + 1);
                }
                has_color = true;
                std::string hexstr = ttag.substr(pbegin + 1, pend - pbegin - 1);
                if (hexstr[0] == '#') { hexstr.erase(hexstr.begin()); }
                if (hex_to_rgb(hexstr, current_tag.value().color.value()) != 3) {
                    ERR_EXIT(1, "tag file \"%s\" line %i had bad hex color: \"%s\"", tags_file.c_str(), i + 1, hexstr.c_str());
                }
            }

            if (has_color || has_states) {
                tname = ttag.substr(0, std::min(pbegin, sqbegin));
            } else {
                tname = ttag;
            }

            if (tname.empty()) {
                ERR_EXIT(1, "tag file \"%s\" line %i had empty tag name", tags_file.c_str(), i + 1);
            }
            /* check if tname is good */
            if (tag_name_bad(tname)) {
                ERR_EXIT(1, "tag file \"%s\" line %i had bad tag name: \"%s\"", tags_file.c_str(), i + 1, tname.c_str());
            }
            current_tag.value().name = tname;

            for (const auto &[_, tag] : tags) {
                if (tag.name == tname) {
                    ERR_EXIT(1, "tag file \"%s\" line %i redefined tag \"%s\"", tags_file.c_str(), i + 1, tname.c_str());
                }
            }

            /* no supertags */
            if (!has_colon) { continue; }

            trim_whitespace(supertags);
            if (supertags.empty()) {
                WARN("tag file \"%s\" line %i tag name \"%s\" had empty supertags, expected supertags due to ':'", tags_file.c_str(), i + 1, tname.c_str());
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
    FINISH_TAG;

#undef FINISH_TAG
#undef START_TAG

    /* resolve unresolved supertags */
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
                ERR_EXIT(1, "tag file \"%s\" tag \"%s\" referenced unresolved supertag \"%s\" which was never declared after", tags_file.c_str(), tags[utag].name.c_str(), stag_name.c_str());
            }
        }
        tags[utag].super.insert(tags[utag].super.end(), resolved_stag_ids.begin(), resolved_stag_ids.end());
    }
}

/* overwrites the file */
void dump_saved_tags() {
    std::ofstream file(tags_file);
    for (const auto &[id, tag] : tags) {
        file << tag.name;

        /* states */
        if (!tag.enabled) {
            file << " [d]";
        }
        /* end states */

        if (tag.color.has_value()) {
            file << " (#" << rgb_to_hex(tag.color.value()) << ')'; /* NOLINT */
        }
        if (!tag.super.empty()) {
            file << ':';
            for (const tid_t &id : tag.super) {
                file << ' ' << tags[id].name;
            }
        }
        file << '\n';
        for (const ino_t &file_ino : tag.files) {
            file << "  -" << file_ino << '\n';
        }
    }
}

/* --- index file structure ---
 *
 * [file inode number]:[full path]\0
 * [file inode number]:[full path]\0
 */
void read_file_index() {
    std::string index_content = get_file_content(index_file);
    std::vector<std::string> lines;
    split(index_content, std::string{'\0'} + "\n", lines);
    lines.erase(std::remove(lines.begin(), lines.end(), ""), lines.end());
    for (std::uint32_t i = 0; i < lines.size(); i++) {
        const std::string &line = lines[i];
        std::size_t colon_pos = line.find(':');
        if (colon_pos == std::string::npos) {
            ERR_EXIT(1, "index file \"%s\" line %i had no ':', could not parse", index_file.c_str(), i);
        }
        std::string file_ino_str = line.substr(0, colon_pos);
        ino_t file_ino = std::strtoul(file_ino_str.c_str(), nullptr, 0);
        if (file_ino == 0) {
            ERR_EXIT(1, "index file \"%s\" line %i had bad file inode number \"%s\"", index_file.c_str(), i, file_ino_str.c_str());
        }
        std::string pathstr = line.substr(colon_pos + 1);
        /* struct stat buffer{};
        bool exists = file_exists(pathstr, &buffer); */
        if (pathstr.empty()) {
            WARN("index file \"%s\" had file inode number %lu with empty file path, you might want to run the update command", index_file.c_str(), file_ino);
        }
        /* else if (!pathstr.empty() && !exists) {
            WARN("index file \"%s\" had file inode number %lu with file path \"%s\" which does not exist, you might want to run the update command", index_file.c_str(), file_ino, pathstr.c_str());
        } else if (exists && buffer.st_ino != file_ino) {
            WARN("index file \"%s\" had file inode number %lu with file path \"%s\" which exists but has different inode number %lu on disk, you might want to run the update command", index_file.c_str(), file_ino, pathstr.c_str(), buffer.st_ino);
        } */
        /* ***
         * weakly_canonical does file exists checks... performance killer!
         * *** */
        /* std::filesystem::path can = std::filesystem::weakly_canonical(pathstr); */
        file_index[file_ino] = file_info_t{file_ino, std::filesystem::path(pathstr)};
    }
}

void dump_file_index() {
    std::ofstream file(index_file);
    for (const auto &[file_ino, file_info] : file_index) {
        /* file << file_ino << ':' << std::filesystem::weakly_canonical(file_info.pathstr).string() << std::string{'\0'} + "\n"; */
        file << file_ino << ':' << std::filesystem::path(file_info.pathstr).string() << std::string{'\0'} + "\n";
    }
}


std::vector<tid_t> enabled_only(const std::vector<tid_t> &tagids) {
    std::vector<tid_t> ret;
    for (const tid_t &id : tagids) {
        if (tags[id].enabled) {
            ret.push_back(id);
        }
    }
    return ret;
}


enum struct display_type_t : std::uint16_t {
    tags_files, tags, files
};

enum struct show_file_info_t : std::uint16_t {
    filename_only, full_path_only, inum_only, include_parent_dir, relative_path, full_info
};

enum struct show_tag_info_t : std::uint16_t {
    name_only, full_info, chain
};

enum struct search_rule_type_t : std::uint16_t {
    tag, tag_exclude, file, file_exclude, all, all_exclude,
    all_list, all_list_exclude,
    inode, inode_exclude
};

enum struct search_opt_t : std::uint16_t {
    exact, text_includes, regex
};

struct search_rule_t {
    search_rule_type_t type = search_rule_type_t::tag; /* doesn't matter not used */
    search_opt_t opt = search_opt_t::exact;
    std::string text;
    ino_t inum = 0;
};

const std::unordered_map<std::string, search_rule_type_t> arg_to_rule_type = { /* NOLINT */
    {"t", search_rule_type_t::tag},
    {"tag", search_rule_type_t::tag},
    {"te", search_rule_type_t::tag_exclude},
    {"tag-exclude", search_rule_type_t::tag_exclude},
    {"f", search_rule_type_t::file},
    {"file", search_rule_type_t::file},
    {"fe", search_rule_type_t::file_exclude},
    {"file-exclude", search_rule_type_t::file_exclude},
    {"a", search_rule_type_t::all},
    {"all", search_rule_type_t::all},
    {"ae", search_rule_type_t::all_exclude},
    {"all-exclude", search_rule_type_t::all_exclude},

    {"al", search_rule_type_t::all_list},
    {"all-list", search_rule_type_t::all_list},
    {"ale", search_rule_type_t::all_list_exclude},
    {"all-list-exclude", search_rule_type_t::all_list_exclude},

    {"i", search_rule_type_t::inode},
    {"inode", search_rule_type_t::inode},
    {"ie", search_rule_type_t::inode_exclude},
    {"inode-exclude", search_rule_type_t::inode_exclude}
};

const std::unordered_map<std::string, search_opt_t> arg_to_opt = { /* NOLINT */
    {"s", search_opt_t::text_includes},
    {"r", search_opt_t::regex}
};

enum struct change_entry_type_t : std::uint16_t {
    /* all entries is slightly misleading since we don't include like symlinks, /dev/null (character files), etc. */
    only_files, only_directories, all_entries
};

enum struct change_rule_type_t : std::uint16_t {
    single_file, recursive, inode_number
};

struct change_rule_t {
    std::filesystem::path path;
    change_rule_type_t type;
    ino_t file_ino = 0;
    bool from_ino = false;
};

enum struct fix_rule_type_t : std::uint16_t {
    rip, rii, rpp, rpi, path_all, path_p, path_i
};

struct fix_rule_t {
    std::variant<ino_t, std::filesystem::path> a, b;
    std::variant<ino_t, std::filesystem::path> path_d; /* used for when type is path_all, path_p, or path_i */
    fix_rule_type_t type;
};

void add_all(const tid_t &tagid, std::vector<tid_t> &tags_visited, std::map<tid_t, bool, tagcmp_t> &tags_map, std::map<ino_t, bool> &files_map, bool exclude) {
    if (std::find(tags_visited.begin(), tags_visited.end(), tagid) == tags_visited.end()) {
        tags_visited.push_back(tagid);
        tags_map[tagid] = !exclude;
        for (const ino_t &file_ino : tags[tagid].files) {
            files_map[file_ino] = !exclude;
        }
    } else {
        return;
    }
    for (const tid_t &id : enabled_only(tags[tagid].sub)) {
        add_all(id, tags_visited, tags_map, files_map, exclude);
    }
}

enum struct chain_relation_type_t : std::uint16_t {
    original, super, sub
};

void display_tag_info(const tag_t &tag, std::vector<tid_t> &tags_visited, std::map<tid_t, bool, tagcmp_t> &tags_matched, bool color_enabled, const show_tag_info_t &show_tag_info, bool no_formatting, chain_relation_type_t relation, std::optional<std::uint32_t> custom_file_count = {}) { /* notably, does not append newline */
    if (std::find(tags_visited.begin(), tags_visited.end(), tag.id) == tags_visited.end()) {
        tags_visited.push_back(tag.id);
    } else {
        if (relation == chain_relation_type_t::original && !no_formatting) {
            underline_out();
        }
        if (tags_matched[tag.id] && !no_formatting) {
            bold_out();
        }
        if (color_enabled && tag.color.has_value() && !no_formatting) {
            string_color_fg(tag.color.value(), tag.name);
        } else {
            std::cout << tag.name;
        }
        if (relation == chain_relation_type_t::original && !no_formatting) {
            reset_out();
        }
        if (show_tag_info == show_tag_info_t::full_info && relation == chain_relation_type_t::original) {
            if (custom_file_count.has_value()) {
                std::cout << " {" << custom_file_count.value() << "}";
            } else {
                std::cout << " {" << tag.files.size() << "}";
            }
        }
        return;
    }
    if (relation != chain_relation_type_t::sub && show_tag_info != show_tag_info_t::name_only) {
        std::vector<tid_t> tagsuper = enabled_only(tag.super);
        if (!tagsuper.empty()) {
            if (tagsuper.size() > 1) {
                std::cout << '(';
                for (std::uint32_t i = 0; i < tagsuper.size() - 1; i++) {
                    display_tag_info(tags[tagsuper[i]], tags_visited, tags_matched, color_enabled, show_tag_info, no_formatting, chain_relation_type_t::super);
                    std::cout << " | ";
                }
                display_tag_info(tags[tagsuper[tagsuper.size() - 1]], tags_visited, tags_matched, color_enabled, show_tag_info, no_formatting, chain_relation_type_t::super);
                std::cout << ')';
            } else {
                display_tag_info(tags[tagsuper[0]], tags_visited, tags_matched, color_enabled, show_tag_info, no_formatting, chain_relation_type_t::super);
            }
            std::cout << " > ";
        }
    }
    if (relation == chain_relation_type_t::original && !no_formatting) {
        underline_out();
    }
    if (tags_matched[tag.id] && !no_formatting) {
        bold_out();
    }
    if (color_enabled && tag.color.has_value() && !no_formatting) {
        string_color_fg(tag.color.value(), tag.name);
    } else {
        std::cout << tag.name;
    }
    if (relation == chain_relation_type_t::original && !no_formatting) {
        reset_out();
    }
    if (show_tag_info == show_tag_info_t::full_info && relation == chain_relation_type_t::original) {
        if (custom_file_count.has_value()) {
            std::cout << " {" << custom_file_count.value() << "}";
        } else {
            std::cout << " {" << tag.files.size() << "}";
        }
    }
    if (relation != chain_relation_type_t::super && show_tag_info == show_tag_info_t::full_info) {
        std::vector<tid_t> tagsub = enabled_only(tag.sub);
        if (!tagsub.empty()) {
            std::cout << " > ";
            if (tagsub.size() > 1) {
                std::cout << '(';
                for (std::uint32_t i = 0; i < tagsub.size() - 1; i++) {
                    display_tag_info(tags[tagsub[i]], tags_visited, tags_matched, color_enabled, show_tag_info, no_formatting, chain_relation_type_t::sub);
                    std::cout << " | ";
                }
                display_tag_info(tags[tagsub[tagsub.size() - 1]], tags_visited, tags_matched, color_enabled, show_tag_info, no_formatting, chain_relation_type_t::sub);
                std::cout << ')';
            } else {
                display_tag_info(tags[tagsub[0]], tags_visited, tags_matched, color_enabled, show_tag_info, no_formatting, chain_relation_type_t::sub);
            }
        }
    }
}


struct string_format_t {
    std::string str;
    bool underline = false;
    bool bold = false;

    void display(bool no_formatting, std::ostream &out = std::cout) const {
        if (!no_formatting) {
            if (underline) {
                underline_out(out);
            }
            if (bold) {
                bold_out(out);
            }
            out << str;
            if (bold || underline) {
                reset_out(out);
            }
        } else {
            out << str;
        }
    }
};


/* does not handle "  " in the beginning for noncompact output */
string_format_t string_format_file_info(const file_info_t &file_info, bool was_matched, const show_file_info_t &show_file_info, bool no_formatting, bool quoted) {
    std::stringstream ret;
    bool underline = false, bold = false;
    if (show_file_info == show_file_info_t::inum_only) {
        ret << file_info.file_ino;
    } else {
        if (file_info.unresolved()) {
            if (!no_formatting) {
                underline = true;
            }
            if (was_matched && !no_formatting) {
                bold = true;
            }
            ret << "<unresolved>";
            if (show_file_info == show_file_info_t::full_info) {
                ret << " {" << file_info.tags.size() << "} (" << file_info.file_ino << ')';
            }
        } else {
            if (was_matched && !no_formatting) {
                bold = true;
            }
            if (show_file_info == show_file_info_t::full_path_only) {
                ret << std::filesystem::path(file_info.pathstr);
            } else if (show_file_info == show_file_info_t::full_info) {
                ret << file_info.filename() << " {" << file_info.tags.size() << "} (" << file_info.file_ino << "): " << std::filesystem::path(file_info.pathstr);
            } else if (show_file_info == show_file_info_t::filename_only) {
                if (quoted) {
                    ret << std::quoted(file_info.filename());
                } else {
                    ret << file_info.filename();
                }
            } else if (show_file_info == show_file_info_t::include_parent_dir) {
                std::filesystem::path tpath = file_info.path();
                if (quoted) {
                    ret << tpath.parent_path().filename()/tpath.filename();
                } else {
                    ret << (tpath.parent_path().filename()/tpath.filename()).string();
                }
            } else if (show_file_info == show_file_info_t::relative_path) {
                std::filesystem::path tpath = file_info.path();
                ret << tpath.lexically_proximate(std::filesystem::current_path());
            } else if (show_file_info == show_file_info_t::inum_only) {
                if (quoted) {
                    ret << std::quoted(std::to_string(file_info.file_ino));
                } else {
                    ret << std::to_string(file_info.file_ino);
                }
            }
        }
    }
    return string_format_t{.str = ret.str(), .underline = underline, .bold = bold};
}

void display_file_list(const std::vector<ino_t> &file_inos, const std::map<ino_t, bool> &matched, bool compact_output, const show_file_info_t &show_file_info, bool no_formatting, bool quoted) {
    static thread_local std::uint16_t cols = 0;
    static constexpr std::uint64_t name_sep = 2;
    const std::string sep(name_sep, ' ');
    std::vector<string_format_t> formats;
    formats.reserve(file_inos.size());
    for (const ino_t &file_ino : file_inos) {
        formats.push_back(string_format_file_info(file_index[file_ino], matched.at(file_ino), show_file_info, no_formatting, quoted));
    }
    if (compact_output) {
        if (cols == 0) {
            cols = get_columns();
        }
        std::uint64_t length = std::max(static_cast<std::int64_t>(formats.size()) * static_cast<std::int64_t>(name_sep), 0L);
        for (const string_format_t &tformat : formats) {
            length += tformat.str.size();
        }
        if (length > cols) {
            for (std::uint32_t r = 2;; r++) { /* try all dimensions */
                for (std::int32_t c = std::ceil(formats.size() / static_cast<long double>(r)); c >= 0; c--) {
                    if (r * c < formats.size()) { continue; }
                    std::vector<std::uint64_t> tlengths(c);
                    std::uint64_t ttlength = name_sep * c;
                    for (std::uint32_t ci = 0; ci < c; ci++) {
                        std::uint64_t tlength = 0;
                        for (std::uint32_t ri = 0; ri < r; ri++) {
                            if (r * ci + ri >= formats.size()) { continue; }
                            tlength = std::max(tlength, formats[r * ci + ri].str.size());
                        }
                        ttlength += tlength;
                        tlengths[ci] = tlength;
                    }
                    if (ttlength <= cols || c == 1) { /* is ok, now we use */
                        for (std::uint32_t ri = 0; ri < r; ri++) {
                            for (std::uint32_t ci = 0; ci < c; ci++) {
                                if (r * ci + ri >= formats.size()) { continue; }
                                const string_format_t &tformat = formats[r * ci + ri];
                                std::cout << sep;
                                tformat.display(no_formatting);
                                std::cout << std::string(tlengths[ci] - tformat.str.size(), ' ');
                            }
                            std::cout << '\n';
                        }
                        goto end_output;
                    }
                }
            }
        } else { /* can just output all in one line */
            for (const string_format_t &tformat : formats) {
                std::cout << sep;
                tformat.display(no_formatting);
            }
            std::cout << '\n';
        }
        end_output: {}
    } else {
        for (const string_format_t &tformat : formats) {
            std::cout << "  ";
            tformat.display(no_formatting);
            std::cout << '\n';
        }
    }
}


ino_t search_index(const std::filesystem::path &tpath) {
    for (const auto &[file_ino, file_info] : file_index) {
        if (file_info.pathstr_ok()) {
            std::filesystem::path opath = std::filesystem::path(file_info.pathstr).lexically_normal();
            if (tpath == opath) {
                return file_ino;
            }
        }
    }
    return 0;
}

ino_t search_use_fs(const std::filesystem::path &tpath) {
    struct stat buf{};
    if (file_exists(tpath.string(), &buf)) {
        if (map_contains(file_index, buf.st_ino)) {
            return buf.st_ino;
        }
    }
    return 0;
}

std::string read_stdin() {
    static constexpr std::streamsize n = 128;
    std::streamsize readn = 0;
    std::string in(n, '\0');
    std::cin.read(in.data(), n);
    readn += std::cin.gcount();
    while (!std::cin.eof()) {
        in.resize(readn * 2, '\0');
        std::cin.read(in.data() + readn, readn);
        readn += std::cin.gcount();
    }
    in.resize(readn);
    return in;
}

void parse_as_args(std::vector<std::string> &ret, const std::string &s, const std::string &err_command_name, const std::string &err_could_not) {
    bool prev_backslash = false;
    bool in_quote = false;
    bool prev_space = false;
    std::string current_arg = "";
    for (const char &c : s) {
        if (c == '\\' && !prev_backslash) {
            prev_backslash = true;
            continue;
        } else if (c == '"' && !prev_backslash) {
            in_quote = !in_quote;
            continue;
        } else if (std::isspace(static_cast<unsigned char>(c)) && !prev_backslash && !in_quote) {
            if (prev_space || current_arg.empty()) {
                prev_space = true;
                continue;
            }
            prev_space = true;
            ret.push_back(current_arg);
            current_arg = "";
            continue;
        }
        prev_space = false;
        prev_backslash = false;
        current_arg.push_back(c);
    }
    if (!current_arg.empty()) {
        ret.push_back(current_arg);
    }
    if (in_quote) {
        std::fprintf(stderr, "%s: %s, unclosed quote\n", err_command_name.c_str(), err_could_not.c_str());
        exit(1);
    }
}


/* starts parsing from position 0 in argv, offset it if need be */
void parse_file_args(int argc, char **argv, const std::string &err_command_name, bool is_update, std::vector<change_rule_t> &to_change, bool &search_index_first, change_entry_type_t &change_entry_type, bool use_canonical) {
    std::vector<std::string> sargv;
    bool recognize_dash = true;
    bool parse_per_line = true;
    for (std::uint32_t i = 0; i < argc; i++) {
        if (!std::strcmp(argv[i], "-rd") || !std::strcmp(argv[i], "--recognize-dash")) {
            recognize_dash = true;
        } else if (!std::strcmp(argv[i], "-id") || !std::strcmp(argv[i], "--ignore-dash")) {
            recognize_dash = false;
        } else if (!std::strcmp(argv[i], "-sa") || !std::strcmp(argv[i], "--stdin-parse-as-args")) {
            parse_per_line = false;
        } else if (!std::strcmp(argv[i], "-sl") || !std::strcmp(argv[i], "--stdin-parse-per-line")) {
            parse_per_line = true;
        } else {
            sargv.emplace_back(argv[i]);
        }
    }
    std::uint32_t sargc = sargv.size();
    for (std::uint32_t i = 0; i < sargc; i++) {
        if (sargv[i] == "-f" || sargv[i] == "--file") {
            i++;
            if (i >= sargc) {
                ERR_EXIT(1, "%s: expected at least one file/directory after file flag", err_command_name.c_str());
            }
            for (; i < sargc; i++) {
                if (sargv[i] == "-" && recognize_dash) {
                    WARN("%s: recognizing \"-\", reading remaining file names from stdin", err_command_name.c_str());
                    std::string in = read_stdin();
                    std::vector<std::string> inargs;
                    if (parse_per_line) {
                        split_no_rep_delims(in, "\n", inargs);
                    } else {
                        parse_as_args(inargs, in, err_command_name, "could not parse stdin as file name args");
                    }
                    for (const std::string &s : inargs) {
                        to_change.push_back(change_rule_t{s, change_rule_type_t::single_file});
                    }
                    break;
                }
                if (sargv[i][0] == '-' && sargv[i] != "-") {
                    WARN("%s: argument %i file/directory \"%s\" began with '-', interpreting as a file, you cannot pass another flag", err_command_name.c_str(), i, sargv[i].c_str());
                }
                if (!path_ok(sargv[i])) {
                    ERR_EXIT(1, "%s: argument %i file/directory \"%s\" could not construct path", err_command_name.c_str(), i, sargv[i].c_str());
                }

                std::filesystem::path tpath;
                if (use_canonical) {
                    tpath = sargv[i];
                    if (!std::filesystem::exists(tpath)) {
                        ERR_EXIT(1, "%s: argument %i file/directory \"%s\" could not use, does not exist", err_command_name.c_str(), i, tpath.c_str());
                    }
                    tpath = std::filesystem::canonical(tpath);
                } else {
                    tpath = std::filesystem::path(sargv[i]).lexically_normal();
                }
                if (tpath.empty()) {
                    ERR_EXIT(1, "%s: argument %i was empty file/directory path", err_command_name.c_str(), i);
                }

                to_change.push_back(change_rule_t{tpath, change_rule_type_t::single_file});
            }
        } else if (sargv[i] == "-r" || sargv[i] == "--recursive") {
            i++;
            if (i >= sargc) {
                ERR_EXIT(1, "%s: expected at least one directory after recursive flag", err_command_name.c_str());
            }
            for (; i < sargc; i++) {
                if (sargv[i] == "-" && recognize_dash) {
                    WARN("%s: recognizing \"-\", reading remaining directory names from stdin", err_command_name.c_str());
                    std::string in = read_stdin();
                    std::vector<std::string> inargs;
                    if (parse_per_line) {
                        split_no_rep_delims(in, "\n", inargs);
                    } else {
                        parse_as_args(inargs, in, err_command_name, "could not parse stdin as directory name args");
                    }
                    for (const std::string &s : inargs) {
                        to_change.push_back(change_rule_t{s, change_rule_type_t::recursive});
                    }
                    break;
                }
                if (sargv[i][0] == '-' && sargv[i] != "-") {
                    WARN("%s: argument %i directory \"%s\" began with '-', interpreting as a directory, you cannot pass another flag", err_command_name.c_str(), i, sargv[i].c_str());
                }
                if (!path_ok(sargv[i])) {
                    ERR_EXIT(1, "%s: argument %i directory \"%s\" could not construct path", err_command_name.c_str(), i, sargv[i].c_str());
                }

                std::filesystem::path tpath;
                if (use_canonical) {
                    tpath = sargv[i];
                    if (!std::filesystem::exists(tpath)) {
                        ERR_EXIT(1, "%s: argument %i file/directory \"%s\" could not use, does not exist", err_command_name.c_str(), i, tpath.c_str());
                    }
                    tpath = std::filesystem::canonical(tpath);
                } else {
                    tpath = std::filesystem::path(sargv[i]).lexically_normal();
                }
                if (tpath.empty()) {
                    ERR_EXIT(1, "%s: argument %i was empty file/directory path", err_command_name.c_str(), i);
                }

                to_change.push_back(change_rule_t{tpath, change_rule_type_t::recursive});
            }
        } else if (sargv[i] == "-i" || sargv[i] == "--inode") {
            if (is_update) {
                ERR_EXIT(1, "%s: cannot update from inode numbers, specify files or directories with the appropriate flags, read the update command section of ftag --help for more info", err_command_name.c_str());
            }
            i++;
            if (i >= sargc) {
                ERR_EXIT(1, "%s: expected at least one inode number after inode flag", err_command_name.c_str());
            }
            for (; i < sargc; i++) {
                if (sargv[i] == "-" && recognize_dash) {
                    WARN("%s: recognizing \"-\", reading remaining inode numbers for this flag from stdin", err_command_name.c_str());
                    std::string in = read_stdin();
                    std::vector<std::string> inargs;
                    if (parse_per_line) {
                        split_no_rep_delims(in, "\n", inargs);
                    } else {
                        parse_as_args(inargs, in, err_command_name, "could not parse stdin as inode number args");
                    }
                    for (const std::string &s : inargs) {
                        to_change.push_back(change_rule_t{s, change_rule_type_t::inode_number});
                    }
                    break;
                }
                if (sargv[i][0] == '-') {
                    i--;
                    break;
                }
                ino_t file_ino = std::strtoul(sargv[i].c_str(), nullptr, 0);
                if (file_ino == 0) {
                    ERR_EXIT(1, "%s: argument %i inode number \"%s\" was not valid", err_command_name.c_str(), i, sargv[i].c_str());
                }
                to_change.push_back(change_rule_t{"", change_rule_type_t::inode_number, file_ino});
            }
        } else if (sargv[i] == "--search-index") {
            search_index_first = true;
        } else if (sargv[i] == "--no-search-index") {
            search_index_first = false;
        } else if (sargv[i] == "--only-files") {
            change_entry_type = change_entry_type_t::only_files;
        } else if (sargv[i] == "--only-directories") {
            change_entry_type = change_entry_type_t::only_directories;
        } else if (sargv[i] == "--all-entries") {
            change_entry_type = change_entry_type_t::all_entries;
        } else {
            ERR_EXIT(1, "%s: flag \"%s\" was not recognized", err_command_name.c_str(), sargv[i].c_str());
        }
    }
}

/* recursive */
void get_all(const std::filesystem::path &path, std::vector<change_rule_t> &out, std::uint32_t position, const change_entry_type_t &change_entry_type) {
    for (const auto &entry : std::filesystem::recursive_directory_iterator(path)) {
        if ((change_entry_type == change_entry_type_t::all_entries      && (entry.is_regular_file() || entry.is_directory())) ||
            (change_entry_type == change_entry_type_t::only_files       && entry.is_regular_file()) ||
            (change_entry_type == change_entry_type_t::only_directories && entry.is_directory())
        ) {
            out.insert(out.begin() + position++, change_rule_t{entry.path(), change_rule_type_t::single_file, path_get_ino(entry.path())});
        }
    }
}



int main(int argc, char **argv) { /* NOLINT */
    bool custom_tags_file = false;
    bool custom_index_file = false;
    const char *envindex = std::getenv("FTAG_INDEX_FILE");
    if (envindex && *envindex && !set_index_file) {
        index_file = envindex;
        custom_index_file = true;
        set_index_file = true;
    }
    const char *envtags = std::getenv("FTAG_TAGS_FILE");
    if (envtags && *envtags && !set_tags_file) {
        tags_file = envtags;
        custom_tags_file = true;
        set_tags_file = true;
    }

    if (!set_tags_file || !set_index_file) {
        const char *envhome = std::getenv("HOME");
        if (envhome && *envhome) {
            config_directory = envhome + config_directory;
            if (!set_tags_file) {
                index_file = config_directory + index_file;
                set_tags_file = true;
            }
            if (!set_index_file) {
                tags_file = config_directory + tags_file;
                set_index_file = true;
            }
        }
    }

    if (argc <= 1) {
        WARN("no action provided, see %s --help for more information", argv[0]);
        return 1;
    }


    for (std::uint32_t i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
            std::cout << "usage: " << argv[0] << R"( [command] [flags]

description:
    ftag is a utility to tag files/directories on your filesystem, using inode numbers to track and identify them,
    without modifying files on disk

    tags consist of a name, an optional color, and so-called supertags that they descend from

commands:
    search [flags]                      : searches for and returns tags and files
    tag <subcommand> <tagname> [flags]  : create/edit/delete tags, and assign and remove files from tags
    add <flags>                         : adds files to be tracked/tagged by ftag
    rm <flags>                          : removes files to be tracked/tagged by ftag
    update [flags]                      : updates the index of tracked files, use if some have been moved/renamed
    fix [flags]                         : fixes the inode numbers used in the tags file and index file

no command flags:
    -h, --help                    : displays basic help
    -H, --HELP                    : displays extended help
    -v, --version                 : displays ftag's version
    -w, --warn <warnlevel>        : sets warn level

)";
            return 0;
        }
        if (!std::strcmp(argv[i], "--HELP") || !std::strcmp(argv[i], "-H")) {
            std::cout << "usage: " << argv[0] << R"( [command] [flags]

description:
    ftag is a utility to tag files/directories on your filesystem, using inode numbers to track and identify them,
    without modifying files on disk

    tags consist of a name, an optional color, and so-called supertags that they descend from.
    tag names can't have spaces, parens, square brackets, colons, and cannot start with a dash, encouraging a
    plain naming style like-this

    with designating supertags, you can construct a large and complicated tag graph. ftag supports it fine and works with
    it, but placing a tag in a cycle with itself is discouraged for obvious reasons

    ftag (by default) stores saved tags in "$HOME)" << config_directory << c_tags_filename << R"(" and the
    index file in "$HOME)" << config_directory << c_index_filename << R"(".
    the tag file format and index file format are designed to be almost entirely human-readable and editable.
    however, they do reference files by their inode numbers, which might be slightly unwieldly

commands:
    search [flags]                      : searches for and returns tags and files
    tag <subcommand> <tagname> [flags]  : create/edit/delete tags, and assign and remove files from tags
    add <flags>                         : adds files to be tracked/tagged by ftag
    rm <flags>                          : removes files to be tracked/tagged by ftag
    update [flags]                      : updates the index of tracked files, use if some have been moved/renamed
    fix [flags]                         : fixes the inode numbers used in the tags file and index file

no command flags:
    -h, --help                    : displays basic help
    -H, --HELP                    : displays extended help
    -v, --version                 : displays ftag's version
    -w, --warn <warnlevel>        : sets warn level

command flags:
    search:
        -al,  --all-list              : includes all tags and files
        -ale, --all-list-exclude      : excludes all tags and files

        -a,   --all <text>            : includes all files under tag <text> and subtags
        -ae,  --all-exclude <text>    : excludes all files under tag <text> and subtags
        -t,   --tag <text>            : includes all files with tag <text>
        -te,  --tag-exclude <text>    : excludes all files with tag <text>
        -f,   --file <text>           : includes all files with filename/path <text>
                                        (see --search-file-name and --search-file-path)
        -fe,  --file-exclude <text>   : excludes all files with filename/path <text>
                                        (see --search-file-name and --search-file-path)
        -i,   --inode <inum>          : include the file with inode <inum>
        -ie,  --inode-exclude <inum>  : exclude the file with inode <inum>

        --search-file-name            : uses filenames when searching for files (default)
                                        only has an effect when used with --file and --file-exclude
        --search-file-path            : instead of searching by filenames, search the entire file path
                                        only has an effect when used with --file and --file-exclude
                                      *** warning: may produce unexpected results

        --compact-layout              : displays files like the multi column output `ls` or `dir has
                                        (default)
        --no-compact-layout           : displays one file per line

        --tags-files                  : displays both tags and files in result (default)
        --tags-only                   : only displays tags in result, no files
        --files-only                  : only displays files in result, no tags

        --enable-color                : enables displaying tag color (default)
        --disable-color               : disables displaying tag color

        --tag-name-only               : shows only the tag name (still includes color) (default)
        --display-tag-chain           : shows the tag chain each tag descends from, up to and including repeats
        --full-tag-info               : shows all information about a tag

        --filename-only               : shows only the filename of each file (default)
        --include-parent              : shows the parent directory of the file along with the filename
        --full-path-only              : shows only the full file path
        --relative-path-only          : shows only the file path, but relative to the current directory
        --inum-only                   : shows only the file inode number
        --full-file-info              : shows all information about a file, including inode numbers

        --quoted                      : quotes each individual file info output, only has an effect when used
                                        with --filename-only, --include-parent, and --inum-only
        --normal-quotes               : only quotes --full-path-only and --relative-path-only

        --organize-by-tag             : organizes by tag, allows duplicate file output (default)
        --organize-by-file            : organizes by file, allows duplicate tag output

        --formatting                  : uses formatting (default)
        --no-formatting               : doesn't output any formatting, useful for piping/sending to other tools

        all search flags that take in <text> can be modified to do a basic search for <text> by adding an "s", like -fs or
        --file-s, or modified to interpret <text> as regex with "r", like -ter or --tag-exclude-r
        regex should probably be passed with quotes so as not to trigger normal shell wildcards

        without any flags, the search command runs --all-list

    tag:
        subcommands:
            create  <name> [color]    : creates a tag with the name <name> and hex color [color]
            delete  <name>            : deletes a tag with the name <name>
            enable  <name>            : enables a tag with the name <name>
            disable <name>            : disables a tag with the name <name>
            add  <name> <flags>       : tags file(s) with tag <name>, interprets <flags> exactly like the add command does
            rm   <name> <flags>       : untags file(s) with tag <name>, interprets <flags> exactly like the rm command does
            edit <name> <flags>       : edits a tag
                flags:
                    -as,  --add-super <supername>        : adds tag <supername> to tag <name>'s supertags
                    -rs,  --remove-super <supername>     : removes tag <supername> from tag <name>'s supertags
                    -ras, --remove-all-super             : removes all supertags from tag <name>

                    -ab,  --add-sub <subname>            : forcibly make tag <subname> descend from tag <name>
                    -rb,  --remove-sub <subname>         : forcibly removes tag <name> from tag <subname>'s supertags
                    -rab, --remove-all-sub               : forcibly removes tag <name> from all tags' supertags

                    -c,   --color <color>                : changes tag <name>'s hex color to <color>
                    -rc,  --remove-color                 : removes tag <name>'s color

                    -n,   --rename <newname>             : renames tag <name> to <newname>

    add, rm:
        -f, --file <file OR directory> [file OR directory] ...  : adds/removes files or single directories to be tracked
                                                                  (does not iterate through the contents of the directories)
        -r, --recursive <directory> [directory] ...             : adds/removes everything in the directories (recursive)
                                                                *** note: the rm command first tries to find the passed path in the
                                                                *** index file simply by comparing paths, then tries to remove by
                                                                *** the inode number found from disk. to change this behavior, see
                                                                *** --no-search-index

        -i, --inode <inum> [inum] ...                           : adds/removes inode numbers from the index

        -rd, --recognize-dash                                   : when "-" is passed to --file, --recursive, or --inode, read the
                                                                  remaining names/inode numbers from stdin (default)
        -id, --ignore-dash                                      : do not treat "-" differently

        -sl, --stdin-parse-per-line                             : if reading names/inode numbers from stdin, parse stdin as one
                                                                  argument per line (default)
        -sa, --stdin-parse-as-args                              : if reading names/inode numbers from stdin, parse stdin as if
                                                                  normal shell arguments

        --only-files                                            : only adds/removes regular files (default)
                                                                  only has an effect with --recursive
        --only-directories                                      : only adds/removes directories, including the initial <directory>
                                                                  only has an effect with --recursive
        --all-entries                                           : adds both regular files and directories, including the initial
                                                                  <directory>
                                                                  only has an effect with --recursive

    rm:
       --search-index                                           : searches through the index first to match paths when passed a
                                                                  --file or --recursive (default)
       --no-search-index                                        : removes from the index file by the inode number found on the
                                                                  filesystem from the passed path when passed a
                                                                  --file or --recursive

    update:
        -f, --file <file OR directory> [file OR directory] ...  : updates files or single directories to be tracked
                                                                  (does not iterate through the contents of the directories)
        -r, --recursive <directory> [directory] ...             : updates everything in the directories (recursive)

    add, rm, update:
        unfortunately, you cannot pass multiple flags (excluding -i, --inode, or when using "-" to indicate from stdin) for
        adding/removing/updating in one invocation of ftag to allow you to use all file/directory names, i.e. invoke only one of
        them at a time like this:
            )" << argv[0] << R"( add -f file1.txt ../script.py
            )" << argv[0] << R"( update --recursive ./directory1 /home/user
        you may, however, pass multiple inode flags and then end with a file or directory flag like such:
            )" << argv[0] << R"( rm -i 293 100 --inode 104853 --recursive ../testing /usr/lib
        this is because it is impossible for an <inum> to be a valid flag, and any argument passed in that position can
        be unambiguously determined to be a flag or a positive integer

        when update-ing, ftag always assumes the inode numbers stored in the index file ")" << index_file << R"("
        and tags file ")" << tags_file << R"(" are correct

        to reassign/change the inode numbers in the index file and tags file, use the fix command

    fix:
        -p,  --path-all                        : replaces the inode number indexed with the one found at the current indexed path
                                                 for all bad index file entries (i.e. assumes all paths are correct) 
        -pi, --path-i <inum>                   : replaces the inode number indexed with the one from <inum>'s current indexed path
                                                 from disk
        -pp, --path-p <path>                   : replaces the inode number indexed with <path> with the current inode number found
                                                 at <path> from disk

        -rip, --replace-ip <inum> <path>       : manually replaces inode number <inum> in index file with the one found at <path>
        -rii, --replace-ii <inum> <newinum>    : manually replaces inode number <inum> in index file with <newinum>
        -rpp, --replace-pp <path> <newpath>    : manually replaces inode number associated with <path> in index file with the
                                                 one from <newpath>
        -rpi, --replace-pi <path> <inum>       : manually replaces inode number associated with <path> in index file with <inum>

other:
    config file paths can be changed through $FTAG_TAGS_FILE and $FTAG_INDEX_FILE

)";
            return 0;
        }
        if (!std::strcmp(argv[i], "--version") || !std::strcmp(argv[i], "-v")) {
            std::cout << "ftag version " VERSION << std::endl;
            return 0;
        }
        if (!std::strcmp(argv[i], "--warn") || !std::strcmp(argv[i], "-w")) {
            if (i >= argc - 1) {
                ERR_EXIT(1, "expected argument <warnlevel> due to warn flag (argument %i)", i);
            }
            std::uint32_t twarn_level = std::strtoul(argv[++i], nullptr, 0);
            if (twarn_level == 0) {
                ERR_EXIT(1, "invalid warn level \"%s\"", argv[i]);
            }
            warn_level = static_cast<warn_level_t>(twarn_level);
        } else if (!std::strcmp(argv[i], "--set-tags-file") || !std::strcmp(argv[i], "-st")) {
            if (i >= argc - 1) {
                ERR_EXIT(1, "expected argument <file> due to set tags file flag (argument %i)", i);
            }
            std::string tpathstr = argv[++i];
            if (!path_ok(tpathstr)) {
                ERR_EXIT(1, "argument %i could not construct path \"%s\"", i, argv[i]);
            }
            if (!file_exists(tpathstr)) {
                ERR_EXIT(1, "argument %i set tags file path \"%s\" does not exist", i, argv[i]);
            }
            tags_file = tpathstr;
        } else if (!std::strcmp(argv[i], "--set-file-index") || !std::strcmp(argv[i], "-sf")) {
            if (i >= argc - 1) {
                ERR_EXIT(1, "expected argument <file> due to set index file flag (argument %i)", i);
            }
            std::string tpathstr = argv[++i];
            if (!path_ok(tpathstr)) {
                ERR_EXIT(1, "argument %i could not construct path \"%s\"", i, argv[i]);
            }
            if (!file_exists(tpathstr)) {
                ERR_EXIT(1, "argument %i set index file path \"%s\" does not exist", i, argv[i]);
            }
            index_file = tpathstr;
        }
    }

    bool is_search = false;

    bool is_add = false;
    bool is_rm = false;
    bool is_update = false;
    bool is_fix = false;
    bool is_tag = false;

    std::vector<std::string> commands = {
        "search", "add", "rm", "update", "fix", "tag"
    };
    std::vector<std::string> matches;
    for (const std::string &cmdname : commands) {
        if (cmdname.starts_with(argv[1])) {
            matches.push_back(cmdname);
        }
    }
    if (matches.size() == 1) {
        if (matches[0] == "search") {
            is_search = true;
        } else if (matches[0] == "add") {
            is_add = true;
        } else if (matches[0] == "rm") {
            is_rm = true;
        } else if (matches[0] == "update") {
            is_update = true;
        } else if (matches[0] == "fix") {
            is_fix = true;
        } else if (matches[0] == "tag") {
            is_tag = true;
        }
    }

    if (!set_tags_file) {
        ERR_EXIT(1, "could not get valid path for the tags file");
    }
    if (!set_index_file) {
        ERR_EXIT(1, "could not get valid path for the tags file");
    }

    if (!custom_tags_file || !custom_index_file) {
        if (!file_exists(config_directory)) {
            std::filesystem::create_directory(config_directory);
        }
    }
    if (!custom_index_file) {
        if (!file_exists(index_file)) {
            std::ofstream temp(index_file);
            temp.close();
        }
    }
    if (!custom_tags_file) {
        if (!file_exists(tags_file)) {
            std::ofstream temp(tags_file);
            temp.close();
        }
    }

    read_file_index();
    read_saved_tags();

    /* TODO(stole): fully validate parsed tags and index file here, warn/suggest file editing if non-fix-able or non-update-able */

    /* commands */
    if (is_search) {

        std::vector<search_rule_t> search_rules;
        display_type_t display_type = display_type_t::tags_files;
        bool color_enabled = true;
        bool organize_by_tag = true;
        bool compact_output = true;
        bool quoted = false;
        bool no_formatting = false;
        bool search_file_path = false;
        show_tag_info_t show_tag_info = show_tag_info_t::name_only;
        show_file_info_t show_file_info = show_file_info_t::filename_only;

        for (std::uint32_t i = 2; i < argc; i++) {
            std::string targ = argv[i];
            if (targ == "--tags-files") {
                display_type = display_type_t::tags_files;
                continue;
            } else if (targ == "--tags-only") {
                display_type = display_type_t::tags;
                continue;
            } else if (targ == "--files-only") {
                display_type = display_type_t::files;
                continue;
            } else if (targ == "--search-file-path") {
                search_file_path = true;
                continue;
            } else if (targ == "--search-file-name") {
                search_file_path = false;
                continue;
            } else if (targ == "--enable-color") {
                color_enabled = true;
                continue;
            } else if (targ == "--disable-color") {
                color_enabled = false;
                continue;
            } else if (targ == "--formatting") {
                no_formatting = false;
                continue;
            } else if (targ == "--no-formatting") {
                no_formatting = true;
                continue;
            } else if (targ == "--filename-only") {
                show_file_info = show_file_info_t::filename_only;
                continue;
            } else if (targ == "--include-parent") {
                show_file_info = show_file_info_t::include_parent_dir;
                continue;
            } else if (targ == "--full-path-only") {
                show_file_info = show_file_info_t::full_path_only;
                continue;
            } else if (targ == "--relative-path-only") {
                show_file_info = show_file_info_t::relative_path;
                continue;
            } else if (targ == "--full-file-info") {
                show_file_info = show_file_info_t::full_info;
                continue;
            } else if (targ == "--inum-only") {
                show_file_info = show_file_info_t::inum_only;
                continue;
            } else if (targ == "--organize-by-file") {
                organize_by_tag = false;
                continue;
            } else if (targ == "--organize-by-tag") {
                organize_by_tag = true;
                continue;
            } else if (targ == "--full-tag-info") {
                show_tag_info = show_tag_info_t::full_info;
                continue;
            } else if (targ == "--display-tag-chain") {
                show_tag_info = show_tag_info_t::chain;
                continue;
            } else if (targ == "--tag-name-only") {
                show_tag_info = show_tag_info_t::name_only;
                continue;
            } else if (targ == "--compact-output") {
                compact_output = true;
                continue;
            } else if (targ == "--no-compact-output") {
                compact_output = false;
                continue;
            } else if (targ == "--quoted") {
                quoted = true;
                continue;
            } else if (targ == "--normal-quotes") {
                quoted = false;
                continue;
            }

            std::string main_arg;
            bool has_opt = false;
            if (targ.starts_with("--")) {
                main_arg = targ.substr(2, targ.size() - 2);
                if (!map_contains(arg_to_rule_type, main_arg)) {
                    has_opt = true;
                    main_arg = targ.substr(2, targ.size() - 4);
                    if (targ[targ.size() - 2] != '-') {
                        ERR_EXIT(1, "search: argument %i not recognized: \"%s\"", i, argv[i]);
                    }
                }
            } else if (targ[0] == '-') {
                main_arg = targ.substr(1, targ.size() - 1);
                if (!map_contains(arg_to_rule_type, main_arg)) {
                    has_opt = true;
                    main_arg = targ.substr(1, targ.size() - 2);
                }
            }
            if (!map_contains(arg_to_rule_type, main_arg)) {
                ERR_EXIT(1, "search: argument %i not recognized: \"%s\"", i, argv[i]);
            }
            search_rule_type_t rule_type = arg_to_rule_type.find(main_arg)->second;
            std::string opt;
            search_opt_t sopt = search_opt_t::exact;
            if (has_opt) {
                opt = targ.substr(targ.size() - 1, 1);
                if (!map_contains(arg_to_opt, opt)) {
                    ERR_EXIT(1, "search: argument %i search option \"%s\" not found", i, opt.c_str());
                }
                sopt = arg_to_opt.find(opt)->second;
            }
            if (rule_type == search_rule_type_t::inode || rule_type == search_rule_type_t::inode_exclude) {
                if (i >= argc - 1) {
                    ERR_EXIT(1, "search: expected argument <inum> after \"%s\"", targ.c_str());
                }
                ino_t inum = std::strtoul(argv[++i], nullptr, 0);
                if (inum == 0) {
                    ERR_EXIT(1, "search: argument %i inode number \"%s\" was not valid", i, argv[i]);
                }
                if (!map_contains(file_index, inum)) {
                    ERR_EXIT(1, "search: argument %i inode number %lu was not in index file", i, inum);
                }
                search_rules.push_back(search_rule_t{.type = rule_type, .inum = inum});
            } else if (rule_type == search_rule_type_t::all_list || rule_type == search_rule_type_t::all_list_exclude) {
                search_rules.push_back(search_rule_t{rule_type});
            } else { /* takes <text> */
                if (i >= argc - 1) {
                    ERR_EXIT(1, "search: expected argument <text> after \"%s\"", targ.c_str());
                }
                search_rules.push_back(search_rule_t{rule_type, sopt, std::string(argv[++i])});
            }
        }
        std::map<tid_t, bool, tagcmp_t> tags_returned;
        std::map<tid_t, bool, tagcmp_t> tags_matched;
        for (const auto &[id, _] : tags) {
            tags_returned[id] = false;
            tags_matched[id] = false;
        }
        std::map<ino_t, bool> files_returned;
        std::map<ino_t, bool> files_matched;
        for (const auto &[file_ino, _] : file_index) {
            files_returned[file_ino] = false;
            files_matched[file_ino] = false;
        }
        if (search_rules.empty()) {
            search_rules.push_back(search_rule_t{search_rule_type_t::all_list});
        }
        for (const search_rule_t &search_rule : search_rules) {
            bool exclude = search_rule.type == search_rule_type_t::tag_exclude || search_rule.type == search_rule_type_t::file_exclude || search_rule.type == search_rule_type_t::all_exclude || search_rule.type == search_rule_type_t::all_list_exclude || search_rule.type == search_rule_type_t::inode_exclude;
            bool is_file = search_rule.type == search_rule_type_t::file || search_rule.type == search_rule_type_t::file_exclude;
            bool is_tag = search_rule.type == search_rule_type_t::tag || search_rule.type == search_rule_type_t::tag_exclude;
            bool is_all = search_rule.type == search_rule_type_t::all || search_rule.type == search_rule_type_t::all_exclude;
            bool is_all_list = search_rule.type == search_rule_type_t::all_list || search_rule.type == search_rule_type_t::all_list_exclude;
            bool is_inode = search_rule.type == search_rule_type_t::inode || search_rule.type == search_rule_type_t::inode_exclude;
            if (is_all_list) {
                for (const auto &[id, _] : tags) {
                    tags_returned[id] = !exclude;
                    tags_matched[id] = !exclude;
                }
                for (const auto &[file_ino, _] : file_index) {
                    files_returned[file_ino] = !exclude;
                    files_matched[file_ino] = !exclude;
                }
            } else if (is_inode) {
                files_returned[search_rule.inum] = !exclude;
            } else if (search_rule.opt == search_opt_t::exact) {
                if (is_file) {
                    for (const auto &[file_ino, file_info] : file_index) {
                        if (search_file_path) {
                            if (file_info.pathstr == search_rule.text) {
                                files_returned[file_ino] = !exclude;
                                files_matched[file_ino] = !exclude;
                            }
                        } else {
                            if (file_info.filename() == search_rule.text) {
                                files_returned[file_ino] = !exclude;
                                files_matched[file_ino] = !exclude;
                            }
                        }
                    }
                } else if (is_tag) {
                    for (const auto &[id, tag] : tags) {
                        if (!tag.enabled) { continue; }
                        if (tag.name == search_rule.text) {
                            tags_returned[id] = !exclude;
                            tags_matched[id] = !exclude;
                            for (const ino_t &file_ino : tag.files) {
                                files_returned[file_ino] = !exclude;
                            }
                        }
                    }
                } else if (is_all) {
                    for (const auto &[id, tag] : tags) {
                        if (!tag.enabled) { continue; }
                        if (tag.name == search_rule.text) {
                            tags_matched[id] = !exclude;
                            tags_returned[id] = !exclude;
                            std::vector<tid_t> tags_visited;
                            add_all(id, tags_visited, tags_returned, files_returned, exclude);
                        }
                    }
                }
            } else if (search_rule.opt == search_opt_t::text_includes) {
                if (is_file) {
                    for (const auto &[file_ino, file_info] : file_index) {
                        if (search_file_path) {
                            if (file_info.pathstr.find(search_rule.text) != std::string::npos) {
                                files_returned[file_ino] = !exclude;
                                files_matched[file_ino] = !exclude;
                            }
                        } else {
                            if (file_info.filename().find(search_rule.text) != std::string::npos) {
                                files_returned[file_ino] = !exclude;
                                files_matched[file_ino] = !exclude;
                            }
                        }
                    }
                } else if (is_tag) {
                    for (const auto &[id, tag] : tags) {
                        if (!tag.enabled) { continue; }
                        if (tag.name.find(search_rule.text) != std::string::npos) {
                            tags_returned[id] = !exclude;
                            tags_matched[id] = !exclude;
                            for (const ino_t &file_ino : tag.files) {
                                files_returned[file_ino] = !exclude;
                            }
                        }
                    }
                } else if (is_all) {
                    for (const auto &[id, tag] : tags) {
                        if (!tag.enabled) { continue; }
                        if (tag.name.find(search_rule.text) != std::string::npos) {
                            tags_returned[id] = !exclude;
                            tags_matched[id] = !exclude;
                            std::vector<tid_t> tags_visited;
                            add_all(id, tags_visited, tags_returned, files_returned, exclude);
                        }
                    }
                }
            } else if (search_rule.opt == search_opt_t::regex) {
                std::regex rg(search_rule.text);
                if (is_file) {
                    for (const auto &[file_ino, file_info] : file_index) {
                        if (search_file_path) {
                            if (std::regex_search(file_info.pathstr, rg)) {
                                files_returned[file_ino] = !exclude;
                                files_matched[file_ino] = !exclude;
                            }
                        } else {
                            if (std::regex_search(file_info.filename(), rg)) {
                                files_returned[file_ino] = !exclude;
                                files_matched[file_ino] = !exclude;
                            }
                        }
                    }
                } else if (is_tag) {
                    for (const auto &[id, tag] : tags) {
                        if (!tag.enabled) { continue; }
                        if (std::regex_search(tag.name, rg)) {
                            tags_returned[id] = !exclude;
                            tags_matched[id] = !exclude;
                            for (const ino_t &file_ino : tag.files) {
                                files_returned[file_ino] = !exclude;
                            }
                        }
                    }
                } else if (is_all) {
                    for (const auto &[id, tag] : tags) {
                        if (!tag.enabled) { continue; }
                        if (std::regex_search(tag.name, rg)) {
                            tags_returned[id] = !exclude;
                            tags_matched[id] = !exclude;
                            std::vector<tid_t> tags_visited;
                            add_all(id, tags_visited, tags_returned, files_returned, exclude);
                        }
                    }
                }
            }
        }

        /* now display the results */
        if (organize_by_tag) {
            /* residual files, we select */
            for (const auto &[file_ino, file_inc] : files_returned) {
                if (!file_inc) { continue; }
                for (const tid_t &id : file_index[file_ino].tags) {
                    tags_returned[id] = true;
                }
            }
            for (const auto &[id, inc] : tags_returned) {
                if (!inc) { continue; }
                const tag_t &tag = tags[id];
                bool has_any_returned = false;
                for (const ino_t &file_ino : tag.files) {
                    if (!files_returned[file_ino]) { continue; }
                    has_any_returned = true;
                }
                if (display_type == display_type_t::tags || display_type == display_type_t::tags_files) {
                    std::vector<tid_t> tags_visited;
                    display_tag_info(tag, tags_visited, tags_matched, color_enabled, show_tag_info, no_formatting, chain_relation_type_t::original);
                    if (display_type == display_type_t::tags_files && (tag.files.empty() || has_any_returned)) {
                        std::cout << ':';
                        if (tag.files.empty()) {
                            std::cout << ' ';
                        }
                    }
                    if (display_type == display_type_t::tags) {
                        std::cout << '\n';
                    }
                }
                if (display_type == display_type_t::files || display_type == display_type_t::tags_files) {
                    if (!tag.files.empty()) {
                        if (display_type == display_type_t::tags || display_type == display_type_t::tags_files) {
                            std::cout << '\n';
                        }
                        std::vector<ino_t> display_file_inos;
                        for (const ino_t &file_ino : tag.files) {
                            if (!files_returned[file_ino]) { continue; }
                            display_file_inos.push_back(file_ino);
                        }
                        display_file_list(display_file_inos, files_matched, compact_output, show_file_info, no_formatting || (display_type == display_type_t::files), quoted);
                    } else if (display_type == display_type_t::tags_files || (show_file_info == show_file_info_t::filename_only && display_type != display_type_t::files)) {
                        std::cout << "(no files)\n";
                    }
                    if (display_type == display_type_t::tags_files) {
                        std::cout << '\n';
                    }
                }
            }

            /* files with no tags */
            std::vector<ino_t> files_no_tags;
            for (const auto &[file_ino, file_inc] : files_returned) {
                if (!file_inc) { continue; }
                if (file_index[file_ino].tags.empty()) {
                    files_no_tags.push_back(file_ino);
                }
            }

            if (!files_no_tags.empty()) {
                if (display_type == display_type_t::tags || display_type == display_type_t::tags_files) {
                    std::vector<tid_t> tags_visited;
                    std::map<tid_t, bool, tagcmp_t> fake_tags_matched = {{0, false}};
                    display_tag_info(tag_t{.id = 0, .name = "(no tags)"}, tags_visited, fake_tags_matched, color_enabled, show_tag_info, no_formatting, chain_relation_type_t::original, files_no_tags.size());
                    if (display_type == display_type_t::tags_files) {
                        std::cout << ':';
                    }
                    std::cout << '\n';
                }
                if (display_type == display_type_t::files || display_type == display_type_t::tags_files) {
                    display_file_list(files_no_tags, files_matched, compact_output, show_file_info, no_formatting || (display_type == display_type_t::files), quoted);
                    /* if (display_type == display_type_t::tags_files && !no_formatting) {
                        std::cout << '\n';
                    } */
                }
                if (display_type == display_type_t::tags) {
                    std::cout << '\n';
                }
            }

        } else {
            std::vector<ino_t> no_tag_group;
            for (const auto &[file_ino, file_inc] : files_returned) {
                if (!file_inc) { continue; }
                std::vector<ino_t> group = {file_ino};
                std::vector<tid_t> ttags = enabled_only(file_index[file_ino].tags);
                if (ttags.empty()) {
                    no_tag_group.push_back(file_ino);
                    continue;
                }
                std::sort(ttags.begin(), ttags.end(), [](const tid_t &a, const tid_t &b) -> bool { return tags[a].name.compare(tags[b].name); });
                for (const auto &[ofile_ino, ofile_inc] : files_returned) {
                    if (!ofile_inc || ofile_ino == file_ino) { continue; }
                    std::vector<tid_t> otags = enabled_only(file_index[ofile_ino].tags);
                    std::sort(otags.begin(), otags.end(), [](const tid_t &a, const tid_t &b) -> bool { return tags[a].name.compare(tags[b].name); });
                    if (otags == ttags) {
                        group.push_back(ofile_ino);
                        files_returned[ofile_ino] = false; /* so we won't go over it again in the outer loop */
                    }
                }
                if (display_type == display_type_t::tags || display_type == display_type_t::tags_files) {
                    for (std::uint32_t i = 0; i < ttags.size() - 1; i++) {
                        std::vector<tid_t> tags_visited;
                        display_tag_info(tags[ttags[i]], tags_visited, tags_matched, color_enabled, show_tag_info, no_formatting, chain_relation_type_t::original);
                        std::cout << ", ";
                    }
                    std::vector<tid_t> tags_visited;
                    display_tag_info(tags[ttags[ttags.size() - 1]], tags_visited, tags_matched, color_enabled, show_tag_info, no_formatting, chain_relation_type_t::original);
                    if (display_type == display_type_t::tags_files) {
                        std::cout << ':';
                    }
                    std::cout << '\n';
                }
                if (display_type == display_type_t::files || display_type == display_type_t::tags_files) {
                    display_file_list(group, files_matched, compact_output, show_file_info, no_formatting || (display_type == display_type_t::files), quoted);
                }
                if (display_type == display_type_t::tags_files && !no_formatting) {
                    std::cout << '\n';
                }
            }
            /* tags with no files */
            std::vector<tid_t> tags_no_files;
            for (const auto &[id, inc] : tags_returned) {
                if (!inc) { continue; }
                if (tags[id].files.empty()) {
                    tags_no_files.push_back(id);
                }
            }
            if (!tags_no_files.empty()) {
                if (display_type == display_type_t::tags || display_type == display_type_t::tags_files) {
                    for (std::uint32_t i = 0; i < tags_no_files.size() - 1; i++) {
                        std::vector<tid_t> tags_visited;
                        display_tag_info(tags[tags_no_files[i]], tags_visited, tags_matched, color_enabled, show_tag_info, no_formatting, chain_relation_type_t::original);
                        std::cout << ", ";
                    }
                    std::vector<tid_t> tags_visited;
                    display_tag_info(tags[tags_no_files[tags_no_files.size() - 1]], tags_visited, tags_matched, color_enabled, show_tag_info, no_formatting, chain_relation_type_t::original);
                    if (display_type == display_type_t::tags_files) {
                        std::cout << ": ";
                    }
                }
                if (display_type == display_type_t::tags_files || (show_file_info == show_file_info_t::filename_only && display_type != display_type_t::files)) {
                    std::cout << "(no files)\n";
                    if (display_type == display_type_t::tags_files && !no_formatting) {
                        std::cout << '\n';
                    }
                }
            }
            if (!no_tag_group.empty()) {
                if (display_type == display_type_t::tags || display_type == display_type_t::tags_files) {
                    std::vector<tid_t> tags_visited;
                    std::map<tid_t, bool, tagcmp_t> fake_tags_matched = {{0, false}};
                    display_tag_info(tag_t{.id = 0, .name = "(no tags)"}, tags_visited, fake_tags_matched, color_enabled, show_tag_info, no_formatting, chain_relation_type_t::original, no_tag_group.size());
                    if (display_type == display_type_t::tags_files) {
                        std::cout << ':';
                    }
                    std::cout << '\n';
                }
                if (display_type == display_type_t::files || display_type == display_type_t::tags_files) {
                    display_file_list(no_tag_group, files_matched, compact_output, show_file_info, no_formatting || (display_type == display_type_t::files), quoted);
                }
                if (display_type == display_type_t::tags_files && !no_formatting) {
                    std::cout << '\n';
                }
            }
        }
    /* end of search command */

    } else if (is_add || is_rm || is_update) {
        std::vector<change_rule_t> to_change;

        bool search_index_first = true;
        change_entry_type_t change_entry_type = change_entry_type_t::only_files;
        

        parse_file_args(argc - 2, argv + 2, argv[1], is_update, to_change, search_index_first, change_entry_type, is_add || is_update);
        if (to_change.empty()) {
            WARN("%s: no action provided, see %s --help for more information", argv[1], argv[0]);
            return 0;
        }

        bool changed_tags = false;
        bool changed_index = false;
        for (std::int32_t ci = 0; ci < to_change.size(); ci++) { /* NOLINT */
            const change_rule_t &change_rule = to_change[ci];

            if (change_rule.type == change_rule_type_t::single_file) {
                if (is_add) {
                    if (!std::filesystem::exists(change_rule.path)) {
                        const std::string tpathstr = change_rule.path.string();
                        ino_t maybe_ino = search_index(change_rule.path);
                        if (maybe_ino != 0) {
                            if (tpathstr[0] == '"' && tpathstr[tpathstr.size() - 1] == '"') {
                                ERR_EXIT(1, "add: file/directory \"%s\" could not be added, does not exist, but exists in index file with inode number %lu, you might want to run the update command, path is also possibly quoted, you might want to use --stdin-parse-as-args or -sa", change_rule.path.c_str(), maybe_ino);
                            }
                            ERR_EXIT(1, "add: file/directory \"%s\" could not be added, does not exist, but exists in index file with inode number %lu, you might want to run the update command", change_rule.path.c_str(), maybe_ino);
                        } else {
                            if (tpathstr[0] == '"' && tpathstr[tpathstr.size() - 1] == '"') {
                                ERR_EXIT(1, "add: file/directory \"%s\" could not be added, does not exist, path is possibly quoted, you might want to use --stdin-parse-as-args or -sa", change_rule.path.c_str());
                            }
                            ERR_EXIT(1, "add: file/directory \"%s\" could not be added, does not exist", change_rule.path.c_str());
                        }
                    }

                    if (!std::filesystem::is_regular_file(change_rule.path) && !std::filesystem::is_directory(change_rule.path)) {
                        ino_t maybe_ino = search_index(change_rule.path);
                        if (maybe_ino != 0) {
                            WARN("add: file/directory \"%s\" could not be added, exists but was not a regular file or directory, but also exists in index file with inode number %lu, you might want to run the update command", change_rule.path.c_str(), maybe_ino);
                        } else {
                            WARN("add: file/directory \"%s\" could not be added, exists but was not a regular file or directory", change_rule.path.c_str());
                        }
                        continue;
                    }
                    ino_t file_ino = path_get_ino(change_rule.path); /* inode adder here does not insert into to_change, can ignore change_rule.file_ino */
                    if (map_contains(file_index, file_ino)) {
                        WARN("add: file/directory \"%s\" could not be added, inode number %lu already exists in index file (associated with path \"%s\"), you might want to run update on it, skipping", change_rule.path.c_str(), file_ino, file_index[file_ino].pathstr.c_str());
                        continue;
                    }
                    file_index[file_ino] = file_info_t{file_ino, std::filesystem::canonical(change_rule.path)};
                    changed_index = true;

                } else if (is_rm) {
                    ino_t file_ino = change_rule.file_ino;
                    if (file_ino == 0 && search_index_first) {
                        file_ino = search_index(change_rule.path);
                    }
                    if (file_ino == 0) {
                        file_ino = search_use_fs(change_rule.path);
                    }
                    if (file_ino == 0) {
                        std::string twarn_str = "rm: file/directory \"%s\" could not be removed";
                        if (search_index_first) {
                            twarn_str += ", searched both by path in index file and by its inode number (from disk) and was not found";
                            const std::string tpathstr = change_rule.path.string();
                            if (tpathstr[0] == '"' && tpathstr[tpathstr.size() - 1] == '"') {
                                twarn_str += ", path is possibly quoted, you might want to use --stdin-parse-as-args or -sa";
                            }
                        } else {
                            twarn_str += ", searched by its inode number (from disk) and was not found";
                        }
                        WARN(twarn_str.c_str(), change_rule.path.c_str());
                        continue;
                    }
                    for (const tid_t &tagid : file_index[file_ino].tags) {
                        std::erase(tags[tagid].files, file_ino);
                    }
                    if (!file_index[file_ino].tags.empty()) {
                        changed_tags = true;
                    }
                    file_index.erase(file_ino);
                    changed_index = true;

                } else if (is_update) {
                    if (!std::filesystem::exists(change_rule.path)) {
                        ERR_EXIT(1, "update: file/directory \"%s\" could not be updated, does not exist", change_rule.path.c_str());
                    }
                    ino_t file_ino = path_get_ino(change_rule.path);
                    if (map_contains(file_index, file_ino)) {
                        file_index[file_ino].pathstr = change_rule.path;
                        changed_index = true;
                    }
                }

            } else if (change_rule.type == change_rule_type_t::recursive) {
                if (!std::filesystem::is_directory(change_rule.path)) {
                    ERR_EXIT(1, "%s: directory \"%s\" was not a directory, could not walk recursively", argv[1], change_rule.path.c_str());
                }
                /* because we want to process them in the same order as they were passed, we insert into to_change here and
                 * also in get_all instead of just push_back */
                if (change_entry_type == change_entry_type_t::only_directories || change_entry_type == change_entry_type_t::all_entries) {
                    to_change.insert(to_change.begin() + ci+1, change_rule_t{change_rule.path, change_rule_type_t::single_file});
                    get_all(change_rule.path, to_change, ci+2, change_entry_type);
                } else {
                    get_all(change_rule.path, to_change, ci+1, change_entry_type);
                }

            } else if (change_rule.type == change_rule_type_t::inode_number) {
                if (is_rm) {
                    if (!map_contains(file_index, change_rule.file_ino)) {
                        ERR_EXIT(1, "%s: inode number %lu could not be removed, was not found in index file", argv[1], change_rule.file_ino);
                    }
                    to_change.insert(to_change.begin() + ci+1, change_rule_t{.type = change_rule_type_t::single_file, .file_ino = change_rule.file_ino, .from_ino = true});
                } else if (is_add) {
                    if (map_contains(file_index, change_rule.file_ino)) {
                        WARN("%s: inode number %lu could not be added, already exists in index file (associated with path \"%s\"), skipping", argv[1], change_rule.file_ino, file_index[change_rule.file_ino].pathstr.c_str());
                        continue;
                    }
                    file_index[change_rule.file_ino] = file_info_t{change_rule.file_ino};
                    changed_index = true;
                    WARN("%s: inode number %lu adding to index file with unresolved path, you might want to run the update command", argv[1], change_rule.file_ino);
                }
            }
        }
        if (changed_tags) {
            dump_saved_tags();
        }
        if (changed_index) {
            dump_file_index();
        }

    } else if (is_fix) {
        if (argc < 3) {
            ERR_EXIT(1, "fix: expected a flag, see %s --help for more information", argv[0]);
        }
        std::vector<fix_rule_t> fix_rules;
        for (std::uint32_t i = 2; i < argc; i++) {
            if (!std::strcmp(argv[i], "-p") || !std::strcmp(argv[i], "--path-all")) {
                fix_rules.push_back(fix_rule_t{.type = fix_rule_type_t::path_all});
            } else if (!std::strcmp(argv[i], "-pi") || !std::strcmp(argv[i], "--path-i")) {
                if (i >= argc - 1) {
                    ERR_EXIT(1, "fix: expected argument <inum> due to path i flag (argument %i)", i);
                }
                ino_t inum = std::strtoul(argv[++i], nullptr, 0);
                if (inum == 0) {
                    ERR_EXIT(1, "fix: argument %i inode number \"%s\" was not valid", i, argv[i]);
                }
                fix_rules.push_back(fix_rule_t{.path_d = inum, .type = fix_rule_type_t::path_i});
            } else if (!std::strcmp(argv[i], "-pp") || !std::strcmp(argv[i], "--path-p")) {
                if (i >= argc - 1) {
                    ERR_EXIT(1, "fix: expected argument <path> due to path i flag (argument %i)", i);
                }
                std::string pathstr = argv[++i];
                if (!path_ok(pathstr)) {
                    ERR_EXIT(1, "fix: argument %i could not construct path \"%s\"", i, argv[i]);
                }
                fix_rules.push_back(fix_rule_t{.path_d = std::filesystem::path(pathstr), .type = fix_rule_type_t::path_p});
            } else if (!std::strcmp(argv[i], "-rip") || !std::strcmp(argv[i], "--replace-ip")) {
                if (i >= argc - 2) {
                    ERR_EXIT(1, "fix: expected arguments <inum> <path> due to replace ip flag (argument %i)", i);
                }
                ino_t inum = 0;
                inum = std::strtoul(argv[++i], nullptr, 0);
                if (inum == 0) {
                    ERR_EXIT(1, "fix: argument %i inode number \"%s\" was not valid", i, argv[i]);
                }
                std::string pathstr = argv[++i];
                if (!path_ok(pathstr)) {
                    ERR_EXIT(1, "fix: argument %i could not construct path \"%s\"", i, argv[i]);
                }
                fix_rules.push_back(fix_rule_t{.a = inum, .b = std::filesystem::path(pathstr), .type = fix_rule_type_t::rip});
            } else if (!std::strcmp(argv[i], "-rii") || !std::strcmp(argv[i], "--replace-ii")) {
                if (i >= argc - 2) {
                    ERR_EXIT(1, "fix: expected arguments <inum> <newinum> due to replace ii flag (argument %i)", i);
                }
                ino_t inum = 0;
                inum = std::strtoul(argv[++i], nullptr, 0);
                if (inum == 0) {
                    ERR_EXIT(1, "fix: argument %i inode number \"%s\" was not valid", i, argv[i]);
                }
                ino_t newinum = 0;
                newinum = std::strtoul(argv[++i], nullptr, 0);
                if (newinum == 0) {
                    ERR_EXIT(1, "fix: argument %i inode number \"%s\" was not valid", i, argv[i]);
                }
                fix_rules.push_back(fix_rule_t{.a = inum, .b = newinum, .type = fix_rule_type_t::rii});
            } else if (!std::strcmp(argv[i], "-rpp") || !std::strcmp(argv[i], "--replace-pp")) {
                if (i >= argc - 2) {
                    ERR_EXIT(1, "fix: expected arguments <path> <newpath> due to replace pp flag (argument %i)", i);
                }
                std::string pathstr = argv[++i];
                if (!path_ok(pathstr)) {
                    ERR_EXIT(1, "fix: argument %i could not construct path \"%s\"", i, argv[i]);
                }
                std::string newpathstr = argv[++i];
                if (!path_ok(newpathstr)) {
                    ERR_EXIT(1, "fix: argument %i could not construct path \"%s\"", i, argv[i]);
                }
                fix_rules.push_back(fix_rule_t{.a = std::filesystem::path(pathstr), .b = std::filesystem::path(newpathstr), .type = fix_rule_type_t::rpp});
            } else if (!std::strcmp(argv[i], "-rpi") || !std::strcmp(argv[i], "--replace-pi")) {
                if (i >= argc - 2) {
                    ERR_EXIT(1, "fix: expected arguments <path> <newpath> due to replace pi flag (argument %i)", i);
                }
                std::string pathstr = argv[++i];
                if (!path_ok(pathstr)) {
                    ERR_EXIT(1, "fix: argument %i could not construct path \"%s\"", i, argv[i]);
                }
                ino_t inum = 0;
                inum = std::strtoul(argv[++i], nullptr, 0);
                if (inum == 0) {
                    ERR_EXIT(1, "fix: argument %i inode number \"%s\" was not valid", i, argv[i]);
                }
                fix_rules.push_back(fix_rule_t{.a = std::filesystem::path(pathstr), .b = inum, .type = fix_rule_type_t::rpi});
            } else {
                ERR_EXIT(1, "fix: flag \"%s\" was not recognized", argv[i]);
            }
        }

        bool changed_tags = false;
        bool changed_index = false;
        for (const fix_rule_t &fix_rule : fix_rules) {
            bool is_rip = fix_rule.type == fix_rule_type_t::rip;
            bool is_rii = fix_rule.type == fix_rule_type_t::rii;
            bool is_rpi = fix_rule.type == fix_rule_type_t::rpi;
            bool is_rpp = fix_rule.type == fix_rule_type_t::rpp;
            if (fix_rule.type == fix_rule_type_t::path_all) {
                std::vector<std::pair<ino_t, ino_t>> ino_changes; /* old, new */
                for (const auto &[file_ino, file_info] : file_index) {
                    struct stat buffer{};
                    if (!file_exists(file_info.pathstr, &buffer)) {
                        continue;
                    }
                    if (buffer.st_ino == file_ino) {
                        continue; /* is good */
                    }
                    if (map_contains(file_index, buffer.st_ino)) {
                        WARN("fix: old inode number %lu (associated with path \"%s\") could not be fixed, new inode number %lu (from old inode number path) was already in index file (associated with path \"%s\"), you might want to run the fix command with a manual replace flag, update command, or rm command, skipping", file_ino, file_info.pathstr.c_str(), buffer.st_ino, file_index[buffer.st_ino].pathstr.c_str());
                        continue;
                    }
                    ino_changes.emplace_back(file_ino, buffer.st_ino);
                }
                for (const auto &[oldino, newino] : ino_changes) {
                    for (const tid_t &tagid : file_index[oldino].tags) {
                        std::replace(tags[tagid].files.begin(), tags[tagid].files.end(), oldino, newino);
                    }
                    if (!file_index[oldino].tags.empty()) {
                        changed_tags = true;
                    }
                    file_index[newino] = file_index[oldino];
                    file_index.erase(oldino);
                }
                if (!ino_changes.empty()) {
                    changed_index = true;
                }
            } else if (fix_rule.type == fix_rule_type_t::path_i) {
                ino_t oldino = std::get<ino_t>(fix_rule.path_d);
                if (!map_contains(file_index, oldino)) {
                    ERR_EXIT(1, "fix: old inode number %lu could not be fixed, was not in index file", oldino);
                }
                struct stat buffer{};
                if (!file_exists(file_index[oldino].pathstr, &buffer)) {
                    ERR_EXIT(1, "fix: old inode number %lu could not be fixed, associated path \"%s\" was not found", oldino, file_index[oldino].pathstr.c_str());
                }
                if (buffer.st_ino == oldino) {
                    WARN("fix: old inode number %lu could not be fixed, index file entry was already good (inode number matches that found at the associated path \"%s\"), skipping", oldino, file_index[oldino].pathstr.c_str());
                    continue;
                }
                if (map_contains(file_index, buffer.st_ino)) {
                    WARN("fix: old inode number %lu (associated with path \"%s\") could not be fixed, new inode number %lu (from old inode number path) was already in index file (associated with path \"%s\"), you might want to run the fix command with a manual replace flag, update command, or rm command, skipping", oldino, file_index[oldino].pathstr.c_str(), buffer.st_ino, file_index[buffer.st_ino].pathstr.c_str());
                    continue;
                }
                for (const tid_t &tagid : file_index[oldino].tags) {
                    std::replace(tags[tagid].files.begin(), tags[tagid].files.end(), oldino, buffer.st_ino);
                }
                if (!file_index[oldino].tags.empty()) {
                    changed_tags = true;
                }
                file_index[buffer.st_ino] = file_index[oldino];
                file_index.erase(oldino);
                changed_index = true;

            } else if (fix_rule.type == fix_rule_type_t::path_p) {
                auto path = std::get<std::filesystem::path>(fix_rule.path_d);
                ino_t oldino = search_index(path);
                if (oldino == 0) {
                    ERR_EXIT(1, "fix: old inode number could not be fixed, passed path \"%s\" was not found in index file", path.c_str());
                }
                struct stat buffer{};
                if (!file_exists(file_index[oldino].pathstr, &buffer)) {
                    ERR_EXIT(1, "fix: old inode number %lu (from passed path \"%s\") could not be fixed, passed path was not found", oldino, path.c_str());
                }
                if (buffer.st_ino == oldino) {
                    WARN("fix: old inode number %lu (from passed path \"%s\") could not be fixed, index file entry was already good (inode number matches that found at the associated path \"%s\"), skipping", oldino, path.c_str(), file_index[oldino].pathstr.c_str()); /* here associated path and passed path should be identical but whatever */
                    continue;
                }
                if (map_contains(file_index, buffer.st_ino)) {
                    WARN("fix: old inode number %lu (from passed path \"%s\") could not be fixed, new inode number %lu (from old inode number path) was already in index file (associated with path \"%s\"), you might want to run the fix command with a manual replace flag, update command, or rm command, skipping", oldino, path.c_str(), buffer.st_ino, file_index[buffer.st_ino].pathstr.c_str());
                    continue;
                }
                for (const tid_t &tagid : file_index[oldino].tags) {
                    std::replace(tags[tagid].files.begin(), tags[tagid].files.end(), oldino, buffer.st_ino);
                }
                if (!file_index[oldino].tags.empty()) {
                    changed_tags = true;
                }
                file_index[buffer.st_ino] = file_index[oldino];
                file_index.erase(oldino);
                changed_index = true;

            } else if (is_rip || is_rii || is_rpi || is_rpp) {
                ino_t oldino = 0;
                ino_t newino = 0;
                if (is_rip) {
                    oldino = std::get<ino_t>(fix_rule.a);
                    auto newpath = std::get<std::filesystem::path>(fix_rule.b);
                    struct stat buffer{};
                    if (!file_exists(newpath, &buffer)) {
                        ERR_EXIT(1, "fix: old inode number %lu could not be fixed, passed path \"%s\" was not found", oldino, newpath.c_str());
                    }
                    /* we won't actually require this, as we only need the inode number
                     * and trust the user knows because this is a very manual flag
                     * same goes for --replace-pp */
                    /* if (!std::filesystem::is_directory(path) && !std::filesystem::is_regular_file(path)) {
                        ERR_EXIT(1, "fix: old inode number %lu could not be fixed, passed path \"%s\" exists but was not a regular file or directory", oldino, path.c_str());
                    } */
                    newino = buffer.st_ino;
                    if (!map_contains(file_index, oldino)) {
                        ERR_EXIT(1, "fix: old inode number %lu could not be fixed, was not in index file", oldino);
                    }
                    if (map_contains(file_index, newino)) {
                        ERR_EXIT(1, "fix: old inode number %lu could not be fixed, new inode number %lu (from passed path \"%s\") was already in index file (associated with path \"%s\"), cannot replace", oldino, newino, newpath.c_str(), file_index[newino].pathstr.c_str());
                    }
                } else if (is_rii) {
                    oldino = std::get<ino_t>(fix_rule.a);
                    newino = std::get<ino_t>(fix_rule.b);
                    if (!map_contains(file_index, oldino)) {
                        ERR_EXIT(1, "fix: old inode number %lu could not be fixed, was not in index file", oldino);
                    }
                    if (map_contains(file_index, newino)) {
                        ERR_EXIT(1, "fix: old inode number %lu could not be fixed, new inode number %lu was already in index file (associated with path \"%s\"), cannot replace", oldino, newino, file_index[newino].pathstr.c_str());
                    }
                } else if (is_rpi) {
                    auto path = std::get<std::filesystem::path>(fix_rule.a);
                    newino = std::get<ino_t>(fix_rule.b);
                    path = std::filesystem::weakly_canonical(path);
                    oldino = search_index(path);
                    if (oldino == 0) {
                        ERR_EXIT(1, "fix: old inode number (from passed path \"%s\") could not be fixed, passed path was not found in index file", path.c_str());
                    }
                    if (!map_contains(file_index, oldino)) {
                        ERR_EXIT(1, "fix: old inode number %lu (from passed path \"%s\") could not be fixed, was not in index file", oldino, path.c_str());
                    }
                    if (map_contains(file_index, newino)) {
                        ERR_EXIT(1, "fix: old inode number %lu (from passed path \"%s\") could not be fixed, new inode number %lu was already in index file (associated with path \"%s\"), cannot replace", oldino, path.c_str(), newino, file_index[newino].pathstr.c_str());
                    }
                } else if (is_rpp) {
                    auto path = std::get<std::filesystem::path>(fix_rule.a);
                    auto newpath = std::get<std::filesystem::path>(fix_rule.b);
                    path = std::filesystem::weakly_canonical(path);
                    oldino = search_index(path);
                    if (oldino == 0) {
                        ERR_EXIT(1, "fix: old inode number (from passed path \"%s\") could not be fixed, passed path was not found in index file", path.c_str());
                    }
                    if (!map_contains(file_index, oldino)) {
                        ERR_EXIT(1, "fix: old inode number %lu (from passed path \"%s\") could not be fixed, was not in index file", oldino, path.c_str());
                    }
                    struct stat buffer{};
                    if (!file_exists(newpath, &buffer)) {
                        ERR_EXIT(1, "fix: old inode number %lu (from passed path \"%s\") could not be fixed, passed path \"%s\" for new inode number was not found", oldino, path.c_str(), newpath.c_str());
                    }
                    newino = buffer.st_ino;
                    if (map_contains(file_index, newino)) {
                        ERR_EXIT(1, "fix: old inode number %lu (from passed path \"%s\") could not be fixed, new inode number %lu (from passed path \"%s\") was already in index file (associated with path \"%s\"), cannot replace", oldino, path.c_str(), newino, newpath.c_str(), file_index[newino].pathstr.c_str());
                    }
                }

                for (const tid_t &tagid : file_index[oldino].tags) {
                    std::replace(tags[tagid].files.begin(), tags[tagid].files.end(), oldino, newino);
                }
                if (!file_index[oldino].tags.empty()) {
                    changed_tags = true;
                }
                file_index[newino] = file_index[oldino];
                file_index.erase(oldino);
                changed_index = true;

            }
        }
        if (changed_tags) {
            dump_saved_tags();
        }
        if (changed_index) {
            dump_file_index();
        }

    } else if (is_tag) {
        if (argc < 3) {
            ERR_EXIT(1, "tag: expected a subcommand, see %s --help for more information", argv[0]);
        }
        
        const auto tag_by_name = [](const std::string &name, bool &found) -> tag_t& {
            static tag_t temp; /* bs */
            for (auto &[_, tag] : tags) {
                if (tag.name == name) {
                    found = true;
                    return tag;
                }
            }
            return temp;
        };

        std::string subcommand = argv[2];
        bool is_tag_add = subcommand == "add";
        bool is_tag_rm = subcommand == "rm";

        if (subcommand == "create") {
            if (argc < 4) {
                ERR_EXIT(1, "tag: create: expected argument <name>");
            }
            std::optional<color_t> color;
            if (argc > 4) {
                color = color_t{};
                if (hex_to_rgb(argv[4], color.value()) != 3) {
                    ERR_EXIT(1, "tag: create: hex color \"%s\" was bad", argv[4]);
                }
            }
            tid_t tagid = generate_unique_tid();
            if (tag_name_bad(argv[3])) {
                ERR_EXIT(1, "tag: create: bad tag name \"%s\"", argv[3]);
            }
            for (const auto &[tagid, tag] : tags) {
                if (tag.name == argv[3]) {
                    ERR_EXIT(1, "tag: create: tag \"%s\" could not be created, already exists", argv[3]);
                }
            }
            tags[tagid] = tag_t{tagid, argv[3], color};
            dump_saved_tags();

        } else if (subcommand == "delete") {
            if (argc < 4) {
                ERR_EXIT(1, "tag: delete: expected argument <name>");
            }
            std::string name = argv[3];
            bool found = false;
            tag_t &tag = tag_by_name(name, found);
            if (!found) {
                ERR_EXIT(1, "tag: delete: tag \"%s\" could not be deleted, was not found", name.c_str());
            }
            for (const tid_t &id : tag.sub) {
                std::erase(tags[id].super, tag.id);
            }
            for (const tid_t &id : tag.super) {
                std::erase(tags[id].sub, tag.id);
            }
            for (const ino_t &file_ino : tag.files) {
                std::erase(file_index[file_ino].tags, tag.id);
            }
            tags.erase(tag.id);
            dump_saved_tags();

        } else if (subcommand == "enable") {
            if (argc < 4) {
                ERR_EXIT(1, "tag: enable: expected argument <name>");
            }
            std::string name = argv[3];
            bool found = false;
            tag_t &tag = tag_by_name(name, found);
            if (!found) {
                ERR_EXIT(1, "tag: enable: tag \"%s\" could not be enabled, was not found", name.c_str());
            }
            tag.enabled = true;
            dump_saved_tags();

        } else if (subcommand == "disable") {
            if (argc < 4) {
                ERR_EXIT(1, "tag: disable: expected argument <name>");
            }
            std::string name = argv[3];
            bool found = false;
            tag_t &tag = tag_by_name(name, found);
            if (!found) {
                ERR_EXIT(1, "tag: disable: tag \"%s\" could not be disabled, was not found", name.c_str());
            }
            tag.enabled = false;
            dump_saved_tags();

        } else if (subcommand == "edit") {
            if (argc < 5) {
                ERR_EXIT(1, "tag: edit: expected arguments <name> <flags>");
            }
            std::string name = argv[3];
            bool found = false;
            tag_t &ttag = tag_by_name(name, found);
            if (!found) {
                ERR_EXIT(1, "tag: edit: tag \"%s\" could not be edited, was not found", name.c_str());
            }
            bool changed = false;
            for (std::uint32_t i = 4; i < argc; i++) {
                if (!std::strcmp(argv[i], "-ras") || !std::strcmp(argv[i], "--remove-all-super")) {
                    for (const tid_t &id : ttag.super) {
                        std::erase(tags[id].sub, ttag.id);
                    }
                    ttag.super.clear();
                    changed = true;

                } else if (!std::strcmp(argv[i], "-rab") || !std::strcmp(argv[i], "--remove-all-sub")) {
                    for (const tid_t &id : ttag.sub) {
                        std::erase(tags[id].super, ttag.id);
                    }
                    ttag.sub.clear();
                    changed = true;

                } else if (!std::strcmp(argv[i], "-rc") || !std::strcmp(argv[i], "--remove-color")) {
                    ttag.color.reset();
                    changed = true;

                } else if (!std::strcmp(argv[i], "-as") || !std::strcmp(argv[i], "--add-super")) {
                    if (i >= argc - 1) {
                        ERR_EXIT(1, "tag: edit: add super flag expected argument <supername>");
                    }
                    std::string supername = argv[++i];
                    bool found = false;
                    tag_t &tag = tag_by_name(supername, found);
                    if (!found) {
                        WARN("tag: edit: tag \"%s\" could not be added as a supertag to tag \"%s\", the first was not found, skipping", tag.name.c_str(), ttag.name.c_str());
                        continue;
                    }
                    bool already_super = std::find(ttag.super.begin(), ttag.super.end(), tag.id) != ttag.super.end();
                    bool already_sub = std::find(tag.sub.begin(), tag.sub.end(), ttag.id) != tag.sub.end();
                    if (already_sub || already_super) {
                        WARN("tag: edit: tag \"%s\" was already a supertag of tag \"%s\"", tag.name.c_str(), ttag.name.c_str());
                    }
                    if (!already_super) {
                        ttag.super.push_back(tag.id);
                        changed = true;
                    }
                    if (!already_sub) {
                        tag.sub.push_back(ttag.id);
                        changed = true;
                    }

                } else if (!std::strcmp(argv[i], "-rs") || !std::strcmp(argv[i], "--remove-super")) {
                    if (i >= argc - 1) {
                        ERR_EXIT(1, "tag: edit: remove super flag expected argument <supername>");
                    }
                    std::string supername = argv[++i];
                    bool found = false;
                    tag_t &tag = tag_by_name(supername, found);
                    if (!found) {
                        WARN("tag: edit: tag \"%s\" could not be removed as a supertag from tag \"%s\", the first was not found, skipping", tag.name.c_str(), ttag.name.c_str());
                        continue;
                    }
                    bool already_super = std::find(ttag.super.begin(), ttag.super.end(), tag.id) == ttag.super.end();
                    bool already_sub = std::find(tag.sub.begin(), tag.sub.end(), ttag.id) == tag.sub.end();
                    if (already_sub && already_super) {
                        WARN("tag: edit: tag \"%s\" was not a supertag of tag \"%s\", skipping", tag.name.c_str(), ttag.name.c_str());
                        continue;
                    }
                    if (!already_super) {
                        std::erase(ttag.super, tag.id);
                        changed = true;
                    }
                    if (!already_sub) {
                        std::erase(tag.sub, ttag.id);
                        changed = true;
                    }

                } else if (!std::strcmp(argv[i], "-ab") || !std::strcmp(argv[i], "--add-sub")) {
                    if (i >= argc - 1) {
                        ERR_EXIT(1, "tag: edit: add sub flag expected argument <subname>");
                    }
                    std::string subname = argv[++i];
                    bool found = false;
                    tag_t &tag = tag_by_name(subname, found);
                    if (!found) {
                        WARN("tag: edit: tag \"%s\" could not be added as a subtag to tag \"%s\", the first was not found, skipping", tag.name.c_str(), ttag.name.c_str());
                        continue;
                    }
                    bool already_super = std::find(tag.super.begin(), tag.super.end(), ttag.id) != tag.super.end();
                    bool already_sub = std::find(ttag.sub.begin(), ttag.sub.end(), tag.id) != ttag.sub.end();
                    if (already_sub || already_super) {
                        WARN("tag: edit: tag \"%s\" was already a subtag of tag \"%s\"", tag.name.c_str(), ttag.name.c_str());
                    }
                    if (!already_super) {
                        tag.super.push_back(ttag.id);
                        changed = true;
                    }
                    if (!already_sub) {
                        ttag.sub.push_back(tag.id);
                        changed = true;
                    }

                } else if (!std::strcmp(argv[i], "-rb") || !std::strcmp(argv[i], "--remove-sub")) {
                    if (i >= argc - 1) {
                        ERR_EXIT(1, "tag: edit: remove sub flag expected argument <subname>");
                    }
                    std::string subname = argv[++i];
                    bool found = false;
                    tag_t &tag = tag_by_name(subname, found);
                    if (!found) {
                        WARN("tag: edit: tag \"%s\" could not be removed as a subtag from tag \"%s\", the first was not found, skipping", tag.name.c_str(), ttag.name.c_str());
                        continue;
                    }
                    bool already_super = std::find(tag.super.begin(), tag.super.end(), ttag.id) == tag.super.end();
                    bool already_sub = std::find(ttag.sub.begin(), ttag.sub.end(), tag.id) == ttag.sub.end();
                    if (already_sub && already_super) {
                        WARN("tag: edit: tag \"%s\" was not a subtag of tag \"%s\", skipping", tag.name.c_str(), ttag.name.c_str());
                        continue;
                    }
                    if (!already_super) {
                        std::erase(tag.super, ttag.id);
                        changed = true;
                    }
                    if (!already_sub) {
                        std::erase(ttag.sub, tag.id);
                        changed = true;
                    }

                } else if (!std::strcmp(argv[i], "-n") || !std::strcmp(argv[i], "--rename")) {
                    if (i >= argc - 1) {
                        ERR_EXIT(1, "tag: edit: rename flag expected argument <newname>");
                    }
                    std::string newname = argv[++i];
                    if (tag_name_bad(newname)) {
                        ERR_EXIT(1, "tag: edit: rename flag was passed bad tag name \"%s\"", argv[3]);
                    }
                    ttag.name = newname;
                    changed = true;

                } else if (!std::strcmp(argv[i], "-c") || !std::strcmp(argv[i], "--color")) {
                    color_t color;
                    std::string colorstr = argv[++i];
                    if (hex_to_rgb(colorstr, color) != 3) {
                        ERR_EXIT(1, "tag: edit: color flag hex color \"%s\" was bad", colorstr.c_str());
                    }
                    ttag.color = color;
                    changed = true;

                } else {
                    ERR_EXIT(1, "tag: edit: flag \"%s\" was not recognized", argv[i]);
                }

            }
            if (changed) {
                dump_saved_tags();
            }
        } else if (is_tag_add || is_tag_rm) {
            if (argc < 5) {
                ERR_EXIT(1, "tag: %s: expected arguments <name> <flags>", subcommand.c_str());
            }
            std::string name = argv[3];
            bool found = false;
            tag_t &ttag = tag_by_name(name, found);
            if (!found) {
                if (is_tag_add) {
                    ERR_EXIT(1, "tag: add: tag \"%s\" could not be added to file(s) and/or inode number(s), was not found", name.c_str());
                } else {
                    ERR_EXIT(1, "tag: rm: tag \"%s\" could not be removed from file(s) and/or inode number(s), was not found", name.c_str());
                }
            }
            std::vector<change_rule_t> to_change;

            bool search_index_first = true;
            change_entry_type_t change_entry_type = change_entry_type_t::only_files;

            parse_file_args(argc - 4, argv + 4, "tag: " + subcommand, false, to_change, search_index_first, change_entry_type, true);

            bool changed_tags = false;
            bool changed_index = false;
            for (std::int32_t ci = 0; ci < to_change.size(); ci++) { /* NOLINT */
                change_rule_t change_rule = to_change[ci];

                if (change_rule.type == change_rule_type_t::single_file) {
                    if (is_tag_add) {
                        if (!change_rule.from_ino) {
                            if (!std::filesystem::exists(change_rule.path)) {
                                const std::string tpathstr = change_rule.path.string();
                                ino_t maybe_ino = search_index(change_rule.path);
                                if (maybe_ino != 0) {
                                    if (tpathstr[0] == '"' && tpathstr[tpathstr.size() - 1] == '"') {
                                        ERR_EXIT(1, "tag: add: file/directory \"%s\" could not be tagged with tag \"%s\", path does not exist, but exists in index file with inode number %lu, you might want to run the update command, path is also possibly quoted, you might want to use --stdin-parse-as-args or -sa", ttag.name.c_str(), change_rule.path.c_str(), maybe_ino);
                                    }
                                    ERR_EXIT(1, "tag: add: file/directory \"%s\" could not be tagged with tag \"%s\", path does not exist, but exists in index file with inode number %lu, you might want to run the update command", change_rule.path.c_str(), ttag.name.c_str(), maybe_ino);
                                } else {
                                    if (tpathstr[0] == '"' && tpathstr[tpathstr.size() - 1] == '"') {
                                        ERR_EXIT(1, "tag: add: file/directory \"%s\" could not be tagged with tag \"%s\", path does not exist, path is possibly quoted, you might want to use --stdin-parse-as-args or -sa", change_rule.path.c_str(), ttag.name.c_str());
                                    }
                                    ERR_EXIT(1, "tag: add: file/directory \"%s\" could not be tagged with tag \"%s\", path does not exist", change_rule.path.c_str(), ttag.name.c_str());
                                }
                            }

                            if (!std::filesystem::is_regular_file(change_rule.path) && !std::filesystem::is_directory(change_rule.path)) {
                                ino_t maybe_ino = search_index(change_rule.path);
                                if (maybe_ino != 0) {
                                    WARN("tag: add: file/directory \"%s\" could not be tagged with tag \"%s\", path exists but was not a regular file or directory, but also exists in index file with inode number %lu, you might want to run the update command", change_rule.path.c_str(), ttag.name.c_str(), maybe_ino);
                                } else {
                                    WARN("tag: add: file/directory \"%s\" could not be tagged with tag \"%s\", path exists but was not a regular file or directory", change_rule.path.c_str(), ttag.name.c_str());
                                }
                                continue;
                            }
                        }

                        ino_t file_ino = change_rule.file_ino;
                        if (file_ino == 0) {
                            file_ino = path_get_ino(change_rule.path);
                        }
                        if (!map_contains(file_index, file_ino)) {
                            if (!change_rule.from_ino) {
                                WARN("tag: add: file/directory \"%s\" was not in index file, adding and tagging with tag \"%s\"", change_rule.path.c_str(), ttag.name.c_str());
                            } else {
                                WARN("tag: add: inode number %lu was not in index file, adding with unresolved path and tagging with tag \"%s\", you might want to run the update command", change_rule.file_ino, ttag.name.c_str());
                            }
                            if (!file_exists(change_rule.path)) {
                                ERR_EXIT(1, "tag: add: file/directory \"%s\" could not be added, does not exist", change_rule.path.c_str());
                            }
                            file_index[file_ino] = file_info_t{file_ino, std::filesystem::canonical(change_rule.path)};
                            changed_index = true;
                        }
                        file_info_t &file_info = file_index[file_ino];
                        bool already_tagged = std::find(file_info.tags.begin(), file_info.tags.end(), ttag.id) != file_info.tags.end();
                        bool already_revtagged = std::find(ttag.files.begin(), ttag.files.end(), file_ino) != ttag.files.end();
                        if (already_tagged || already_revtagged) {
                            if (!change_rule.from_ino) {
                                WARN("tag: add: file/directory \"%s\" was already tagged with tag \"%s\"", change_rule.path.c_str(), ttag.name.c_str());
                            } else {
                                WARN("tag: add: inode number %lu (path \"%s\") was already tagged with tag \"%s\"", change_rule.file_ino, change_rule.path.c_str(), ttag.name.c_str());
                            }
                        }
                        if (!already_tagged) {
                            file_index[file_ino].tags.push_back(ttag.id);
                            changed_tags = true;
                        }
                        if (!already_revtagged) {
                            ttag.files.push_back(file_ino);
                            changed_tags = true;
                        }

                    } else if (is_tag_rm) {
                        ino_t file_ino = change_rule.file_ino;
                        if (file_ino == 0 && search_index_first) {
                            file_ino = search_index(change_rule.path);
                        }
                        if (file_ino == 0) {
                            file_ino = search_use_fs(change_rule.path);
                        }
                        if (file_ino == 0) {
                            std::string twarn_str = "tag rm: file/directory \"%s\" could not be untagged from tag \"%s\""; /* NOLINT */
                            if (search_index_first) {
                                twarn_str += ", searched both by path in index file and by its inode number (from disk) and was not found";
                                const std::string tpathstr = change_rule.path.string();
                                if (tpathstr[0] == '"' && tpathstr[tpathstr.size() - 1] == '"') {
                                    twarn_str += ", path is possibly quoted, you might want to use --stdin-parse-as-args or -sa";
                                }
                            } else {
                                twarn_str += ", searched by its inode number (from disk) and was not found";
                            }
                            WARN(twarn_str.c_str(), change_rule.path.c_str(), ttag.name.c_str());
                            continue;
                        }
                        file_info_t &file_info = file_index[file_ino];
                        bool already_tagged = std::find(file_info.tags.begin(), file_info.tags.end(), ttag.id) != file_info.tags.end();
                        bool already_revtagged = std::find(ttag.files.begin(), ttag.files.end(), file_ino) != ttag.files.end();
                        if (!already_tagged && !already_revtagged) {
                            WARN("tag: rm: file/directory \"%s\" could not be untagged from tag \"%s\", was not tagged with it", change_rule.path.c_str(), ttag.name.c_str());
                            continue;
                        }
                        if (already_tagged) {
                            std::erase(file_info.tags, ttag.id);
                            changed_tags = true;
                        }
                        if (already_revtagged) {
                            std::erase(ttag.files, file_ino);
                            changed_tags = true;
                        }

                    }
                } else if (change_rule.type == change_rule_type_t::recursive) {
                    if (!std::filesystem::is_directory(change_rule.path)) {
                        ERR_EXIT(1, "tag: %s: directory \"%s\" was not a directory, could not walk recursively", subcommand.c_str(), change_rule.path.c_str());
                    }
                    /* because we want to process them in the same order as they were passed, we insert into to_change here and
                     * also in get_all instead of just push_back */
                    if (change_entry_type == change_entry_type_t::only_directories || change_entry_type == change_entry_type_t::all_entries) {
                        to_change.insert(to_change.begin() + ci+1, change_rule_t{change_rule.path, change_rule_type_t::single_file});
                        get_all(change_rule.path, to_change, ci+2, change_entry_type);
                    } else {
                        get_all(change_rule.path, to_change, ci+1, change_entry_type);
                    }

                } else if (change_rule.type == change_rule_type_t::inode_number) {
                    if (is_tag_rm) {
                        if (!map_contains(file_index, change_rule.file_ino) && std::find(ttag.files.begin(), ttag.files.end(), change_rule.file_ino) == ttag.files.end()) {
                            ERR_EXIT(1, "tag: %s: inode number %lu could not be untagged from tag \"%s\", was not found in index file", subcommand.c_str(), change_rule.file_ino, ttag.name.c_str());
                        }
                        to_change.insert(to_change.begin() + ci+1, change_rule_t{file_index[change_rule.file_ino].pathstr, change_rule_type_t::single_file, change_rule.file_ino, true});
                    } else if (is_tag_add) {
                        to_change.insert(to_change.begin() + ci+1, change_rule_t{file_index[change_rule.file_ino].pathstr, change_rule_type_t::single_file, change_rule.file_ino, true});
                    }
                }
            }

            if (changed_tags) {
                dump_saved_tags();
            }
            if (changed_index) {
                dump_file_index();
            }

        } else {
            ERR_EXIT(1, "tag: subcommand \"%s\" was not recognized", subcommand.c_str());
        }
    } else {
        ERR_EXIT(1, "command \"%s\" was not recognized, see %s --help", argv[1], argv[0]);
    }

    return 0;
}
