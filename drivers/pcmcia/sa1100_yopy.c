/*
 * drivers/pcmcia/sa1100_yopy.c
 *
 * PCMCIA implementation routines for Yopy
 *
 */
#include <linux/kernel.h>
#include <linux/sched.h>
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

static struct irqs {
	int irq;
	const char *str;
} irqs[] = {
	{ IRQ_CF_CD,   "CF_CD"   },
	{ IRQ_CF_BVD2, "CF_BVD2" },
	{ IRQ_CF_BVD1, "CF_BVD1" },
};

static int yopy_pcmcia_init(struct pcmcia_init *init)
{
	int i, res;

	pcmcia_power(0);
	pcmcia_reset(1);

	/* Set transition detect */
	set_irq_type(IRQ_CF_IREQ, IRQT_FALLING);

	/* Register interrupts */
	for (i = 0; i < ARRAY_SIZE(irqs); i++) {
		set_irq_type(irqs[i].irq, IRQT_NOEDGE);
		res = request_irq(irqs[i].irq, init->handler, SA_INTERRUPT,
				  irqs[i].str, NULL);
		if (res)
			goto irq_err;
	}

	return 1;

 irq_err:
	printk(KERN_ERR "%s: request for IRQ%d failed (%d)\n",
	       __FUNCTION__, irqs[i].irq, res);

	while (i--)
		free_irq(irqs[i].irq, NULL);

	return res;
}

static int yopy_pcmcia_shutdown(void)
{
	int i;

	/* disable IRQs */
	for (i = 0; i < ARRAY_SIZE(irqs); i++)
		free_irq(irqs[i].irq, NULL);

	/* Disable CF */
	pcmcia_reset(1);
	pcmcia_power(0);

	return 0;
}

static void yopy_pcmcia_socket_state(int sock, struct pcmcia_state_array *state)
{
	unsigned long levels = GPLR;

	if (sock == 0) {
		state->detect = (levels & GPIO_CF_CD)    ? 0 : 1;
		state->ready  = (levels & GPIO_CF_READY) ? 1 : 0;
		state->bvd1   = (levels & GPIO_CF_BVD1)  ? 1 : 0;
		state->bvd2   = (levels & GPIO_CF_BVD2)  ? 1 : 0;
		state->wrprot = 0; /* Not available on Yopy. */
		state->vs_3v  = 0; /* FIXME Can only apply 3.3V on Yopy. */
		state->vs_Xv  = 0;
	}
}

static int yopy_pcmcia_get_irq_info(struct pcmcia_irq_info *info)
{
	if (info->sock != 0)
		return -1;

	info->irq = IRQ_CF_IREQ;

	return 0;
}

static int yopy_pcmcia_configure_socket(int sock, const struct pcmcia_configure *configure)
{
	if (sock != 0)
		return -1;

	switch (configure->vcc) {
	case 0:	/* power off */
		pcmcia_power(0);
		break;
	case 50:
		printk(KERN_WARNING __FUNCTION__"(): CS asked for 5V, applying 3.3V..\n");
	case 33:
		pcmcia_power(1);
		break;
	default:
		printk(KERN_ERR __FUNCTION__"(): unrecognized Vcc %u\n",
		       configure->vcc);
		return -1;
	}

	pcmcia_reset(configure->reset);

	/* Silently ignore Vpp, output enable, speaker enable. */

	return 0;
}

static int yopy_pcmcia_socket_init(int sock)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(irqs); i++)
		set_irq_type(irqs[i].irq, IRQT_BOTHEDGE);

	return 0;
}

static int yopy_pcmcia_socket_suspend(int sock)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(irqs); i++)
		set_irq_type(irqs[i].irq, IRQT_NOEDGE);

	return 0;
}

static struct pcmcia_low_level yopy_pcmcia_ops = {
	.init			= yopy_pcmcia_init,
	.shutdown		= yopy_pcmcia_shutdown,
	.socket_state		= yopy_pcmcia_socket_state,
	.get_irq_info		= yopy_pcmcia_get_irq_info,
	.configure_socket	= yopy_pcmcia_configure_socket,

	.socket_init		= yopy_pcmcia_socket_init,
	.socket_suspend		= yopy_pcmcia_socket_suspend,
};

int __init pcmcia_yopy_init(void)
{
	int ret = -ENODEV;

	if (machine_is_yopy())
		ret = sa1100_register_pcmcia(&yopy_pcmcia_ops);

	return ret;
}

void __exit pcmcia_yopy_exit(void)
{
	sa1100_unregister_pcmcia(&yopy_pcmcia_ops);
}

