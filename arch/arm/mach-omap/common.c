/*
 * linux/arch/arm/mach-omap/common.c
 *
 * Code common to all OMAP machines.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/console.h>

#include <asm/hardware.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/mach/map.h>
#include <asm/arch/clocks.h>
#include <asm/arch/board.h>
#include <asm/io.h>

/*
 * ----------------------------------------------------------------------------
 * OMAP revision check
 *
 * Since we use the cpu_is_omapnnnn() macros, there's a chance that a board
 * switches to an updated core. We want to print out the OMAP revision early.
 *
 * We use the system_serial registers for the revision information so we
 * can see it in /proc/cpuinfo.
 *
 * If the OMAP detection gets more complicated, we may want to expand this
 * to store the OMAP version and replace the current cpu_is_omapnnnn() macros.
 *
 * ----------------------------------------------------------------------------
 */
static void __init omap_check_revision(void)
{
	system_serial_high = omap_readl(OMAP_ID_BASE);
	system_serial_low = OMAP_ID_REG;
	system_rev = (OMAP_ID_REG >> ID_SHIFT) & ID_MASK;

	printk("OMAP revision: %d.%d (0x%08x) id: 0x%08x detected as OMAP-",
	       (system_serial_high >> 20) & 0xf,
	       (system_serial_high >> 16) & 0xf,
	       system_serial_high, system_serial_low);

	switch (system_rev) {
	case OMAP_ID_730:
		printk("730\n");
		system_rev = 0x730;
		break;
	case OMAP_ID_1510:
		printk("1510\n");
		system_rev = 0x1510;
		break;
	case OMAP_ID_1610:
		printk("1610\n");
		system_rev = 0x1610;
		break;
	case OMAP_ID_1710:
		printk("1710\n");
		system_rev = 0x1710;
		break;
	case OMAP_ID_5912:
		printk("5912/1611B\n");
		system_rev = 0x5912;
		break;
	default:
		printk("unknown, please add support!\n");
	}
}

/*
 * ----------------------------------------------------------------------------
 * OMAP I/O mapping
 *
 * The machine specific code may provide the extra mapping besides the
 * default mapping provided here.
 * ----------------------------------------------------------------------------
 */

static struct map_desc omap_io_desc[] __initdata = {
 { IO_VIRT,      	IO_PHYS,             IO_SIZE,        	   MT_DEVICE },
};

#ifdef CONFIG_ARCH_OMAP730
static struct map_desc omap730_io_desc[] __initdata = {
 { OMAP730_DSP_BASE,    OMAP730_DSP_START,    OMAP730_DSP_SIZE,    MT_DEVICE },
 { OMAP730_DSPREG_BASE, OMAP730_DSPREG_START, OMAP730_DSPREG_SIZE, MT_DEVICE },
 { OMAP730_SRAM_BASE,   OMAP730_SRAM_START,   OMAP730_SRAM_SIZE,   MT_DEVICE }
};
#endif

#ifdef CONFIG_ARCH_OMAP1510
static struct map_desc omap1510_io_desc[] __initdata = {
 { OMAP1510_DSP_BASE,    OMAP1510_DSP_START,    OMAP1510_DSP_SIZE,    MT_DEVICE },
 { OMAP1510_DSPREG_BASE, OMAP1510_DSPREG_START, OMAP1510_DSPREG_SIZE, MT_DEVICE },
 { OMAP1510_SRAM_BASE,   OMAP1510_SRAM_START,   OMAP1510_SRAM_SIZE,   MT_DEVICE }
};
#endif

#ifdef CONFIG_ARCH_OMAP1610
static struct map_desc omap1610_io_desc[] __initdata = {
 { OMAP1610_DSP_BASE,    OMAP1610_DSP_START,    OMAP1610_DSP_SIZE,    MT_DEVICE },
 { OMAP1610_DSPREG_BASE, OMAP1610_DSPREG_START, OMAP1610_DSPREG_SIZE, MT_DEVICE },
 { OMAP1610_SRAM_BASE,   OMAP1610_SRAM_START,   OMAP1610_SRAM_SIZE,   MT_DEVICE }
};
#endif

#ifdef CONFIG_ARCH_OMAP5912
static struct map_desc omap5912_io_desc[] __initdata = {
 { OMAP5912_DSP_BASE,    OMAP5912_DSP_START,    OMAP5912_DSP_SIZE,    MT_DEVICE },
 { OMAP5912_DSPREG_BASE, OMAP5912_DSPREG_START, OMAP5912_DSPREG_SIZE, MT_DEVICE },
/*
 * The OMAP5912 has 250kByte internal SRAM. Because the mapping is baseed on page
 * size (4kByte), it seems that the last 2kByte (=0x800) of the 250kByte are not mapped.
 * Add additional 2kByte (0x800) so that the last page is mapped and the last 2kByte
 * can be used.
 */
 { OMAP5912_SRAM_BASE,   OMAP5912_SRAM_START,   OMAP5912_SRAM_SIZE + 0x800,   MT_DEVICE }
};
#endif

static int initialized = 0;

static void __init _omap_map_io(void)
{
	initialized = 1;

	/* We have to initialize the IO space mapping before we can run
	 * cpu_is_omapxxx() macros. */
	iotable_init(omap_io_desc, ARRAY_SIZE(omap_io_desc));
	omap_check_revision();

#ifdef CONFIG_ARCH_OMAP730
	if (cpu_is_omap730()) {
		iotable_init(omap730_io_desc, ARRAY_SIZE(omap730_io_desc));
	}
#endif
#ifdef CONFIG_ARCH_OMAP1510
	if (cpu_is_omap1510()) {
		iotable_init(omap1510_io_desc, ARRAY_SIZE(omap1510_io_desc));
	}
#endif
#ifdef CONFIG_ARCH_OMAP1610
	if (cpu_is_omap1610()) {
		iotable_init(omap1610_io_desc, ARRAY_SIZE(omap1610_io_desc));
	}
#endif
#ifdef CONFIG_ARCH_OMAP5912
	if (cpu_is_omap5912()) {
		iotable_init(omap5912_io_desc, ARRAY_SIZE(omap5912_io_desc));
	}
#endif

	/* REVISIT: Refer to OMAP5910 Errata, Advisory SYS_1: "Timeout Abort
	 * on a Posted Write in the TIPB Bridge".
	 */
	omap_writew(0x0, MPU_PUBLIC_TIPB_CNTL);
	omap_writew(0x0, MPU_PRIVATE_TIPB_CNTL);

	/* Must init clocks early to assure that timer interrupt works
	 */
	init_ck();
}

/*
 * This should only get called from board specific init
 */
void omap_map_io(void)
{
	if (!initialized)
		_omap_map_io();
}

extern int omap_bootloader_tag_len;
extern u8 omap_bootloader_tag[];

const void *__omap_get_per_info(u16 tag, size_t len)
{
	struct omap_board_info_entry *info = NULL;

#ifdef CONFIG_OMAP_BOOT_TAG
	if (omap_bootloader_tag_len > 4)
		info = (struct omap_board_info_entry *) omap_bootloader_tag;
	while (info != NULL) {
		u8 *next;

		if (info->tag == tag)
			break;

		next = (u8 *) info + sizeof(*info) + info->len;
		if (next >= omap_bootloader_tag + omap_bootloader_tag_len)
			info = NULL;
		else
			info = (struct omap_board_info_entry *) next;
	}
#endif
	if (info == NULL)
		return NULL;
	if (info->len != len) {
		printk(KERN_ERR "OMAP per_info: Length mismatch with tag %x (want %d, got %d)\n",
		       tag, len, info->len);
		return NULL;
	}

	return info->data;
}
EXPORT_SYMBOL(__omap_get_per_info);

static int __init omap_add_serial_console(void)
{
	const struct omap_uart_info *info;

	info = omap_get_per_info(OMAP_TAG_UART, struct omap_uart_info);
	if (info != NULL && info->console_uart) {
		static char speed[11], *opt = NULL;

		if (info->console_speed) {
			snprintf(speed, sizeof(speed), "%u", info->console_speed);
			opt = speed;
		}
		return add_preferred_console("ttyS", info->console_uart - 1, opt);
	}
	return 0;
}
console_initcall(omap_add_serial_console);
