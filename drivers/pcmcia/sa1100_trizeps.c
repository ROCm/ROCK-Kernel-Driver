/*
 * drivers/pcmcia/sa1100_trizeps.c
 *
 * PCMCIA implementation routines for Trizeps
 *
 * Authors:
 * Andreas Hofer <ho@dsa-ac.de>,
 * Peter Lueg <pl@dsa-ac.de>,
 * Guennadi Liakhovetski <gl@dsa-ac.de>
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/arch/trizeps.h>
#include <asm/mach-types.h>
#include <asm/system.h>
#include <asm/irq.h>
#include "sa1100_generic.h"

#define NUMBER_OF_TRIZEPS_PCMCIA_SLOTS 1

static struct pcmcia_irqs irqs[] = {
	{ 0, TRIZEPS_IRQ_PCMCIA_CD0, "PCMCIA_CD0" },
};

/**
 *
 *
 ******************************************************/
static int trizeps_pcmcia_init(struct sa1100_pcmcia_socket *skt)
{
	skt->irq = TRIZEPS_IRQ_PCMCIA_IRQ0;

	/* Enable CF bus: */
	TRIZEPS_BCR_clear(TRIZEPS_BCR1, TRIZEPS_nPCM_ENA_REG);

	/* All those are inputs */
	GPDR &= ~((GPIO_GPIO(TRIZEPS_GPIO_PCMCIA_CD0))
		    | (GPIO_GPIO(TRIZEPS_GPIO_PCMCIA_IRQ0)));

	return sa11xx_request_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

/**
 *
 *
 ******************************************************/
static void trizeps_pcmcia_shutdown(struct sa1100_pcmcia_socket *skt)
{
	printk(">>>>>PCMCIA TRIZEPS shutdown\n");

	sa11xx_free_irqs(skt, irqs, ARRAY_SIZE(irqs));

	/* Disable CF bus: */
	TRIZEPS_BCR_set(TRIZEPS_BCR1, TRIZEPS_nPCM_ENA_REG);
}

/**
 *
 ******************************************************/
static void
trizeps_pcmcia_socket_state(struct sa1100_pcmcia_socket *skt,
			    struct pcmcia_state *state_array)
{
	unsigned long levels = GPLR;

	state->detect = ((levels & GPIO_GPIO(TRIZEPS_GPIO_PCMCIA_CD0)) == 0) ? 1 : 0;
	state->ready  = ((levels & GPIO_GPIO(TRIZEPS_GPIO_PCMCIA_IRQ0)) != 0) ? 1 : 0;
	state->bvd1   = ((TRIZEPS_BCR1 & TRIZEPS_PCM_BVD1) !=0 ) ? 1 : 0;
	state->bvd2   = ((TRIZEPS_BCR1 & TRIZEPS_PCM_BVD2) != 0) ? 1 : 0;
	state->wrprot = 0; // not write protected
	state->vs_3v  = ((TRIZEPS_BCR1 & TRIZEPS_nPCM_VS1) == 0) ? 1 : 0; //VS1=0 -> vs_3v=1
	state->vs_Xv  = ((TRIZEPS_BCR1 & TRIZEPS_nPCM_VS2) == 0) ? 1 : 0; //VS2=0 -> vs_Xv=1
}

/**
 *
 *
 ******************************************************/
static int
trizeps_pcmcia_configure_socket(struct sa1100_pcmcia_socket *skt,
				const socket_state_t *state)
{
	unsigned long flags;

	local_irq_save(flags);

	switch (state->Vcc) {
	case 0:
		printk(">>> PCMCIA Power off\n");
		TRIZEPS_BCR_clear(TRIZEPS_BCR1, TRIZEPS_PCM_V3_EN_REG);
		TRIZEPS_BCR_clear(TRIZEPS_BCR1, TRIZEPS_PCM_V5_EN_REG);
		break;

	case 33:
		// 3.3V Power on
		TRIZEPS_BCR_clear(TRIZEPS_BCR1, TRIZEPS_PCM_V3_EN_REG);
		TRIZEPS_BCR_set(TRIZEPS_BCR1, TRIZEPS_PCM_V5_EN_REG);
		break;
	case 50:
		// 5.0V Power on
		TRIZEPS_BCR_set(TRIZEPS_BCR1, TRIZEPS_PCM_V3_EN_REG);
		TRIZEPS_BCR_clear(TRIZEPS_BCR1, TRIZEPS_PCM_V5_EN_REG);
		break;
	default:
		printk(KERN_ERR "%s(): unrecognized Vcc %u\n", __FUNCTION__,
		       state->Vcc);
		local_irq_restore(flags);
		return -1;
	}

	if (state->flags & SS_RESET)
		TRIZEPS_BCR_set(TRIZEPS_BCR1, TRIZEPS_nPCM_RESET_DISABLE);   // Reset
	else
		TRIZEPS_BCR_clear(TRIZEPS_BCR1, TRIZEPS_nPCM_RESET_DISABLE); // no Reset
	/*
	  printk(" vcc=%u vpp=%u -->reset=%i\n",
	  state->Vcc,
	  state->Vpp,
	  ((BCR_read(1) & nPCM_RESET_DISABLE)? 1:0));
	*/
	local_irq_restore(flags);

	return 0;
}

static void trizeps_pcmcia_socket_init(struct sa1100_pcmcia_socket *skt)
{
	sa11xx_enable_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static void trizeps_pcmcia_socket_suspend(struct sa1100_pcmcia_socket *skt)
{
	sa11xx_disable_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

/**
 * low-level PCMCIA interface
 *
 ******************************************************/
struct pcmcia_low_level trizeps_pcmcia_ops = {
	.owner			= THIS_MODULE,
	.hw_init		= trizeps_pcmcia_hw_init,
	.hw_shutdown		= trizeps_pcmcia_hw_shutdown,
	.socket_state		= trizeps_pcmcia_socket_state,
	.configure_socket	= trizeps_pcmcia_configure_socket,
	.socket_init		= trizeps_pcmcia_socket_init,
	.socket_suspend		= trizeps_pcmcia_socket_suspend,
};

int __init pcmcia_trizeps_init(struct device *dev)
{
	int ret = -ENODEV;

	if (machine_is_trizeps())
		ret = sa11xx_drv_pcmcia_probe(dev, &trizeps_pcmcia_ops, 0,
					      NUMBER_OF_TRIZEPS_PCMCIA_SLOTS);

	return ret;
}
