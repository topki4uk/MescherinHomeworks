/*
 * simple_elf_dump.c
 * Минимальный пример работы с ELF через libelf.
 *
 * Сборка:
 *   gcc -Wall -o simple_elf_dump simple_elf_dump.c -lelf
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <libelf.h>
#include <gelf.h>
#include <string.h>


int main(int argc, char **argv) {
    int in_fd, out_fd;
    Elf *in_elf, *out_elf; 

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <elf-file>\n", argv[0]);
        return 1;
    }

    if (elf_version(EV_CURRENT) == EV_NONE) {
        fprintf(stderr, "libelf init failed\n");
        return 1;
    }

    if ((in_fd = open(argv[1], O_RDONLY)) < 0) {
        perror("open");
        return 1;
    }

    if ((in_elf = elf_begin(in_fd, ELF_C_READ, NULL)) == NULL) {
        fprintf(stderr, "elf_begin failed: %s\n", elf_errmsg(-1));
        close(in_fd);
        return 1;
    }

    if (elf_kind(in_elf) != ELF_K_ELF) {
        fprintf(stderr, "%s is not an ELF file\n", argv[1]);
        elf_end(in_elf);
        close(in_fd);
        return 1;
    }

    if ((out_fd = open("out", O_WRONLY | O_CREAT, 0644)) < 0) {
        perror("open");
        return 1;
    }
    
    if ((out_elf = elf_begin(out_fd, ELF_C_WRITE, NULL)) == NULL) {
        fprintf(stderr, "elf_begin failed: %s\n", elf_errmsg(-1));
        close(out_fd);
        return 1;
    }

    // Заголовок ELF
    GElf_Ehdr ehdr;
    if (gelf_getehdr(in_elf, &ehdr) == NULL) {
        fprintf(stderr, "gelf_getehdr failed: %s\n", elf_errmsg(-1));
        elf_end(in_elf);
        close(in_fd);
        return 1;
    }

    // Create new ELF header
    GElf_Ehdr *out_ehdr = gelf_newehdr(out_elf, ELFCLASS64);
    if (!out_ehdr) {
        fprintf(stderr, "elf64_newehdr failed: %s\n", elf_errmsg(-1));
        elf_end(out_elf);
        close(out_fd);
        return 1;
    }

    out_ehdr->e_ident[EI_MAG0] = ehdr.e_ident[EI_MAG0];
    out_ehdr->e_ident[EI_MAG1] = ehdr.e_ident[EI_MAG1];
    out_ehdr->e_ident[EI_MAG2] = ehdr.e_ident[EI_MAG2];
    out_ehdr->e_ident[EI_MAG3] = ehdr.e_ident[EI_MAG3];
    out_ehdr->e_ident[EI_CLASS] = ehdr.e_ident[EI_CLASS];
    out_ehdr->e_ident[EI_DATA]  = ehdr.e_ident[EI_DATA];
    out_ehdr->e_ident[EI_VERSION] = ehdr.e_ident[EI_VERSION];

    out_ehdr->e_type    = ehdr.e_type;
    out_ehdr->e_machine = ehdr.e_machine;
    out_ehdr->e_version = ehdr.e_version;

    size_t shstrndx;
    Elf_Scn *out_scn = NULL, *in_scn = NULL;
    Elf_Data *out_data = NULL, *in_data = NULL;
    GElf_Shdr in_shdr, out_shdr;

    while ((in_scn = elf_nextscn(in_elf, in_scn)) != NULL) {
        if ((out_scn = elf_newscn(out_elf)) == NULL) {
            fprintf(stderr, "elf_newscn failed: %s\n", elf_errmsg(-1));
            elf_end(out_elf);
            close(out_fd);
            return 1;
        }

        if (gelf_getshdr(in_scn, &in_shdr) == NULL) {
            fprintf(stderr, "elf64_getshdr failed: %s\n", elf_errmsg(-1));
            elf_end(in_elf);
            close(in_fd);
            return 1;
        }

        if (gelf_getshdr(out_scn, &out_shdr) == NULL) {
            fprintf(stderr, "elf64_getshdr failed: %s\n", elf_errmsg(-1));
            elf_end(out_elf);
            close(out_fd);
            return 1;
        }

        out_shdr.sh_name = in_shdr.sh_name;
        out_shdr.sh_type = in_shdr.sh_type;
        out_shdr.sh_flags = in_shdr.sh_flags;
        out_shdr.sh_entsize = in_shdr.sh_entsize;
        out_shdr.sh_addralign = in_shdr.sh_addralign;
        out_shdr.sh_info = in_shdr.sh_info;
        out_shdr.sh_size = in_shdr.sh_size;
        gelf_update_shdr(out_scn, &out_shdr);

        while ((in_data = elf_getdata(in_scn, in_data)) != NULL) {
            if ((out_data = elf_newdata(out_scn)) == NULL) {
                fprintf(stderr, "elf_newdata failed: %s\n", elf_errmsg(-1));
                elf_end(out_elf);
                close(out_fd);
                return 1;
            }

            out_data->d_align = out_shdr.sh_addralign;
            out_data->d_buf = in_data->d_buf;
            out_data->d_size = in_data->d_size;
            out_data->d_off = in_data->d_off;
        }
    }

    Elf64_Phdr *in_phdr, *out_phdr;
    if ((in_phdr = elf64_getphdr(in_elf)) == NULL) {
        fprintf(stderr, "elf64_getphdr failed: %s\n", elf_errmsg(-1));
        elf_end(in_elf);
        close(in_fd);
        return 1;
    }

    if ((out_phdr = elf64_newphdr(out_elf, ehdr.e_phnum)) == NULL) {
        fprintf(stderr, "elf64_newphdr failed: %s\n", elf_errmsg(-1));
        elf_end(out_elf);
        close(out_fd);
        return 1;
    }

    for (int i = 0; i < ehdr.e_phnum; i++) {
        out_phdr[i].p_type = in_phdr[i].p_type;
        out_phdr[i].p_offset = in_phdr[i].p_offset;
        out_phdr[i].p_vaddr = in_phdr[i].p_vaddr;
        out_phdr[i].p_filesz = in_phdr[i].p_filesz;
        out_phdr[i].p_memsz = in_phdr[i].p_memsz;
    }

    // elf_flagelf(out_elf, ELF_C_SET, ELF_F_LAYOUT);

    if (elf_update(out_elf, ELF_C_WRITE) < 0) {
        fprintf(stderr, "elf_update failed: %s\n", elf_errmsg(-1));
    }

    elf_end(out_elf);
    elf_end(in_elf);
    close(in_fd);
    close(out_fd);
    return 0;
}
