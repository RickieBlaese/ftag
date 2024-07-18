#include <algorithm>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <optional>
#include <random>
#include <regex>
#include <string>
#include <sstream>
#include <map>
#include <unordered_map>
#include <vector>

#include <cstring>

#include <sys/stat.h>


/*
 * Turn A into a string literal without expanding macro definitions
 * (however, if invoked from a macro, macro arguments are expanded).
 */
#define STRINGIZE_NX(A) #A

/*
 * Turn A into a string literal after macro-expanding it.
 */
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
    std::string filename;
    std::string pathstr;
    std::vector<tid_t> tags;
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
 * [file inode number]:[filename]\0[full path]\0
 * [file inode number]:[filename]\0[full path]\0
 */
void read_file_index() {
    if (!file_exists(index_filename)) {
        ERR_EXIT(1, "index file \"%s\" not found", index_filename.c_str());
    }
    std::string index_content = get_file_content(index_filename);
    std::vector<std::string> lines;
    split(index_content, std::string{'\0'} + "\n", lines);
    lines.erase(std::remove(lines.begin(), lines.end(), ""), lines.end());
    bool any_not_exist = false;
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
        std::vector<std::string> filename_path;
        filename_path.reserve(2);
        split(sections[1], std::string{'\0'}, filename_path, 2);
        if (filename_path.size() < 2) {
            ERR_EXIT(1, "index file \"%s\" line %i had bad location data \"%s\"", index_filename.c_str(), i, sections[1].c_str());
        }
        std::string filename = filename_path[0];
        std::string pathstr = filename_path[1];
        if (!any_not_exist) {
            if (!file_exists(pathstr)) {
                any_not_exist = true;
                WARN("index file \"%s\" had file inode number %lu with file \"%s\" which does not exist, you might want to run the update command", index_filename.c_str(), file_ino, pathstr.c_str());
            }
        }
        file_index[file_ino] = file_info_t{filename, pathstr, {}};
    }
}

void dump_file_index() {
    std::ofstream file(index_filename);
    for (const auto &[file_ino, file_info] : file_index) {
        file << file_ino << ':' << file_info.filename << '\0' << file_info.pathstr << std::string{'\0'} + "\n";
    }
}



enum struct display_type_t : std::uint16_t {
    tags_files, tags, files
};

enum struct search_rule_type_t : std::uint16_t {
    tag, tag_exclude, file, file_exclude, list_tag
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
    {"l", search_rule_type_t::list_tag},
    {"list", search_rule_type_t::list_tag}
};

const std::unordered_map<std::string, search_opt_t> arg_to_opt = { /* NOLINT */
    {"s", search_opt_t::text_includes},
    {"r", search_opt_t::regex}
};


int main(int argc, char **argv) {
    if (argc <= 1) {
        std::cout << "error: no action provided\ntry " << argv[0] << " --help for more information\n";
        return 1;
    }

    for (std::uint32_t i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
            std::cout << "usage: " << argv[0] << R"( [command] [flags]
commands:
    search  : takes in search parameters and returns tags/files
    add     : adds files to be tracked/tagged by ftag
    rm      : removes files to be tracked/tagged by ftag
    update  : updates the index of tracked files, use if some have been moved/renamed

no command flags:
    -h, --help     : displays this help
    -v, --version  : displays ftag's version

command flags:
    search:
        -l <text>, --list <text>           : includes all files under tag <text> and subtags
        -t <text>, --tag <text>            : includes all files with tag <text>
        -te <text>, --tag-exclude <text>   : excludes all files with tag <text>
        -f <text>, --file <text>           : includes all files with filename <text>
        -fe <text>, --file-exclude <text>  : excludes all files with filename <text>
        --tags-files                       : displays both tags and files in result (default)
        --tags-only                        : only displays tags in result, no files
        --files-only                       : only displays files in result, no tags
        --no-tag-chain                     : does not show the tag chain each tag descends from 
        --display-tag-chain                : shows the tag chain each tag descends from, up to and including repeat
        --enable-color                     : enables displaying tag color (default)
        --disable-color                    : disables displaying tag color
        --only-filename                    : shows only the filename of each file (default)
        --show-full-path                   : shows the full file path
        --organize-by-tag                  : organizes by tag, allows duplicate file output (default)
        --organize-by-file                 : organizes by file, allows duplicate tag output

        all search flags that take in <text> can be modified to do a basic search for <text> by adding an "s", like -fs or --file-s, or modified to interpret <text> as regex with "r", like -ter or --tag-exclude-r
        regex should probably be passed with quotes so as not to trigger normal shell wildcards
        without any flags, search displays all tags and their files
)";
            return 0;
        }
        if (!std::strcmp(argv[i], "--version") || !std::strcmp(argv[i], "-v")) {
            std::cout << "ftag version " VERSION << std::endl;
            return 0;
        }
    }

    /* commands */
    if (!std::strcmp(argv[1], "search")) {
        read_file_index();
        read_saved_tags();
        display_type_t display_type = display_type_t::tags_files;
        std::vector<search_rule_t> search_rules;
        bool color_enabled = true;
        bool show_full_path = false;
        bool organize_by_tag = true;
        bool display_tag_chain = false;
        for (std::uint32_t i = 2; i < argc; i++) {
            std::string targ = argv[i];
            if (targ == "--tags-files") {
                display_type = display_type_t::tags_files;
                continue;
            }
            if (targ == "--tags-only") {
                display_type = display_type_t::tags;
                continue;
            }
            if (targ == "--files-only") {
                display_type = display_type_t::files;
                continue;
            }
            if (targ == "--enable-color") {
                color_enabled = true;
                continue;
            }
            if (targ == "--disable-color") {
                color_enabled = false;
                continue;
            }
            if (targ == "--only-filename") {
                show_full_path = false;
                continue;
            }
            if (targ == "--show-full-path") {
                show_full_path = true;
                continue;
            }
            if (targ == "--organize-by-file") {
                organize_by_tag = false;
                continue;
            }
            if (targ == "--organize-by-tag") {
                organize_by_tag = true;
                continue;
            }
            if (targ == "--display-tag-chain") {
                display_tag_chain = true;
                continue;
            }
            if (targ == "--no-tag-chain") {
                display_tag_chain = false;
                continue;
            }

            std::string main_arg;
            if (targ.starts_with("--")) {
                main_arg = targ.substr(2, targ.size() - 4);
                if (targ[targ.size() - 2] != '-') {
                    ERR_EXIT(1, "option %i not recognized: \"%s\"", i, argv[i]);
                }
            } else if (targ[0] == '-') {
                main_arg = targ.substr(1, 1);
            }
            std::string opt = targ.substr(targ.size() - 1, 1);
            search_opt_t sopt = search_opt_t::exact;
            if (arg_to_opt.contains(opt)) {
                sopt = arg_to_opt.find(opt)->second;
            }
            search_rules.push_back(search_rule_t{arg_to_rule_type.find(main_arg)->second, sopt, std::string(argv[++i])});
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
            if (search_rule.type != search_rule_type_t::list_tag) {
                bool exclude = search_rule.type == search_rule_type_t::tag_exclude || search_rule.type == search_rule_type_t::file_exclude;
                bool is_file = search_rule.type == search_rule_type_t::file || search_rule.type == search_rule_type_t::file_exclude;
                if (search_rule.opt == search_opt_t::exact) {
                    if (is_file) {
                        for (const auto &[file_ino, file_info] : file_index) {
                            if (file_info.filename == search_rule.text) {
                                files_returned[file_ino] = !exclude;
                            }
                        }
                    } else {
                        for (const auto &[id, tag] : tags) {
                            if (tag.name == search_rule.text) {
                                tags_returned[id] = !exclude;
                                for (const __ino_t &file_ino : tag.files) {
                                    files_returned[file_ino] = !exclude;
                                }
                            }
                        }
                    }
                } else if (search_rule.opt == search_opt_t::text_includes) {
                    if (is_file) {
                        for (const auto &[file_ino, file_info] : file_index) {
                            if (file_info.filename.find(search_rule.text) != std::string::npos) {
                                files_returned[file_ino] = !exclude;
                            }
                        }
                    } else {
                        for (const auto &[id, tag] : tags) {
                            if (tag.name.find(search_rule.text) != std::string::npos) {
                                tags_returned[id] = !exclude;
                                for (const __ino_t &file_ino : tag.files) {
                                    files_returned[file_ino] = !exclude;
                                }
                            }
                        }
                    }
                } else if (search_rule.opt == search_opt_t::regex) {
                    std::regex rg(search_rule.text);
                    if (is_file) {
                        for (const auto &[file_ino, file_info] : file_index) {
                            if (std::regex_search(file_info.filename, rg)) {
                                files_returned[file_ino] = !exclude;
                            }
                        }
                    } else {
                        for (const auto &[id, tag] : tags) {
                            if (std::regex_search(tag.name, rg)) {
                                tags_returned[id] = !exclude;
                                for (const __ino_t &file_ino : tag.files) {
                                    files_returned[file_ino] = !exclude;
                                }
                            }
                        }
                    }
                }
            } else if (search_rule.type == search_rule_type_t::list_tag) {

            }
        }

        const auto display_tag_name = [color_enabled](const tag_t &tag) { /* notably, does not append newline */
            if (color_enabled && tag.color.has_value()) {
                string_color_fg(tag.color.value(), tag.name);
            } else {
                std::cout << tag.name;
            }
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
                    display_tag_name(tag);
                }
                if (display_type == display_type_t::files || display_type == display_type_t::tags_files) {
                    std::cout << ":\n";
                    for (const __ino_t &file_ino : tag.files) {
                        if (!files_returned[file_ino]) { continue; }
                        std::cout << "    " << file_index[file_ino].filename;
                        if (show_full_path) {
                            std::cout << ": " << file_index[file_ino].pathstr;
                        }
                        std::cout << '\n';
                        files_returned[file_ino] = false;
                    }
                }
                std::cout << '\n';
            }
        }
    }
}
