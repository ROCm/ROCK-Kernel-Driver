/*
 * drivers/pcmcia/sa1100_graphicsclient.c
 *
 * PCMCIA implementation routines for Graphics Client Plus
 *
 * 9/12/01   Woojung
 *    Turn power OFF at startup
 * 1/31/2001 Woojung Huh
 *    Fix for GC Plus PCMCIA Reset Problem
 * 2/27/2001 Woojung Huh [whuh@applieddata.net]
 *    Fix
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/irq.h>
#include "sa1100_generic.h"

#error This is broken!

#define	S0_CD_IRQ		60				// Socket 0 Card Detect IRQ
#define	S0_STS_IRQ		55				// Socket 0 PCMCIA IRQ

static volatile unsigned long *PCMCIA_Status = 
		((volatile unsigned long *) ADS_p2v(_ADS_CS_STATUS));

static volatile unsigned long *PCMCIA_Power = 
		((volatile unsigned long *) ADS_p2v(_ADS_CS_PR));

static struct pcmcia_irqs irqs[] = {
	{ 0, S0_CD_IRQ, "PCMCIA 0 CD" },
};

static int gcplus_pcmcia_init(struct sa1100_pcmcia_socket *skt)
{
	// Reset PCMCIA
	// Reset Timing for CPLD(U2) version 8001E or later
	*PCMCIA_Power &= ~ ADS_CS_PR_A_RESET;
	udelay(12);			// 12 uSec

	*PCMCIA_Power |= ADS_CS_PR_A_RESET;
	mdelay(30);			// 30 mSec

	// Turn off 5V
	*PCMCIA_Power &= ~0x03;

	skt->irq = S0_STS_IRQ;

	/* Register interrupts */
	return sa11xx_request_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static void gcplus_pcmcia_hw_shutdown(struct sa1100_pcmcia_socket *skt)
{
	/* disable IRQs */
	free_irq(S0_CD_IRQ, skt);
  
	/* Shutdown PCMCIA power */
	mdelay(2);			// 2msec
	*PCMCIA_Power &= ~0x03;
}

static void
gcplus_pcmcia_socket_state(struct sa1100_pcmcia_socket *skt, struct pcmcia_state *state)
{
	unsigned long levels = *PCMCIA_Status;

	state->detect=(levels & ADS_CS_ST_A_CD)?1:0;
	state->ready=(levels & ADS_CS_ST_A_READY)?1:0;
	state->bvd1= 0;
	state->bvd2= 0;
	state->wrprot=0;
	state->vs_3v=0;
	state->vs_Xv=0;
}

static int
gcplus_pcmcia_configure_socket(struct sa1100_pcmcia_socket *skt,
			       const socket_state_t *state)
{
	unsigned long flags;

	local_irq_save(flags);

	switch (state->Vcc) {
	case 0:
		*PCMCIA_Power &= ~(ADS_CS_PR_A_3V_POWER | ADS_CS_PR_A_5V_POWER);
		break;

	case 50:
		*PCMCIA_Power &= ~(ADS_CS_PR_A_3V_POWER | ADS_CS_PR_A_5V_POWER);
		*PCMCIA_Power |= ADS_CS_PR_A_5V_POWER;
		break;

	case 33:
		*PCMCIA_Power &= ~(ADS_CS_PR_A_3V_POWER | ADS_CS_PR_A_5V_POWER);
		*PCMCIA_Power |= ADS_CS_PR_A_3V_POWER;
		break;

	default:
		printk(KERN_ERR "%s(): unrecognized Vcc %u\n",
			__FUNCTION__, state->Vcc);
		local_irq_restore(flags);
		return -1;
	}

	/* Silently ignore Vpp, output enable, speaker enable. */

	// Reset PCMCIA
	*PCMCIA_Power &= ~ ADS_CS_PR_A_RESET;
	udelay(12);

	*PCMCIA_Power |= ADS_CS_PR_A_RESET;
	mdelay(30);

	local_irq_restore(flags);

	return 0;
}

static void gcplus_pcmcia_socket_init(struct sa1100_pcmcia_socket *skt)
{
}

static void gcplus_pcmcia_socket_suspend(struct sa1100_pcmcia_socket *skt)
{
}

static struct pcmcia_low_level gcplus_pcmcia_ops = { 
	.owner			= THIS_MODULE,
	.hw_init		= gcplus_pcmcia_hw_init,
	.hw_shutdown		= gcplus_pcmcia_hw_shutdown,
	.socket_state		= gcplus_pcmcia_socket_state,
	.configure_socket	= gcplus_pcmcia_configure_socket,
	.socket_init		= gcplus_pcmcia_socket_init,
	.socket_suspend		= gcplus_pcmcia_socket_suspend,
};

int __init pcmcia_gcplus_init(struct device *dev)
{
	int ret = -ENODEV;

	if (machine_is_gcplus())
		ret = sa11xx_drv_pcmcia_probe(dev, &gcplus_pcmcia_ops, 0, 1);

	return ret;
}
