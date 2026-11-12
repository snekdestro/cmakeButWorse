#include "fileMonitor.h"
#include <unordered_set>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <iostream>

std::unordered_map<fs::path, std::unordered_set<fs::path>> header_to_cpps;

std::unordered_set<fs::path> dependents_of(
    const fs::path& header_path) 
{
    std::error_code ec;
    fs::path normalized = fs::weakly_canonical(header_path, ec);
    if (ec) normalized = header_path.lexically_normal();

    auto it = header_to_cpps.find(normalized);
    if (it != header_to_cpps.end()) {
        return it->second;
    }
    return {};
}
fs::path artifact_path(const fs::path& src_file,
                       const fs::path& src_root,
                       const fs::path& build_dir,
                       const std::string& new_ext) {
    std::error_code ec;
    fs::path rel = fs::relative(src_file, src_root, ec);
    if (ec || rel.empty()) {
        // Fallback if file is outside src_root
        rel = src_file.filename();
    }
    fs::path out = build_dir / rel;
    out.replace_extension(new_ext);
    return out;
}

void parse_dep_file(
    const fs::path& dep_file,
    const fs::path& cpp_file) 
{
    std::ifstream in(dep_file);
    if (!in) return;

    // 1. Read the entire file and collapse backslash-newline continuations
    std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string cleaned;
    cleaned.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == '\n') {
            ++i; // Skip both '\' and '\n'
            continue;
        }
        cleaned.push_back(raw[i]);
    }

    // 2. Process line by line
    std::istringstream lines(cleaned);
    std::string line;
    while (std::getline(lines, line)) {
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;

        // Everything after ':' is a space-separated list of dependencies
        std::string rhs = line.substr(colon + 1);
        std::istringstream tokens(rhs);
        std::string token;

        while (tokens >> token) {
            // Handle escaped spaces ("\ ") if your paths contain spaces
            while (!token.empty() && token.back() == '\\') {
                token.pop_back();
                std::string next_part;
                if (tokens >> next_part) {
                    token += " " + next_part;
                }
            }

            // Normalize the path so "include/../include/foo.h" matches "include/foo.h"
            std::error_code ec;
            fs::path dep_path = fs::weakly_canonical(token, ec);
            if (ec) dep_path = fs::path(token).lexically_normal();

            // Skip the .cpp file itself; register every header
            if (dep_path != fs::weakly_canonical(cpp_file, ec)) {
                header_to_cpps[dep_path].insert(cpp_file);
            }
        }
    }
}
bool compile_to_object(
    const fs::path& cpp_file,
    const fs::path& src_root,
    const fs::path& build_dir,
    const std::string& compiler,                  // e.g. "g++"
    const std::vector<fs::path>& include_dirs,    // from INCLUDE lines
    const std::vector<std::string>& flags       // e.g. {"-std=c++20", "-Wall"}
) 
{
    fs::path obj_file = artifact_path(cpp_file, src_root, build_dir, ".o");
    fs::path dep_file = artifact_path(cpp_file, src_root, build_dir, ".d");

    // 1. Ensure build subdirectory exists (e.g., build/net/)
    std::error_code ec;
    fs::create_directories(obj_file.parent_path(), ec);

    // 2. Build the compiler command
    std::string cmd = compiler + " -c " + cpp_file.string() +
                      " -o " + obj_file.string() +
                      " -MMD -MP -MF " + dep_file.string();

    for (const auto& inc : include_dirs) {
        cmd += " -I" + inc.string();
    }
    for (const auto& flag : flags) {
        cmd += " " + flag;
    }

    std::cout << "[CC] " << cpp_file.filename().string() << "\n";

    // 3. Execute compilation
    int exit_code = std::system(cmd.c_str());
    if (exit_code != 0) {
        return false;
    }

    // 4. Refresh dependency map so any newly added/removed #includes take effect
    for (auto& [header, cpps] : header_to_cpps) {
        cpps.erase(cpp_file);
    }
    parse_dep_file(dep_file, cpp_file);

    return true;
}


void remove_artifacts_for(
    const fs::path& deleted_path,
    const fs::path& src_root,
    const fs::path& build_dir) 
{
    auto ext = deleted_path.extension();
    if (ext != ".c" && ext != ".cpp") {
        return; // Deleted headers don't have .o/.d files of their own
    }

    fs::path obj_file = artifact_path(deleted_path, src_root, build_dir, ".o");
    fs::path dep_file = artifact_path(deleted_path, src_root, build_dir, ".d");

    std::error_code ec;
    fs::remove(obj_file, ec);
    fs::remove(dep_file, ec);

    // Also remove this .cpp from all header dependency sets
    for (auto& [header, cpps] : header_to_cpps) {
        cpps.erase(deleted_path);
    }
}
// Maps "src/net/utils.cpp" -> "build/net/utils.o" (or ".d")



void compile(ChangeSet& changes,
    const fs::path& src_root,
    const fs::path& build_dir,
    const std::string& compiler,                  // e.g. "g++"
    const std::vector<fs::path>& include_dirs,    // from INCLUDE lines
    const std::vector<std::string>& flags       // e.g. {"-std=c++20", "-Wall"}
){
    if(!changes.any()){
        return; //ain't shit.
    }
    std::unordered_set<fs::path> to_compile;
    for (const auto& path : changes.modified) {
    auto ext = path.extension();
    if (ext == ".c" || ext == ".cpp") {
        to_compile.insert(path);
    } else if (ext == ".h" || ext == ".hpp") {
        // Find all .cpp files whose .d file lists this header
        for (const auto& cpp : dependents_of(path)) {
            to_compile.insert(cpp);
        }
    }
    for (const auto& path : changes.deleted) {
    remove_artifacts_for(path, src_root, build_dir); // delete .o and .d
}

    bool all_ok = true;
    for (const auto& cpp : to_compile) {
        if (!compile_to_object(cpp,src_root, build_dir, compiler, include_dirs, flags)) { // runs g++ -c ... -MMD -MP
            all_ok = false;
        }
    }
}
}
std::string quote(const fs::path& p) {
    return "\"" + p.string() + "\"";
}

bool link_all_objects(
    const fs::path& build_dir,
    const fs::path& output_binary,                // e.g. "app" or "bin/app"
    const std::string& compiler,                  // e.g. "g++"
    const std::vector<fs::path>& lib_dirs,        // from LIB_DIR lines (-L)
    const std::vector<std::string>& libs,         // from LIB lines (-l)
    const std::vector<std::string>& linker_flags) // optional extra flags
{
    std::error_code ec;
    if (!fs::exists(build_dir, ec)) {
        std::cerr << "[LINK] Build directory does not exist: " << build_dir;
        return false;
    }

    // 1. Recursively gather all .o files in build_dir
    std::vector<fs::path> obj_files;
    for (const auto& entry : fs::recursive_directory_iterator(build_dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".o") {
            obj_files.push_back(entry.path());
        }
    }

    if (obj_files.empty()) {
        std::cerr << "[LINK] No object files found to link.\n";
        return false;
    }

    // 2. Ensure the output binary's parent directory exists (e.g., if OUT is "bin/app")
    if (output_binary.has_parent_path()) {
        fs::create_directories(output_binary.parent_path(), ec);
    }

    // 3. Construct the linker command
    // Order matters for GNU ld: object files FIRST, then -L paths, then -l libraries
    std::string cmd = compiler;

    for (const auto& obj : obj_files) {
        cmd += " " + quote(obj);
    }

    cmd += " -o " + quote(output_binary);

    for (const auto& dir : lib_dirs) {
        cmd += " -L" + quote(dir);
    }

    for (const auto& lib : libs) {
        // Strip leading "-l" if the user already wrote "-lSDL2" in the build file
        if (lib.rfind("-l", 0) == 0) {
            cmd += " " + lib;
        } else {
            cmd += " -l" + lib;
        }
    }

    for (const auto& flag : linker_flags) {
        cmd += " " + flag;
    }

    std::cout << "[LINK] " << output_binary.filename().string() << "\n";

    // 4. Execute linking
    int exit_code = std::system(cmd.c_str());
    return exit_code == 0;
}


int main(int argc, char const *argv[])
{
    
    return 0;
}
