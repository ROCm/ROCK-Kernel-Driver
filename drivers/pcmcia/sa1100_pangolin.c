/*
 * drivers/pcmcia/sa1100_pangolin.c
 *
 * PCMCIA implementation routines for Pangolin
 *
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/irq.h>
#include "sa1100_generic.h"

#ifndef CONFIG_SA1100_PANGOLIN_PCMCIA_IDE
#define PANGOLIN_SOCK	1
#else
#define PANGOLIN_SOCK	0
#endif

static struct pcmcia_irqs irqs[] = {
	{ PANGOLIN_SOCK, IRQ_PCMCIA_CD, "PCMCIA CD" },
};

static int pangolin_pcmcia_hw_init(struct sa1100_pcmcia_socket *skt)
{
	int res;

#ifndef CONFIG_SA1100_PANGOLIN_PCMCIA_IDE
	/* Enable PCMCIA bus: */
	GPCR = GPIO_PCMCIA_BUS_ON;
#endif

	skt->irq = IRQ_PCMCIA_IRQ;

	return sa11xx_request_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static void pangolin_pcmcia_hw_shutdown(struct sa1100_pcmcia_socket *skt)
{
	sa11xx_free_irqs(skt, irqs, ARRAY_SIZE(irqs));

#ifndef CONFIG_SA1100_PANGOLIN_PCMCIA_IDE
	/* Disable PCMCIA bus: */
	GPSR = GPIO_PCMCIA_BUS_ON;
#endif
}

static void
pangolin_pcmcia_socket_state(struct sa1100_pcmcia_socket *skt,
			     struct pcmcia_state *state)
{
	unsigned long levels = GPLR;;

	state->detect=((levels & GPIO_PCMCIA_CD)==0)?1:0;
	state->ready=(levels & GPIO_PCMCIA_IRQ)?1:0;
	state->bvd1=1; /* Not available on Pangolin. */
	state->bvd2=1; /* Not available on Pangolin. */
	state->wrprot=0; /* Not available on Pangolin. */
	state->vs_3v=1;  /* Can only apply 3.3V on Pangolin. */
	state->vs_Xv=0;
}

static int
pangolin_pcmcia_configure_socket(struct sa1100_pcmcia_socket *skt,
				 const socket_state_t *state)
{
	unsigned long value, flags;

	local_irq_save(flags);

	/* Murphy: BUS_ON different from POWER ? */

	switch (state->Vcc) {
	case 0:
		break;
#ifndef CONFIG_SA1100_PANGOLIN_PCMCIA_IDE
	case 50:
		printk(KERN_WARNING "%s(): CS asked for 5V, applying 3.3V...\n",
			__FUNCTION__);
	case 33:  /* Can only apply 3.3V to the CF slot. */
		break;
#else
	case 50:
		printk(KERN_WARNING "%s(): CS asked for 5V, determinded by "
			"jumper setting...\n", __FUNCTION__);
		break;
	case 33:
		printk(KERN_WARNING "%s(): CS asked for 3.3V, determined by "
			"jumper setting...\n", __FUNCTION__);
		break;
#endif
	default:
		printk(KERN_ERR "%s(): unrecognized Vcc %u\n",
			__FUNCTION__, state->Vcc);
		local_irq_restore(flags);
		return -1;
	}
#ifdef CONFIG_SA1100_PANGOLIN_PCMCIA_IDE
	/* reset & unreset request */
	if (skt->nr == 0) {
		if (state->flags & SS_RESET) {
			GPSR = GPIO_PCMCIA_RESET;
		} else {
			GPCR = GPIO_PCMCIA_RESET;
		}
	}
#endif
	/* Silently ignore Vpp, output enable, speaker enable. */
	local_irq_restore(flags);
	return 0;
}

static void pangolin_pcmcia_socket_init(struct sa1100_pcmcia_socket *skt)
{
	sa11xx_enable_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static void pangolin_pcmcia_socket_suspend(struct sa1100_pcmcia_socket *skt)
{
	sa11xx_disable_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static struct pcmcia_low_level pangolin_pcmcia_ops = { 
	.owner			= THIS_MODULE,
	.hw_init		= pangolin_pcmcia_hw_init,
	.hw_shutdown		= pangolin_pcmcia_hw_shutdown,
	.socket_state		= pangolin_pcmcia_socket_state,
	.configure_socket	= pangolin_pcmcia_configure_socket,

	.socket_init		= pangolin_pcmcia_socket_init,
	.socket_suspend		= pangolin_pcmcia_socket_suspend,
};

int __init pcmcia_pangolin_init(struct device *dev)
{
	int ret = -ENODEV;

	if (machine_is_pangolin())
		ret = sa11xx_drv_pcmcia_probe(dev, &pangolin_pcmcia_ops, PANGOLIN_SOCK, 1);

	return ret;
}
