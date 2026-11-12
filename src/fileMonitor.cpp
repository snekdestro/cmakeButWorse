#include "fileMonitor.h"
#include <string>
#include <unordered_set>
#include <iostream>


const std::unordered_set<std::string> target_exts = {".c", ".cpp", ".h", ".hpp"};
const auto options = fs::directory_options::skip_permission_denied;
void fileMonitor::grab(){
    fs::path target_dir = folderPath;


    // Check if the path exists and is a directory before iterating
    if (!fs::exists(target_dir) || !fs::is_directory(target_dir)) {
        std::cerr << "Invalid directory path.\n";
        return;
    }

    // Use skip_permission_denied to prevent exceptions on locked system folders

    for (const auto& entry : fs::recursive_directory_iterator(target_dir, options)) {
        // Ensure we are only looking at regular files (ignores directories, symlinks, etc.)
        if (!entry.is_regular_file()) {
            continue;
        }

        // Extract the extension and check if it matches .c, .cpp, or .h
        std::string ext = entry.path().extension().string();
        if (target_exts.find(ext) != target_exts.end()) {
            try {
                // Grab the last_write_time
                auto ftime = entry.last_write_time();
                
                file_timestamps[entry.path()] = FileInfo{ftime, entry.file_size()}; 
            } catch (const fs::filesystem_error& e) {
                std::cerr << "Error reading timestamp for " << entry.path() 
                          << ": " << e.what() << '\n';
            }
        }
    }



}

ChangeSet fileMonitor::changed(){
    std::unordered_map<fs::path, FileInfo> current_state;
    ChangeSet changes;
    for(const auto& entry : fs::recursive_directory_iterator(folderPath,options)){
        if (!entry.is_regular_file()) {
            continue;
        }
        std::string ext = entry.path().extension().string();
        if (target_exts.find(ext) != target_exts.end()) {
            FileInfo info{entry.last_write_time(), entry.file_size()};
            current_state[entry.path()] =  info;
            auto it = file_timestamps.find(entry.path());
            if(it == file_timestamps.end()){ //not present previously
                changes.modified.insert(entry.path());
            }else if(it->second.last_write != info.last_write || it->second.size != info.size){
                changes.modified.insert(entry.path());
            }

        }

    }
    for (const auto& [path, info] : file_timestamps) {
        if (current_state.find(path) == current_state.end()) {
            changes.deleted.insert(path);
        }
    }
    file_timestamps = std::move(current_state);
    return changes;


}