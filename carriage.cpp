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
#include <vector>
#include <signal.h>

#define MAX_STAT_SIZE 1024
#define PROC_DIR (const char*)"/proc"
#define UPTIME_PATH (const char*)"/proc/uptime"
#define TOTAL_MEM_PATH (const char*)"/proc/meminfo"

volatile sig_atomic_t keep_running = 1;

struct ProcessStats {
    char pid[16];
    char user[32];
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

    char command[64];
};

bool CompareByRes(const ProcessStats& a, const ProcessStats& b) {
    return a.cpu_usage > b.cpu_usage;
}

void signal_handler(int signal) {
    keep_running = 0;
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
    for (size_t i = 0; name[i] != '\0'; ++i) {
        if (!isdigit(static_cast<unsigned char>(name[i]))) {
            return false;
        }
    }
    return true;
}

int GetUptime(double* time) {
    FILE* file = fopen(UPTIME_PATH, "r");
    if (!file) {
        return 1;
    }
    
    fscanf(file, "%lf", time);
    fclose(file);
    return 0;
}

int GetTotalMem(size_t* size) {
    FILE* file = fopen(TOTAL_MEM_PATH, "r");
    if (!file) {
        return 1;
    }
    
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            sscanf(line + 9, "%lu", size);
            break;
        }
    }
    fclose(file);
    return 0;
}

double CalcCpuPercent(ProcessStats* stats, double uptime) {
    double total_time = stats->utime + stats->stime;
    double seconds = uptime - (stats->start_time / get_clock_ticks_per_second());
    
    if (seconds <= 0) seconds = 1.0;
    
    double cpu_usage = 100 * ((total_time / get_clock_ticks_per_second()) / seconds);
    return cpu_usage > 0 ? cpu_usage : 0;
}

double CalcMemPercent(ProcessStats* stats, size_t total_mem_kb) {
    if (total_mem_kb == 0) return 0;
    
    // RES в страницах, конвертируем в KB (страница = 4 KB)
    size_t res_kb = stats->res * 4;
    return 100.0 * res_kb / total_mem_kb;
}

// Функция для форматирования памяти в человеко-читаемый вид
void format_memory(size_t kb, char* buffer, size_t buf_size) {
    if (kb < 1024) {
        snprintf(buffer, buf_size, "%luk", kb);
    } else if (kb < 1024 * 1024) {
        snprintf(buffer, buf_size, "%lum", kb / 1024);
    } else {
        snprintf(buffer, buf_size, "%lug", kb / (1024 * 1024));
    }
}

void PrintStats(ProcessStats* stats, int line_num, double uptime, size_t total_mem) {
    double process_start_seconds = ticks_to_seconds(stats->start_time);
    double process_uptime_seconds = uptime - process_start_seconds;
    if (process_uptime_seconds < 0) {
        process_uptime_seconds = 0;
    }

    size_t total_seconds = static_cast<size_t>(process_uptime_seconds);
    size_t seconds = total_seconds % 60;
    size_t minutes = (total_seconds / 60) % 60;
    size_t hours = total_seconds / 3600;

    double cpu_usage = stats->cpu_usage;
    double mem_usage = CalcMemPercent(stats, total_mem);

    // Форматируем память
    char virt_buffer[16], res_buffer[16];
    format_memory(stats->virt / 1024, virt_buffer, sizeof(virt_buffer)); // VIRT в KB
    format_memory(stats->res * 4, res_buffer, sizeof(res_buffer));       // RES в KB

    // Форматируем время
    char time_buffer[16];
    if (hours > 0) {
        snprintf(time_buffer, sizeof(time_buffer), "%02lu:%02lu:%02lu", hours, minutes, seconds);
    } else {
        snprintf(time_buffer, sizeof(time_buffer), "   %02lu:%02lu", minutes, seconds);
    }

    printf("\033[%d;1H\033[K", line_num + 2);

    // ✅ НОРМАЛЬНЫЙ ФОРМАТИРОВАННЫЙ ВЫВОД
    printf("%-6.6s "    // PID
           "%-8.8s "    // USER
           "%3d "       // PR
           "%3d "       // NI
           "%7s "       // VIRT
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
           cpu_usage,
           mem_usage,
           time_buffer,
           stats->command);
}

int ExtractStat(char* filepath, char* buffer) {
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        return 1;
    }

    ssize_t bytes_read = read(fd, buffer, MAX_STAT_SIZE - 1);
    close(fd);
    
    if (bytes_read <= 0) {
        return 1;
    }

    buffer[bytes_read] = '\0';
    return 0;
}

int ExtractUserFromStat(int pid, char* path, ProcessStats* stats) {
    struct stat buf;
    if (stat(path, &buf) != 0) {
        strncpy(stats->user, "?", sizeof(stats->user) - 1);
        return 1;
    }

    struct passwd* pwd = getpwuid(buf.st_uid);
    if (pwd != nullptr) {
        strncpy(stats->user, pwd->pw_name, sizeof(stats->user) - 1);
    } else {
        snprintf(stats->user, sizeof(stats->user), "%d", buf.st_uid);
    }
    return 0;
}

int ExtractStatsToStruct(char* buffer, char* filename, ProcessStats* stats) {
    char buffer_copy[MAX_STAT_SIZE];
    strcpy(buffer_copy, buffer);

    int token_count = 0;
    char* token = strtok(buffer_copy, " ");

    while (token != nullptr) {
        token_count++;

        switch (token_count) {
            case 1: // PID
                strncpy(stats->pid, token, sizeof(stats->pid) - 1);
                break;

            case 2: // Program name
                // Убираем скобки из имени команды
                if (token[0] == '(' && token[strlen(token)-1] == ')') {
                    size_t len = strlen(token) - 2;
                    if (len >= sizeof(stats->command)) len = sizeof(stats->command) - 1;
                    strncpy(stats->command, token + 1, len);
                    stats->command[len] = '\0';
                } else {
                    strncpy(stats->command, token, sizeof(stats->command) - 1);
                }
                break;

            case 3: // Status
                strncpy(stats->status, token, sizeof(stats->status) - 1);
                break;

            case 14: // User time
                stats->utime = atol(token);
                break;

            case 15: // Kernel time
                stats->stime = atol(token);
                break;

            case 16: // Child user time
                stats->children_utime = atol(token);
                break;

            case 17: // Child kernel time
                stats->children_stime = atol(token);
                break;

            case 18: // Priority
                stats->priority = atol(token);
                break;

            case 19: // Nice
                stats->nice = atoi(token);
                break;

            case 22: // Process start time
                stats->start_time = atol(token);
                break;

            case 23: // Virtual MEM
                stats->virt = atol(token);
                break;

            case 24: // Real MEM
                stats->res = atol(token);
                break;
        }
        
        token = strtok(nullptr, " ");
    }

    ExtractUserFromStat(atoi(stats->pid), filename, stats);
    return 0;
}

int ExtractStatsFromProccess(char* filepath, ProcessStats* process_stats) {
    DIR* process = opendir(filepath);
    if (process == nullptr) {
        return 1;
    }

    struct dirent* entry;
    bool has_stat = false;

    while ((entry = readdir(process)) != nullptr) {
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
    double uptime;
    size_t total_mem;

    if (GetUptime(&uptime) != 0) uptime = 0;
    if (GetTotalMem(&total_mem) != 0) total_mem = 1;

    DIR* proc = opendir(PROC_DIR);
    if (proc == nullptr) {
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
            
            if (ExtractStatsFromProccess(concat, &process_stats) == 0 && 
                process_stats.priority >= 0) {
                process_stats.cpu_usage = CalcCpuPercent(&process_stats, uptime);
                processes.push_back(process_stats);
            }
            free(concat);
        }
    }
    closedir(proc);

    std::sort(processes.begin(), processes.end(), CompareByRes);

    // Очищаем область вывода процессов
    printf("\033[2;1H\033[J");

    // Выводим процессы (ограничиваем количество)
    size_t max_processes = std::min(processes.size(), (size_t)30);
    for (size_t i = 0; i < max_processes; ++i) {
        PrintStats(&processes[i], i, uptime, total_mem);
    }

    // Очищаем оставшиеся строки
    for (size_t i = max_processes; i < 40; i++) {
        printf("\033[%zu;1H\033[K", i + 2);
    }

    return 0;
}

int main() {
    signal(SIGINT, signal_handler);
    
    // Очищаем экран и скрываем курсор
    printf("\033[2J\033[H\033[?25l");
    
    // Выводим заголовок один раз
    printf("%-6s %-8s %3s %3s %7s %7s %1s %5s %5s %9s %s\n", 
           "PID", "USER", "PR", "NI", "VIRT", "RES", "S", "%CPU", "%MEM", "TIME+", "COMMAND");

    int refresh_count = 0;
    while (keep_running) {
        GetProcesses();
        
        // Обновляем статусную строку
        printf("\033[999;1H\033[K");
        printf("Refresh: %d | Sorted by CPU | Press Ctrl+C to exit", ++refresh_count);
        fflush(stdout);

        sleep(2);
    }
    
    // Восстанавливаем курсор
    printf("\033[2J\033[H\033[?25h");
    printf("Monitoring stopped.\n");
    
    return 0;
}