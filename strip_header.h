#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <libelf.h>
#include <gelf.h>
#include <string.h>
#include <errno.h>

int InitLibElf();

int OpenElf(char* const, int*, int const, Elf**, Elf_Cmd const);
void CloseElf(int const, Elf* const);
int CreatElf(char* const, int*, int const, int const, Elf**, Elf_Cmd const);

int ExtractArguments(int const, char** const, char**, int*);

int CopyElfHeader(Elf* const, Elf* const, GElf_Ehdr*, GElf_Ehdr**);
int CopyElfSectionData(Elf_Scn*, Elf_Data*, Elf_Scn*, Elf_Data*);
int CopyElfSections(Elf*, Elf*, GElf_Ehdr*, size_t, int const);
int CopyProgramHeaders(Elf*, Elf*, GElf_Ehdr);