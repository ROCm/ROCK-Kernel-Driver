/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/kernel.h"
#include "linux/list.h"
#include "linux/slab.h"
#include "asm/irq.h"
#include "init.h"
#include "sigio.h"
#include "irq_user.h"

static int sigio_irq_fd = -1;

void sigio_interrupt(int irq, void *data, struct pt_regs *unused)
{
	read_sigio_fd(sigio_irq_fd);
	reactivate_fd(sigio_irq_fd, SIGIO_WRITE_IRQ);
}

int write_sigio_irq(int fd)
{
	if(um_request_irq(SIGIO_WRITE_IRQ, fd, IRQ_READ, sigio_interrupt,
			  SA_INTERRUPT | SA_SAMPLE_RANDOM, "write sigio", 
			  NULL)){
		printk("write_sigio_irq : um_request_irq failed\n");
		return(-1);
	}
	sigio_irq_fd = fd;
	return(0);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
