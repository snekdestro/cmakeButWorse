#include <fstream>
#include <unordered_map>
#include <filesystem>
#include <unordered_set>
namespace fs = std::filesystem;
struct FileInfo {
    fs::file_time_type last_write;
    uintmax_t size;
    // Optional: uint64_t inode; (useful for rename detection)
};
struct ChangeSet {
    std::unordered_set<fs::path> modified; // added or edited (.c, .cpp, .h, .hpp)
    std::unordered_set<fs::path> deleted;  // removed files
    bool any() const { return !modified.empty() || !deleted.empty(); }
};

class fileMonitor
{
private:
    std::string folderPath;
    std::unordered_map<fs::path, FileInfo> file_timestamps;
public:
    void grab();
    ChangeSet changed();
    fileMonitor(std::string& path){
        folderPath = path;
        grab();
    }
    fileMonitor(std::string& path, std::unordered_map<fs::path, FileInfo>& file){
        folderPath = path;
        file_timestamps = file;
    }
};
