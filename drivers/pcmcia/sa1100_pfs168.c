#warning	"REVISIT_PFS168: Need to verify and test GPIO power encodings."
/*
 * drivers/pcmcia/sa1100_pfs168.c
 *
 * PFS168 PCMCIA specific routines
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/irq.h>

#include "sa1111_generic.h"

static int pfs168_pcmcia_init(struct sa1100_pcmcia_socket *skt)
{
  /* TPS2211 to standby mode: */
  PA_DWR &= ~(GPIO_GPIO0 | GPIO_GPIO1 | GPIO_GPIO2 | GPIO_GPIO3);

  /* Set GPIO_A<3:0> to be outputs for PCMCIA (socket 0) power controller: */
  PA_DDR &= ~(GPIO_GPIO0 | GPIO_GPIO1 | GPIO_GPIO2 | GPIO_GPIO3);

  return sa1111_pcmcia_init(skt);
}

static int
pfs168_pcmcia_configure_socket(struct sa1100_pcmcia_socket *skt,
			       const socket_state_t *state)
{
  unsigned int pa_dwr_mask = 0, pa_dwr_set = 0;
  int ret;

  /* PFS168 uses the Texas Instruments TPS2211 for PCMCIA (socket 0) voltage control only,
   * with the following connections:
   *
   *   TPS2211      PFS168
   *
   *    -VCCD0      SA-1111 GPIO A<0>
   *    -VCCD0      SA-1111 GPIO A<1>
   *     VPPD0      SA-1111 GPIO A<2>
   *     VPPD0      SA-1111 GPIO A<2>
   *
   */

  switch (skt->nr) {
  case 0:
    pa_dwr_mask = GPIO_GPIO0 | GPIO_GPIO1 | GPIO_GPIO2 | GPIO_GPIO3;

    switch (state->Vcc) {
    default:
    case 0:	pa_dwr_set = 0;			break;
    case 33:	pa_dwr_set = GPIO_GPIO0;	break;
    case 50:	pa_dwr_set = GPIO_GPIO1;	break;
    }

    switch (state->Vpp) {
    case 0:
      break;

    case 120:
      printk(KERN_ERR "%s(): PFS-168 does not support VPP %uV\n",
	     __FUNCTION__, state->Vpp / 10);
      return -1;
      break;

    default:
      if (state->Vpp == state->Vcc)
        pa_dwr_set |= GPIO_GPIO3;
      else {
	printk(KERN_ERR "%s(): unrecognized VPP %u\n", __FUNCTION__,
	       state->Vpp);
	return -1;
      }
    }
    break;

  case 1:
    pa_dwr_mask = 0;
    pa_dwr_set = 0;

    switch (conf->vcc) {
    case 0:
    case 33:
      break;

    case 50:
      printk(KERN_ERR "%s(): PFS-168 CompactFlash socket does not support VCC %uV\n",
	     __FUNCTION__, state->Vcc / 10);
      return -1;

    default:
      printk(KERN_ERR "%s(): unrecognized VCC %u\n", __FUNCTION__,
	     state->Vcc);
      return -1;
    }

    if (state->Vpp != state->Vcc && state->Vpp != 0) {
      printk(KERN_ERR "%s(): CompactFlash socket does not support VPP %uV\n",
	     __FUNCTION__, state->Vpp / 10);
      return -1;
    }
    break;
  }

  ret = sa1111_pcmcia_configure_socket(skt, state);
  if (ret == 0) {
    unsigned long flags;

    local_irq_save(flags);
    PA_DWR = (PA_DWR & ~pa_dwr_mask) | pa_dwr_set;
    local_irq_restore(flags);
  }

  return 0;
}

static struct pcmcia_low_level pfs168_pcmcia_ops = {
	.owner			= THIS_MODULE,
	.hw_init		= pfs168_pcmcia_hw_init,
	.hw_shutdown		= sa1111_pcmcia_hw_shutdown,
	.socket_state		= sa1111_pcmcia_socket_state,
	.configure_socket	= pfs168_pcmcia_configure_socket,
	.socket_init		= sa1111_pcmcia_socket_init,
	.socket_suspend		= sa1111_pcmcia_socket_suspend,
};

int __init pcmcia_pfs168_init(struct device *dev)
{
	int ret = -ENODEV;

	if (machine_is_pfs168())
		ret = sa11xx_drv_pcmcia_probe(dev, &pfs168_pcmcia_ops, 0, 2);

	return ret;
}
