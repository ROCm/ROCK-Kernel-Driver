/*
 * linux/drivers/pcmcia/sa1100_sa1111.c
 *
 * We implement the generic parts of a SA1111 PCMCIA driver.  This
 * basically means we handle everything except controlling the
 * power.  Power is machine specific...
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/hardware/sa1111.h>
#include <asm/irq.h>

#include "sa1100_generic.h"
#include "sa1111_generic.h"

static struct irqs {
	int irq;
	const char *str;
} irqs[] = {
	{ IRQ_S0_CD_VALID,    "SA1111 PCMCIA card detect" },
	{ IRQ_S0_BVD1_STSCHG, "SA1111 PCMCIA BVD1"        },
	{ IRQ_S1_CD_VALID,    "SA1111 CF card detect"     },
	{ IRQ_S1_BVD1_STSCHG, "SA1111 CF BVD1"            },
};

static struct sa1111_dev *pcmcia;

int sa1111_pcmcia_init(struct pcmcia_init *init)
{
	int i, ret;

	for (i = ret = 0; i < ARRAY_SIZE(irqs); i++) {
		set_irq_type(irqs[i].irq, IRQT_FALLING);
		ret = request_irq(irqs[i].irq, init->handler, SA_INTERRUPT,
				  irqs[i].str, NULL);
		if (ret)
			break;
	}

	if (i < ARRAY_SIZE(irqs)) {
		printk(KERN_ERR "sa1111_pcmcia: unable to grab IRQ%d (%d)\n",
			irqs[i].irq, ret);
		while (i--)
			free_irq(irqs[i].irq, NULL);
	}

	return ret ? -1 : 2;
}

int sa1111_pcmcia_shutdown(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(irqs); i++)
		free_irq(irqs[i].irq, NULL);

	return 0;
}

int sa1111_pcmcia_socket_state(struct pcmcia_state_array *state)
{
	unsigned long status;

	if (state->size < 2)
		return -1;

	status = sa1111_readl(pcmcia->mapbase + SA1111_PCSR);

	state->state[0].detect = status & PCSR_S0_DETECT ? 0 : 1;
	state->state[0].ready  = status & PCSR_S0_READY  ? 1 : 0;
	state->state[0].bvd1   = status & PCSR_S0_BVD1   ? 1 : 0;
	state->state[0].bvd2   = status & PCSR_S0_BVD2   ? 1 : 0;
	state->state[0].wrprot = status & PCSR_S0_WP     ? 1 : 0;
	state->state[0].vs_3v  = status & PCSR_S0_VS1    ? 0 : 1;
	state->state[0].vs_Xv  = status & PCSR_S0_VS2    ? 0 : 1;

	state->state[1].detect = status & PCSR_S1_DETECT ? 0 : 1;
	state->state[1].ready  = status & PCSR_S1_READY  ? 1 : 0;
	state->state[1].bvd1   = status & PCSR_S1_BVD1   ? 1 : 0;
	state->state[1].bvd2   = status & PCSR_S1_BVD2   ? 1 : 0;
	state->state[1].wrprot = status & PCSR_S1_WP     ? 1 : 0;
	state->state[1].vs_3v  = status & PCSR_S1_VS1    ? 0 : 1;
	state->state[1].vs_Xv  = status & PCSR_S1_VS2    ? 0 : 1;

	return 1;
}

int sa1111_pcmcia_get_irq_info(struct pcmcia_irq_info *info)
{
	int ret = 0;

	switch (info->sock) {
	case 0:	info->irq = IRQ_S0_READY_NINT;	break;
	case 1: info->irq = IRQ_S1_READY_NINT;	break;
	default: ret = 1;
	}

	return ret;
}

int sa1111_pcmcia_configure_socket(const struct pcmcia_configure *conf)
{
	unsigned int rst, flt, wait, pse, irq, pccr_mask, val;
	unsigned long flags;

	switch (conf->sock) {
	case 0:
		rst = PCCR_S0_RST;
		flt = PCCR_S0_FLT;
		wait = PCCR_S0_PWAITEN;
		pse = PCCR_S0_PSE;
		irq = IRQ_S0_READY_NINT;
		break;

	case 1:
		rst = PCCR_S1_RST;
		flt = PCCR_S1_FLT;
		wait = PCCR_S1_PWAITEN;
		pse = PCCR_S1_PSE;
		irq = IRQ_S1_READY_NINT;
		break;

	default:
		return -1;
	}

	switch (conf->vcc) {
	case 0:
		pccr_mask = 0;
		break;

	case 33:
		pccr_mask = wait;
		break;

	case 50:
		pccr_mask = pse | wait;
		break;

	default:
		printk(KERN_ERR "sa1111_pcmcia: unrecognised VCC %u\n",
			conf->vcc);
		return -1;
	}

	if (conf->reset)
		pccr_mask |= rst;

	if (conf->output)
		pccr_mask |= flt;

	local_irq_save(flags);
	val = sa1111_readl(pcmcia->mapbase + SA1111_PCCR);
	val = (val & ~(pse | flt | wait | rst)) | pccr_mask;
	sa1111_writel(val, pcmcia->mapbase + SA1111_PCCR);
	local_irq_restore(flags);

	if (conf->irq)
		enable_irq(irq);
	else
		disable_irq(irq);

	return 0;
}

int sa1111_pcmcia_socket_init(int sock)
{
	return 0;
}

int sa1111_pcmcia_socket_suspend(int sock)
{
	return 0;
}

static int pcmcia_probe(struct device *dev)
{
	struct sa1111_dev *sadev = SA1111_DEV(dev);
	unsigned long flags;
	char *base;

	local_irq_save(flags);
	if (pcmcia) {
		local_irq_restore(flags);
		return -EBUSY;
	}

	pcmcia = sadev;
	local_irq_restore(flags);

	if (!request_mem_region(sadev->res.start, 512,
				SA1111_DRIVER_NAME(sadev)))
		return -EBUSY;

	base = sadev->mapbase;

	/*
	 * Initialise the suspend state.
	 */
	sa1111_writel(PCSSR_S0_SLEEP | PCSSR_S1_SLEEP, base + SA1111_PCSSR);
	sa1111_writel(PCCR_S0_FLT | PCCR_S1_FLT, base + SA1111_PCCR);

#ifdef CONFIG_SA1100_ADSBITSY
	pcmcia_adsbitsy_init();
#endif
#ifdef CONFIG_SA1100_BADGE4
	pcmcia_badge4_init();
#endif
#ifdef CONFIG_SA1100_GRAPHICSMASTER
	pcmcia_graphicsmaster_init();
#endif
#ifdef CONFIG_SA1100_JORNADA720
	pcmcia_jornada720_init();
#endif
#ifdef CONFIG_ASSABET_NEPONSET
	pcmcia_neponset_init();
#endif
#ifdef CONFIG_SA1100_PFS168
	pcmcia_pfs_init();
#endif
#ifdef CONFIG_SA1100_PT_SYSTEM3
	pcmcia_system3_init();
#endif
#ifdef CONFIG_SA1100_XP860
	pcmcia_xp860_init();
#endif
	return 0;
}

static int __devexit pcmcia_remove(struct device *dev)
{
	struct sa1111_dev *sadev = SA1111_DEV(dev);

#ifdef CONFIG_SA1100_ADSBITSY
	pcmcia_adsbitsy_exit();
#endif
#ifdef CONFIG_SA1100_BADGE4
	pcmcia_badge4_exit();
#endif
#ifdef CONFIG_SA1100_GRAPHICSMASTER
	pcmcia_graphicsmaster_exit();
#endif
#ifdef CONFIG_SA1100_JORNADA720
	pcmcia_jornada720_exit();
#endif
#ifdef CONFIG_ASSABET_NEPONSET
	pcmcia_neponset_exit();
#endif
#ifdef CONFIG_SA1100_PFS168
	pcmcia_pfs_exit();
#endif
#ifdef CONFIG_SA1100_PT_SYSTEM3
	pcmcia_system3_exit();
#endif
#ifdef CONFIG_SA1100_XP860
	pcmcia_xp860_exit();
#endif

	release_mem_region(sadev->res.start, 512);
	pcmcia = NULL;

	return 0;
}

static int pcmcia_suspend(struct device *dev, u32 state, u32 level)
{
	return 0;
}

static int pcmcia_resume(struct device *dev, u32 level)
{
	return 0;
}

static struct sa1111_driver pcmcia_driver = {
	.drv = {
		.name		= "SA1111 PCMCIA",
		.bus		= &sa1111_bus_type,
		.probe		= pcmcia_probe,
		.remove		= __devexit_p(pcmcia_remove),
		.suspend	= pcmcia_suspend,
		.resume		= pcmcia_resume,
	},
	.devid			= SA1111_DEVID_PCMCIA,
};

static int __init sa1111_drv_pcmcia_init(void)
{
	return driver_register(&pcmcia_driver.drv);
}

static void __exit sa1111_drv_pcmcia_exit(void)
{
	remove_driver(&pcmcia_driver.drv);
}

module_init(sa1111_drv_pcmcia_init);
module_exit(sa1111_drv_pcmcia_exit);

MODULE_DESCRIPTION("SA1111 PCMCIA card socket driver");
MODULE_LICENSE("GPL");
