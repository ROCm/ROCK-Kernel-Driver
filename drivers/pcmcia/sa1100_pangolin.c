/*
 * drivers/pcmcia/sa1100_pangolin.c
 *
 * PCMCIA implementation routines for Pangolin
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

#ifndef CONFIG_SA1100_PANGOLIN_PCMCIA_IDE
#define PANGOLIN_SOCK	1
#else
#define PANGOLIN_SOCK	0
#endif

static int pangolin_pcmcia_init(struct pcmcia_init *init){
  int res;

#ifndef CONFIG_SA1100_PANGOLIN_PCMCIA_IDE
  /* Enable PCMCIA bus: */
  GPCR = GPIO_PCMCIA_BUS_ON;
#endif

  /* Set transition detect */
  set_irq_type(IRQ_PCMCIA_CD, IRQT_NOEDGE);
  set_irq_type(IRQ_PCMCIA_IRQ, IRQT_FALLING);

  /* Register interrupts */
  res = request_irq(IRQ_PCMCIA_CD, init->handler, SA_INTERRUPT,
		    "PCMCIA_CD", NULL);
  if (res >= 0)
    /* There's only one slot, but it's "Slot 1": */
    return 2;

irq_err:
  printk(KERN_ERR "%s: request for IRQ%d failed (%d)\n",
	 __FUNCTION__, IRQ_PCMCIA_CD, res);

  return res;
}

static int pangolin_pcmcia_shutdown(void)
{
  /* disable IRQs */
  free_irq(IRQ_PCMCIA_CD, NULL);
#ifndef CONFIG_SA1100_PANGOLIN_PCMCIA_IDE
    /* Disable PCMCIA bus: */
    GPSR = GPIO_PCMCIA_BUS_ON;
#endif
  return 0;
}

static void pangolin_pcmcia_socket_state(int sock, struct pcmcia_state *state)
{
  unsigned long levels = GPLR;;

  if (sock == PANGOLIN_SOCK) {
    state->detect=((levels & GPIO_PCMCIA_CD)==0)?1:0;
    state->ready=(levels & GPIO_PCMCIA_IRQ)?1:0;
    state->bvd1=1; /* Not available on Pangolin. */
    state->bvd2=1; /* Not available on Pangolin. */
    state->wrprot=0; /* Not available on Pangolin. */
    state->vs_3v=1;  /* Can only apply 3.3V on Pangolin. */
    state->vs_Xv=0;
  }
}

static int pangolin_pcmcia_get_irq_info(struct pcmcia_irq_info *info){

  if(info->sock>1) return -1;
#ifndef CONFIG_SA1100_PANGOLIN_PCMCIA_IDE
  if(info->sock==1)
	info->irq=IRQ_PCMCIA_IRQ;
#else
  if(info->sock==0)
        info->irq=IRQ_PCMCIA_IRQ;
#endif
  return 0;
}

static int pangolin_pcmcia_configure_socket(int sock, const struct pcmcia_configure
					   *configure)
{
  unsigned long value, flags;

  if(sock>1) return -1;
#ifndef CONFIG_SA1100_PANGOLIN_PCMCIA_IDE
  if(sock==0) return 0;
#endif
  local_irq_save(flags);

  /* Murphy: BUS_ON different from POWER ? */

  switch(configure->vcc){
  case 0:
    break;
#ifndef CONFIG_SA1100_PANGOLIN_PCMCIA_IDE
  case 50:
    printk(KERN_WARNING "%s(): CS asked for 5V, applying 3.3V...\n",
	   __FUNCTION__);
  case 33:  /* Can only apply 3.3V to the CF slot. */
    break;
#else
  case 50:
    printk(KERN_WARNING "%s(): CS asked for 5V, determinded by jumper setting...\n", __FUNCTION__);
    break;
  case 33:
    printk(KERN_WARNING "%s(): CS asked for 3.3V, determined by jumper setting...\n", __FUNCTION__);
    break;
#endif
  default:
    printk(KERN_ERR "%s(): unrecognized Vcc %u\n", __FUNCTION__,
	   configure->vcc);
    local_irq_restore(flags);
    return -1;
  }
#ifdef CONFIG_SA1100_PANGOLIN_PCMCIA_IDE
  /* reset & unreset request */
  if(sock==0) {
	if(configure->reset) {
		GPSR |= GPIO_PCMCIA_RESET;
	} else {
		GPCR |= GPIO_PCMCIA_RESET;
	}
  }
#endif
  /* Silently ignore Vpp, output enable, speaker enable. */
  local_irq_restore(flags);
  return 0;
}

static int pangolin_pcmcia_socket_init(int sock)
{
  if (sock == 1)
    set_irq_type(IRQ_PCmCIA_CD, IRQT_BOTHEDGE);
  return 0;
}

static int pangolin_pcmcia_socket_suspend(int sock)
{
  if (sock == 1)
    set_irq_type(IRQ_PCmCIA_CD, IRQT_NOEDGE);
  return 0;
}

static struct pcmcia_low_level pangolin_pcmcia_ops = { 
  .owner		= THIS_MODULE,
  .init			= pangolin_pcmcia_init,
  .shutdown		= pangolin_pcmcia_shutdown,
  .socket_state		= pangolin_pcmcia_socket_state,
  .get_irq_info		= pangolin_pcmcia_get_irq_info,
  .configure_socket	= pangolin_pcmcia_configure_socket,

  .socket_init		= pangolin_pcmcia_socket_init,
  socket_suspend,	pangolin_pcmcia_socket_suspend,
};

int __init pcmcia_pangolin_init(struct device *dev)
{
	int ret = -ENODEV;

	if (machine_is_pangolin())
		ret = sa1100_register_pcmcia(&pangolin_pcmcia_ops, dev);

	return ret;
}

void __exit pcmcia_pangolin_exit(struct device *dev)
{
	sa1100_unregister_pcmcia(&pangolin_pcmcia_ops, dev);
}

