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

static struct irqs {
	int irq;
	const char *str;
} irqs[] = {
	{ IRQ_GPIO_FREEBIRD_CF_CD,  "CF_CD"   },
	{ IRQ_GPIO_FREEBIRD_CF_BVD, "CF_BVD1" },
};

static int freebird_pcmcia_init(struct pcmcia_init *init){
  int i, res;

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

  /* Set transition detect */
  set_irq_type(IRQ_GPIO_FREEBIRD_CF_IRQ, IRQT_FALLING);

  /* Register interrupts */
  for (i = 0; i < ARRAY_SIZE(irqs); i++) {
    set_irq_type(irqs[i].irq, IRQT_NOEDGE);
    res = request_irq(irqs[i].irq, init->handler, SA_INTERRUPT,
    		      irqs[i].str, NULL);
    if (res)
      goto irq_err;
  }

  init->socket_irq[0] = IRQ_GPIO_FREEBIRD_CF_IRQ;

  /* There's only one slot, but it's "Slot 1": */
  return 2;

irq_err:
  printk(KERN_ERR "%s: request for IRQ%d failed (%d)\n",
	 __FUNCTION__, irqs[i].irq, res);

  while (i--)
    free_irq(irqs[i].irq, NULL);

  return res;
}

static int freebird_pcmcia_shutdown(void)
{
  int i;

  /* disable IRQs */
  for (i = 0; i < ARRAY_SIZE(irqs); i++)
    free_irq(irqs[i].irq, NULL);

  /* Disable CF card */
  LINKUP_PRC = 0x40;  /* SSP=1   SOE=0 */
  mdelay(100);

  return 0;
}

static void freebird_pcmcia_socket_state(int sock, struct pcmcia_state *state)
{
  unsigned long levels = LINKUP_PRS;
//printk("LINKUP_PRS=%x\n",levels);

  if (sock == 0) {
    state->detect = ((levels & (LINKUP_CD1 | LINKUP_CD2))==0)?1:0;
    state->ready  = (levels & LINKUP_RDY)?1:0;
    state->bvd1   = (levels & LINKUP_BVD1)?1:0;
    state->bvd2   = (levels & LINKUP_BVD2)?1:0;
    state->wrprot = 0; /* Not available on Assabet. */
    state->vs_3v  = 1;  /* Can only apply 3.3V on Assabet. */
    state->vs_Xv  = 0;
  }
}

static int freebird_pcmcia_configure_socket(int sock, const struct pcmcia_configure
					   *configure)
{
  unsigned long value, flags;

  if(sock>1) return -1;

  if(sock==1) return 0;

  local_irq_save(flags);

  value = 0xc0;   /* SSP=1  SOE=1  CFE=1 */

  switch(configure->vcc){
  case 0:

    break;

  case 50:
    printk(KERN_WARNING "%s(): CS asked for 5V, applying 3.3V...\n",
	   __FUNCTION__);

  case 33:  /* Can only apply 3.3V to the CF slot. */
    value |= LINKUP_S1;
    break;

  default:
    printk(KERN_ERR "%s(): unrecognized Vcc %u\n", __FUNCTION__,
	   configure->vcc);
    local_irq_restore(flags);
    return -1;
  }

  if (configure->reset)
  value = (configure->reset) ? (value | LINKUP_RESET) : (value & ~LINKUP_RESET);

  /* Silently ignore Vpp, output enable, speaker enable. */

  LINKUP_PRC = value;
//printk("LINKUP_PRC=%x\n",value);
  local_irq_restore(flags);

  return 0;
}

static int freebird_pcmcia_socket_init(int sock)
{
  if (sock == 1) {
    set_irq_type(IRQ_GPIO_FREEBIRD_CF_CD, IRQT_BOTHEDGE);
    set_irq_type(IRQ_GPIO_FREEBIRD_CF_BVD, IRQT_BOTHEDGE);
  }
  return 0;
}

static int freebird_pcmcia_socket_suspend(int sock)
{
  if (sock == 1) {
    set_irq_type(IRQ_GPIO_FREEBIRD_CF_CD, IRQT_NOEDGE);
    set_irq_type(IRQ_GPIO_FREEBIRD_CF_BVD, IRQT_NOEDGE);
  }
  return 0;
}

static struct pcmcia_low_level freebird_pcmcia_ops = {
  .owner		= THIS_MODULE,
  .init			= freebird_pcmcia_init,
  .shutdown		= freebird_pcmcia_shutdown,
  .socket_state		= freebird_pcmcia_socket_state,
  .configure_socket	= freebird_pcmcia_configure_socket,

  .socket_init		= freebird_pcmcia_socket_init,
  .socket_suspend	= freebird_pcmcia_socket_suspend,
};

int __init pcmcia_freebird_init(struct device *dev)
{
	int ret = -ENODEV;

	if (machine_is_freebird())
		ret = sa1100_register_pcmcia(&freebird_pcmcia_ops, dev);

	return ret;
}

void __exit pcmcia_freebird_exit(struct device *dev)
{
	sa1100_unregister_pcmcia(&freebird_pcmcia_ops, dev);
}
