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
#include <linux/device.h>
#include <linux/i2c.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/irq.h>
#include "sa1100_generic.h"

static int debug = 0;

static struct pcmcia_irqs irqs[] = {
	{ 0, IRQ_GPIO_STORK_PCMCIA_A_CARD_DETECT, "PCMCIA_CD0" },
	{ 1, IRQ_GPIO_STORK_PCMCIA_B_CARD_DETECT, "PCMCIA_CD1" },
};

static int stork_pcmcia_hw_init(struct sa1100_pcmcia_socket *skt)
{
	printk("in stork_pcmcia_init\n");

	skt->irq = skt->nr ? IRQ_GPIO_STORK_PCMCIA_B_RDY
			   : IRQ_GPIO_STORK_PCMCIA_A_RDY;

	return sa11xx_request_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static void stork_pcmcia_hw_shutdown(struct sa1100_pcmcia_socket *skt)
{
	int i;

        printk(__FUNCTION__ "\n");

        /* disable IRQs */
        sa11xx_free_irqs(skt, irqs, ARRAY_SIZE(irqs));
  
        /* Disable CF bus: */
        storkClearLatchA(STORK_PCMCIA_PULL_UPS_POWER_ON);
	storkClearLatchA(STORK_PCMCIA_A_POWER_ON);
	storkClearLatchA(STORK_PCMCIA_B_POWER_ON);
}

static void
stork_pcmcia_socket_state(struct sa1100_pcmcia_socket *skt,
			  struct pcmcia_state *state)
{
        unsigned long levels = GPLR;

	if (debug > 1)
		printk(__FUNCTION__ " GPLR=%x IRQ[1:0]=%x\n", levels,
			(levels & (GPIO_STORK_PCMCIA_A_RDY|GPIO_STORK_PCMCIA_B_RDY)));

	switch (skt->nr) {
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

static int
stork_pcmcia_configure_socket(struct sa1100_pcmcia_socket *skt,
			      const socket_state_t *state)
{
	unsigned long flags;
        int DETECT, RDY, POWER, RESET;

	printk("%s: socket=%d vcc=%d vpp=%d reset=%d\n", __FUNCTION__,
		skt->nr, state->Vcc, state->Vpp, state->flags & SS_RESET ? 1 : 0);

	local_irq_save(flags);

        if (skt->nr == 0) {
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
	switch (state->Vcc) {
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
		       state->Vcc);
		local_irq_restore(flags);
		return -1;
	}

	if (state->flags & SS_RESET)
                storkSetLatchB(RESET);
	else
                storkClearLatchB(RESET);

	local_irq_restore(flags);

        /* silently ignore vpp and speaker enables. */

        printk("%s: finished\n", __FUNCTION__);

        return 0;
}

static void stork_pcmcia_socket_init(struct sa1100_pcmcia_socket *skt)
{
        storkSetLatchA(STORK_PCMCIA_PULL_UPS_POWER_ON);

        sa11xx_enable_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static void stork_pcmcia_socket_suspend(struct sa1100_pcmcia_socket *skt)
{
	sa11xx_disable_irqs(skt, irqs, ARRAY_SIZE(irqs));

	/*
	 * Hack!
	 */
	if (skt->nr == 1)
	        storkClearLatchA(STORK_PCMCIA_PULL_UPS_POWER_ON);

	return 0;
}

static struct pcmcia_low_level stork_pcmcia_ops = { 
	.owner			= THIS_MODULE,
	.hw_init		= stork_pcmcia_hw_init,
	.hw_shutdown		= stork_pcmcia_hw_shutdown,
	.socket_state		= stork_pcmcia_socket_state,
	.configure_socket	= stork_pcmcia_configure_socket,

	.socket_init		= stork_pcmcia_socket_init,
	.socket_suspend		= stork_pcmcia_socket_suspend,
};

int __init pcmcia_stork_init(struct device *dev)
{
	int ret = -ENODEV;

	if (machine_is_stork())
		ret = sa11xx_drv_pcmcia_probe(dev, &stork_pcmcia_ops, 0, 2);

	return ret;
}
