/* 
 *  linux/arch/arm/kernel/arch.c
 *
 *  Architecture specific fixups.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/types.h>

#include <asm/elf.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/arch.h>
#include <asm/page.h>

unsigned int vram_size;


unsigned int memc_ctrl_reg;
unsigned int number_mfm_drives;

static int __init parse_tag_acorn(const struct tag *tag)
{
	memc_ctrl_reg = tag->u.acorn.memc_control_reg;
	number_mfm_drives = tag->u.acorn.adfsdrives;
	return 0;
}

__tagtable(ATAG_ACORN, parse_tag_acorn);

