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

#include <cstring>

#include <sys/stat.h>


#define STRINGIZE_NX(A) #A

#define STRINGIZE(A) STRINGIZE_NX(A)

#define VERSIONA 0
#define VERSIONB 1
#define VERSIONC 0
#define VERSION STRINGIZE(VERSIONA) "." STRINGIZE(VERSIONB) "." STRINGIZE(VERSIONC)


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

__ino_t path_get_ino(const std::filesystem::path &path) {
    struct stat buffer{};
    stat(path.string().c_str(), &buffer);
    return buffer.st_ino;
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
    std::string name; /* can't have spaces, parens, colons, and cannot start with a dash, encourages plain naming style something-like-this */
    std::optional<color_t> color;
    std::vector<tid_t> sub;
    std::vector<tid_t> super;
    std::vector<__ino_t> files; /* file inode numbers */
};

struct file_info_t {
    __ino_t file_ino;
    std::string pathstr;
    std::vector<tid_t> tags;

    bool unresolved() const {
        return pathstr.empty();
    }

    std::string filename() const {
        /* caching? why not! clearly grabbing the filename from pathstr is extremely expensive... */
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

static constexpr std::string tags_filename = "main.tags";
static constexpr std::string index_filename = ".fileindex";

std::map<__ino_t, file_info_t> file_index; /* NOLINT */

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

/* *** RUN read_file_index BEFORE THIS ***
 * in order to correctly/efficiently add to file_info_t::tags
 *
 * --- tag file structure ---
 *
 * tag-name: super-tag other-super-tag
 * -[file inode number]
 * -[file inode number]
 * other-tag-name (FF0000): blah-super-tag super-tag
 * blah-tag-name (#FF7F7F)
 */
void read_saved_tags() {
    if (!file_exists(tags_filename)) {
        ERR_EXIT(1, "tag file \"%s\" not found", tags_filename.c_str());
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
            __ino_t file_ino = std::strtoul(file_ino_str.c_str(), nullptr, 0);
            if (file_ino == 0) {
                ERR_EXIT(1, "tag file \"%s\" line %i had bad file inode number: \"%s\"", tags_filename.c_str(), i + 1, file_ino_str.c_str());
            }
            current_tag.value().files.push_back(file_ino);
            if (file_index.contains(file_ino)) {
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
            if (ttag.find(')') != std::string::npos && ttag.find('(') != std::string::npos) {
                current_tag.value().color = color_t();
                if (ttag.find('(') >= ttag.find(')')) {
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
        for (const __ino_t &file_ino : tag.files) {
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
    if (!file_exists(index_filename)) {
        ERR_EXIT(1, "index file \"%s\" not found", index_filename.c_str());
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
        split(line, ":", sections, 2);
        __ino_t file_ino = std::strtoul(sections[0].c_str(), nullptr, 0);
        if (file_ino == 0) {
            ERR_EXIT(1, "index file \"%s\" line %i had bad file inode number \"%s\"", index_filename.c_str(), i, sections[0].c_str());
        }
        std::size_t null_loc = sections[1].find('\0');
        if (null_loc == std::string::npos) {
            ERR_EXIT(1, "index file \"%s\" line %i had bad location data \"%s\", could not find null delimiter", index_filename.c_str(), i, sections[1].c_str());
        }
        std::string pathstr = sections[1].substr(0, null_loc);
        if (pathstr.empty()) {
            WARN("index file \"%s\" had file inode number %lu with empty file path, you might want to run the update command", index_filename.c_str(), file_ino);
        } else if (!pathstr.empty() && !file_exists(pathstr)) {
            WARN("index file \"%s\" had file inode number %lu with file path \"%s\" which does not exist, you might want to run the update command", index_filename.c_str(), file_ino, pathstr.c_str());
        }
        file_index[file_ino] = file_info_t{file_ino, pathstr};
    }
}

void dump_file_index() {
    std::ofstream file(index_filename);
    for (const auto &[file_ino, file_info] : file_index) {
        file << file_ino << ':' << std::filesystem::path(file_info.pathstr).root_path().string() << std::string{'\0'} + "\n";
    }
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

void add_all(const tid_t &tagid, std::vector<tid_t> &tags_visited, std::unordered_map<tid_t, bool> &tags_map, std::unordered_map<__ino_t, bool> &files_map, bool exclude) {
    for (const tid_t &id : tags[tagid].sub) {
        if (std::find(tags_visited.begin(), tags_visited.end(), id) == tags_visited.end()) {
            tags_visited.push_back(id);
            tags_map[id] = !exclude;
            for (const __ino_t &file_ino : tags[id].files) {
                files_map[file_ino] = !exclude;
            }
            add_all(id, tags_visited, tags_map, files_map, exclude);
        }
    }
}

void display_tag_info(const tag_t &tag, std::vector<tid_t> &tags_visited, bool color_enabled, const show_tag_info_t &show_tag_info, bool no_formatting, bool original = false) { /* notably, does not append newline */
    if (std::find(tags_visited.begin(), tags_visited.end(), tag.id) == tags_visited.end()) {
        tags_visited.push_back(tag.id);
        if (show_tag_info != show_tag_info_t::name_only && !tag.super.empty()) { /* displaying chain */
            if (tag.super.size() > 1) {
                std::cout << '(';
                for (std::uint32_t i = 0; i < tag.super.size() - 1; i++) {
                    display_tag_info(tags[tag.super[i]], tags_visited, color_enabled, show_tag_info, no_formatting);
                    std::cout << " | ";
                }
                display_tag_info(tags[tag.super[tag.super.size() - 1]], tags_visited, color_enabled, show_tag_info, no_formatting);
                std::cout << ')';
            } else {
                display_tag_info(tags[tag.super[0]], tags_visited, color_enabled, show_tag_info, no_formatting);
            }
            std::cout << " > ";
        }
    }
    if (original) {
        bold_underline_out();
    }
    if (color_enabled && tag.color.has_value() && (!no_formatting && show_tag_info != show_tag_info_t::name_only)) {
        string_color_fg(tag.color.value(), tag.name);
    } else {
        std::cout << tag.name;
    }
    if (original) {
        reset_out();
    }
    if (show_tag_info == show_tag_info_t::full_info) {
        std::cout << " [" << tag.files.size() << "]";
        if (!tag.sub.empty()) {
            std::cout << " > ";
            for (std::uint32_t i = 0; i < tag.sub.size() - 1; i++) {
                std::vector<tid_t> fake_tags_visited = {tag.sub[i]};
                display_tag_info(tags[tag.sub[i]], fake_tags_visited, color_enabled, show_tag_info_t::name_only, no_formatting);
                std::cout << " | ";
            }
            std::vector<tid_t> fake_tags_visited = {tag.sub[tag.sub.size() - 1]};
            display_tag_info(tags[tag.sub[tag.sub.size() - 1]], fake_tags_visited, color_enabled, show_tag_info_t::name_only, no_formatting);
        }
    }
}



int main(int argc, char **argv) {
    if (argc <= 1) {
        WARN("no action provided\ntry %s --help for more information", argv[0]);
        return 1;
    }

    for (std::uint32_t i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
            std::cout << "usage: " << argv[0] << R"( [command] [flags]


description:
    ftag is a utility to tag files/directories on your filesystem, using inode numbers to track them, it
    doesn't modify the files on disk at all

    tags consist of a name, an optional color, and so-called supertags that it descends from
    tag names can't have spaces, parens, colons, and cannot start with a dash, encouraging a plain naming style like-this

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
                                           ***              may produce unexpected results              ***

        --tags-files                       : displays both tags and files in result (default)
        --tags-only                        : only displays tags in result, no files
        --files-only                       : only displays files in result, no tags

        --enable-color                     : enables displaying tag color (default)
        --disable-color                    : disables displaying tag color

        --tag-name-only                    : shows only the tag name (still includes color) (default)
        --display-tag-chain                : shows the tag chain each tag descends from, up to and including repeat
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
        -c, --create <name> [color]   : creates a tag with the name <name> and hex color [color]
        -d, --delete <name>           : deletes a tag with the name <name>
        -e, --edit <name> <flags>
            flags:
                -as,  --add-super <supername>        : adds the tag <supername> to the tag <name>'s supertags
                -rs,  --remove-super <supername>     : removes the tag <supername> from the tag <name>'s supertags
                                                       (errors if <supername> is not in tag <name>'s supertags)
                -rsq, --remove-super-q <supername>   : same as --remove-super, except it does not error and is silent
                -ras,  --remove-all-super            : removes all supertags from tag <name>

    add, rm:
        -f, --file <file OR directory> [file OR directory] ...  : adds/removes files or single directories to be tracked
                                                                  (does not iterate through the contents of the directories)
        -d, --directory <directory> [directory] ...             : adds/removes everything in the directories (recursive)
        -i, --inode <inum> [inum] ...                           : adds/removes inode numbers from the index

    update:
        -f, --file <file OR directory> [file OR directory] ...  : updates files or single directories to be tracked
                                                                  (does not iterate through the contents of the directories)
        -d, --directory <directory> [directory] ...             : updates everything in the directories (recursive)

        unfortunately, you cannot pass multiple flags (excluding -i, --inode) for adding/updating/removing in one invocation
        of ftag to allow you to use all file/directory names, i.e. invoke only one of them at a time like this:
            )" << argv[0] << R"( add -f file1.txt ../script.py
            )" << argv[0] << R"( update --directory ./directory1 /home/user
        you may, however, pass multiple inode flags and then end with a file or directory flag like such:
            )" << argv[0] << R"( rm -i 293 100 --inode 104853 --directory ../testing /usr/lib
        this is because it is impossible for an <inum> to be a valid flag, and any argument passed in that position can
        be unambiguously determined to be a flag or a positive integer

        when update-ing, ftag always assumes the inode numbers stored in the file index ")" << index_filename << R"(
        and tags file \")" << tags_filename << R"(" are correct
        to reassign/change the inodes in the file index and tags file, use the fix command

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
                if (!arg_to_rule_type.contains(main_arg)) {
                    has_opt = true;
                    main_arg = targ.substr(2, targ.size() - 4);
                    if (targ[targ.size() - 2] != '-') {
                        ERR_EXIT(1, "argument %i not recognized: \"%s\"", i, argv[i]);
                    }
                }
            } else if (targ[0] == '-') {
                main_arg = targ.substr(1, targ.size() - 1);
                if (!arg_to_rule_type.contains(main_arg)) {
                    has_opt = true;
                    main_arg = targ.substr(1, targ.size() - 2);
                }
            }
            if (!arg_to_rule_type.contains(main_arg)) {
                ERR_EXIT(1, "argument %i not recognized: \"%s\"", i, argv[i]);
            }
            search_rule_type_t rule_type = arg_to_rule_type.find(main_arg)->second;
            std::string opt;
            search_opt_t sopt = search_opt_t::exact;
            if (has_opt) {
                opt = targ.substr(targ.size() - 1, 1);
                if (!arg_to_opt.contains(opt)) {
                    ERR_EXIT(1, "argument %i search option \"%s\" not found", i, opt.c_str());
                }
                sopt = arg_to_opt.find(opt)->second;
            }
            if (i >= argc - 1) {
                ERR_EXIT(1, "expected argument <text> after \"%s\"", targ.c_str());
            }
            search_rules.push_back(search_rule_t{rule_type, sopt, std::string(argv[++i])});
        }
        if (color_enabled && !no_formatting) {
            reset_out();
        }
        std::unordered_map<tid_t, bool> tags_returned;
        for (const auto &[id, _] : tags) {
            tags_returned[id] = false;
        }
        std::unordered_map<__ino_t, bool> files_returned;
        for (const auto &[file_ino, _] : file_index) {
            files_returned[file_ino] = false;
        }
        for (const search_rule_t &search_rule : search_rules) {
            bool exclude = search_rule.type == search_rule_type_t::tag_exclude || search_rule.type == search_rule_type_t::file_exclude || search_rule.type == search_rule_type_t::all_exclude;
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
                        if (file_info.filename() == search_rule.text) {
                            files_returned[file_ino] = !exclude;
                        }
                    }
                } else if (is_tag) {
                    for (const auto &[id, tag] : tags) {
                        if (tag.name == search_rule.text) {
                            tags_returned[id] = !exclude;
                            for (const __ino_t &file_ino : tag.files) {
                                files_returned[file_ino] = !exclude;
                            }
                        }
                    }
                } else if (is_all) {
                    for (const auto &[id, tag] : tags) {
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
                        if (file_info.filename().find(search_rule.text) != std::string::npos) {
                            files_returned[file_ino] = !exclude;
                        }
                    }
                } else if (is_tag) {
                    for (const auto &[id, tag] : tags) {
                        if (tag.name.find(search_rule.text) != std::string::npos) {
                            tags_returned[id] = !exclude;
                            for (const __ino_t &file_ino : tag.files) {
                                files_returned[file_ino] = !exclude;
                            }
                        }
                    }
                } else if (is_all) {
                    for (const auto &[id, tag] : tags) {
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
                        if (std::regex_search(file_info.filename(), rg)) {
                            files_returned[file_ino] = !exclude;
                        }
                    }
                } else if (is_tag) {
                    for (const auto &[id, tag] : tags) {
                        if (std::regex_search(tag.name, rg)) {
                            tags_returned[id] = !exclude;
                            for (const __ino_t &file_ino : tag.files) {
                                files_returned[file_ino] = !exclude;
                            }
                        }
                    }
                } else if (is_all) {
                    for (const auto &[id, tag] : tags) {
                        if (std::regex_search(tag.name, rg)) {
                            tags_returned[id] = !exclude;
                            std::vector<tid_t> tags_visited;
                            add_all(id, tags_visited, tags_returned, files_returned, exclude);
                        }
                    }
                }
            }
        }

        /* does also output a newline, unlike display_tag_info */
        const auto display_file_info = [show_file_info, no_formatting](const file_info_t &file_info) {
            if (!no_formatting) {
                std::cout << "    ";
            }
            if (show_file_info == show_file_info_t::full_path) {
                std::cout << file_info.pathstr;
            } else if (show_file_info == show_file_info_t::full_info) {
                std::cout << file_info.filename() << " (" << file_info.file_ino << "): " << file_info.pathstr;
            } else {
                std::cout << file_info.filename();
            }
            std::cout << '\n';
        };

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
                    display_tag_info(tag, tags_visited, color_enabled, show_tag_info, no_formatting, true);
                }
                if (display_type == display_type_t::files || display_type == display_type_t::tags_files) {
                    std::cout << ":\n";
                    for (const __ino_t &file_ino : tag.files) {
                        if (!files_returned[file_ino]) { continue; }
                        display_file_info(file_index[file_ino]);
                    }
                }
                std::cout << '\n';
            }
        } else {
            for (const auto &[file_ino, file_inc] : files_returned) {
                if (!file_inc) { continue; }
                std::vector<__ino_t> group = {file_ino};
                std::vector<tid_t> ttags = file_index[file_ino].tags;
                std::sort(ttags.begin(), ttags.end());
                for (const auto &[ofile_ino, ofile_inc] : files_returned) {
                    if (!ofile_inc || ofile_ino == file_ino) { continue; }
                    std::vector<tid_t> otags = file_index[ofile_ino].tags;
                    std::sort(otags.begin(), otags.end());
                    if (otags == ttags) {
                        group.push_back(ofile_ino);
                    }
                }
                if (!ttags.empty()) {
                    for (std::uint32_t i = 0; i < ttags.size() - 1; i++) {
                        std::vector<tid_t> tags_visited;
                        display_tag_info(tags[ttags[i]], tags_visited, color_enabled, show_tag_info, no_formatting, true);
                        std::cout << ", ";
                    }
                    std::vector<tid_t> tags_visited;
                    display_tag_info(tags[ttags[ttags.size() - 1]], tags_visited, color_enabled, show_tag_info, no_formatting, true);
                }
                std::cout << ":\n";
                for (const __ino_t &ofile_ino : group) {
                    display_file_info(file_index[ofile_ino]);
                }
            }
        }
    /* end of search command */
    } else if (!std::strcmp(argv[1], "add") || !std::strcmp(argv[1], "rm") || !std::strcmp(argv[1], "update")) {
        std::vector<file_info_t> to_change;
        const auto get_all = [&to_change](const std::filesystem::path &path, bool recurse) -> void {
            if (!recurse) {
                for (const auto &entry : std::filesystem::directory_iterator(path)) {
                    if (entry.is_regular_file() || entry.is_directory()) {
                        to_change.push_back(file_info_t{path_get_ino(entry.path()), entry.path().string()});
                    }
                }
            } else {
                for (const auto &entry : std::filesystem::recursive_directory_iterator(path)) {
                    if (entry.is_regular_file() || entry.is_directory()) {
                        to_change.push_back(file_info_t{path_get_ino(entry.path()), entry.path().string()});
                    }
                }
            }
        };
        for (std::uint32_t i = 2; i < argc; i++) {
            if (!std::strcmp(argv[i], "-f") || !std::strcmp(argv[i], "--file")) {
                i++;
                for (; i < argc; i++) {
                    if (argv[i][0] == '-') {
                        WARN("argument %i file/directory \"%s\" began with '-', interpreting as a file, you cannot pass another flag", i, argv[i]);
                    }
                    const std::filesystem::path tpath(argv[i]);
                    if (!file_exists(argv[i])) {
                        ERR_EXIT(1, "argument %i file/directory \"%s\" does not exist, cannot get inode number", i, tpath.c_str());
                    }
                    if (std::filesystem::is_regular_file(tpath) || std::filesystem::is_directory(tpath)) {
                        to_change.push_back(file_info_t{path_get_ino(argv[i]), argv[i]});
                    } else {
                        ERR_EXIT(1, "argument %i file/directory \"%s\" was not a regular file or directory", i, tpath.c_str());
                    }
                }
            } else if (!std::strcmp(argv[i], "-d") || !std::strcmp(argv[i], "--directory")) {
                i++;
                if (i >= argc) {
                    ERR_EXIT(1, "directory flag ")
                }
                for (; i < argc; i++) {
                    if (argv[i][0] == '-') {
                        WARN("argument %i directory \"%s\" began with '-', interpreting as a directory, you cannot pass another flag", i, argv[i]);
                    }
                    const std::filesystem::path tpath(argv[i]);
                    if (!file_exists(argv[i])) {
                        ERR_EXIT(1, "argument %i directory \"%s\" does not exist, cannot get inode number", i, tpath.c_str());
                    }
                    if (std::filesystem::is_directory(tpath)) {
                        get_all(tpath, true);
                    } else {
                        ERR_EXIT(1, "argument %i directory \"%s\" was not a directory", i, tpath.c_str());
                    }
                }
            } else if (!std::strcmp(argv[i], "-i") || !std::strcmp(argv[i], "--inode")) {
                if (is_update) {
                    ERR_EXIT(1, "cannot update from inode numbers, specify files or directories with the appropriate flags,\ntry reading the update command section of %s --help for more info", argv[0]);
                }
                i++;
                for (; i < argc; i++) {
                    if (argv[i][0] == '-') {
                        i--;
                        break;
                    }
                    __ino_t file_ino = std::strtoul(argv[i], nullptr, 0);
                    if (file_ino == 0) {
                        ERR_EXIT(1, "argument %i inode number \"%s\" was not valid", i, argv[i]);
                    }
                    to_change.push_back(file_info_t{file_ino});
                }
            }

        }
        if (is_add) {
        }
    }

    return 0;
}
