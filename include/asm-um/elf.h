#ifndef __UM_ELF_H
#define __UM_ELF_H

#include "asm/archparam.h"

#define ELF_HWCAP (0)

#define SET_PERSONALITY(ex, ibcs2) do ; while(0)

#define ELF_EXEC_PAGESIZE 4096

#define elf_check_arch(x) (1)

#define ELF_CLASS ELFCLASS32

#define USE_ELF_CORE_DUMP

#endif
