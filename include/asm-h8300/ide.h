/****************************************************************************/

/*
 *  linux/include/asm-h8300/ide.h
 *
 *  Copyright (C) 1994-1996  Linus Torvalds & authors
 *  Copyright (C) 2001       Lineo Inc., davidm@snapgear.com
 *  Copyright (C) 2002       Greg Ungerer (gerg@snapgear.com)
 *  Copyright (C) 2002       Yoshinori Sato (ysato@users.sourceforge.jp)
 */

/****************************************************************************/
#ifndef _H8300_IDE_H
#define _H8300_IDE_H
/****************************************************************************/
#ifdef __KERNEL__
/****************************************************************************/

#include <linux/config.h>
#include <linux/interrupt.h>

#include <asm/setup.h>
#include <asm/io.h>
#include <asm/irq.h>

/*
 *	Some coldfire specifics.
 */

/*
 *	Save some space, only have 1 interface.
 */
#define MAX_HWIFS	1

/*
 *	Fix up things that may not have been provided.
 */

#undef SUPPORT_SLOW_DATA_PORTS
#define SUPPORT_SLOW_DATA_PORTS 0

#undef SUPPORT_VLB_SYNC
#define SUPPORT_VLB_SYNC 0

/* this definition is used only on startup .. */
#undef HD_DATA
#define HD_DATA NULL

#define	DBGIDE(fmt,a...)
// #define	DBGIDE(fmt,a...) printk(fmt, ##a)
#define IDE_INLINE __inline__
// #define IDE_INLINE

#define ide__sti()	__sti()

/****************************************************************************/

typedef union {
	unsigned all			: 8;	/* all of the bits together */
	struct {
		unsigned bit7		: 1;	/* always 1 */
		unsigned lba		: 1;	/* using LBA instead of CHS */
		unsigned bit5		: 1;	/* always 1 */
		unsigned unit		: 1;	/* drive select number, 0 or 1 */
		unsigned head		: 4;	/* always zeros here */
	} b;
} select_t;

/*
 *	Our list of ports/irq's for different boards.
 */

/* Such a description is OK ? */
#define DEPEND_HEADER(target) <asm/target/ide.h> 
#include DEPEND_HEADER(TARGET)

/****************************************************************************/

static IDE_INLINE int ide_default_irq(ide_ioreg_t base)
{
	return H8300_IDE_IRQ+12;
}

static IDE_INLINE ide_ioreg_t ide_default_io_base(int index)
{
	return (ide_ioreg_t)H8300_IDE_BASE;
}

/*
 * Set up a hw structure for a specified data port, control port and IRQ.
 * This should follow whatever the default interface uses.
 */
static IDE_INLINE void ide_init_hwif_ports(
	hw_regs_t *hw,
	ide_ioreg_t data_port,
	ide_ioreg_t ctrl_port,
	int *irq)
{
	ide_ioreg_t reg = data_port;
	int i;

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = reg;
		reg += H8300_IDE_REG_OFFSET;
	}
	if (ctrl_port) {
		hw->io_ports[IDE_CONTROL_OFFSET] = ctrl_port;
	} else {
		hw->io_ports[IDE_CONTROL_OFFSET] = (ide_ioreg_t)H8300_IDE_CTRL;
	}
}


/*
 * This registers the standard ports for this architecture with the IDE
 * driver.
 */
static IDE_INLINE void ide_init_default_hwifs(void)
{
	hw_regs_t hw;
	ide_ioreg_t base;
	int index;

	for (index = 0; index < MAX_HWIFS; index++) {
		base = ide_default_io_base(index);
		if (!base)
			continue;
		memset(&hw, 0, sizeof(hw));
		ide_init_hwif_ports(&hw, base, 0, NULL);
		hw.irq = ide_default_irq(base);
		ide_register_hw(&hw, NULL);
	}
}

#define ide_request_irq(irq,hand,flg,dev,id)	request_irq((irq),(hand),(flg),(dev),(id))
#define ide_free_irq(irq,dev_id)		free_irq((irq), (dev_id))
#define ide_check_region(from,extent)		(0)
#define ide_request_region(from,extent,name)	do {} while(0)
#define ide_release_region(from,extent)		do {} while(0)

/*
 * The following are not needed for the non-m68k ports
 */
#define ide_ack_intr(hwif)		(1)
#define ide_fix_driveid(id)		target_ide_fix_driveid(id)
#define ide_release_lock(lock)		do {} while (0)
#define ide_get_lock(lock, hdlr, data)	do {} while (0)

static IDE_INLINE void ide_print_resource(char *name,hw_regs_t *hw)
{
	printk("%s at 0x%08x-0x%08x,0x%08x on irq %d", name,
		(unsigned int)hw->io_ports[IDE_DATA_OFFSET],
		(unsigned int)hw->io_ports[IDE_DATA_OFFSET]+(8*H8300_IDE_REG_OFFSET)-1,
		(unsigned int)hw->io_ports[IDE_CONTROL_OFFSET],
		hw->irq);
}

/****************************************************************************/
#endif /* __KERNEL__ */
#endif /* _H8300_IDE_H */
/****************************************************************************/
