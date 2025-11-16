#include <errno.h>
#include <fcntl.h>
#include <algorithm>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/stat.h>
#include <pwd.h>
#include <dirent.h>
#include <string.h>
#include <iostream>
#include <unordered_map>
#include <vector>

#define MAX_STAT_SIZE 2048
#define PROC_DIR (const char*)"/proc"
#define UPTIME_PATH (const char*)"/proc/uptime"
#define TOTAL_MEM_PATH (const char*)"/proc/meminfo"

struct ProcessStats {
    int pid;
    char user[256];
    int priority;
    int nice;
    size_t virt;
    size_t res;
    char status[8];

    // Data for CPU info
    size_t utime;
    size_t stime;
    size_t children_utime;
    size_t children_stime;
    size_t start_time;

    // Sort param
    double cpu_usage;

    char command[256];
};

std::unordered_map<int, size_t> prev_cpu_stats;
double prev_uptime = 0;

bool CompareByRes(const ProcessStats& a, const ProcessStats& b) {
    return a.cpu_usage > b.cpu_usage;
}

long get_clock_ticks_per_second() {
    long ticks = sysconf(_SC_CLK_TCK);
    return ticks > 0 ? ticks : 100;
}

double ticks_to_seconds(unsigned long ticks) {
    return (double)ticks / get_clock_ticks_per_second();
}

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

int GetUptime(double* time) {
    ssize_t bytes_read;
    int fd = open(UPTIME_PATH, O_RDONLY);

    if (fd < 0) {
        printf("open failed: %s", strerror(errno));
        return 1;
    }

    char buffer[MAX_STAT_SIZE];
    bytes_read = read(fd, buffer, MAX_STAT_SIZE - 1);
    if (bytes_read == -1) {
        printf("read failed: %s", strerror(errno));
        close(fd);
        return 1;
    }

    buffer[bytes_read] = '\0';
    close(fd);

    char* token = strtok(buffer, " ");
    // token = strtok(nullptr, " ");
    *time = atof(token);

    return 0;
}

int GetTotalMem(size_t* size) {
    ssize_t bytes_read;
    int fd = open(TOTAL_MEM_PATH, O_RDONLY);
    if (fd < 0) {
        printf("open failed: %s", strerror(errno));
        return 1;
    }

    char buffer[MAX_STAT_SIZE];
    bytes_read = read(fd, buffer, MAX_STAT_SIZE - 1);
    if (bytes_read == -1) {
        printf("read failed: %s", strerror(errno));
        close(fd);
        return 1;
    }

    buffer[bytes_read] = '\0';
    close(fd);

    char* token = strtok(buffer, " ");
    token = strtok(nullptr, " ");
    *size = atol(token);

    return 0;
}

double CalcCpuPercent(ProcessStats* stats, double uptime) {
    if (prev_cpu_stats.find(stats->pid) == nullptr) {
        return 0.0;
    }
    
    size_t curr_total_time = stats->utime + stats->stime
                        + stats->children_stime + stats->children_utime;
    
    size_t prev_total_time = prev_cpu_stats[stats->pid];

    double system_time_interval = uptime - prev_uptime;
    if (system_time_interval <= 0) {
        return 0.0;
    }

    size_t process_time_diff = curr_total_time - prev_total_time;
    
    double process_seconds = (double)process_time_diff / get_clock_ticks_per_second();
    double cpu_usage = (process_seconds / system_time_interval) * 100.0;
    
    if (cpu_usage < 0) cpu_usage = 0;
    
    return cpu_usage;
}

double CalcMemPercent(ProcessStats* stats, size_t total_mem) {
    size_t res_page_size = stats->res * 4;
    return 100 * (1.0 * res_page_size / total_mem);
}

void format_memory(size_t kb, char* buffer, size_t buf_size) {
    if (kb < 1024) {
        snprintf(buffer, buf_size, "%luK", kb);
    } else if (kb < 1024 * 1024) {
        snprintf(buffer, buf_size, "%luM", kb / 1024);
    } else {
        snprintf(buffer, buf_size, "%luG", kb / (1024 * 1024));
    }
}

void PrintStats(ProcessStats* stats, int line_num, size_t total_mem) {
    double uptime;
    GetUptime(&uptime);

    double process_start_seconds = ticks_to_seconds(stats->start_time);
    double process_uptime_seconds = uptime - process_start_seconds;

    if (process_uptime_seconds < 0) {
        process_uptime_seconds = 0;
    }

    size_t all_seconds = static_cast<size_t>(process_uptime_seconds);
    size_t seconds = all_seconds % 60;
    size_t minutes = (all_seconds / 60) % 60;
    size_t hours = all_seconds / 3600;

    double mem_usage = CalcMemPercent(stats, total_mem);

    char virt_buffer[16], res_buffer[16];
    format_memory(stats->virt / 1024, virt_buffer, sizeof(virt_buffer)); // VIRT в KB
    format_memory(stats->res * 4, res_buffer, sizeof(res_buffer));       // RES в KB

    char time_buffer[32];
    if (hours > 0) {
        snprintf(time_buffer, sizeof(time_buffer), "%02lu:%02lu:%02lu", hours, minutes, seconds);
    } else {
        snprintf(time_buffer, sizeof(time_buffer), "   %02lu:%02lu", minutes, seconds);
    }

    printf("\033[%d;1H\033[K", line_num + 2);

    printf("%-.6d "    // PID
           "%-8.8s "    // USER
           "%4d "       // PR
           "%4d "       // NI
           "%7s "    // VIRT
           "%7s "       // RES
           "%-1.1s "    // STATUS
           "%5.1f "     // %CPU
           "%5.1f "     // %MEM
           "%9s "       // TIME+
           "%-20.20s",  // COMMAND
           stats->pid,
           stats->user,
           stats->priority,
           stats->nice,
           virt_buffer,
           res_buffer,
           stats->status,
           stats->cpu_usage,
           mem_usage,
           time_buffer,
           stats->command);
}

int ExtractStat(char* filepath, char* buffer) {
    ssize_t bytes_read;

    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        printf("open failed: %s", strerror(errno));
        return 1;
    }

    bytes_read = read(fd, buffer, MAX_STAT_SIZE - 1);
    if (bytes_read == -1) {
        printf("read failed: %s", strerror(errno));
        close(fd);
        return 1;
    }

    buffer[bytes_read] = '\0';
    close(fd);

    return 0;
}

int ExtractUserFromStat(int pid, char* path, ProcessStats* stats) {
    struct stat buf;
    if (stat(path, &buf) != 0) {
        printf("stat failed: %s", strerror(errno));
        return 1;
    }

    struct passwd* pwd = getpwuid(buf.st_uid);
    if (pwd != nullptr) {
        strcpy(stats->user, pwd->pw_name);
        stats->user[31] = '\0';
    } else {
        printf("pwd failed");
    }

    return 0;
}

int ExtractStatsToStruct(char* buffer, char* filename, ProcessStats* stats) {
    char buffer_copy[MAX_STAT_SIZE];
    strcpy(buffer_copy, buffer);

    int token_count = 0;

    // printf("%s\n", buffer);
    char* token = strtok(buffer_copy, " ");
    while (token != nullptr) {
        token_count++;

        switch (token_count) {
            case 1: // PID
                stats->pid = atoi(token);
                break;

            case 2: // Program name
                strncpy(stats->command, token, sizeof(stats->command) - 1);
                stats->command[sizeof(stats->command) - 1] = '\0';
                break;

            case 3: // Status
                strncpy(stats->status, token, sizeof(stats->status) - 1);
                break;

            case 14: // User time (tacts)
                stats->utime = strtoul(token, nullptr, 10);
                break;

            case 15: // Kernel time (tacts)
                stats->stime = strtoul(token, nullptr, 10);
                break;

            case 16: // Child user time (tacts)
                stats->children_utime = strtoul(token, nullptr, 10);
                break;

            case 17: // Child kernel time (tacts)
                stats->children_stime = strtoul(token, nullptr, 10);
                break;

            case 18: // Priority
                stats->priority = atol(token);
                break;

            case 19: // Nice
                stats->nice = atoi(token);
                break;

            case 22: // Process start time
                stats->start_time = strtoul(token, nullptr, 10);
                break;

            case 23: // Virtual MEM
                stats->virt = strtoul(token, nullptr, 10);
                break;

            case 24: // Real MEM
                stats->res = strtoul(token, nullptr, 10);
                break;
        }
        
        token = strtok(nullptr, " ");
    }

    ExtractUserFromStat(stats->pid, filename, stats);
    return 0;
}

int ExtractStatsFromProccess(char* filepath, ProcessStats* process_stats) {
    DIR* process = opendir(filepath);
        if (process == nullptr) {
        printf("opendir failed: %s", strerror(errno));
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
    ExtractStatsToStruct(buffer, stat_path, process_stats);

    free(stat_path);

    return 0;
}

int GetProcesses() {
    DIR* proc = opendir(PROC_DIR);
    if (proc == nullptr) {
        printf("opendir failed: %s", strerror(errno));
        return 1;
    }

    std::vector<ProcessStats> processes;

    struct dirent* entry;
    while ((entry = readdir(proc)) != nullptr) {
        if (strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, ".") == 0) {
            continue;
        }

        if (IsProccess(entry->d_name)) {
            char* concat = static_cast<char*>(malloc(strlen(PROC_DIR) + strlen(entry->d_name) + 2));
            JoinPath(PROC_DIR, entry->d_name, concat);

            ProcessStats process_stats;
            memset(&process_stats, 0, sizeof(ProcessStats));
            if (ExtractStatsFromProccess(concat, &process_stats) != 0) {
                free(concat);
                continue;
            }

            processes.push_back(process_stats);       
            free(concat);
        }
    }
    closedir(proc);

    double uptime;
    GetUptime(&uptime);
    for (auto& stats : processes) {
        stats.cpu_usage = CalcCpuPercent(&stats, uptime);
    }

    std::sort(processes.begin(), processes.end(), CompareByRes);
    for (int i = 2; i <= 32; i++) {
        printf("\033[%d;1H\033[K", i);
    }

    prev_uptime = uptime;

    size_t total_mem = 0;
    GetTotalMem(&total_mem);
    size_t max_processes = std::min(processes.size(), (size_t)30);
    for (size_t i = 0; i < max_processes; ++i) {
        PrintStats(&processes[i], i, total_mem);
    }

    prev_cpu_stats.clear();
    for (const auto& stats : processes) {
        prev_cpu_stats[stats.pid] = stats.utime + stats.stime
                        + stats.children_stime + stats.children_utime;  // Сохраняем текущие статистики для следующего цикла
    }

    // Очищаем оставшиеся строки
    for (size_t i = max_processes; i < 30; i++) {
        printf("\033[%zu;1H\033[K", i + 2);
    }

    return 0;
}

int main() {
    printf("\033[2J\033[H\033[?25l");

    printf("%-6s %-8s %4s %4s %7s %7s %1s %5s %5s %9s %s\n", 
           "PID", "USER", "PR", "NI", "VIRT", "RES", "S", "%CPU", "%MEM", "TIME+", "COMMAND");

    int refresh_count = 0;
    while (true) {
        GetProcesses();
        printf("\033[999;1H\033[K");

        printf("Refresh: %d | Sorted by CPU | Press Ctrl+C to exit", ++refresh_count);
        fflush(stdout);
        sleep(1);
    }

    GetProcesses();
}