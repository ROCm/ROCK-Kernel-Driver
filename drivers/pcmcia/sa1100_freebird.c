/*
 * drivers/pcmcia/sa1100_freebird.c
 *
 * Created by Eric Peng <ericpeng@coventive.com>
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
	{ 0, IRQ_GPIO_FREEBIRD_CF_CD,  "CF_CD"   },
	{ 0, IRQ_GPIO_FREEBIRD_CF_BVD, "CF_BVD1" },
};

static int freebird_pcmcia_init(struct sa1100_pcmcia_socket *skt)
{
	/* Enable Linkup CF card */
	LINKUP_PRC = 0xc0;
	mdelay(100);
	LINKUP_PRC = 0xc1;
	mdelay(100);
	LINKUP_PRC = 0xd1;
	mdelay(100);
	LINKUP_PRC = 0xd1;
	mdelay(100);
	LINKUP_PRC = 0xc0;

	skt->irq = IRQ_GPIO_FREEBIRD_CF_IRQ;

	return sa11xx_request_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static void freebird_pcmcia_shutdown(struct sa1100_pcmcia_socket *skt)
{
	sa11xx_free_irqs(skt, irqs, ARRAY_SIZE(irqs);

	/* Disable CF card */
	LINKUP_PRC = 0x40;  /* SSP=1   SOE=0 */
	mdelay(100);
}

static void
freebird_pcmcia_socket_state(struct sa1100_pcmcia_socket *skt, struct pcmcia_state *state)
{
	unsigned long levels = LINKUP_PRS;
//	printk("LINKUP_PRS=%x\n",levels);

	state->detect = ((levels & (LINKUP_CD1 | LINKUP_CD2))==0)?1:0;
	state->ready  = (levels & LINKUP_RDY)?1:0;
	state->bvd1   = (levels & LINKUP_BVD1)?1:0;
	state->bvd2   = (levels & LINKUP_BVD2)?1:0;
	state->wrprot = 0; /* Not available on Assabet. */
	state->vs_3v  = 1;  /* Can only apply 3.3V on Assabet. */
	state->vs_Xv  = 0;
}

static int
freebird_pcmcia_configure_socket(struct sa1100_pcmcia_socket *skt,
				 socket_state_t *state)
{
	unsigned long value, flags;

	local_irq_save(flags);

	value = 0xc0;   /* SSP=1  SOE=1  CFE=1 */

	switch (state->Vcc) {
	case 0:
		break;

	case 50:
		printk(KERN_WARNING "%s(): CS asked for 5V, applying 3.3V...\n",
			__FUNCTION__);

	case 33:  /* Can only apply 3.3V to the CF slot. */
		value |= LINKUP_S1;
		break;

	default:
		printk(KERN_ERR "%s(): unrecognized Vcc %u\n",
			__FUNCTION__, state->Vcc);
		local_irq_restore(flags);
		return -1;
	}

	if (state->flags & SS_RESET)
		value |= LINKUP_RESET;

	/* Silently ignore Vpp, output enable, speaker enable. */

	LINKUP_PRC = value;
//	printk("LINKUP_PRC=%x\n",value);
	local_irq_restore(flags);

	return 0;
}

static void freebird_pcmcia_socket_init(struct sa1100_pcmcia_socket *skt)
{
	sa11xx_disable_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static void freebird_pcmcia_socket_suspend(struct sa1100_pcmcia_socket *skt)
{
	sa11xx_enable_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static struct pcmcia_low_level freebird_pcmcia_ops = {
	.owner			= THIS_MODULE,
	.hw_init		= freebird_pcmcia_hw_init,
	.hw_shutdown		= freebird_pcmcia_hw_shutdown,
	.socket_state		= freebird_pcmcia_socket_state,
	.configure_socket	= freebird_pcmcia_configure_socket,

	.socket_init		= freebird_pcmcia_socket_init,
	.socket_suspend		= freebird_pcmcia_socket_suspend,
};

int __init pcmcia_freebird_init(struct device *dev)
{
	int ret = -ENODEV;

	if (machine_is_freebird())
		ret = sa11xx_drv_pcmcia_probe(dev, &freebird_pcmcia_ops, 0, 1);

	return ret;
}
