/*
 * drivers/pcmcia/sa1100_yopy.c
 *
 * PCMCIA implementation routines for Yopy
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/irq.h>
#include "sa1100_generic.h"


static inline void pcmcia_power(int on) {
	/* high for power up */
	yopy_gpio_set(GPIO_CF_POWER, on);
}

static inline void pcmcia_reset(int reset)
{
	/* high for reset */
	yopy_gpio_set(GPIO_CF_RESET, reset);
}

static struct pcmcia_irqs irqs[] = {
	{ 0, IRQ_CF_CD,   "CF_CD"   },
	{ 0, IRQ_CF_BVD2, "CF_BVD2" },
	{ 0, IRQ_CF_BVD1, "CF_BVD1" },
};

static int yopy_pcmcia_hw_init(struct sa1100_pcmcia_socket *skt)
{
	skt->irq = IRQ_CF_IREQ;

	pcmcia_power(0);
	pcmcia_reset(1);

	return sa11xx_request_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static void yopy_pcmcia_hw_shutdown(struct sa1100_pcmcia_socket *skt)
{
	sa11xx_free_irqs(skt, irqs, ARRAY_SIZE(irqs));

	/* Disable CF */
	pcmcia_reset(1);
	pcmcia_power(0);
}

static void
yopy_pcmcia_socket_state(struct sa1100_pcmcia_socket *skt,
			 struct pcmcia_state_array *state)
{
	unsigned long levels = GPLR;

	state->detect = (levels & GPIO_CF_CD)    ? 0 : 1;
	state->ready  = (levels & GPIO_CF_READY) ? 1 : 0;
	state->bvd1   = (levels & GPIO_CF_BVD1)  ? 1 : 0;
	state->bvd2   = (levels & GPIO_CF_BVD2)  ? 1 : 0;
	state->wrprot = 0; /* Not available on Yopy. */
	state->vs_3v  = 0; /* FIXME Can only apply 3.3V on Yopy. */
	state->vs_Xv  = 0;
}

static int
yopy_pcmcia_configure_socket(struct sa1100_pcmcia_socket *skt,
			     const socket_state_t *state)
{
	switch (state->Vcc) {
	case 0:	/* power off */
		pcmcia_power(0);
		break;
	case 50:
		printk(KERN_WARNING "%s(): CS asked for 5V, applying 3.3V..\n", __FUNCTION__);
	case 33:
		pcmcia_power(1);
		break;
	default:
		printk(KERN_ERR "%s(): unrecognized Vcc %u\n",
		       __FUNCTION__, state->Vcc);
		return -1;
	}

	pcmcia_reset(state->flags & SS_RESET ? 1 : 0);

	/* Silently ignore Vpp, output enable, speaker enable. */

	return 0;
}

static void yopy_pcmcia_socket_init(struct sa1100_pcmcia_socket *skt)
{
	sa11xx_enable_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static void yopy_pcmcia_socket_suspend(struct sa1100_pcmcia_socket *skt)
{
	sa11xx_disable_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static struct pcmcia_low_level yopy_pcmcia_ops = {
	.owner			= THIS_MODULE,
	.init			= yopy_pcmcia_init,
	.shutdown		= yopy_pcmcia_shutdown,
	.socket_state		= yopy_pcmcia_socket_state,
	.configure_socket	= yopy_pcmcia_configure_socket,

	.socket_init		= yopy_pcmcia_socket_init,
	.socket_suspend		= yopy_pcmcia_socket_suspend,
};

int __init pcmcia_yopy_init(struct device *dev)
{
	int ret = -ENODEV;

	if (machine_is_yopy())
		ret = sa11xx_drv_pcmcia_probe(dev, &yopy_pcmcia_ops, 0, 1);

	return ret;
}
