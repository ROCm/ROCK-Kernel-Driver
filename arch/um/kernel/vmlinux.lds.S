#include <asm-generic/vmlinux.lds.h>
	
OUTPUT_FORMAT(ELF_FORMAT)
OUTPUT_ARCH(ELF_ARCH)
ENTRY(_start)
jiffies = jiffies_64;

SECTIONS
{
#include "asm/common.lds.S"
}
