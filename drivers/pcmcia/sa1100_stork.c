/* 
 * drivers/pcmcia/sa1100_stork.c
 *
    Copyright 2001 (C) Ken Gordon

    This is derived from pre-existing drivers/pcmcia/sa1100_?????.c

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

 * 
 * PCMCIA implementation routines for stork
 *
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/i2c.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/irq.h>
#include "sa1100_generic.h"

static int debug = 0;

static struct irqs {
	int irq;
	const char *str;
} irqs[] = {
	{ IRQ_GPIO_STORK_PCMCIA_A_CARD_DETECT, "PCMCIA_CD0" },
	{ IRQ_GPIO_STORK_PCMCIA_B_CARD_DETECT, "PCMCIA_CD1" },
};

static int stork_pcmcia_init(struct pcmcia_init *init)
{
	int irq, res;

	printk("in stork_pcmcia_init\n");

	/* Set transition detect */
	set_irq_type(IRQ_GPIO_STORK_PCMCIA_A_RDY, IRQT_FALLING);
	set_irq_type(IRQ_GPIO_STORK_PCMCIA_B_RDY, IRQT_FALLING);

	/* Register interrupts */
	for (i = 0; i < ARRAY_SIZE(irqs); i++) {
		set_irq_type(irqs[i].irq, IRQT_NOEDGE);
		res = request_irq(irqs[i].irq, init->handler, SA_INTERRUPT,
				  irqs[i].str, NULL);
		if (res)
			goto irq_err;
	}

        return 2;

 irq_err:
        printk(KERN_ERR "%s: request for IRQ%d failed (%d)\n",
	       __FUNCTION__, irq, res);

	while (i--)
		free_irq(irqs[i].irq, NULL);

        return res;
}

static int stork_pcmcia_shutdown(void)
{
	int i;

        printk(__FUNCTION__ "\n");

        /* disable IRQs */
        for (i = 0; i < ARRAY_SIZE(irqs); i++)
        	free_irq(irqs[i].irq, NULL);
  
        /* Disable CF bus: */
        storkClearLatchA(STORK_PCMCIA_PULL_UPS_POWER_ON);
	storkClearLatchA(STORK_PCMCIA_A_POWER_ON);
	storkClearLatchA(STORK_PCMCIA_B_POWER_ON);
        return 0;
}

static void stork_pcmcia_socket_state(int sock, struct pcmcia_state *state)
{
        unsigned long levels = GPLR;

	if (debug > 1)
		printk(__FUNCTION__ " GPLR=%x IRQ[1:0]=%x\n", levels,
			(levels & (GPIO_STORK_PCMCIA_A_RDY|GPIO_STORK_PCMCIA_B_RDY)));

	switch (sock) {
	case 0:
		state->detect=((levels & GPIO_STORK_PCMCIA_A_CARD_DETECT)==0)?1:0;
		state->ready=(levels & GPIO_STORK_PCMCIA_A_RDY)?1:0;
		state->bvd1= 1;
		state->bvd2= 1;
		state->wrprot=0;
		state->vs_3v=1;
		state->vs_Xv=0;
		break;

	case 1:
		state->detect=((levels & GPIO_STORK_PCMCIA_B_CARD_DETECT)==0)?1:0;
		state->ready=(levels & GPIO_STORK_PCMCIA_B_RDY)?1:0;
		state->bvd1=1;
		state->bvd2=1;
		state->wrprot=0;
		state->vs_3v=1;
		state->vs_Xv=0;
		break;
	}
}

static int stork_pcmcia_get_irq_info(struct pcmcia_irq_info *info)
{

        switch (info->sock) {
        case 0:
                info->irq=IRQ_GPIO_STORK_PCMCIA_A_RDY;
                break;
        case 1:
                info->irq=IRQ_GPIO_STORK_PCMCIA_B_RDY;
                break;
        default:
                return -1;
        }
        return 0;
}

static int stork_pcmcia_configure_socket(int sock, const struct pcmcia_configure *configure)
{
	unsigned long flags;

        int DETECT, RDY, POWER, RESET;

        if (sock > 1) return -1;

	printk(__FUNCTION__ ": socket=%d vcc=%d vpp=%d reset=%d\n", 
                       sock, configure->vcc, configure->vpp, configure->reset);

	local_irq_save(flags);

        if (sock == 0) {
    	    DETECT = GPIO_STORK_PCMCIA_A_CARD_DETECT;
    	    RDY = GPIO_STORK_PCMCIA_A_RDY;
    	    POWER = STORK_PCMCIA_A_POWER_ON;
    	    RESET = STORK_PCMCIA_A_RESET;
        } else {
    	    DETECT = GPIO_STORK_PCMCIA_B_CARD_DETECT;
    	    RDY = GPIO_STORK_PCMCIA_B_RDY;
    	    POWER = STORK_PCMCIA_B_POWER_ON;
    	    RESET = STORK_PCMCIA_B_RESET;
        }
    
/*
        if (storkTestGPIO(DETECT)) {
           printk("no card detected - but resetting anyway\r\n");
        }
*/
	switch (configure->vcc) {
	case 0:
/*		storkClearLatchA(STORK_PCMCIA_PULL_UPS_POWER_ON); */
                storkClearLatchA(POWER);
		break;

	case 50:
	case 33:
                storkSetLatchA(STORK_PCMCIA_PULL_UPS_POWER_ON);
                storkSetLatchA(POWER);
		break;

	default:
		printk(KERN_ERR "%s(): unrecognized Vcc %u\n", __FUNCTION__,
		       configure->vcc);
		local_irq_restore(flags);
		return -1;
	}

	if (configure->reset)
                storkSetLatchB(RESET);
	else
                storkClearLatchB(RESET);

	local_irq_restore(flags);

        /* silently ignore vpp and speaker enables. */

        printk(__FUNCTION__ ": finished\n");

        return 0;
}

static int stork_pcmcia_socket_init(int sock)
{
        storkSetLatchA(STORK_PCMCIA_PULL_UPS_POWER_ON);

        if (sock == 0)
		set_irq_type(IRQ_GPIO_STORK_PCMCIA_A_CARD_DETECT, IRQT_BOTHEDGE);
        else if (sock == 1)
		set_irq_type(IRQ_GPIO_STORK_PCMCIA_B_CARD_DETECT, IRQT_BOTHEDGE);

	return 0;
}

static int stork_pcmcia_socket_suspend(int sock)
{
        if (sock == 0)
		set_irq_type(IRQ_GPIO_STORK_PCMCIA_A_CARD_DETECT, IRQT_NOEDGE);
        else if (sock == 1) {
		set_irq_type(IRQ_GPIO_STORK_PCMCIA_B_CARD_DETECT, IRQT_NOEDGE);

		/*
		 * Hack!
		 */
	        storkClearLatchA(STORK_PCMCIA_PULL_UPS_POWER_ON);
	}

	return 0;
}

static struct pcmcia_low_level stork_pcmcia_ops = { 
	.init			= stork_pcmcia_init,
	.shutdown		= stork_pcmcia_shutdown,
	.socket_state		= stork_pcmcia_socket_state,
	.get_irq_info		= stork_pcmcia_get_irq_info,
	.configure_socket	= stork_pcmcia_configure_socket,

	.socket_init		= stork_pcmcia_socket_init,
	.socket_suspend		= stork_pcmcia_socket_suspend,
};

int __init pcmcia_stork_init(void)
{
	int ret = -ENODEV;

	if (machine_is_stork())
		ret = sa1100_register_pcmcia(&stork_pcmcia_ops);

	return ret;
}

void __exit pcmcia_stork_exit(void)
{
	sa1100_unregister_pcmcia(&stork_pcmcia_ops);
}

