/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * This file contains the MIPS architecture specific IDE code.
 *
 * Copyright (C) 1994-1996  Linus Torvalds & authors
 */

/*
 *  This file contains the MIPS architecture specific IDE code.
 */

#ifndef __ASM_IDE_H
#define __ASM_IDE_H

#ifdef __KERNEL__

#include <linux/config.h>

#ifndef MAX_HWIFS
# ifdef CONFIG_BLK_DEV_IDEPCI
#define MAX_HWIFS	10
# else
#define MAX_HWIFS	6
# endif
#endif

#define ide__sti()	__sti()

struct ide_ops {
	int (*ide_default_irq)(ide_ioreg_t base);
	ide_ioreg_t (*ide_default_io_base)(int index);
	void (*ide_init_hwif_ports)(hw_regs_t *hw, ide_ioreg_t data_port,
	                            ide_ioreg_t ctrl_port, int *irq);
	int (*ide_request_irq)(unsigned int irq, void (*handler)(int, void *,
	                       struct pt_regs *), unsigned long flags,
	                       const char *device, void *dev_id);
	void (*ide_free_irq)(unsigned int irq, void *dev_id);
	int (*ide_check_region) (ide_ioreg_t from, unsigned int extent);
	void (*ide_request_region)(ide_ioreg_t from, unsigned int extent,
	                        const char *name);
	void (*ide_release_region)(ide_ioreg_t from, unsigned int extent);
};

extern struct ide_ops *ide_ops;

static __inline__ int ide_default_irq(ide_ioreg_t base)
{
	return ide_ops->ide_default_irq(base);
}

static __inline__ ide_ioreg_t ide_default_io_base(int index)
{
	return ide_ops->ide_default_io_base(index);
}

static inline void ide_init_hwif_ports(hw_regs_t *hw, ide_ioreg_t data_port,
                                       ide_ioreg_t ctrl_port, int *irq)
{
	ide_ioreg_t reg = data_port;
	int i;

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = reg;
		reg += 1;
	}
	if (ctrl_port) {
		hw->io_ports[IDE_CONTROL_OFFSET] = ctrl_port;
	} else {
		hw->io_ports[IDE_CONTROL_OFFSET] = hw->io_ports[IDE_DATA_OFFSET] + 0x206;
	}
	if (irq != NULL)
		*irq = 0;
	hw->io_ports[IDE_IRQ_OFFSET] = 0;
}

static __inline__ void ide_init_default_hwifs(void)
{
#ifndef CONFIG_BLK_DEV_IDEPCI
	hw_regs_t hw;
	int index;

	for(index = 0; index < MAX_HWIFS; index++) {
		ide_init_hwif_ports(&hw, ide_default_io_base(index), 0, NULL);
		hw.irq = ide_default_irq(ide_default_io_base(index));
		ide_register_hw(&hw, NULL);
	}
#endif /* CONFIG_BLK_DEV_IDEPCI */
}

typedef union {
	unsigned all			: 8;	/* all of the bits together */
	struct {
		unsigned head		: 4;	/* always zeros here */
		unsigned unit		: 1;	/* drive select number, 0 or 1 */
		unsigned bit5		: 1;	/* always 1 */
		unsigned lba		: 1;	/* using LBA instead of CHS */
		unsigned bit7		: 1;	/* always 1 */
	} b;
	} select_t;

static __inline__ int ide_request_irq(unsigned int irq, void (*handler)(int,void *, struct pt_regs *),
			unsigned long flags, const char *device, void *dev_id)
{
	return ide_ops->ide_request_irq(irq, handler, flags, device, dev_id);
}

static __inline__ void ide_free_irq(unsigned int irq, void *dev_id)
{
	ide_ops->ide_free_irq(irq, dev_id);
}

static __inline__ int ide_check_region (ide_ioreg_t from, unsigned int extent)
{
	return ide_ops->ide_check_region(from, extent);
}

static __inline__ void ide_request_region(ide_ioreg_t from,
                                          unsigned int extent, const char *name)
{
	ide_ops->ide_request_region(from, extent, name);
}

static __inline__ void ide_release_region(ide_ioreg_t from,
                                          unsigned int extent)
{
	ide_ops->ide_release_region(from, extent);
}

/*
 * The following are not needed for the non-m68k ports
 */
#define ide_ack_intr(hwif)		(1)
#define ide_fix_driveid(id)		do {} while (0)
#define ide_release_lock(lock)		do {} while (0)
#define ide_get_lock(lock, hdlr, data)	do {} while (0)

#endif /* __KERNEL__ */

#endif /* __ASM_IDE_H */
