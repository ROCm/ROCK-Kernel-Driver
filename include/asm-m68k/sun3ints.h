/*
 * sun3ints.h -- Linux/Sun3 interrupt handling code definitions
 *
 * Erik Verbruggen (erik@bigmama.xtdnet.nl)
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 */

#ifndef SUN3INTS_H
#define SUN3INTS_H

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/interrupt.h>
#include <asm/segment.h>
#include <asm/intersil.h>
#include <asm/oplib.h>

void sun3_enable_irq(unsigned int irq);
void sun3_disable_irq(unsigned int irq);
int sun3_request_irq(unsigned int irq,
                     void (*handler)(int, void *, struct pt_regs *),
                     unsigned long flags, const char *devname, void *dev_id
		    );

#endif /* SUN3INTS_H */
