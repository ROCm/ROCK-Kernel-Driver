/*
 * drivers/pcmcia/sa1100_shannon.c
 *
 * PCMCIA implementation routines for Shannon
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/arch/shannon.h>
#include <asm/irq.h>
#include "sa1100_generic.h"

static struct irqs {
	int irq;
	const char *str;
} irqs[] = {
	{ SHANNON_IRQ_GPIO_EJECT_0, "PCMCIA_CD_0" },
	{ SHANNON_IRQ_GPIO_EJECT_1, "PCMCIA_CD_1" },
};

static int shannon_pcmcia_init(struct pcmcia_init *init)
{
	int i, res;

	/* All those are inputs */
	GPDR &= ~(SHANNON_GPIO_EJECT_0 | SHANNON_GPIO_EJECT_1 | 
		  SHANNON_GPIO_RDY_0 | SHANNON_GPIO_RDY_1);
	GAFR &= ~(SHANNON_GPIO_EJECT_0 | SHANNON_GPIO_EJECT_1 | 
		  SHANNON_GPIO_RDY_0 | SHANNON_GPIO_RDY_1);

	init->socket_irq[0] = SHANNON_IRQ_GPIO_RDY_0;
	init->socket_irq[1] = SHANNON_IRQ_GPIO_RDY_1;

	/* Register interrupts */
	for (i = 0; i < ARRAY_SIZE(irqs); i++) {
		res = request_irq(irqs[i].irq, sa1100_pcmcia_interrupt,
				  SA_INTERRUPT, irqs[i].str, NULL);
		if (res)
			goto irq_err;
		set_irq_type(irqs[i].irq, IRQT_NOEDGE);
	}

	return 2;

 irq_err:
	printk(KERN_ERR "%s: request for IRQ%d failed (%d)\n",
		__FUNCTION__, irqs[i].irq, res);

	while (i--)
		free_irq(irqs[i].irq, NULL);

	return res;
}

static int shannon_pcmcia_shutdown(void)
{
	int i;

	/* disable IRQs */
	for (i = 0; i < ARRAY_SIZE(irqs); i++)
		free_irq(irqs[i].irq, NULL);

	return 0;
}

static void shannon_pcmcia_socket_state(int sock, struct pcmcia_state *state)
{
	unsigned long levels = GPLR;

	switch (sock) {
	case 0:
		state->detect = (levels & SHANNON_GPIO_EJECT_0) ? 0 : 1;
		state->ready  = (levels & SHANNON_GPIO_RDY_0) ? 1 : 0;
		state->wrprot = 0; /* Not available on Shannon. */
		state->bvd1   = 1; 
		state->bvd2   = 1; 
		state->vs_3v  = 1; /* FIXME Can only apply 3.3V on Shannon. */
		state->vs_Xv  = 0;
		break;

	case 1:
		state->detect = (levels & SHANNON_GPIO_EJECT_1) ? 0 : 1;
		state->ready  = (levels & SHANNON_GPIO_RDY_1) ? 1 : 0;
		state->wrprot = 0; /* Not available on Shannon. */
		state->bvd1   = 1; 
		state->bvd2   = 1; 
		state->vs_3v  = 1; /* FIXME Can only apply 3.3V on Shannon. */
		state->vs_Xv  = 0;
		break;
	}
}

static int shannon_pcmcia_configure_socket(int sock, const struct pcmcia_configure *configure)
{
	switch (configure->vcc) {
	case 0:	/* power off */
		printk(KERN_WARNING __FUNCTION__"(): CS asked for 0V, still applying 3.3V..\n");
		break;
	case 50:
		printk(KERN_WARNING __FUNCTION__"(): CS asked for 5V, applying 3.3V..\n");
	case 33:
		break;
	default:
		printk(KERN_ERR __FUNCTION__"(): unrecognized Vcc %u\n",
		       configure->vcc);
		return -1;
	}

	printk(KERN_WARNING __FUNCTION__"(): Warning, Can't perform reset\n");
	
	/* Silently ignore Vpp, output enable, speaker enable. */

	return 0;
}

static int shannon_pcmcia_socket_init(int sock)
{
	if (sock == 0)
		set_irq_type(SHANNON_IRQ_GPIO_EJECT_0, IRQT_BOTHEDGE);
	else if (sock == 1)
		set_irq_Type(SHANNON_IRQ_GPIO_EJECT_1, IRQT_BOTHEDGE);

	return 0;
}

static int shannon_pcmcia_socket_suspend(int sock)
{
	if (sock == 0)
		set_irq_type(SHANNON_IRQ_GPIO_EJECT_0, IRQT_NOEDGE);
	else if (sock == 1)
		set_irq_type(SHANNON_IRQ_GPIO_EJECT_1, IRQT_NOEDGE);

	return 0;
}

static struct pcmcia_low_level shannon_pcmcia_ops = {
	.owner			= THIS_MODULE,
	.init			= shannon_pcmcia_init,
	.shutdown		= shannon_pcmcia_shutdown,
	.socket_state		= shannon_pcmcia_socket_state,
	.configure_socket	= shannon_pcmcia_configure_socket,

	.socket_init		= shannon_pcmcia_socket_init,
	.socket_suspend		= shannon_pcmcia_socket_suspend,
};

int __init pcmcia_shannon_init(struct device *dev)
{
	int ret = -ENODEV;

	if (machine_is_shannon())
		ret = sa1100_register_pcmcia(&shannon_pcmcia_ops, dev);

	return ret;
}

void __exit pcmcia_shannon_exit(struct device *dev)
{
	sa1100_unregister_pcmcia(&shannon_pcmcia_ops, dev);
}
