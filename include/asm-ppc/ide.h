/*
 *  linux/include/asm-ppc/ide.h
 *
 *  Copyright (C) 1994-1996 Linus Torvalds & authors */

/*
 *  This file contains the ppc architecture specific IDE code.
 */

#ifndef __ASMPPC_IDE_H
#define __ASMPPC_IDE_H

#ifdef __KERNEL__

#include <linux/sched.h>
#include <asm/processor.h>
#include <asm/mpc8xx.h>

#ifndef MAX_HWIFS
#define MAX_HWIFS	8
#endif

#include <asm/hdreg.h>

#include <linux/config.h>
#include <linux/hdreg.h>
#include <linux/ioport.h>
#include <asm/io.h>

struct ide_machdep_calls {
        int         (*default_irq)(ide_ioreg_t base);
        ide_ioreg_t (*default_io_base)(int index);
        void        (*ide_init_hwif)(hw_regs_t *hw,
                                     ide_ioreg_t data_port,
                                     ide_ioreg_t ctrl_port,
                                     int *irq);
};

extern struct ide_machdep_calls ppc_ide_md;

#undef	SUPPORT_SLOW_DATA_PORTS
#define	SUPPORT_SLOW_DATA_PORTS	0

static __inline__ int ide_default_irq(ide_ioreg_t base)
{
	if (ppc_ide_md.default_irq)
		return ppc_ide_md.default_irq(base);
	return 0;
}

static __inline__ ide_ioreg_t ide_default_io_base(int index)
{
	if (ppc_ide_md.default_io_base)
		return ppc_ide_md.default_io_base(index);
	return 0;
}

/*
 * This is only used for PC-style IDE controllers (e.g. as on PReP)
 * or for PCI IDE devices, not for other types of IDE interface such
 * as the pmac IDE interfaces.
 */
static __inline__ void ide_init_hwif_ports(hw_regs_t *hw,
					   ide_ioreg_t data_port,
					   ide_ioreg_t ctrl_port, int *irq)
{
	ide_ioreg_t reg = data_port;
	int i;

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++)
		hw->io_ports[i] = reg++;
	if (ctrl_port) {
		hw->io_ports[IDE_CONTROL_OFFSET] = ctrl_port;
	} else {
		hw->io_ports[IDE_CONTROL_OFFSET] =
			hw->io_ports[IDE_DATA_OFFSET] + 0x206;
	}
	if (irq != NULL)
		*irq = 0;
	hw->io_ports[IDE_IRQ_OFFSET] = 0;
	if (ppc_ide_md.ide_init_hwif != NULL)
		ppc_ide_md.ide_init_hwif(hw, data_port, ctrl_port, irq);
}

static __inline__ void ide_init_default_hwifs(void)
{
#ifndef CONFIG_PCI
	hw_regs_t hw;
	int index;
	ide_ioreg_t base;

	for (index = 0; index < MAX_HWIFS; index++) {
		base = ide_default_io_base(index);
		if (base == 0)
			continue;
		ide_init_hwif_ports(&hw, base, 0, NULL);
		hw.irq = ide_default_irq(base);
		ide_register_hw(&hw, NULL);
	}
#endif
}

#if (defined CONFIG_APUS || defined CONFIG_BLK_DEV_MPC8xx_IDE )
#define IDE_ARCH_ACK_INTR  1
#define ide_ack_intr(hwif) (hwif->hw.ack_intr ? hwif->hw.ack_intr(hwif) : 1)
#endif

#endif /* __KERNEL__ */

#endif /* __ASMPPC_IDE_H */
