#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <string>
#include <unordered_set>
#include <iostream>
#define MAX_PATH_SIZE 1024

struct FlagsManager {
    bool default_params = false;
    bool only_dirsize = false;
    bool all_stuff = false;
    bool link_unwrap = false;
    char* filename = nullptr;
};

int GetFileInfo(char*, size_t*, std::unordered_set<size_t>&, FlagsManager&);
int ManageDirectory(char*, size_t*, std::unordered_set<size_t>&, FlagsManager&);

std::string CheckFileType(struct stat* stat_buf) {
    if ((stat_buf->st_mode & S_IFMT) == S_IFREG) {
        return "regular";
    } else if ((stat_buf->st_mode & S_IFMT) == S_IFDIR) {
        return "directory";
    } else if ((stat_buf->st_mode & S_IFMT) == S_IFLNK) {
        return "symbolic link";
    } else if ((stat_buf->st_mode & S_IFMT) == S_IFCHR) {
        return "character device";
    } else if ((stat_buf->st_mode & S_IFMT) == S_IFBLK) {
        return "block device";
    } else if ((stat_buf->st_mode & S_IFMT) == S_IFIFO) {
        return "FIFO/pipe";
    } else if ((stat_buf->st_mode & S_IFMT) == S_IFSOCK) {
        return "socket";
    } else {
        return "unknown";
    }
}

bool IsDirectory(struct stat* stat_buf) {
    return (stat_buf->st_mode & S_IFMT) == S_IFDIR;
}

bool IsSymLink(struct stat* stat_buf) {
    return (stat_buf->st_mode & S_IFMT) == S_IFLNK;
}

int GetFileInfo(char* filename, size_t* size_count, std::unordered_set<size_t>& inodes, FlagsManager& manager) {
    struct stat stat_buf;

    int res_code = 0;
    ssize_t len = 0;
    char buffer[MAX_PATH_SIZE];

    // If -L flag
    if (manager.link_unwrap) {
        res_code = stat(filename, &stat_buf);
        len = readlink(filename, buffer, sizeof(buffer) - 1);
        buffer[len] = '\0';
    } else {
        res_code = lstat(filename, &stat_buf);
    }

    if (res_code < 0) {
        return 1;
    }

    if (inodes.find(stat_buf.st_ino) != inodes.end()) {
        return 0;
    }

    inodes.insert(stat_buf.st_ino);
    std::string stat_mode = CheckFileType(&stat_buf);

    if (!IsDirectory(&stat_buf) && strcmp(filename, manager.filename) == 0) {
        std::cout << stat_buf.st_size << " " << filename << '\n';
        return 0;
    }

    size_t dir_size = 0;
    if (IsDirectory(&stat_buf)) {
        ManageDirectory(filename, &dir_size, inodes, manager);

        // If default settings
        if (!manager.only_dirsize) {
            std::cout << dir_size << " " << filename << '\n';
        }

        *size_count += dir_size;
    } else {
        *size_count += stat_buf.st_size; 
    }

    // If -s flag
    if (IsDirectory(&stat_buf) && manager.only_dirsize && strcmp(filename, manager.filename) == 0) {
        std::cout << dir_size << " " << filename << '\n';
        return 0;
    }
    
    // If -a flag
    if (!IsDirectory(&stat_buf) && manager.all_stuff) {
        std::cout << stat_buf.st_size << " " << filename << '\n';
    }

    return 0;
}

int GetFileInfo(char* filename, std::unordered_set<size_t>& inodes, FlagsManager& manager) {
    size_t ignore_value;
    return GetFileInfo(filename, &ignore_value, inodes, manager);
}

int ManageDirectory(char* path, size_t* dir_size, std::unordered_set<size_t>& inodes, FlagsManager& manager) {
    DIR* dir = opendir(path);
    if (dir == nullptr) {
        printf("opendir failed: %s\n", strerror(errno));
        return 1;
    }
 
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, ".") == 0) {
            continue;
        }

        char* concat = (char*) malloc(strlen(path) + strlen(entry->d_name) + 2);
        strcpy(concat, path);
        strcat(concat, "/");
        strcat(concat, entry->d_name);

        GetFileInfo(concat, dir_size, inodes, manager);
        free(concat);
    }

    closedir(dir);
    return 0;
}

void ExtractFlags(int argc, char** argv, FlagsManager& manager) {
    manager.filename = argv[argc - 1];

    if (argc == 2) {
        manager.default_params = true;
        return;
    }

    if (strstr(argv[1], "a") != nullptr) {
        manager.all_stuff = true;
    }

    if (strstr(argv[1], "s") != nullptr) {
        manager.only_dirsize = true;
    }

    if (strstr(argv[1], "L") != nullptr) {
        manager.link_unwrap = true;
    }
}

int main(int argc, char** argv) {
    std::unordered_set<size_t> inodes;

    FlagsManager manager{};
    ExtractFlags(argc, argv, manager);

    // std::cout << "Default mode: " << manager.default_params << '\n';
    // std::cout << "Dirsize mode: " << manager.only_dirsize << '\n';
    // std::cout << "All files mode: " << manager.all_stuff << '\n';
    // std::cout << "Link unwrap mode: " << manager.link_unwrap << '\n';

    GetFileInfo(manager.filename, inodes, manager);
}