/*
 * linux/arch/arm/mach-sa1100/arch.c
 *
 * Architecture specific fixups.  This is where any
 * parameters in the params struct are fixed up, or
 * any additional architecture specific information
 * is pulled from the params struct.
 */
#include <linux/config.h>
#include <linux/tty.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/init.h>

#include <asm/elf.h>
#include <asm/hardware.h>
#include <asm/setup.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>

extern void setup_initrd(unsigned int start, unsigned int size);
extern void setup_ramdisk(int doload, int prompt, int start, unsigned int rd_sz);

static void victor_power_off(void)
{
	/* switch off power supply */
	mdelay(2000);
	GPCR = GPIO_GPIO23;
	while (1);
}


static void xp860_power_off(void)
{
	GPDR |= GPIO_GPIO20;
	GPSR = GPIO_GPIO20;
	mdelay(1000);
	GPCR = GPIO_GPIO20;
	while(1);
}


extern void __init sa1100_map_io(void);

#define SET_BANK(__nr,__start,__size) \
	mi->bank[__nr].start = (__start), \
	mi->bank[__nr].size = (__size), \
	mi->bank[__nr].node = (((unsigned)(__start) - PHYS_OFFSET) >> 27)
static void __init
fixup_sa1100(struct machine_desc *desc, struct param_struct *params,
	     char **cmdline, struct meminfo *mi)
{
	if (machine_is_assabet()) {
		/* 
		 * On Assabet, we must probe for the Neponset board *before*
		 * paging_init() has occured to actually determine the amount
		 * of RAM available.
		 */
		extern void map_sa1100_gpio_regs(void);
		extern void get_assabet_scr(void);
		map_sa1100_gpio_regs();
		get_assabet_scr();

		SET_BANK( 0, 0xc0000000, 32*1024*1024 );
		mi->nr_banks = 1;

		if (machine_has_neponset()) {
			printk("Neponset expansion board detected\n");
			/* 
			 * Note that Neponset RAM is slower...
			 * and still untested. 
			 * This would be a candidate for
			 * _real_ NUMA support. 
			 */
			//SET_BANK( 1, 0xd0000000, 32*1024*1024 );
			//mi->nr_banks = 2;
		}

		ROOT_DEV = MKDEV(RAMDISK_MAJOR,0);
		setup_ramdisk( 1, 0, 0, 8192 );
		setup_initrd( 0xc0800000, 3*1024*1024 );
	}

	else if (machine_is_brutus()) {
		SET_BANK( 0, 0xc0000000, 4*1024*1024 );
		SET_BANK( 1, 0xc8000000, 4*1024*1024 );
		SET_BANK( 2, 0xd0000000, 4*1024*1024 );
		SET_BANK( 3, 0xd8000000, 4*1024*1024 );
		mi->nr_banks = 4;

		ROOT_DEV = MKDEV(RAMDISK_MAJOR,0);
		setup_ramdisk( 1, 0, 0, 8192 );
		setup_initrd( __phys_to_virt(0xd8000000), 3*1024*1024 );
	}

        else if (machine_is_cerf()) {
                // 16Meg Ram.
                SET_BANK( 0, 0xc0000000, 8*1024*1024 );
                SET_BANK( 1, 0xc8000000, 8*1024*1024 );			// comment this out for 8MB Cerfs
                mi->nr_banks = 2;

                ROOT_DEV = MKDEV(RAMDISK_MAJOR,0);
                setup_ramdisk(1,  0, 0, 8192);
                // Save 2Meg for RAMDisk
                setup_initrd(0xc0500000, 3*1024*1024);
        }

	else if (machine_is_empeg()) {
		SET_BANK( 0, 0xc0000000, 4*1024*1024 );
		SET_BANK( 1, 0xc8000000, 4*1024*1024 );
		mi->nr_banks = 2;

		ROOT_DEV = MKDEV( 3, 1 );  /* /dev/hda1 */
		setup_ramdisk( 1, 0, 0, 4096 );
		setup_initrd( 0xd0000000+((1024-320)*1024), (320*1024) );
	}

	else if (machine_is_lart()) {
		/*
		 * Note that LART is a special case - it doesn't use physical
		 * address line A23 on the DRAM, so we effectively have 4 * 8MB
		 * in two SA1100 banks.
		 */
		SET_BANK( 0, 0xc0000000, 8*1024*1024 );
		SET_BANK( 1, 0xc1000000, 8*1024*1024 );
		SET_BANK( 2, 0xc8000000, 8*1024*1024 );
		SET_BANK( 3, 0xc9000000, 8*1024*1024 );
		mi->nr_banks = 4;

		ROOT_DEV = MKDEV(RAMDISK_MAJOR,0);
		setup_ramdisk(1, 0, 0, 8192);
		setup_initrd(0xc0400000, 4*1024*1024);
	}

	else if (machine_is_thinclient() || machine_is_graphicsclient()) {
		SET_BANK( 0, 0xc0000000, 16*1024*1024 );
		mi->nr_banks = 1;

		ROOT_DEV = MKDEV(RAMDISK_MAJOR,0);
		setup_ramdisk( 1, 0, 0, 8192 );
		setup_initrd( __phys_to_virt(0xc0800000), 4*1024*1024 );
	}

	else if (machine_is_nanoengine()) {
		SET_BANK( 0, 0xc0000000, 32*1024*1024 );
		mi->nr_banks = 1;

		ROOT_DEV = MKDEV(RAMDISK_MAJOR,0);
		setup_ramdisk( 1, 0, 0, 8192 );
		setup_initrd( __phys_to_virt(0xc0800000), 4*1024*1024 );

		/* Get command line parameters passed from the loader (if any) */
		if( *((char*)0xc0000100) )
			*cmdline = ((char *)0xc0000100);
	}
	else if (machine_is_tifon()) {
		SET_BANK( 0, 0xc0000000, 16*1024*1024 );
		SET_BANK( 1, 0xc8000000, 16*1024*1024 );
		mi->nr_banks = 2;

		ROOT_DEV = MKDEV(UNNAMED_MAJOR, 0);
		setup_ramdisk(1, 0, 0, 4096);
		setup_initrd( 0xd0000000 + 0x1100004, 0x140000 );
	}

	else if (machine_is_victor()) {
		SET_BANK( 0, 0xc0000000, 4*1024*1024 );
		mi->nr_banks = 1;

		ROOT_DEV = MKDEV( 60, 2 );

		/* Get command line parameters passed from the loader (if any) */
		if( *((char*)0xc0000000) )
			strcpy( *cmdline, ((char *)0xc0000000) );

		/* power off if any problem */
		strcat( *cmdline, " panic=1" );

		pm_power_off = victor_power_off;
	}

	else if (machine_is_xp860()) {
		SET_BANK( 0, 0xc0000000, 32*1024*1024 );
		mi->nr_banks = 1;

		pm_power_off = xp860_power_off;
	}
}

#ifdef CONFIG_SA1100_ASSABET
MACHINE_START(ASSABET, "Intel-Assabet")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_sa1100)
	MAPIO(sa1100_map_io)
MACHINE_END
#endif
#ifdef CONFIG_SA1100_BITSY
MACHINE_START(BITSY, "Compaq Bitsy")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	BOOT_PARAMS(0xc0000100)
	FIXUP(fixup_sa1100)
	MAPIO(sa1100_map_io)
MACHINE_END
#endif
#ifdef CONFIG_SA1100_BRUTUS
MACHINE_START(BRUTUS, "Intel Brutus (SA1100 eval board)")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_sa1100)
	MAPIO(sa1100_map_io)
MACHINE_END
#endif
#ifdef CONFIG_SA1100_CERF
MACHINE_START(CERF, "Intrinsyc CerfBoard")
	MAINTAINER("Pieter Truter")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_sa1100)
	MAPIO(sa1100_map_io)
MACHINE_END
#endif
#ifdef CONFIG_SA1100_EMPEG
MACHINE_START(EMPEG, "empeg MP3 Car Audio Player")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_sa1100)
	MAPIO(sa1100_map_io)
MACHINE_END
#endif
#ifdef CONFIG_SA1100_GRAPHICSCLIENT
MACHINE_START(GRAPHICSCLIENT, "ADS GraphicsClient")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_sa1100)
	MAPIO(sa1100_map_io)
MACHINE_END
#endif
#ifdef CONFIG_SA1100_ITSY
MACHINE_START(ITSY, "Compaq Itsy")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	BOOT_PARAMS(0xc0000100
	FIXUP(fixup_sa1100)
	MAPIO(sa1100_map_io)
MACHINE_END
#endif
#ifdef CONFIG_SA1100_LART
MACHINE_START(LART, "LART")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_sa1100)
	MAPIO(sa1100_map_io)
MACHINE_END
#endif
#ifdef CONFIG_SA1100_NANOENGINE
MACHINE_START(NANOENGINE, "BSE nanoEngine")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_sa1100)
	MAPIO(sa1100_map_io)
MACHINE_END
#endif
#ifdef CONFIG_SA1100_PLEB
MACHINE_START(PLEB, "PLEB")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_sa1100)
	MAPIO(sa1100_map_io)
MACHINE_END
#endif
#ifdef CONFIG_SA1100_THINCLIENT
MACHINE_START(THINCLIENT, "ADS ThinClient")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_sa1100)
	MAPIO(sa1100_map_io)
MACHINE_END
#endif
#ifdef CONFIG_SA1100_TIFON
MACHINE_START(TIFON, "Tifon")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_sa1100)
	MAPIO(sa1100_map_io)
MACHINE_END
#endif
#ifdef CONFIG_SA1100_VICTOR
MACHINE_START(VICTOR, "VisuAide Victor")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_sa1100)
	MAPIO(sa1100_map_io)
MACHINE_END
#endif
#ifdef CONFIG_SA1100_XP860
MACHINE_START(XP860, "XP860")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_sa1100)
	MAPIO(sa1100_map_io)
MACHINE_END
#endif
