/*
 * BK Id: SCCS/s.ide.h 1.16 09/28/01 07:54:24 trini
 */
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

extern void ppc_generic_ide_fix_driveid(struct hd_driveid *id);

struct ide_machdep_calls {
        int         (*default_irq)(ide_ioreg_t base);
        ide_ioreg_t (*default_io_base)(int index);
        int         (*ide_check_region)(ide_ioreg_t from, unsigned int extent);
        void        (*ide_request_region)(ide_ioreg_t from,
                                      unsigned int extent,
                                      const char *name);
        void        (*ide_release_region)(ide_ioreg_t from,
                                      unsigned int extent);
        void        (*ide_init_hwif)(hw_regs_t *hw,
                                     ide_ioreg_t data_port,
                                     ide_ioreg_t ctrl_port,
                                     int *irq);
};

extern struct ide_machdep_calls ppc_ide_md;

void ppc_generic_ide_fix_driveid(struct hd_driveid *id);
#define ide_fix_driveid(id)	ppc_generic_ide_fix_driveid((id))

#undef	SUPPORT_SLOW_DATA_PORTS
#define	SUPPORT_SLOW_DATA_PORTS	0
#undef	SUPPORT_VLB_SYNC
#define SUPPORT_VLB_SYNC	0

#define ide__sti()	__sti()

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

static __inline__ void ide_init_hwif_ports(hw_regs_t *hw,
					   ide_ioreg_t data_port,
					   ide_ioreg_t ctrl_port, int *irq)
{
	if (ppc_ide_md.ide_init_hwif != NULL)
		ppc_ide_md.ide_init_hwif(hw, data_port, ctrl_port, irq);
}

static __inline__ void ide_init_default_hwifs(void)
{
#ifndef CONFIG_BLK_DEV_IDEPCI
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
#endif /* CONFIG_BLK_DEV_IDEPCI */
}

static __inline__ int ide_check_region (ide_ioreg_t from, unsigned int extent)
{
	if (ppc_ide_md.ide_check_region)
		return ppc_ide_md.ide_check_region(from, extent);
	return 0;
}

static __inline__ void ide_request_region (ide_ioreg_t from, unsigned int extent, const char *name)
{
	if (ppc_ide_md.ide_request_region)
		ppc_ide_md.ide_request_region(from, extent, name);
}

static __inline__ void ide_release_region (ide_ioreg_t from, unsigned int extent)
{
	if (ppc_ide_md.ide_release_region)
		ppc_ide_md.ide_release_region(from, extent);
}

typedef union {
	unsigned all			: 8;	/* all of the bits together */
	struct {
		unsigned bit7		: 1;	/* always 1 */
		unsigned lba		: 1;	/* using LBA instead of CHS */
		unsigned bit5		: 1;	/* always 1 */
		unsigned unit		: 1;	/* drive select number, 0/1 */
		unsigned head		: 4;	/* always zeros here */
	} b;
} select_t;

typedef union {
	unsigned all			: 8;	/* all of the bits together */
	struct {
		unsigned HOB		: 1;	/* 48-bit address ordering */
		unsigned reserved456	: 3;
		unsigned bit3		: 1;	/* ATA-2 thingy */
		unsigned SRST		: 1;	/* host soft reset bit */
		unsigned nIEN		: 1;	/* device INTRQ to host */
		unsigned bit0		: 1;
	} b;
} control_t;

#if !defined(ide_request_irq)
#define ide_request_irq(irq,hand,flg,dev,id)	request_irq((irq),(hand),(flg),(dev),(id))
#endif

#if !defined(ide_free_irq)
#define ide_free_irq(irq,dev_id)		free_irq((irq), (dev_id))
#endif

/*
 * The following are not needed for the non-m68k ports
 * unless direct IDE on 8xx
 */
#if (defined CONFIG_APUS || defined CONFIG_BLK_DEV_MPC8xx_IDE )
#define ide_ack_intr(hwif) (hwif->hw.ack_intr ? hwif->hw.ack_intr(hwif) : 1)
#else
#define ide_ack_intr(hwif)		(1)
#endif
#define ide_release_lock(lock)		do {} while (0)
#define ide_get_lock(lock, hdlr, data)	do {} while (0)

#endif /* __KERNEL__ */

#endif /* __ASMPPC_IDE_H */
