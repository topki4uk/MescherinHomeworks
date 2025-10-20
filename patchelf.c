#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <libelf.h>
#include <errno.h>
#include <gelf.h>

struct ProgramMode GetMode(int,char**);
void print_usage(const char *);

struct ProgramMode {
    int status;
    int set_runpath;
    char *runpath;
    int set_interp;
    char *interp;
    int print_runpath;
    int print_interp;
    char *filename;
};

void print_program_mode(const struct ProgramMode *mode) {
    printf("Program Mode Configuration:\n");
    printf("  status: %s\n", !mode->status ? "OK" : "Bad");
    printf("  set_runpath: %d\n", mode->set_runpath);
    printf("  runpath: %s\n", mode->runpath ? mode->runpath : "(null)");
    printf("  set_interp: %d\n", mode->set_interp);
    printf("  interp: %s\n", mode->interp ? mode->interp : "(null)");
    printf("  print_runpath: %d\n", mode->print_runpath);
    printf("  print_interp: %d\n", mode->print_interp);
    printf("  filename: %s\n", mode->filename ? mode->filename : "(null)");
}

struct ProgramMode GetMode(int argc, char **argv) {
    struct ProgramMode mode = {0};

    for (int i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "--set-rpath") == 0) {
            if (argc != 4) {
                print_usage(argv[0]);
                mode.status = 1;
                return mode;
            }
            mode.set_runpath = 1;
            mode.runpath = argv[i + 1];
            break;
        }
        if (strcmp(argv[i], "--set-interpreter") == 0) {
            if (argc != 4) {
                print_usage(argv[0]);
                mode.status = 1;
                return mode;
            }
            mode.set_interp = 1;
            mode.interp = argv[i + 1];
            break;
        }
        if (strcmp(argv[i], "--print-rpath") == 0) {
            mode.print_runpath = 1;
            break;
        }
        if (strcmp(argv[i], "--print-interpreter") == 0) {
            mode.print_interp = 1;
            break;
        }
    }

    mode.filename = argv[argc - 1];
    return mode;
}

size_t vaddr_to_offset(Elf *elf, GElf_Addr vaddr) {
    size_t n;
    if (elf_getphdrnum(elf, &n) != 0) {
        return 0;
    }
    
    for (size_t i = 0; i < n; i++) {
        GElf_Phdr phdr;
        if (gelf_getphdr(elf, i, &phdr) != &phdr) {
            continue;
        }
        
        // Проверяем, попадает ли виртуальный адрес в этот сегмент
        if (vaddr >= phdr.p_vaddr && vaddr < phdr.p_vaddr + phdr.p_memsz) {
            // Вычисляем смещение в файле
            return phdr.p_offset + (vaddr - phdr.p_vaddr);
        }
    }
    
    return 0;
}

void print_usage(const char *program_name) {
    printf("Usage: %s [OPTIONS] <elf-file>\n\n", program_name);
    printf("A stupid ass patchelf utility for modifying and inspecting ELF files.\n\n");
    printf("OPTIONS:\n");
    printf("  --set-rpath RPATH      Set the RPATH/RUNPATH to the specified value\n");
    printf("  --set-interpreter PATH Set the program interpreter to the specified path\n");
    printf("  --print-rpath          Print the current RPATH/RUNPATH value\n");
    printf("  --print-interpreter    Print the current program interpreter\n");
    printf("  --help                 Display this help message\n\n");
    printf("EXAMPLES:\n");
    printf("  %s --print-rpath /bin/ls\n", program_name);
    printf("  %s --set-rpath '/usr/lib:/lib' myprogram\n", program_name);
    printf("  %s --set-interpreter '/lib/ld-linux.so.2' myprogram\n", program_name);
    printf("  %s --print-interpreter myprogram\n\n", program_name);
    printf("NOTES:\n");
    printf("  - Only one operation can be performed at a time\n");
    printf("  - The ELF file must be writable for set operations\n");
    printf("  - RPATH and RUNPATH are used by the dynamic linker to find shared libraries\n");
    printf("  - The interpreter is the program that loads and runs the ELF executable\n");
}

int InitLibElf() {
    if (elf_version(EV_CURRENT) == EV_NONE) {
        printf("libelf init failed\n");
        return 1;
    }

    return 0;
}

int HasPHeader(Elf *elf, Elf64_Word program_type) {
    size_t phdr_num;
    if (elf_getphdrnum(elf, &phdr_num) != 0) {
        printf("elf_getphdrnum: %s\n", elf_errmsg(-1));
        return 1;
    }

    GElf_Phdr phdr;
    int found_dynamic = 0;

    for (size_t i = 0; i < phdr_num; ++i) {
        if (gelf_getphdr(elf, i, &phdr) != &phdr) {
            continue;
        }

        if (phdr.p_type == program_type) {
            found_dynamic = 1;
            break;
        }
    }

    if (!found_dynamic) {
        printf("no such segment in program headers found\n");
        return 1;
    }

    return 0;
}

int ExtractRunpath(Elf *elf) {
    Elf_Scn *scn = NULL;
    GElf_Shdr shdr;
    Elf_Data *data = NULL;

    while ((scn = elf_nextscn(elf, scn)) != NULL) {
        if (gelf_getshdr(scn, &shdr) != &shdr) {
            continue;
        }
        
        if (shdr.sh_type == SHT_DYNAMIC) {
            data = elf_getdata(scn, NULL);
            if (data != NULL && data->d_size > 0) {
                break;
            }
        }
    }

    if (data == NULL) {
        printf("no dynamic section found\n");
        return 1;
    }

    size_t entries_count = data->d_size / sizeof(GElf_Dyn);
    GElf_Dyn dyn;
    const char *runpath = NULL;

    for (size_t i = 0; i < entries_count; i++) {
        if (gelf_getdyn(data, i, &dyn) != &dyn) {
            continue;
        }
        
        if (dyn.d_tag == DT_RUNPATH) {
            Elf_Scn *dynstr_scn = elf_getscn(elf, shdr.sh_link);
            if (dynstr_scn == NULL) {
                printf("elf_getscn: %s\n", elf_errmsg(-1));
                return 1;
            }
            
            Elf_Data *dynstr_data = elf_getdata(dynstr_scn, NULL);
            if (dynstr_data == NULL) {
                printf("elf_getdata: %s\n", elf_errmsg(-1));
                return 1;
            }
            
            runpath = (const char *)dynstr_data->d_buf + dyn.d_un.d_val;
            printf("%ld\n", dynstr_data->d_size + dyn.d_un.d_val);
            break;
        }
    }

    if (runpath != NULL) {
        printf("RUNPATH: %s\n", runpath);
    } else {
        printf("no RUNPATH found\n");
    }
    
    return 0;
}

int RewriteRunpath(int fd, Elf *elf, const char *new_runpath) {
    size_t runpath_offset;
    char *runpath_addr;
    const char *old_runpath;

    size_t shstrndx;
    size_t dynstr_offset;
    if (elf_getshdrstrndx(elf, &shstrndx) != 0) {
        fprintf(stderr, "Ошибка получения индекса строк секций: %s\n", elf_errmsg(-1));
        return -1;
    }

    GElf_Shdr shdr;
    Elf_Scn *scn = NULL;
    Elf_Data *data = NULL;

    Elf_Data *dyn_data = NULL;
    GElf_Shdr dyn_shdr;
    while ((scn = elf_nextscn(elf, scn)) != NULL) {
        char* name;
        
        if (gelf_getshdr(scn, &shdr) != &shdr) {
            fprintf(stderr, "Ошибка получения заголовка секции: %s\n", elf_errmsg(-1));
            continue;
        }
        
        name = elf_strptr(elf, shstrndx, shdr.sh_name);
        if (name == NULL) {
            fprintf(stderr, "Ошибка получения имени секции: %s\n", elf_errmsg(-1));
            continue;
        }

        if (shdr.sh_type == SHT_DYNAMIC) {
            data = elf_getdata(scn, NULL);
            if (data != NULL && data->d_size > 0) {
                dyn_data = data;
                dyn_shdr = shdr;
            }
        }
        
        if (strcmp(name, ".dynstr") == 0) {
            dynstr_offset = shdr.sh_offset;
        }
    }

    size_t entries_count = dyn_data->d_size / sizeof(GElf_Dyn);
    GElf_Dyn dyn;

    for (size_t i = 0; i < entries_count; i++) {
        if (gelf_getdyn(dyn_data, i, &dyn) != &dyn) {
            continue;
        }
        
        if (dyn.d_tag == DT_RUNPATH) {
            Elf_Scn *dynstr_scn = elf_getscn(elf, dyn_shdr.sh_link);
            if (dynstr_scn == NULL) {
                printf("elf_getscn: %s\n", elf_errmsg(-1));
                return 1;
            }
            
            Elf_Data *dynstr_data = elf_getdata(dynstr_scn, NULL);
            if (dynstr_data == NULL) {
                printf("elf_getdata: %s\n", elf_errmsg(-1));
                return 1;
            }
            
            runpath_addr = (char *)dynstr_data->d_buf + dyn.d_un.d_val;
            old_runpath = (const char *)dynstr_data->d_buf + dyn.d_un.d_val;
            dynstr_offset += dyn.d_un.d_val;
            break;
        }
    }

    if (old_runpath != NULL) {
        size_t new_runpath_len = strlen(new_runpath);
        size_t old_runpath_len = strlen(old_runpath);

        printf("%ld\n", old_runpath_len);
        printf("%s -> %s\n", old_runpath, new_runpath);

        if (new_runpath_len > old_runpath_len) {
            printf("new rpath is too long\n");
            return 1;
        }

        if (lseek(fd, dynstr_offset, SEEK_SET) < 0) {
            printf("lseek failed\n");
            return 1;
        }

        char buffer[256] = {};
        for (size_t i = 0; i < new_runpath_len; ++i) {
            buffer[i] = new_runpath[i];
        }
        // for (size_t i = new_runpath_len; i < old_runpath_len; ++i) {
        //     buffer[i] = '\1';
        // }

        write(fd, buffer, old_runpath_len + 1);
    } else {
        printf("no RUNPATH found\n");
        return 1;
    }

    return 0;
}

int GetPHeader(Elf *elf, GElf_Phdr *phdr, Elf64_Word program_type) {
    size_t phdr_num;

    if (elf_getphdrnum(elf, &phdr_num) != 0) {
        printf("elf_getphdrnum: %s\n", elf_errmsg(-1));
        return 1;
    }

    for (size_t i = 0; i < phdr_num; ++i) {
        if (gelf_getphdr(elf, i, phdr) != phdr) {
            continue;
        }

        if (phdr->p_type == program_type) {
            return 0;
        }
    }

    printf("no such segment in program headers found\n");
    return 1;
}

int GetInterp(int fd, GElf_Phdr phdr) {
    char buffer[256];
    size_t buffer_size = sizeof(buffer);
    if (lseek(fd, phdr.p_offset, SEEK_SET) < 0) {
        perror("Ошибка позиционирования");
        return 1;
    }

    ssize_t bytes_read = read(fd, buffer, phdr.p_memsz);
    if (bytes_read < 0) {
        perror("Ошибка чтения интерпретатора");
        return 1;
    }

    printf("%s\n", buffer);
    return 0;
}

int PrintRunpath(Elf *elf) {
    if (HasPHeader(elf, PT_DYNAMIC) == 1) {
        return 1;
    }

    if (ExtractRunpath(elf) == 1) {
        return 1;
    }

    return 0;
}

int PrintInterp(int fd, Elf *elf) {
    GElf_Phdr phdr;

    if (HasPHeader(elf, PT_INTERP) == 1) {
        return 1;
    }

    if (GetPHeader(elf, &phdr, PT_INTERP) == 1) {
        return 1;
    }

    if (GetInterp(fd, phdr) == 1) {
        return 1;
    }

    return 0;
}

int SetRunpath(int fd, Elf *elf, const char* new_runpath) {
    if (HasPHeader(elf, PT_DYNAMIC) == 1) {
        return 1;
    }

    if (RewriteRunpath(fd, elf, new_runpath) == 1) {
        return 1;
    }

    // if (elf_update(elf, ELF_C_WRITE) < 0) {
    //     printf("elf_update: %s\n", elf_errmsg(-1));
    //     return 1;
    // }

    return 0;
}

int OpenElf(char* const filename, int* fd, int const fd_flag, Elf** elf, Elf_Cmd const elf_mode) {
    if ((*fd = open(filename, fd_flag)) < 0) {
        printf("open failed: %s\n", strerror(errno));
        return 1;
    }

    if ((*elf = elf_begin(*fd, elf_mode, NULL)) == NULL) {
        printf("elf_begin failed: %s\n", elf_errmsg(-1));
        close(*fd);
        return 1;
    }
    
    return 0;
}

void CloseElf(int const fd, Elf* const elf) {
    close(fd);
    elf_end(elf);
}

int main(int argc, char **argv) {
    if (argc < 3 || argc > 4) {
        print_usage(argv[0]);
        return 1;
    }

    struct ProgramMode mode = GetMode(argc, argv);
    if (mode.status == 1) return 1;
    // print_program_mode(&mode);

    if (InitLibElf() == 1) return 1;

    int in_fd;
    Elf *in_elf; 
    if (OpenElf(mode.filename, &in_fd, O_RDWR, &in_elf, ELF_C_RDWR) == 1) return 1;

    if (mode.print_runpath == 1) {
        if (PrintRunpath(in_elf) == 1) {
            CloseElf(in_fd, in_elf);
            return 1;
        }
    }

    if (mode.print_interp == 1) {
        if (PrintInterp(in_fd, in_elf) == 1) {
            CloseElf(in_fd, in_elf);
            return 1;
        }
    }

    if (mode.set_runpath == 1) {
        if (SetRunpath(in_fd, in_elf, mode.runpath) == 1) {
            CloseElf(in_fd, in_elf);
            return 1;
        }
    }

    if (mode.set_interp == 1) {
        
    }

    CloseElf(in_fd, in_elf);
    return 0;
}