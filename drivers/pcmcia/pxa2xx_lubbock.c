/*
 * linux/drivers/pcmcia/pxa2xx_lubbock.c
 *
 * Author:	George Davis
 * Created:	Jan 10, 2002
 * Copyright:	MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Originally based upon linux/drivers/pcmcia/sa1100_neponset.c
 *
 * Lubbock PCMCIA specific routines.
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/hardware/sa1111.h>

#include "sa1111_generic.h"

static int
lubbock_pcmcia_configure_socket(struct soc_pcmcia_socket *skt,
				const socket_state_t *state)
{
  unsigned long flags, gpio, misc_wr;
  int ret = 1;
  struct pcmcia_state new_state;

  local_irq_save(flags);

  gpio = PA_DWR;
  misc_wr = LUB_MISC_WR;

  /* Lubbock uses the Maxim MAX1602, with the following connections:
   *
   * Socket 0 (PCMCIA):
   *	MAX1602	Lubbock		Register
   *	Pin	Signal
   *	-----	-------		----------------------
   *	A0VPP	S0_PWR0		SA-1111 GPIO A<0>
   *	A1VPP	S0_PWR1		SA-1111 GPIO A<1>
   *	A0VCC	S0_PWR2		SA-1111 GPIO A<2>
   *	A1VCC	S0_PWR3		SA-1111 GPIO A<3>
   *	VX	VCC
   *	VY	+3.3V
   *	12IN	+12V
   *	CODE	+3.3V		Cirrus  Code, CODE = High (VY)
   *
   * Socket 1 (CF):
   *	MAX1602	Lubbock		Register
   *	Pin	Signal
   *	-----	-------		----------------------
   *	A0VPP	GND		VPP is not connected
   *	A1VPP	GND		VPP is not connected
   *	A0VCC	S1_PWR0		MISC_WR<14>
   *	A1VCC	S1_PWR0		MISC_WR<15>
   *	VX	VCC
   *	VY	+3.3V
   *	12IN	GND		VPP is not connected
   *	CODE	+3.3V		Cirrus  Code, CODE = High (VY)
   *
   */

again:
  switch(skt->nr){
  case 0:

    switch(state->Vcc){
    case 0:
      gpio &= ~(GPIO_bit(2) | GPIO_bit(3));
      break;

    case 33:
      gpio = (gpio & ~(GPIO_bit(2) | GPIO_bit(3))) | GPIO_bit(3);
      break;

    case 50:
      gpio = (gpio & ~(GPIO_bit(2) | GPIO_bit(3))) | GPIO_bit(2);
      break;

    default:
      printk(KERN_ERR "%s(): unrecognized Vcc %u\n", __FUNCTION__, state->Vcc);
      ret = -1;
    }

    switch(state->Vpp){
    case 0:
      gpio &= ~(GPIO_bit(0) | GPIO_bit(1));
      break;

    case 120:
      gpio = (gpio & ~(GPIO_bit(0) | GPIO_bit(1))) | GPIO_bit(1);
      break;

    default:
      /* REVISIT: I'm not sure about this? Is this correct?
         Is it always safe or do we have potential problems
         with bogus combinations of Vcc and Vpp settings? */
      if(state->Vpp == state->Vcc)
        gpio = (gpio & ~(GPIO_bit(0) | GPIO_bit(1))) | GPIO_bit(0);
      else {
	printk(KERN_ERR "%s(): unrecognized Vpp %u\n", __FUNCTION__, state->Vpp);
	ret = -1;
	break;
      }
    }

    break;

  case 1:
    switch(state->Vcc){
    case 0:
      misc_wr &= ~((1 << 15) | (1 << 14));
      break;

    case 33:
      misc_wr = (misc_wr & ~(1 << 15)) | (1 << 14);
      gpio = (gpio & ~(GPIO_bit(2) | GPIO_bit(3))) | GPIO_bit(2);
      break;

    case 50:
      misc_wr = (misc_wr & ~(1 << 15)) | (1 << 14);
      break;

    default:
      printk(KERN_ERR "%s(): unrecognized Vcc %u\n", __FUNCTION__, state->Vcc);
      ret = -1;
      break;
    }

    if(state->Vpp!=state->Vcc && state->Vpp!=0){
      printk(KERN_ERR "%s(): CF slot cannot support Vpp %u\n", __FUNCTION__, state->Vpp);
      ret = -1;
      break;
    }

    break;

  default:
    ret = -1;
  }

  if (ret >= 0) {
    sa1111_pcmcia_configure_socket(skt, state);
    LUB_MISC_WR = misc_wr;
    PA_DWR = gpio;
  }

  if (ret > 0) {
    ret = 0;
#if 1
    /*
     * HACK ALERT:
     * We can't sense the voltage properly on Lubbock before actually
     * applying some power to the socket (catch 22).
     * Resense the socket Voltage Sense pins after applying socket power.
     */
    sa1111_pcmcia_socket_state(skt, &new_state);
    if (state->Vcc == 33 && !new_state.vs_3v && !new_state.vs_Xv) {
      /* Switch to 5V,  Configure socket with 5V voltage */
      PA_DWR &= ~(GPIO_bit(0) | GPIO_bit(1) | GPIO_bit(2) | GPIO_bit(3));
      PA_DDR &= ~(GPIO_bit(0) | GPIO_bit(1) | GPIO_bit(2) | GPIO_bit(3));
      /* We need to hack around the const qualifier as well to keep this
         ugly workaround localized and not force it to the rest of the code.
         Barf bags avaliable in the seat pocket in front of you! */
      ((socket_state_t *)state)->Vcc = 50;
      ((socket_state_t *)state)->Vpp = 50;
      goto again;
    }
#endif
  }

  local_irq_restore(flags);
  return ret;
}

static struct pcmcia_low_level lubbock_pcmcia_ops = {
	.owner			= THIS_MODULE,
	.hw_init		= sa1111_pcmcia_hw_init,
	.hw_shutdown		= sa1111_pcmcia_hw_shutdown,
	.socket_state		= sa1111_pcmcia_socket_state,
	.configure_socket	= lubbock_pcmcia_configure_socket,
	.socket_init		= sa1111_pcmcia_socket_init,
	.socket_suspend		= sa1111_pcmcia_socket_suspend,
	.first			= 0,
	.nr			= 2,
};

#include "pxa2xx_base.h"

int __init pcmcia_lubbock_init(struct device *dev)
{
	int ret = -ENODEV;

	if (machine_is_lubbock()) {
		/*
		 * Set GPIO_A<3:0> to be outputs for the MAX1600,
		 * and switch to standby mode.
		 */
		PA_DWR = 0;
		PA_DDR = 0;
		PA_SDR = 0;
		PA_SSR = 0;

		/* Set CF Socket 1 power to standby mode. */
		LUB_MISC_WR &= ~(GPIO_bit(15) | GPIO_bit(14));

		dev->platform_data = &lubbock_pcmcia_ops;
		ret = pxa2xx_drv_pcmcia_probe(dev);
	}

	return ret;
}
