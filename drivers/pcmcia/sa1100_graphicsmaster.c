/*
 * drivers/pcmcia/sa1100_graphicsmaster.c
 *
 * PCMCIA implementation routines for GraphicsMaster
 *
 * 9/18/01 Woojung
 *         Fixed wrong PCMCIA voltage setting
 * 7/5/01 Woojung Huh <whuh@applieddata.net>
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>

#include "sa1111_generic.h"

static int graphicsmaster_pcmcia_hw_init(struct sa1100_pcmcia_socket *skt)
{
  int return_val=0;

  /* Set GPIO_A<3:0> to be outputs for PCMCIA/CF power controller: */
  PA_DDR &= ~(GPIO_GPIO0 | GPIO_GPIO1 | GPIO_GPIO2 | GPIO_GPIO3);

  /* Disable Power 3.3V/5V for PCMCIA/CF */
  PA_DWR |= GPIO_GPIO0 | GPIO_GPIO1 | GPIO_GPIO2 | GPIO_GPIO3;

  /* why? */
  MECR = 0x09430943;

  return sa1111_pcmcia_hwinit(skt);
}

static int
graphicsmaster_pcmcia_configure_socket(struct sa1100_pcmcia_socket *skt,
				       const socket_state_t *state)
{
	unsigned int pa_dwr_mask, pa_dwr_set;
	int ret;

	switch (skt->nr) {
	case 0:
		pa_dwr_mask = GPIO_GPIO0 | GPIO_GPIO1;

		switch (state->Vcc) {
		default:
		case 0:  pa_dwr_set = GPIO_GPIO0 | GPIO_GPIO1;	break;
		case 33: pa_dwr_set = GPIO_GPIO1;		break;
		case 50: pa_dwr_set = GPIO_GPIO0;		break;
		}
		break;

	case 1:
		pa_dwr_mask = GPIO_GPIO2 | GPIO_GPIO3;

		switch (state->Vcc) {
		default:
		case 0:  pa_dwr_set = GPIO_GPIO2 | GPIO_GPIO3;	break;
		case 33: pa_dwr_set = GPIO_GPIO3;		break;
		case 50: pa_dwr_set = GPIO_GPIO2;		break;
		}
		break;
	}

	if (state->Vpp != state->Vcc && state->Vpp != 0) {
		printk(KERN_ERR "%s(): CF slot cannot support Vpp %u\n",
			__FUNCTION__, state->Vpp);
		return -1;
	}

	ret = sa1111_pcmcia_configure_socket(skt, state);
	if (ret == 0) {
		unsigned long flags;

		local_irq_save(flags);
		PA_DWR = (PA_DWR & ~pa_dwr_mask) | pa_dwr_set;
		local_irq_restore(flags);
	}

	return ret;
}

static struct pcmcia_low_level graphicsmaster_pcmcia_ops = {
	.owner			= THIS_MODULE,
	.hw_init		= graphicsmaster_pcmcia_init,
	.hw_shutdown		= sa1111_pcmcia_hw_shutdown,
	.socket_state		= sa1111_pcmcia_socket_state,
	.configure_socket	= graphicsmaster_pcmcia_configure_socket,

	.socket_init		= sa1111_pcmcia_socket_init,
	.socket_suspend		= sa1111_pcmcia_socket_suspend,
};

int __init pcmcia_graphicsmaster_init(struct device *dev)
{
	int ret = -ENODEV;

	if (machine_is_graphicsmaster())
		ret = sa11xx_drv_pcmcia_probe(dev, &graphicsmaster_pcmcia_ops, 0, 2);

	return ret;
}
