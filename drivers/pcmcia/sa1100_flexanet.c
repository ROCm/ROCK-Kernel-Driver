/*
 * drivers/pcmcia/sa1100_flexanet.c
 *
 * PCMCIA implementation routines for Flexanet.
 * by Jordi Colomer, 09/05/2001
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

static struct pcmcia_irqs irqs[] = {
	{ 0, IRQ_GPIO_CF1_CD,   "CF1_CD"   },
	{ 0, IRQ_GPIO_CF1_BVD1, "CF1_BVD1" },
	{ 1, IRQ_GPIO_CF2_CD,   "CF2_CD"   },
	{ 1, IRQ_GPIO_CF2_BVD1, "CF2_BVD1" }
};

/*
 * Socket initialization.
 *
 * Called by sa1100_pcmcia_driver_init on startup.
 */
static int flexanet_pcmcia_hw_init(struct sa1100_pcmcia_socket *skt)
{
	skt->irq = skt->nr ? IRQ_GPIO_CF2_IRQ : IRQ_GPIO_CF1_IRQ;

	return sa11xx_request_irqs(skt, irqs, ARRAY_SIZE(irqs));
}


/*
 * Socket shutdown
 */
static void flexanet_pcmcia_hw_shutdown(struct sa1100_pcmcia_socket *skt)
{
	sa11xx_free_irqs(skt, irqs, ARRAY_SIZE(irqs));
}


/*
 * Get the state of the sockets.
 *
 *  Sockets in Flexanet are 3.3V only, without BVD2.
 *
 */
static void
flexanet_pcmcia_socket_state(struct sa1100_pcmcia_socket *skt,
			     struct pcmcia_state *state)
{
	unsigned long levels = GPLR; /* Sense the GPIOs, asynchronously */

	switch (skt->nr) {
	ase 0: /* Socket 0 */
		state->detect = ((levels & GPIO_CF1_NCD)==0)?1:0;
		state->ready  = (levels & GPIO_CF1_IRQ)?1:0;
		state->bvd1   = (levels & GPIO_CF1_BVD1)?1:0;
		state->bvd2   = 1;
		state->wrprot = 0;
		state->vs_3v  = 1;
		state->vs_Xv  = 0;
		break;

	case 1: /* Socket 1 */
		state->detect = ((levels & GPIO_CF2_NCD)==0)?1:0;
		state->ready  = (levels & GPIO_CF2_IRQ)?1:0;
		state->bvd1   = (levels & GPIO_CF2_BVD1)?1:0;
		state->bvd2   = 1;
		state->wrprot = 0;
		state->vs_3v  = 1;
		state->vs_Xv  = 0;
		break;
	}
}


/*
 *
 */
static int
flexanet_pcmcia_configure_socket(struct sa1100_pcmcia_socket *skt,
				 const socket_state_t *state)
{
	unsigned long value, flags, mask;

	/* Ignore the VCC level since it is 3.3V and always on */
	switch (state->Vcc) {
	case 0:
		printk(KERN_WARNING "%s(): CS asked to power off.\n",
			__FUNCTION__);
		break;

	case 50:
		printk(KERN_WARNING "%s(): CS asked for 5V, applying 3.3V...\n",
			__FUNCTION__);

	case 33:
		break;

	default:
		printk(KERN_ERR "%s(): unrecognized Vcc %u\n", __FUNCTION__,
		       state->Vcc);
		return -1;
	}

	/* Reset the slot(s) using the controls in the BCR */
	mask = 0;

	switch (skt->nr) {
	case 0:
		mask = FHH_BCR_CF1_RST;
		break;
	case 1:
		mask = FHH_BCR_CF2_RST;
		break;
	}

	local_irq_save(flags);

	value = flexanet_BCR;
	value = (state->flags & SS_RESET) ? (value | mask) : (value & ~mask);
	FHH_BCR = flexanet_BCR = value;

	local_irq_restore(flags);

	return 0;
}

static void flexanet_pcmcia_socket_init(struct sa1100_pcmcia_socket *skt)
{
	sa11xx_enable_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static void flexanet_pcmcia_socket_suspend(struct sa1100_pcmcia_socket *skt)
{
	sa11xx_disable_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

/*
 * The set of socket operations
 *
 */
static struct pcmcia_low_level flexanet_pcmcia_ops = {
	.owner			= THIS_MODULE,
	.hw_init		= flexanet_pcmcia_hw_init,
	.hw_shutdown		= flexanet_pcmcia_hw_shutdown,
	.socket_state		= flexanet_pcmcia_socket_state,
	.configure_socket	= flexanet_pcmcia_configure_socket,
	.socket_init		= flexanet_pcmcia_socket_init,
	.socket_suspend		= flexanet_pcmcia_socket_suspend,
};

int __init pcmcia_flexanet_init(struct device *dev)
{
	int ret = -ENODEV;

	if (machine_is_flexanet())
		ret = sa11xx_drv_pcmcia_probe(dev, &flexanet_pcmcia_ops, 0, 2);

	return ret;
}
