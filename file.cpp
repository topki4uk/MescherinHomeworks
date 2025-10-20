#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <libelf.h>
#include <gelf.h>
#include <string.h>
#include <errno.h>

int InitLibElf() {
    if (elf_version(EV_CURRENT) == EV_NONE) {
        printf("libelf init failed\n");
        return 1;
    }

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

int CreatElf(char* const filename, int* fd, int const fd_flag, 
                int const creat_flag, Elf** elf, Elf_Cmd const elf_mode) {
    if ((*fd = open(filename, fd_flag, creat_flag)) < 0) {
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

int ExtractArguments(int const argc, char** const argv, char** filename, int* debug_flag) {
    if (argc < 2) {
        printf("Usage: %s [-d] <elf-file>\n", argv[0]);
        return 1;
    }

    if (argc == 2) {
        *filename = argv[1];
    }
    
    if (argc == 3) {
        if (strcmp("-d", argv[1]) != 0) {
            printf("Usage: %s [-d] <elf-file>\n", argv[0]);
            return 1;
        }

        *debug_flag = strcmp("-d", argv[1]) == 0;
        *filename = argv[2];
    }

    return 0;
}

int CopyElfHeader(Elf* const in_elf, Elf* const out_elf, GElf_Ehdr *in_ehdr, GElf_Ehdr **out_ehdr) {
    if (gelf_getehdr(in_elf, in_ehdr) == NULL) {
        printf("gelf_getehdr failed: %s\n", elf_errmsg(-1));
        return 1;
    }

    *out_ehdr = (GElf_Ehdr *) gelf_newehdr(out_elf, ELFCLASS64);
    if (!out_ehdr) {
        printf("gelf_newehdr failed: %s\n", elf_errmsg(-1));
        return 1;
    }

    **out_ehdr = *in_ehdr;
    return 0;
}

int CopyElfSectionData(Elf_Scn *in_scn, Elf_Data *in_data, Elf_Scn *out_scn, Elf_Data *out_data) {
    while ((in_data = elf_getdata(in_scn, in_data)) != NULL) {
        if ((out_data = elf_newdata(out_scn)) == NULL) {
            printf("elf_newdata failed: %s\n", elf_errmsg(-1));
            return 1;
        }

        *out_data = *in_data;
    }

    return 0;
}

int CopyElfSections(Elf* in_elf, Elf* out_elf, GElf_Ehdr *out_ehdr, size_t shstrndx, int const debug_mode) {
    Elf_Scn *out_scn = NULL, *in_scn = NULL;
    Elf_Data *out_data = NULL, *in_data = NULL;
    GElf_Shdr in_shdr, out_shdr;

    while ((in_scn = elf_nextscn(in_elf, in_scn)) != NULL) {
        if (gelf_getshdr(in_scn, &in_shdr) == NULL) {
            printf("elf64_getshdr failed: %s\n", elf_errmsg(-1));
            return 1;
        }

        const char *name = elf_strptr(in_elf, shstrndx, in_shdr.sh_name);

        if (strstr(name, "debug") != NULL) {
            out_ehdr->e_shstrndx--;
            continue;
        }

        if (debug_mode == 0 && strcmp(name, ".symtab") == 0) {    
            out_ehdr->e_shstrndx--;
            continue;
        }

        if ((out_scn = elf_newscn(out_elf)) == NULL) {
            printf("elf_newscn failed: %s\n", elf_errmsg(-1));
            return 1;
        }

        if (gelf_getshdr(out_scn, &out_shdr) == NULL) {
            printf("elf64_getshdr failed: %s\n", elf_errmsg(-1));
            return 1;
        }

        out_shdr = in_shdr;
        gelf_update_shdr(out_scn, &out_shdr);

        if (CopyElfSectionData(in_scn, in_data, out_scn, out_data) == 1) {
            return 1;
        }
    }

    return 0;
}

int CopyProgramHeaders(Elf *in_elf, Elf *out_elf, GElf_Ehdr in_ehdr) {
    Elf64_Phdr *in_phdr, *out_phdr;
    if ((in_phdr = elf64_getphdr(in_elf)) == NULL) {
        printf("elf64_getphdr failed: %s\n", elf_errmsg(-1));
        return 1;
    }

    if ((out_phdr = elf64_newphdr(out_elf, in_ehdr.e_phnum)) == NULL) {
        printf("elf64_newphdr failed: %s\n", elf_errmsg(-1));
        return 1;
    }

    for (int i = 0; i < in_ehdr.e_phnum; i++) {
        out_phdr[i] = in_phdr[i];
    }

    return 0;
}

int main(int argc, char **argv) {
    char* filename;
    int debug_mode = 0;
    if (ExtractArguments(argc, argv, &filename, &debug_mode) == 1) {
        return 1;
    }

    if (debug_mode == 1) {
        printf("Strip only debug sections\n");
    } else {
        printf("Strip debug and symtab sections\n");
    }

    if (InitLibElf() == 1) {
        return 1;
    }

    int in_fd;
    Elf *in_elf; 
    if (OpenElf(filename, &in_fd, O_RDONLY, &in_elf, ELF_C_READ) == 1) {
        return 1;
    }

    if (elf_kind(in_elf) != ELF_K_ELF) {
        printf("%s is not an ELF file\n", argv[1]);
        CloseElf(in_fd, in_elf);
        return 1;
    }

    int out_fd;
    Elf *out_elf;
    char stripped_filename[100] = "stripped_";
    strcat(stripped_filename, filename);
    if (CreatElf(stripped_filename, &out_fd, O_WRONLY | O_CREAT, 0755, &out_elf, ELF_C_WRITE) == 1) {
        return 1;
    }

    GElf_Ehdr in_ehdr, *out_ehdr;
    if (CopyElfHeader(in_elf, out_elf, &in_ehdr, &out_ehdr) == 1) {
        CloseElf(in_fd, in_elf);
        CloseElf(out_fd, out_elf);
        return 1;
    }

    size_t shstrndx;
    if (elf_getshdrstrndx(in_elf, &shstrndx) != 0) {
        printf("elf_getshdrstrndx: %s\n", elf_errmsg(-1));
        CloseElf(in_fd, in_elf);
        CloseElf(out_fd, out_elf);
        return 1;
    }

    if (CopyElfSections(in_elf, out_elf, out_ehdr, shstrndx, debug_mode) == 1) {
        CloseElf(in_fd, in_elf);
        CloseElf(out_fd, out_elf);
        return 1;
    }

    if (CopyProgramHeaders(in_elf, out_elf, in_ehdr) == 1) {
        CloseElf(in_fd, in_elf);
        CloseElf(out_fd, out_elf);
        return 1;
    }

    elf_flagelf(out_elf, ELF_C_SET, ELF_F_LAYOUT);
    if (elf_update(out_elf, ELF_C_WRITE) < 0) {
        printf("elf_update failed: %s\n", elf_errmsg(-1));
        CloseElf(in_fd, in_elf);
        CloseElf(out_fd, out_elf);
        return 1;
    }

    printf("Successfully created stripped ELF file: %s\n", stripped_filename);
    CloseElf(in_fd, in_elf);
    CloseElf(out_fd, out_elf);
    return 0;
}