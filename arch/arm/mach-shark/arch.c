/*
 *  linux/arch/arm/mach-shark/arch.c
 *
 *  Architecture specific fixups.  This is where any
 *  parameters in the params struct are fixed up, or
 *  any additional architecture specific information
 *  is pulled from the params struct.
 */
#include <linux/tty.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/init.h>

#include <asm/hardware/dec21285.h>
#include <asm/elf.h>
#include <asm/setup.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>

static void __init
fixup_shark(struct machine_desc *desc, struct param_struct *params,
	char **cmdline, struct meminfo *mi) {
	int i;

	mi->nr_banks=0;
	for (i=0;i<NR_BANKS;i++) {
		if (params->u1.s.pages_in_bank[i] != 0) {
			mi->nr_banks++;
			mi->bank[i].node  = 0;
			mi->bank[i].start = params->u1.s.pages_in_bank[i] & 0xffff0000;
			mi->bank[i].size  = (params->u1.s.pages_in_bank[i] & 0xffff)*PAGE_SIZE;
			params->u1.s.pages_in_bank[i] &= 0xffff;
		}
	}
}

extern void shark_map_io(void);
extern void genarch_init_irq(void);

MACHINE_START(SHARK, "Shark")
	MAINTAINER("Alexander Schulz")
	BOOT_MEM(0x08000000, 0x40000000, 0xe0000000)
	BOOT_PARAMS(0x08003000)
	FIXUP(fixup_shark)
	MAPIO(shark_map_io)
	INITIRQ(genarch_init_irq)
MACHINE_END
