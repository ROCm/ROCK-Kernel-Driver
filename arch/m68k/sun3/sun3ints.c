 /*
 * linux/arch/m68k/sun3/sun3ints.c -- Sun-3 Linux interrupt handling code
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/interrupt.h>
#include <asm/segment.h>
#include <asm/intersil.h>
#include <asm/oplib.h>
#include <asm/sun3ints.h>

extern void sun3_leds (unsigned char);

void sun3_disable_interrupts(void)
{
	sun3_disable_irq(0);
}

void sun3_enable_interrupts(void)
{
	sun3_enable_irq(0);
}	

int led_pattern[8] = {
       ~(0x80), ~(0x01),
       ~(0x40), ~(0x02),
       ~(0x20), ~(0x04),
       ~(0x10), ~(0x08)
};

unsigned char* sun3_intreg;

void sun3_insert_irq(irq_node_t **list, irq_node_t *node)
{
}

void sun3_delete_irq(irq_node_t **list, void *dev_id)
{
}

void sun3_free_irq(unsigned int irq, void *dev_id)
{
}

void sun3_enable_irq(unsigned int irq)
{
	*sun3_intreg |=  (1<<irq);
}

void sun3_disable_irq(unsigned int irq)
{
	*sun3_intreg &= ~(1<<irq);
}

inline void sun3_do_irq(int irq, struct pt_regs *fp)
{
	kstat.irqs[0][SYS_IRQS + irq]++;
	*sun3_intreg &= ~(1<<irq);
	*sun3_intreg |=  (1<<irq);
}

int sun3_get_irq_list(char *buf)
{
	return 0;
}

static void sun3_int5(int irq, void *dev_id, struct pt_regs *fp)
{
        kstat.irqs[0][SYS_IRQS + irq]++;
        *sun3_intreg &= ~(1<<irq);
	intersil_clear();
        *sun3_intreg |=  (1<<irq);
        do_timer(fp);
        if(!(kstat.irqs[0][SYS_IRQS + irq] % 20))
                sun3_leds(led_pattern[(kstat.irqs[0][SYS_IRQS+irq]%160)
                /20]);
}

static void sun3_int7(int irq, void *dev_id, struct pt_regs *fp)
{
	sun3_do_irq(irq,fp);
	if(!(kstat.irqs[0][SYS_IRQS + irq] % 2000)) 
		sun3_leds(led_pattern[(kstat.irqs[0][SYS_IRQS+irq]%16000)/2000]);
}

/* handle requested ints, excepting 5 and 7, which always do the same
   thing */
static void *dev_ids[SYS_IRQS];
static void (*inthandler[SYS_IRQS])(int, void *, struct pt_regs *) = {
	NULL, NULL, NULL, NULL, NULL, sun3_int5, NULL, sun3_int7
};

static void sun3_inthandle(int irq, void *dev_id, struct pt_regs *fp)
{
	if(inthandler[irq] == NULL)
		panic ("bad interrupt %d received (id %p)\n",irq, dev_id);

        kstat.irqs[0][SYS_IRQS + irq]++;
        *sun3_intreg &= ~(1<<irq);

	inthandler[irq](irq, dev_ids[irq], fp);
}

void (*sun3_default_handler[SYS_IRQS])(int, void *, struct pt_regs *) = {
	sun3_inthandle, sun3_inthandle, sun3_inthandle, sun3_inthandle,
	sun3_inthandle, sun3_int5, sun3_inthandle, sun3_int7
};

static char *dev_names[SYS_IRQS] = { NULL, NULL, NULL, NULL, 
				     NULL, "timer", NULL, NULL };

void sun3_init_IRQ(void)
{
	int i;

	for(i = 0; i < SYS_IRQS; i++)
	{
		if(dev_names[i])
			sys_request_irq(i, sun3_default_handler[i],
					0, dev_names[i], NULL);
	}

}
                                
int sun3_request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
                      unsigned long flags, const char *devname, void *dev_id)
{
	if(inthandler[irq] != NULL) {
		printk("sun3_request_irq: request for irq %d -- already taken!\n", irq);
		return -1;
	}

	inthandler[irq] = handler;
	dev_ids[irq] = dev_id;
	dev_names[irq] = devname;

	/* setting devname would be nice */
	
	sys_request_irq(irq, sun3_default_handler[irq], 0, devname, NULL);
	

	return 0;
}
                        
