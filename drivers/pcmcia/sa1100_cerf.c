/*
 * drivers/pcmcia/sa1100_cerf.c
 *
 * PCMCIA implementation routines for CerfBoard
 * Based off the Assabet.
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

#ifdef CONFIG_SA1100_CERF_CPLD
#define CERF_SOCKET	0
#else
#define CERF_SOCKET	1
#endif

static struct pcmcia_irqs irqs[] = {
	{ CERF_SOCKET, IRQ_GPIO_CF_CD,   "CF_CD"   },
	{ CERF_SOCKET, IRQ_GPIO_CF_BVD2, "CF_BVD2" },
	{ CERF_SOCKET, IRQ_GPIO_CF_BVD1, "CF_BVD1" }
};

static int cerf_pcmcia_hw_init(struct sa1100_pcmcia_socket *skt)
{
	skt->irq = IRQ_GPIO_CF_IRQ;

	return sa11xx_request_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static void cerf_pcmcia_hw_shutdown(struct sa1100_pcmcia_socket *skt)
{
	sa11xx_free_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static void
cerf_pcmcia_socket_state(struct sa1100_pcmcia_socket *skt, struct pcmcia_state *state)
{
	unsigned long levels = GPLR;

	state->detect=((levels & GPIO_CF_CD)==0)?1:0;
	state->ready=(levels & GPIO_CF_IRQ)?1:0;
	state->bvd1=(levels & GPIO_CF_BVD1)?1:0;
	state->bvd2=(levels & GPIO_CF_BVD2)?1:0;
	state->wrprot=0;
	state->vs_3v=1;
	state->vs_Xv=0;
}

static int
cerf_pcmcia_configure_socket(struct sa1100_pcmcia_socket *skt,
			     const socket_state_t *state)
{
	switch (state->Vcc) {
	case 0:
		break;

	case 50:
	case 33:
#ifdef CONFIG_SA1100_CERF_CPLD
		GPCR = GPIO_PWR_SHUTDOWN;
#endif
		break;

	default:
		printk(KERN_ERR "%s(): unrecognized Vcc %u\n",
			__FUNCTION__, state->Vcc);
		return -1;
	}

	if (state->flags & SS_RESET) {
#ifdef CONFIG_SA1100_CERF_CPLD
		GPSR = GPIO_CF_RESET;
#endif
	} else {
#ifdef CONFIG_SA1100_CERF_CPLD
		GPCR = GPIO_CF_RESET;
#endif
	}

	return 0;
}

static void cerf_pcmcia_socket_init(struct sa1100_pcmcia_socket *skt)
{
	sa11xx_enable_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static void cerf_pcmcia_socket_suspend(struct sa1100_pcmcia_socket *skt)
{
	sa11xx_disable_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static struct pcmcia_low_level cerf_pcmcia_ops = { 
	.owner			= THIS_MODULE,
	.init			= cerf_pcmcia_hw_init,
	.shutdown		= cerf_pcmcia_hw_shutdown,
	.socket_state		= cerf_pcmcia_socket_state,
	.configure_socket	= cerf_pcmcia_configure_socket,

	.socket_init		= cerf_pcmcia_socket_init,
	.socket_suspend		= cerf_pcmcia_socket_suspend,
};

int __init pcmcia_cerf_init(struct device *dev)
{
	int ret = -ENODEV;

	if (machine_is_cerf())
		ret = sa11xx_drv_pcmcia_probe(dev, &cerf_pcmcia_ops, CERF_SOCKET, 1);

	return ret;
}
