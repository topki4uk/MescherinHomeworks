#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <iostream>
#include <vector>
#define PROC_DIR (const char*)"/proc"
#define MAX_STAT_SIZE 1024

struct ProcessStats {
    char* pid;
    char* user;
    int priority;
    char* nice;
    char* virt;
    size_t res;
    char* status;

    // Data for CPU info
    size_t utime;
    size_t stime;
    size_t children_utime;
    size_t children_stime;
    size_t start_time;

    // Data for MEM info

    char* command;
};

void JoinPath(const char* str1, const char* str2, char* concat) {
    strcpy(concat, str1);
    strcat(concat, "/");
    strcat(concat, str2);
}

bool IsProccess(char* name) {
    for (size_t i = 0; i < name[i] != '\0'; ++i) {
        if (!isdigit(static_cast<unsigned char>(name[i]))) {
            return false;
        }
    }
    return true;
}

void PrintStats(ProcessStats* stats) {
    printf(
        "%6s %-8.8s %3d %3s %7ld %7ld %1s %5.1f %5.1f %9ld %s\n", 
        stats->pid, 
        "???", 
        stats->priority, 
        stats->nice, 
        stats->virt,    // VIRT в килобайтах
        stats->res,     // RES в килобайтах
        stats->status, 
        0.0,                   // %CPU
        0.0,                   // %MEM
        228l,                  // TIME+ в секундах
        stats->command
    );
}

int ExtractStat(char* filepath, char* buffer) {
    ssize_t bytes_read;

    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        printf("open failed: %s\n", strerror(errno));
        return 1;
    }

    bytes_read = read(fd, buffer, MAX_STAT_SIZE - 1);
    if (bytes_read == -1) {
        printf("read failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    buffer[bytes_read] = '\0';
    close(fd);

    return 0;
}

int ExtractStatsToStruct(char* buffer, ProcessStats* stats) {
    int token_count = 0;
    char* token;

    // printf("%s\n", buffer);
    token = strtok(buffer, " ");
    while (token != nullptr) {
        token_count++;

        switch (token_count) {
            case 1: // PID
                stats->pid = token;
                break;

            case 2: // Program name
                stats->command = token;
                break;

            case 3: // Status
                stats->status = token;
                break;

            case 14: // User time (tacts)
                stats->utime = atol(token);
                break;

            case 15: // Kernel time (tacts)
                stats->stime = atol(token);
                break;

            case 16: // Child user time (tacts)
                stats->children_utime = atol(token);
                break;

            case 17: // Child kernel time (tacts)
                stats->children_stime = atol(token);
                break;

            case 18: // Priority
                stats->priority = atol(token);
                break;

            case 19: // Nice
                stats->nice = token;
                break;

            case 22: // Process start time
                stats->start_time = atol(token);
                break;

            case 23: // Virtual MEM
                stats->virt = token;
                break;

            case 24: // Real MEM
                stats->res = atol(token);
                break;
        }
        
        token = strtok(nullptr, " ");
    }

    return 0;
}

int ExtractStatsFromProccess(char* filepath, ProcessStats* process_stats) {
    DIR* process = opendir(filepath);
        if (process == nullptr) {
        printf("opendir failed: %s\n", strerror(errno));
        return 1;
    }

    struct dirent* entry;
    bool has_stat = false;

    while ((entry = readdir(process)) != nullptr) {
        if (strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, ".") == 0) {
            continue;
        }

        if (strcmp(entry->d_name, "stat") == 0) {
            has_stat = true;
            break;
        }
    }
    closedir(process);

    if (!has_stat) {
        return 1;
    }

    char* statstring = (char*)"stat";
    char buffer[MAX_STAT_SIZE];
    char* stat_path = static_cast<char*>(malloc(strlen(filepath) + strlen(statstring) + 2));

    JoinPath(filepath, statstring, stat_path);
    ExtractStat(stat_path, buffer);
    ExtractStatsToStruct(buffer, process_stats);

    free(stat_path);

    return 0;
}

int GetProcesses() {
    DIR* proc = opendir(PROC_DIR);
    if (proc == nullptr) {
        printf("opendir failed: %s\n", strerror(errno));
        return 1;
    }

    struct dirent* entry;
    while ((entry = readdir(proc)) != nullptr) {
        if (strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, ".") == 0) {
            continue;
        }

        if (IsProccess(entry->d_name)) {
            char* concat = static_cast<char*>(malloc(strlen(PROC_DIR) + strlen(entry->d_name) + 2));
            JoinPath(PROC_DIR, entry->d_name, concat);

            // std::cout << concat << '\n';

            ProcessStats process_stats;
            if (ExtractStatsFromProccess(concat, &process_stats) != 0) {
                printf("ExtractStatsFromProccess failed: %s\n", strerror(errno));
                return 1;
            }

            if (process_stats.priority < 0) {
                continue;
            }

            PrintStats(&process_stats);
            free(concat);
        }
    }
    closedir(proc);
    fflush(stdout);

    return 0;
}

int main() {
    printf("%6s %-8s %3s %3s %15s %7s %1s %5s %5s %9s %s\n", 
       "PID", "USER", "PR", "NI", "VIRT", "RES", "S", "%CPU", "%MEM", "TIME+", "COMMAND");
    GetProcesses();
}