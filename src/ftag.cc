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
#include <vector>

#include <cstdlib>
#include <cstring>

#include <sys/stat.h>


#define STRINGIZE_NX(A) #A

#define STRINGIZE(A) STRINGIZE_NX(A)

#define VERSIONA 0
#define VERSIONB 2
#define VERSIONC 2
#define VERSION STRINGIZE(VERSIONA) "." STRINGIZE(VERSIONB) "." STRINGIZE(VERSIONC)


#ifdef DEBUG_BUILD
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

#else
#define ERR_EXIT(A, ...) { \
    std::fputs("error: ", stderr); \
    std::fprintf(stderr, __VA_ARGS__); \
    std::fputc('\n', stderr); \
    std::exit(static_cast<int>(A)); \
}

#define WARN(...) { \
    std::fputs("warning: ", stderr); \
    std::fprintf(stderr, __VA_ARGS__); \
    std::fputc('\n', stderr); \
}

#endif



template <typename T>
T get_random_int() {
    static std::random_device device{};
    static std::default_random_engine engine(device());
    return std::uniform_int_distribution<T>()(engine);
}

bool file_exists(const std::string &filename, struct stat *pbuffer = nullptr) {
    if (pbuffer != nullptr) {
        return !stat(filename.c_str(), pbuffer);
    } else {
        struct stat buffer{};
        return !stat(filename.c_str(), &buffer);
    }
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

void bold_underline_out(std::ostream &out = std::cout) {
    out << esc << "1m" << esc << "4m";
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
};


std::vector<tid_t> parsed_order; /* NOLINT */

/* have to use a cmp struct here instead of normal lambda cmp because of storage/lifetime bs */
struct tagcmp_t {
    bool operator()(const tid_t &a, const tid_t &b) const {
        return std::find(parsed_order.begin(), parsed_order.end(), a) < std::find(parsed_order.begin(), parsed_order.end(), b);
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

template <class Key, class Tp, class Compare>
bool map_contains(const std::map<Key, Tp, Compare> &m, const Key &key) {
    return m.find(key) != m.end();
}

template <class Key, class Tp, class Compare>
bool map_contains(const std::unordered_map<Key, Tp, Compare> &m, const Key &key) {
    return m.find(key) != m.end();
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


std::string config_directory = "/.config/ftag/"; /* NOLINT */
std::string tags_filename = "main.tags"; /* NOLINT */
std::string index_filename = ".fileindex"; /* NOLINT */

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
    if (!file_exists(config_directory)) {
        std::filesystem::create_directory(config_directory);
    }
    if (!file_exists(tags_filename)) {
        std::ofstream temp(tags_filename);
        temp.close();
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

#define FINISH_TAG { \
    if (current_tag.has_value()) { \
        parsed_order.push_back(current_tag.value().id); /* should do before we add it */ \
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
                ERR_EXIT(1, "tag file \"%s\" line %i had \"-[file inode number]\" under no active tag", tags_filename.c_str(), i + 1);
            }
            std::string file_ino_str = no_whitespace_line.substr(1);
            ino_t file_ino = std::strtoul(file_ino_str.c_str(), nullptr, 0);
            if (file_ino == 0) {
                ERR_EXIT(1, "tag file \"%s\" line %i had bad file inode number: \"%s\"", tags_filename.c_str(), i + 1, file_ino_str.c_str());
            }
            current_tag.value().files.push_back(file_ino);
            if (map_contains(file_index, file_ino)) {
                file_index[file_ino].tags.push_back(current_tag.value().id);
            }
            continue;
        }
        /* is a declaring tag line */
        else {
            FINISH_TAG;
            START_TAG;
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
            std::size_t sqbegin = ttag.find('[');
            std::size_t sqend = ttag.find(']');
            std::size_t pbegin = ttag.find('(');
            std::size_t pend = ttag.find(')');
            bool has_states = false;
            bool has_color = false;

            if (sqend != std::string::npos && sqbegin != std::string::npos) {
                if (sqbegin >= sqend) {
                    ERR_EXIT(1, "tag file \"%s\" line %i state list had ']' before '['", tags_filename.c_str(), i + 1);
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
                    ERR_EXIT(1, "tag file \"%s\" line %i color had ')' before '('", tags_filename.c_str(), i + 1);
                }
                has_color = true;
                std::string hexstr = ttag.substr(pbegin + 1, pend - pbegin - 1);
                if (hexstr[0] == '#') { hexstr.erase(hexstr.begin()); }
                if (hex_to_rgb(hexstr, current_tag.value().color.value()) != 3) {
                    ERR_EXIT(1, "tag file \"%s\" line %i had bad hex color: \"%s\"", tags_filename.c_str(), i + 1, hexstr.c_str());
                }
            }

            if (has_color || has_states) {
                tname = ttag.substr(0, std::min(pbegin, sqbegin));
            } else {
                tname = ttag;
            }

            if (tname.empty()) {
                ERR_EXIT(1, "tag file \"%s\" line %i had empty tag name", tags_filename.c_str(), i + 1);
            }
            /* check if tname is good */
            if (tag_name_bad(tname)) {
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
            file << '-' << file_ino << '\n';
        }
    }
}

/* --- index file structure ---
 *
 * [file inode number]:[full path]\0
 * [file inode number]:[full path]\0
 */
void read_file_index() {
    if (!file_exists(config_directory)) {
        std::filesystem::create_directory(config_directory);
    }
    if (!file_exists(index_filename)) {
        std::ofstream temp(index_filename);
        temp.close();
        return;
    }
    std::string index_content = get_file_content(index_filename);
    std::vector<std::string> lines;
    split(index_content, std::string{'\0'} + "\n", lines);
    lines.erase(std::remove(lines.begin(), lines.end(), ""), lines.end());
    for (std::uint32_t i = 0; i < lines.size(); i++) {
        const std::string &line = lines[i];
        if (line.find(':') == std::string::npos) {
            ERR_EXIT(1, "index file \"%s\" line %i had no ':', could not parse", index_filename.c_str(), i);
        }
        std::vector<std::string> sections;
        sections.reserve(2);
        split(line, ":", sections, 2); /* guaranteed at least size 2 from earlier ':' check */
        ino_t file_ino = std::strtoul(sections[0].c_str(), nullptr, 0);
        if (file_ino == 0) {
            ERR_EXIT(1, "index file \"%s\" line %i had bad file inode number \"%s\"", index_filename.c_str(), i, sections[0].c_str());
        }
        std::string pathstr = sections[1];
        struct stat buffer{};
        bool exists = file_exists(pathstr, &buffer);
        if (pathstr.empty()) {
            WARN("index file \"%s\" had file inode number %lu with empty file path, you might want to run the update command", index_filename.c_str(), file_ino);
        } else if (!pathstr.empty() && !exists) {
            WARN("index file \"%s\" had file inode number %lu with file path \"%s\" which does not exist, you might want to run the update command", index_filename.c_str(), file_ino, pathstr.c_str());
        } else if (exists && buffer.st_ino != file_ino) {
            WARN("index file \"%s\" had file inode number %lu with file path \"%s\" which exists but has different inode number %lu on disk, you might want to run the update command", index_filename.c_str(), file_ino, pathstr.c_str(), buffer.st_ino);
        }
        std::filesystem::path can = std::filesystem::weakly_canonical(pathstr);
        file_index[file_ino] = file_info_t{file_ino, can};
    }
}

void dump_file_index() {
    std::ofstream file(index_filename);
    for (const auto &[file_ino, file_info] : file_index) {
        file << file_ino << ':' << std::filesystem::weakly_canonical(file_info.pathstr).string() << std::string{'\0'} + "\n";
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
    filename_only, full_path, full_info
};

enum struct show_tag_info_t : std::uint16_t {
    name_only, full_info, chain
};

enum struct search_rule_type_t : std::uint16_t {
    tag, tag_exclude, file, file_exclude, all, all_exclude,
    all_list, all_list_exclude,
};

enum struct search_opt_t : std::uint16_t {
    exact, text_includes, regex
};

struct search_rule_t {
    search_rule_type_t type = search_rule_type_t::tag; /* doesn't matter not used */
    search_opt_t opt = search_opt_t::exact;
    std::string text;
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
    {"all-list-exclude", search_rule_type_t::all_list_exclude}
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

void add_all(const tid_t &tagid, std::vector<tid_t> &tags_visited, std::unordered_map<tid_t, bool> &tags_map, std::unordered_map<ino_t, bool> &files_map, bool exclude) {
    for (const tid_t &id : enabled_only(tags[tagid].sub)) {
        if (std::find(tags_visited.begin(), tags_visited.end(), id) == tags_visited.end()) {
            tags_visited.push_back(id);
            tags_map[id] = !exclude;
            for (const ino_t &file_ino : tags[id].files) {
                files_map[file_ino] = !exclude;
            }
            add_all(id, tags_visited, tags_map, files_map, exclude);
        }
    }
}

enum struct chain_relation_type_t : std::uint16_t {
    original, super, sub
};

void display_tag_info(const tag_t &tag, std::vector<tid_t> &tags_visited, bool color_enabled, const show_tag_info_t &show_tag_info, bool no_formatting, chain_relation_type_t relation, std::optional<std::uint32_t> custom_file_count = {}) { /* notably, does not append newline */
    if (std::find(tags_visited.begin(), tags_visited.end(), tag.id) == tags_visited.end()) {
        tags_visited.push_back(tag.id);
    } else {
        if (relation == chain_relation_type_t::original && !no_formatting) {
            bold_underline_out();
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
                    display_tag_info(tags[tagsuper[i]], tags_visited, color_enabled, show_tag_info, no_formatting, chain_relation_type_t::super);
                    std::cout << " | ";
                }
                display_tag_info(tags[tagsuper[tagsuper.size() - 1]], tags_visited, color_enabled, show_tag_info, no_formatting, chain_relation_type_t::super);
                std::cout << ')';
            } else {
                display_tag_info(tags[tagsuper[0]], tags_visited, color_enabled, show_tag_info, no_formatting, chain_relation_type_t::super);
            }
            std::cout << " > ";
        }
    }
    if (relation == chain_relation_type_t::original && !no_formatting) {
        bold_underline_out();
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
                    display_tag_info(tags[tagsub[i]], tags_visited, color_enabled, show_tag_info, no_formatting, chain_relation_type_t::sub);
                    std::cout << " | ";
                }
                display_tag_info(tags[tagsub[tagsub.size() - 1]], tags_visited, color_enabled, show_tag_info, no_formatting, chain_relation_type_t::sub);
                std::cout << ')';
            } else {
                display_tag_info(tags[tagsub[0]], tags_visited, color_enabled, show_tag_info, no_formatting, chain_relation_type_t::sub);
            }
        }
    }
}


/* does also output a newline, unlike display_tag_info */
void display_file_info(const file_info_t &file_info, const show_file_info_t &show_file_info, bool no_formatting) {
    if (!no_formatting) {
        std::cout << "    ";
    }
    if (file_info.unresolved()) {
        if (!no_formatting) {
            bold_underline_out();
        }
        std::cout << "<unresolved>";
        if (show_file_info == show_file_info_t::full_info) {
            std::cout << " (" << file_info.file_ino << ")";
        }
        if (!no_formatting) {
            reset_out();
        }
    } else {
        if (show_file_info == show_file_info_t::full_path) {
            std::cout << std::filesystem::path(file_info.pathstr);
        } else if (show_file_info == show_file_info_t::full_info) {
            std::cout << file_info.filename() << " (" << file_info.file_ino << "): " << std::filesystem::path(file_info.pathstr);
        } else {
            std::cout << file_info.filename();
        }
    }
    std::cout << '\n';
}


ino_t search_index(const std::filesystem::path &tpath) {
    for (const auto &[file_ino, file_info] : file_index) {
        if (file_info.pathstr_ok()) {
            std::filesystem::path opath = std::filesystem::weakly_canonical(file_info.pathstr);
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


/* starts parsing from position 0 in argv, offset it if need be */
void parse_file_args(int argc, char **argv, const std::string &command_name, bool is_update, std::vector<change_rule_t> &to_change, bool &search_index_first, change_entry_type_t &change_entry_type) {
    for (std::uint32_t i = 0; i < argc; i++) {
        if (!std::strcmp(argv[i], "-f") || !std::strcmp(argv[i], "--file")) {
            i++;
            if (i >= argc) {
                ERR_EXIT(1, "%s: expected at least one file/directory after file flag", command_name.c_str());
            }
            for (; i < argc; i++) {
                if (argv[i][0] == '-') {
                    WARN("%s: argument %i file/directory \"%s\" began with '-', interpreting as a file, you cannot pass another flag", command_name.c_str(), i, argv[i]);
                }
                if (!path_ok(argv[i])) {
                    ERR_EXIT(1, "%s: argument %i file/directory \"%s\" could not construct path", command_name.c_str(), i, argv[i]);
                }

                const std::filesystem::path tpath = std::filesystem::weakly_canonical(argv[i]);
                const std::string a = tpath.string();

                to_change.push_back(change_rule_t{tpath, change_rule_type_t::single_file});
            }
        } else if (!std::strcmp(argv[i], "-r") || !std::strcmp(argv[i], "--recursive")) {
            i++;
            if (i >= argc) {
                ERR_EXIT(1, "%s: expected at least one directory after recursive flag", command_name.c_str());
            }
            for (; i < argc; i++) {
                if (argv[i][0] == '-') {
                    WARN("%s: argument %i directory \"%s\" began with '-', interpreting as a directory, you cannot pass another flag", command_name.c_str(), i, argv[i]);
                }
                if (!path_ok(argv[i])) {
                    ERR_EXIT(1, "%s: argument %i directory \"%s\" could not construct path", command_name.c_str(), i, argv[i]);
                }

                const std::filesystem::path tpath(argv[i]);

                to_change.push_back(change_rule_t{tpath, change_rule_type_t::recursive});
            }
        } else if (!std::strcmp(argv[i], "-i") || !std::strcmp(argv[i], "--inode")) {
            if (is_update) {
                ERR_EXIT(1, "%s: cannot update from inode numbers, specify files or directories with the appropriate flags, read the update command section of %s --help for more info", command_name.c_str(), argv[0]);
            }
            i++;
            if (i >= argc) {
                ERR_EXIT(1, "%s: expected at least one inode number after inode flag", command_name.c_str());
            }
            for (; i < argc; i++) {
                if (argv[i][0] == '-') {
                    i--;
                    break;
                }
                ino_t file_ino = std::strtoul(argv[i], nullptr, 0);
                if (file_ino == 0) {
                    ERR_EXIT(1, "%s: argument %i inode number \"%s\" was not valid", command_name.c_str(), i, argv[i]);
                }
                to_change.push_back(change_rule_t{"", change_rule_type_t::inode_number, file_ino});
            }
        } else if (!std::strcmp(argv[i], "--search-index")) {
            search_index_first = true;
        } else if (!std::strcmp(argv[i], "--no-search-index")) {
            search_index_first = false;
        } else if (!std::strcmp(argv[i], "--only-files")) {
            change_entry_type = change_entry_type_t::only_files;
        } else if (!std::strcmp(argv[i], "--only-directories")) {
            change_entry_type = change_entry_type_t::only_directories;
        } else if (!std::strcmp(argv[i], "--all-entries")) {
            change_entry_type = change_entry_type_t::all_entries;
        } else {
            ERR_EXIT(1, "%s: flag \"%s\" was not recognized", command_name.c_str(), argv[i]);
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
    config_directory = std::getenv("HOME") + config_directory;
    index_filename = config_directory + index_filename;
    tags_filename = config_directory + tags_filename;

    if (argc <= 1) {
        WARN("no action provided, see %s --help for more information", argv[0]);
        return 1;
    }

    for (std::uint32_t i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
            std::cout << "usage: " << argv[0] << R"( [command] [flags]


description:
    ftag is a utility to tag files/directories on your filesystem, using inode numbers to track and identify them, without modifying files on disk

    tags consist of a name, an optional color, and so-called supertags that they descend from
    tag names can't have spaces, parens, square brackets, colons, and cannot start with a dash, encouraging a
    plain naming style like-this

    with designating supertags, you can construct a large and complicated tag graph. ftag supports it fine and works with
    it, but placing a tag in a cycle with itself is discouraged for obvious reasons

    this version stores saved tags in ")" << tags_filename << "\" and the file index in \"" << index_filename << R"("
    the tag file format and index file format are designed to be almost entirely human-readable and editable
    however, they do reference files by their inode numbers, which might be slightly unwieldly

commands:
    search  : searches for and returns tags and files
    tag     : create/edit/delete tags, and assign and remove files from tags
    add     : adds files to be tracked/tagged by ftag
    rm      : removes files to be tracked/tagged by ftag
    update  : updates the index of tracked files, use if some have been moved/renamed
    fix     : fixes the inode numbers used in the tags file and file index

no command flags:
    -h, --help     : displays this help
    -v, --version  : displays ftag's version

command flags:
    search:
        -al,  --all-list                   : includes all tags and files
        -ale, --all-list-exclude           : excludes all tags and files

        -a <text>,  --all <text>           : includes all files under tag <text> and subtags
        -ae <text>, --all-exclude <text>   : excludes all files under tag <text> and subtags
        -t <text>,  --tag <text>           : includes all files with tag <text>
        -te <text>, --tag-exclude <text>   : excludes all files with tag <text>
        -f <text>,  --file <text>          : includes all files with filename/path <text>
                                             (see --search-file-name and --search-file-path)
        -fe <text>, --file-exclude <text>  : excludes all files with filename/path <text>
                                             (see --search-file-name and --search-file-path)

        --search-file-name                 : uses filenames when searching for files (default)
                                             only has an effect when used with --file and --file-exclude
        --search-file-path                 : instead of searching by filenames, search the entire file path
                                             only has an effect when used with --file and --file-exclude
                                           *** warning: may produce unexpected results

        --tags-files                       : displays both tags and files in result (default)
        --tags-only                        : only displays tags in result, no files
        --files-only                       : only displays files in result, no tags

        --enable-color                     : enables displaying tag color (default)
        --disable-color                    : disables displaying tag color

        --tag-name-only                    : shows only the tag name (still includes color) (default)
        --display-tag-chain                : shows the tag chain each tag descends from, up to and including repeats
        --full-tag-info                    : shows all information about a tag

        --filename-only                    : shows only the filename of each file (default)
        --show-full-path                   : shows only the full file path
        --full-file-info                   : shows all information about a file, including inode numbers

        --organize-by-tag                  : organizes by tag, allows duplicate file output (default)
        --organize-by-file                 : organizes by file, allows duplicate tag output

        --formatting                       : uses formatting (default)
        --no-formatting                    : doesn't output any formatting, useful for piping/sending to other tools
                                             only has an effect when used with --tag-name-only or --show-full-path

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
            add  <name> <flags>        : tags file(s) with tag <name>, interprets <flags> exactly like the add command does
            rm   <name> <flags>        : untags file(s) with tag <name>, interprets <flags> exactly like the rm command does
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
                                                                *** file index simply by comparing paths, then tries to remove by
                                                                *** the inode number found from disk. to change this behavior, see
                                                                *** --no-search-index

        -i, --inode <inum> [inum] ...                           : adds/removes inode numbers from the index
        
        --only-files                                            : only adds/removes regular files (default)
        --only-directories                                      : only adds/removes directories, including the initial <directory>
                                                                  only has an effect with --recursive
        --all-entries                                           : adds both regular files and directories, including the initial
                                                                  <directory>
                                                                  only has an effect with --recursive

    rm:
       --search-index                                           : searches through the index first to match paths when passed a
                                                                  --file or --recursive (default)
       --no-search-index                                        : removes from the file index by the inode number found on the
                                                                  filesystem from the passed path when passed a
                                                                  --file or --recursive

    update:
        -f, --file <file OR directory> [file OR directory] ...  : updates files or single directories to be tracked
                                                                  (does not iterate through the contents of the directories)
        -r, --recursive <directory> [directory] ...             : updates everything in the directories (recursive)

        unfortunately, you cannot pass multiple flags (excluding -i, --inode) for adding/removing/updating in one invocation
        of ftag to allow you to use all file/directory names, i.e. invoke only one of them at a time like this:
            )" << argv[0] << R"( add -f file1.txt ../script.py
            )" << argv[0] << R"( update --recursive ./directory1 /home/user
        you may, however, pass multiple inode flags and then end with a file or directory flag like such:
            )" << argv[0] << R"( rm -i 293 100 --inode 104853 --recursive ../testing /usr/lib
        this is because it is impossible for an <inum> to be a valid flag, and any argument passed in that position can
        be unambiguously determined to be a flag or a positive integer

        when update-ing, ftag always assumes the inode numbers stored in the file index ")" << index_filename << R"("
        and tags file ")" << tags_filename << R"(" are correct

        to reassign/change the inode numbers in the file index and tags file, use the fix command

    fix:


)";
            return 0;
        }
        if (!std::strcmp(argv[i], "--version") || !std::strcmp(argv[i], "-v")) {
            std::cout << "ftag version " VERSION << std::endl;
            return 0;
        }
    }

    bool is_search = !std::strcmp(argv[1], "search");

    bool is_add = !std::strcmp(argv[1], "add");
    bool is_rm = !std::strcmp(argv[1], "rm");
    bool is_update = !std::strcmp(argv[1], "update");
    bool is_fix = !std::strcmp(argv[1], "fix");
    bool is_tag = !std::strcmp(argv[1], "tag");

    read_file_index();
    read_saved_tags();

    /* commands */
    if (is_search) {

        std::vector<search_rule_t> search_rules;
        display_type_t display_type = display_type_t::tags_files;
        bool color_enabled = true;
        bool organize_by_tag = true;
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
            } else if (targ == "--only-filename") {
                show_file_info = show_file_info_t::filename_only;
                continue;
            } else if (targ == "--show-full-path") {
                show_file_info = show_file_info_t::full_path;
                continue;
            } else if (targ == "--full-file-info") {
                show_file_info = show_file_info_t::full_info;
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
            if (i >= argc - 1 && rule_type != search_rule_type_t::all_list && rule_type != search_rule_type_t::all_list_exclude) {
                ERR_EXIT(1, "search: expected argument <text> after \"%s\"", targ.c_str());
            }
            if (rule_type == search_rule_type_t::all_list || rule_type == search_rule_type_t::all_list_exclude) {
                search_rules.push_back(search_rule_t{rule_type});
            } else {
                search_rules.push_back(search_rule_t{rule_type, sopt, std::string(argv[++i])});
            }
        }
        if (!no_formatting) {
            reset_out();
        }
        std::unordered_map<tid_t, bool> tags_returned;
        for (const auto &[id, _] : tags) {
            tags_returned[id] = false;
        }
        std::unordered_map<ino_t, bool> files_returned;
        for (const auto &[file_ino, _] : file_index) {
            files_returned[file_ino] = false;
        }
        if (search_rules.empty()) {
            search_rules.push_back(search_rule_t{search_rule_type_t::all_list});
        }
        for (const search_rule_t &search_rule : search_rules) {
            bool exclude = search_rule.type == search_rule_type_t::tag_exclude || search_rule.type == search_rule_type_t::file_exclude || search_rule.type == search_rule_type_t::all_exclude || search_rule.type == search_rule_type_t::all_list_exclude;
            bool is_file = search_rule.type == search_rule_type_t::file || search_rule.type == search_rule_type_t::file_exclude;
            bool is_tag = search_rule.type == search_rule_type_t::tag || search_rule.type == search_rule_type_t::tag_exclude;
            bool is_all = search_rule.type == search_rule_type_t::all || search_rule.type == search_rule_type_t::all_exclude;
            bool is_all_list = search_rule.type == search_rule_type_t::all_list || search_rule.type == search_rule_type_t::all_list_exclude;
            if (is_all_list) {
                for (auto &[_, status] : tags_returned) {
                    status = !exclude;
                }
                for (auto &[_, status] : files_returned) {
                    status = !exclude;
                }
            } else if (search_rule.opt == search_opt_t::exact) {
                if (is_file) {
                    for (const auto &[file_ino, file_info] : file_index) {
                        if (search_file_path) {
                            if (file_info.pathstr == search_rule.text) {
                                files_returned[file_ino] = !exclude;
                            }
                        } else {
                            if (file_info.filename() == search_rule.text) {
                                files_returned[file_ino] = !exclude;
                            }
                        }
                    }
                } else if (is_tag) {
                    for (const auto &[id, tag] : tags) {
                        if (!tag.enabled) { continue; }
                        if (tag.name == search_rule.text) {
                            tags_returned[id] = !exclude;
                            for (const ino_t &file_ino : tag.files) {
                                files_returned[file_ino] = !exclude;
                            }
                        }
                    }
                } else if (is_all) {
                    for (const auto &[id, tag] : tags) {
                        if (!tag.enabled) { continue; }
                        if (tag.name == search_rule.text) {
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
                            }
                        } else {
                            if (file_info.filename().find(search_rule.text) != std::string::npos) {
                                files_returned[file_ino] = !exclude;
                            }
                        }
                    }
                } else if (is_tag) {
                    for (const auto &[id, tag] : tags) {
                        if (!tag.enabled) { continue; }
                        if (tag.name.find(search_rule.text) != std::string::npos) {
                            tags_returned[id] = !exclude;
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
                            }
                        } else {
                            if (std::regex_search(file_info.filename(), rg)) {
                                files_returned[file_ino] = !exclude;
                            }
                        }
                    }
                } else if (is_tag) {
                    for (const auto &[id, tag] : tags) {
                        if (!tag.enabled) { continue; }
                        if (std::regex_search(tag.name, rg)) {
                            tags_returned[id] = !exclude;
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
                if (display_type == display_type_t::tags || display_type == display_type_t::tags_files) {
                    std::vector<tid_t> tags_visited;
                    display_tag_info(tag, tags_visited, color_enabled, show_tag_info, no_formatting, chain_relation_type_t::original);
                    if (display_type == display_type_t::tags_files) {
                        std::cout << ':';
                    }
                    std::cout << '\n';
                }
                if (display_type == display_type_t::files || display_type == display_type_t::tags_files) {
                    for (const ino_t &file_ino : tag.files) {
                        if (!files_returned[file_ino]) { continue; }
                        display_file_info(file_index[file_ino], show_file_info, no_formatting || (display_type == display_type_t::files));
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
                    display_tag_info(tag_t{.id = 0, .name = "(no tags)"}, tags_visited, color_enabled, show_tag_info, no_formatting, chain_relation_type_t::original, files_no_tags.size());
                    std::cout << ":\n";
                }
                for (const ino_t &file_ino : files_no_tags) {
                    display_file_info(file_index[file_ino], show_file_info, no_formatting || (display_type == display_type_t::files));
                }
                if ((display_type == display_type_t::tags || display_type == display_type_t::tags_files) && !no_formatting) {
                    std::cout << '\n';
                }
            }
        } else {
            for (const auto &[file_ino, file_inc] : files_returned) {
                if (!file_inc) { continue; }
                std::vector<ino_t> group = {file_ino};
                std::vector<tid_t> ttags = enabled_only(file_index[file_ino].tags);
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
                    if (!ttags.empty()) {
                        for (std::uint32_t i = 0; i < ttags.size() - 1; i++) {
                            std::vector<tid_t> tags_visited;
                            display_tag_info(tags[ttags[i]], tags_visited, color_enabled, show_tag_info, no_formatting, chain_relation_type_t::original);
                            std::cout << ", ";
                        }
                        std::vector<tid_t> tags_visited;
                        display_tag_info(tags[ttags[ttags.size() - 1]], tags_visited, color_enabled, show_tag_info, no_formatting, chain_relation_type_t::original);
                    } else {
                        std::vector<tid_t> tags_visited;
                        display_tag_info(tag_t{.id = 0, .name = "(no tags)"}, tags_visited, color_enabled, show_tag_info, no_formatting, chain_relation_type_t::original, group.size());
                    }
                    std::cout << ":\n";
                }
                if (display_type == display_type_t::files || display_type == display_type_t::tags_files) {
                    for (const ino_t &ofile_ino : group) {
                        display_file_info(file_index[ofile_ino], show_file_info, no_formatting || (display_type == display_type_t::files));
                    }
                }
                if (display_type == display_type_t::tags_files) {
                    std::cout << '\n';
                }
            }
        }
    /* end of search command */
    } else if (is_add || is_rm || is_update) {
        std::vector<change_rule_t> to_change;

        bool search_index_first = true;
        change_entry_type_t change_entry_type = change_entry_type_t::only_files;
        

        parse_file_args(argc - 2, argv + 2, argv[1], is_update, to_change, search_index_first, change_entry_type);


        bool changed_tags = false;
        bool changed_index = false;
        for (std::int32_t ci = 0; ci < to_change.size(); ci++) { /* NOLINT */
            const change_rule_t &change_rule = to_change[ci];

            if (change_rule.type == change_rule_type_t::single_file) {
                if (is_add) {
                    if (!std::filesystem::exists(change_rule.path)) {
                        ino_t maybe_ino = search_index(change_rule.path);
                        if (maybe_ino != 0) {
                            ERR_EXIT(1, "add: file/directory \"%s\" could not be added, does not exist, but exists in file index with inode number %lu, you might want to run the update command", change_rule.path.c_str(), maybe_ino);
                        } else {
                            ERR_EXIT(1, "add: file/directory \"%s\" could not be added, does not exist", change_rule.path.c_str());
                        }
                    }

                    if (!std::filesystem::is_regular_file(change_rule.path) && !std::filesystem::is_directory(change_rule.path)) {
                        ino_t maybe_ino = search_index(change_rule.path);
                        if (maybe_ino != 0) {
                            WARN("add: file/directory \"%s\" could not be added, exists but was not a regular file or directory, but also exists in file index with inode number %lu, you might want to run the update command", change_rule.path.c_str(), maybe_ino);
                        } else {
                            WARN("add: file/directory \"%s\" could not be added, exists but was not a regular file or directory", change_rule.path.c_str());
                        }
                        continue;
                    }
                    ino_t file_ino = path_get_ino(change_rule.path); /* inode adder here does not insert into to_change, can ignore change_rule.file_ino */
                    if (map_contains(file_index, file_ino)) {
                        WARN("add: file/directory \"%s\" could not be added, inode number %lu already exists in file index with path \"%s\", you might want to run update on it, skipping", change_rule.path.c_str(), file_ino, file_index[file_ino].pathstr.c_str());
                        continue;
                    }
                    file_index[file_ino] = file_info_t{file_ino, change_rule.path};
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
                        if (search_index_first) {
                            WARN("rm: file/directory \"%s\" could not be removed, searched both by path in file index and by its inode number (from disk) and was not found", change_rule.path.c_str());
                        } else {
                            WARN("rm: file/directory \"%s\" could not be removed, searched by its inode number (from disk) and was not found", change_rule.path.c_str());
                        }
                        continue;
                    }
                    for (const tid_t &tagid : file_index[file_ino].tags) {
                        tags[tagid].files.erase(std::remove_if(tags[tagid].files.begin(), tags[tagid].files.end(), [&file_ino](const ino_t &tino) -> bool { return tino == file_ino; }), tags[tagid].files.end());
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
                        ERR_EXIT(1, "%s: inode number %lu could not be removed, was not found in file index", argv[1], change_rule.file_ino);
                    }
                    to_change.insert(to_change.begin() + ci+1, change_rule_t{.type = change_rule_type_t::single_file, .file_ino = change_rule.file_ino, .from_ino = true});
                } else if (is_add) {
                    if (map_contains(file_index, change_rule.file_ino)) {
                        WARN("%s: inode number %lu could not be added, already exists in file index with path \"%s\", skipping", argv[1], change_rule.file_ino, file_index[change_rule.file_ino].pathstr.c_str());
                        continue;
                    }
                    file_index[change_rule.file_ino] = file_info_t{change_rule.file_ino};
                    changed_index = true;
                    WARN("%s: inode number %lu added to file index with unresolved path, you might want to run the update command", argv[1], change_rule.file_ino);
                }
            }
        }
        if (changed_index) {
            dump_file_index();
        }

    } else if (is_fix) {

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
                tags[id].super.erase(std::remove_if(tags[id].super.begin(), tags[id].super.end(), [&tag](const tid_t &oid) -> bool { return oid == tag.id; }), tags[id].super.end());
            }
            for (const tid_t &id : tag.super) {
                tags[id].sub.erase(std::remove_if(tags[id].sub.begin(), tags[id].sub.end(), [&tag](const tid_t &oid) -> bool { return oid == tag.id; }), tags[id].sub.end());
            }
            for (const ino_t &file_ino : tag.files) {
                file_index[file_ino].tags.erase(std::remove_if(file_index[file_ino].tags.begin(), file_index[file_ino].tags.end(), [&tag](const ino_t &tino) -> bool { return tino == tag.id; }), file_index[file_ino].tags.end());
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
                        tags[id].sub.erase(std::remove_if(tags[id].sub.begin(), tags[id].sub.end(), [&ttag](const tid_t &oid) -> bool { return oid == ttag.id; }), tags[id].sub.end());
                    }
                    ttag.super.clear();
                    changed = true;

                } else if (!std::strcmp(argv[i], "-rab") || !std::strcmp(argv[i], "--remove-all-sub")) {
                    for (const tid_t &id : ttag.sub) {
                        tags[id].super.erase(std::remove_if(tags[id].super.begin(), tags[id].super.end(), [&ttag](const tid_t &oid) -> bool { return oid == ttag.id; }), tags[id].super.end());
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
                        ttag.super.erase(std::remove_if(ttag.super.begin(), ttag.super.end(), [&tag](const tid_t &oid) -> bool { return oid == tag.id; }), ttag.super.end());
                        changed = true;
                    }
                    if (!already_sub) {
                        tag.sub.erase(std::remove_if(tag.sub.begin(), tag.sub.end(), [&ttag](const tid_t &oid) -> bool { return oid == ttag.id; }), tag.sub.end());
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
                        tag.super.erase(std::remove_if(tag.super.begin(), tag.super.end(), [&ttag](const tid_t &oid) -> bool { return oid == ttag.id; }), tag.super.end());
                        changed = true;
                    }
                    if (!already_sub) {
                        ttag.sub.erase(std::remove_if(ttag.sub.begin(), ttag.sub.end(), [&tag](const tid_t &oid) -> bool { return oid == tag.id; }), ttag.sub.end());
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
                ERR_EXIT(1, "tag: %s: tag \"%s\" could not be edited, was not found", subcommand.c_str(),  name.c_str());
            }
            std::vector<change_rule_t> to_change;

            bool search_index_first = true;
            change_entry_type_t change_entry_type = change_entry_type_t::only_files;

            parse_file_args(argc - 4, argv + 4, "tag: " + subcommand, false, to_change, search_index_first, change_entry_type);

            bool changed_tags = false;
            bool changed_index = false;
            for (std::int32_t ci = 0; ci < to_change.size(); ci++) { /* NOLINT */
                change_rule_t change_rule = to_change[ci];

                if (change_rule.type == change_rule_type_t::single_file) {
                    if (is_tag_add) {
                        if (!change_rule.from_ino) {
                            if (!std::filesystem::exists(change_rule.path)) {
                                ino_t maybe_ino = search_index(change_rule.path);
                                if (maybe_ino != 0) {
                                    ERR_EXIT(1, "tag: add: file/directory \"%s\" could not be tagged with tag \"%s\", does not exist, but exists in file index with inode number %lu, you might want to run the update or fix command", change_rule.path.c_str(), ttag.name.c_str(), maybe_ino);
                                } else {
                                    ERR_EXIT(1, "tag: add: file/directory \"%s\" could not be tagged with tag \"%s\", does not exist", change_rule.path.c_str(), ttag.name.c_str());
                                }
                            }
                            if (!std::filesystem::is_regular_file(change_rule.path) && !std::filesystem::is_directory(change_rule.path)) {
                                ino_t maybe_ino = search_index(change_rule.path);
                                if (maybe_ino != 0) {
                                    WARN("tag: add: file/directory \"%s\" could not be tagged with tag \"%s\", exists but was not a regular file or directory, but also exists in file index with inode number %lu, you might want to run the update command", change_rule.path.c_str(), ttag.name.c_str(), maybe_ino);
                                } else {
                                    WARN("tag: add: file/directory \"%s\" could not be tagged with tag \"%s\", exists but was not a regular file or directory", change_rule.path.c_str(), ttag.name.c_str());
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
                                WARN("tag: add: file/directory \"%s\" was not in file index, adding and tagging with tag \"%s\"", change_rule.path.c_str(), ttag.name.c_str());
                            } else {
                                WARN("tag: add: inode number %lu was not in file index, adding with unresolved path and tagging with tag \"%s\", you might want to run the update command", change_rule.file_ino, ttag.name.c_str());
                            }
                            file_index[file_ino] = file_info_t{file_ino, change_rule.path.string()};
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
                            if (search_index_first) {
                                WARN("tag: rm: file/directory \"%s\" could not be untagged from tag \"%s\", searched both by path in file index and by its inode number (from disk) and was not found", change_rule.path.c_str(), ttag.name.c_str());
                            } else {
                                WARN("tag: rm: file/directory \"%s\" could not be untagged from tag \"%s\", searched by its inode number (from disk) and was not found", change_rule.path.c_str(), ttag.name.c_str());
                            }
                            continue;
                        }
                        file_info_t &file_info = file_index[file_ino];
                        bool already_tagged = std::find(file_info.tags.begin(), file_info.tags.end(), ttag.id) == file_info.tags.end();
                        bool already_revtagged = std::find(ttag.files.begin(), ttag.files.end(), file_ino) == ttag.files.end();
                        if (!already_tagged && !already_revtagged) {
                            WARN("tag: rm: file/directory \"%s\" could not be untagged from tag \"%s\", was not tagged with it", change_rule.path.c_str(), ttag.name.c_str());
                            continue;
                        }
                        if (!already_tagged) {
                            file_info.tags.erase(std::remove_if(file_info.tags.begin(), file_info.tags.end(), [&ttag](const tid_t &tagid) -> bool { return ttag.id == tagid; }), file_info.tags.end());
                            changed_tags = true;
                        }
                        if (!already_revtagged) {
                            ttag.files.erase(std::remove_if(ttag.files.begin(), ttag.files.end(), [&file_ino](const ino_t &tino) -> bool { return tino == file_ino; }), ttag.files.end());
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
                        if (!map_contains(file_index, change_rule.file_ino)) {
                            ERR_EXIT(1, "tag: %s: inode number %lu could not be removed, was not found in file index", subcommand.c_str(), change_rule.file_ino);
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
