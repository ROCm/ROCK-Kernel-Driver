/* $Id: ide-no.c,v 1.1 1999/08/21 21:43:00 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Stub IDE routines to keep Linux from crashing on machine which don't
 * have IDE like the Indy.
 *
 * Copyright (C) 1998, 1999 by Ralf Baechle
 */
#include <linux/hdreg.h>
#include <linux/kernel.h>
#include <linux/ide.h>
#include <asm/hdreg.h>
#include <asm/ptrace.h>

static int no_ide_default_irq(ide_ioreg_t base)
{
	return 0;
}

static ide_ioreg_t no_ide_default_io_base(int index)
{
	return 0;
}

static void no_ide_init_hwif_ports (hw_regs_t *hw, ide_ioreg_t data_port,
                                    ide_ioreg_t ctrl_port, int *irq)
{
}

static int no_ide_request_irq(unsigned int irq,
                              void (*handler)(int,void *, struct pt_regs *),
                              unsigned long flags, const char *device,
                              void *dev_id)
{
	panic("no_no_ide_request_irq called - shouldn't happen");
}			

static void no_ide_free_irq(unsigned int irq, void *dev_id)
{
	panic("no_ide_free_irq called - shouldn't happen");
}

static int no_ide_check_region(ide_ioreg_t from, unsigned int extent)
{
	panic("no_ide_check_region called - shouldn't happen");
}

static void no_ide_request_region(ide_ioreg_t from, unsigned int extent,
                                    const char *name)
{
	panic("no_ide_request_region called - shouldn't happen");
}

static void no_ide_release_region(ide_ioreg_t from, unsigned int extent)
{
	panic("no_ide_release_region called - shouldn't happen");
}

struct ide_ops no_ide_ops = {
	&no_ide_default_irq,
	&no_ide_default_io_base,
	&no_ide_init_hwif_ports,
	&no_ide_request_irq,
	&no_ide_free_irq,
	&no_ide_check_region,
	&no_ide_request_region,
	&no_ide_release_region
};
