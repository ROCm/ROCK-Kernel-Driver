/*
 *  linux/arch/arm/kernel/arch.c
 *
 *  Architecture specific fixups.  This is where any
 *  parameters in the params struct are fixed up, or
 *  any additional architecture specific information
 *  is pulled from the params struct.
 */
#include <linux/config.h>
#include <linux/tty.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/init.h>

#include <asm/elf.h>
#include <asm/setup.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>
#include <asm/hardware/dec21285.h>

unsigned int vram_size;

extern void setup_initrd(unsigned int start, unsigned int size);
extern void setup_ramdisk(int doload, int prompt, int start, unsigned int rd_sz);

#ifdef CONFIG_ARCH_ACORN

unsigned int memc_ctrl_reg;
unsigned int number_mfm_drives;

static void __init
fixup_acorn(struct machine_desc *desc, struct param_struct *params,
	    char **cmdline, struct meminfo *mi)
{
	if (machine_is_riscpc()) {
		int i;

		/*
		 * RiscPC can't handle half-word loads and stores
		 */
		elf_hwcap &= ~HWCAP_HALF;

		switch (params->u1.s.pages_in_vram) {
		case 512:
			vram_size += PAGE_SIZE * 256;
		case 256:
			vram_size += PAGE_SIZE * 256;
		default:
			break;
		}

		if (vram_size) {
			desc->video_start = 0x02000000;
			desc->video_end   = 0x02000000 + vram_size;
		}

		for (i = 0; i < 4; i++) {
			mi->bank[i].start = PHYS_OFFSET + (i << 26);
			mi->bank[i].node  = 0;
			mi->bank[i].size  =
				params->u1.s.pages_in_bank[i] *
				params->u1.s.page_size;
		}
		mi->nr_banks = 4;
	}
	memc_ctrl_reg	  = params->u1.s.memc_control_reg;
	number_mfm_drives = (params->u1.s.adfsdrives >> 3) & 3;
}

#ifdef CONFIG_ARCH_RPC
extern void __init rpc_map_io(void);

MACHINE_START(RISCPC, "Acorn-RiscPC")
	MAINTAINER("Russell King")
	BOOT_MEM(0x10000000, 0x03000000, 0xe0000000)
	BOOT_PARAMS(0x10000100)
	DISABLE_PARPORT(0)
	DISABLE_PARPORT(1)
	FIXUP(fixup_acorn)
	MAPIO(rpc_map_io)
MACHINE_END
#endif
#ifdef CONFIG_ARCH_ARC
MACHINE_START(ARCHIMEDES, "Acorn-Archimedes")
	MAINTAINER("Dave Gilbert")
	BOOT_PARAMS(0x0207c000)
	FIXUP(fixup_acorn)
MACHINE_END
#endif
#ifdef CONFIG_ARCH_A5K
MACHINE_START(A5K, "Acorn-A5000")
	MAINTAINER("Russell King")
	BOOT_PARAMS(0x0207c000)
	FIXUP(fixup_acorn)
MACHINE_END
#endif
#endif

#ifdef CONFIG_ARCH_L7200

static void __init
fixup_l7200(struct machine_desc *desc, struct param_struct *params,
             char **cmdline, struct meminfo *mi)
{
        mi->nr_banks      = 1;
        mi->bank[0].start = PHYS_OFFSET;
        mi->bank[0].size  = (32*1024*1024);
        mi->bank[0].node  = 0;

        ROOT_DEV = MKDEV(RAMDISK_MAJOR,0);
        setup_ramdisk( 1, 0, 0, 8192 );
        setup_initrd( __phys_to_virt(0xf1000000), 0x00162b0d);
}

extern void __init l7200_map_io(void);

MACHINE_START(L7200, "LinkUp Systems L7200SDB")
	MAINTAINER("Steve Hill")
	BOOT_MEM(0xf0000000, 0x80040000, 0xd0000000)
	FIXUP(fixup_l7200)
	MAPIO(l7200_map_io)
MACHINE_END
#endif

#ifdef CONFIG_ARCH_EBSA110

extern void __init ebsa110_map_io(void);

MACHINE_START(EBSA110, "EBSA110")
	MAINTAINER("Russell King")
	BOOT_MEM(0x00000000, 0xe0000000, 0xe0000000)
	BOOT_PARAMS(0x00000400)
	DISABLE_PARPORT(0)
	DISABLE_PARPORT(2)
	SOFT_REBOOT
	MAPIO(ebsa110_map_io)
MACHINE_END
#endif
#ifdef CONFIG_ARCH_NEXUSPCI

extern void __init nexuspci_map_io(void);

MACHINE_START(NEXUSPCI, "FTV/PCI")
	MAINTAINER("Philip Blundell")
	BOOT_MEM(0x40000000, 0x10000000, 0xe0000000)
	MAPIO(nexuspci_map_io)
MACHINE_END
#endif
#ifdef CONFIG_ARCH_TBOX

extern void __init tbox_map_io(void);

MACHINE_START(TBOX, "unknown-TBOX")
	MAINTAINER("Philip Blundell")
	BOOT_MEM(0x80000000, 0x00400000, 0xe0000000)
	MAPIO(tbox_map_io)
MACHINE_END
#endif
#ifdef CONFIG_ARCH_CLPS7110
MACHINE_START(CLPS7110, "CL-PS7110")
	MAINTAINER("Werner Almesberger")
MACHINE_END
#endif
#ifdef CONFIG_ARCH_ETOILE
MACHINE_START(ETOILE, "Etoile")
	MAINTAINER("Alex de Vries")
MACHINE_END
#endif
#ifdef CONFIG_ARCH_LACIE_NAS
MACHINE_START(LACIE_NAS, "LaCie_NAS")
	MAINTAINER("Benjamin Herrenschmidt")
MACHINE_END
#endif
#ifdef CONFIG_ARCH_CLPS7500

extern void __init clps7500_map_io(void);

MACHINE_START(CLPS7500, "CL-PS7500")
	MAINTAINER("Philip Blundell")
	BOOT_MEM(0x10000000, 0x03000000, 0xe0000000)
	MAPIO(clps7500_map_io)
MACHINE_END
#endif
