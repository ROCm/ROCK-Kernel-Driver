/*
 * drivers/pcmcia/sa1100_h3600.c
 *
 * PCMCIA implementation routines for H3600
 *
 */
#include <linux/kernel.h>
#include <linux/sched.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/arch/pcmcia.h>


static int h3600_pcmcia_init(struct pcmcia_init *init){
  int irq, res;

  /* Enable CF bus: */
  set_h3600_egpio(EGPIO_H3600_OPT_NVRAM_ON);
  clr_h3600_egpio(EGPIO_H3600_OPT_RESET);

  /* All those are inputs */
  GPDR &= ~(GPIO_H3600_PCMCIA_CD0 | GPIO_H3600_PCMCIA_CD1 | GPIO_H3600_PCMCIA_IRQ0| GPIO_H3600_PCMCIA_IRQ1);

  /* Set transition detect */
  set_GPIO_IRQ_edge( GPIO_H3600_PCMCIA_CD0 | GPIO_H3600_PCMCIA_CD1, GPIO_BOTH_EDGES );
  set_GPIO_IRQ_edge( GPIO_H3600_PCMCIA_IRQ0| GPIO_H3600_PCMCIA_IRQ1, GPIO_FALLING_EDGE );

  /* Register interrupts */
  irq = IRQ_GPIO_H3600_PCMCIA_CD0;
  res = request_irq( irq, init->handler, SA_INTERRUPT, "PCMCIA_CD0", NULL );
  if( res < 0 ) goto irq_err;
  irq = IRQ_GPIO_H3600_PCMCIA_CD1;
  res = request_irq( irq, init->handler, SA_INTERRUPT, "PCMCIA_CD1", NULL );
  if( res < 0 ) goto irq_err;

  return 2;

irq_err:
  printk( KERN_ERR __FUNCTION__ ": Request for IRQ %u failed\n", irq );
  return -1;
}

static int h3600_pcmcia_shutdown(void)
{
  /* disable IRQs */
  free_irq( IRQ_GPIO_H3600_PCMCIA_CD0, NULL );
  free_irq( IRQ_GPIO_H3600_PCMCIA_CD1, NULL );
  
  /* Disable CF bus: */
  clr_h3600_egpio(EGPIO_H3600_OPT_NVRAM_ON|EGPIO_H3600_OPT_ON);
  set_h3600_egpio(EGPIO_H3600_OPT_RESET);

  return 0;
}

static int h3600_pcmcia_socket_state(struct pcmcia_state_array
				       *state_array){
  unsigned long levels;

  if(state_array->size<2) return -1;

  memset(state_array->state, 0, 
	 (state_array->size)*sizeof(struct pcmcia_state));

  levels=GPLR;

  state_array->state[0].detect=((levels & GPIO_H3600_PCMCIA_CD0)==0)?1:0;
  state_array->state[0].ready=(levels & GPIO_H3600_PCMCIA_IRQ0)?1:0;
  state_array->state[0].bvd1= 0;
  state_array->state[0].bvd2= 0;
  state_array->state[0].wrprot=0; /* Not available on H3600. */
  state_array->state[0].vs_3v=0;
  state_array->state[0].vs_Xv=0;

  state_array->state[1].detect=((levels & GPIO_H3600_PCMCIA_CD1)==0)?1:0;
  state_array->state[1].ready=(levels & GPIO_H3600_PCMCIA_IRQ1)?1:0;
  state_array->state[1].bvd1=0;
  state_array->state[1].bvd2=0;
  state_array->state[1].wrprot=0; /* Not available on H3600. */
  state_array->state[1].vs_3v=0;
  state_array->state[1].vs_Xv=0;

  return 1;
}

static int h3600_pcmcia_get_irq_info(struct pcmcia_irq_info *info){

  switch (info->sock) {
  case 0:
    info->irq=IRQ_GPIO_H3600_PCMCIA_IRQ0;
    break;
  case 1:
    info->irq=IRQ_GPIO_H3600_PCMCIA_IRQ1;
    break;
  default:
    return -1;
  }
  return 0;
}

static int h3600_pcmcia_configure_socket(const struct pcmcia_configure
					   *configure)
{
  unsigned long flags;

  if(configure->sock>1) return -1;

  save_flags_cli(flags);

  switch (configure->vcc) {
  case 0:
    clr_h3600_egpio(EGPIO_H3600_OPT_ON);
    break;

  case 33:
  case 50:
    set_h3600_egpio(EGPIO_H3600_OPT_ON);
    break;

  default:
    printk(KERN_ERR "%s(): unrecognized Vcc %u\n", __FUNCTION__,
	   configure->vcc);
    restore_flags(flags);
    return -1;
  }

  if (configure->reset)
    set_h3600_egpio(EGPIO_H3600_CARD_RESET);
  else
    clr_h3600_egpio(EGPIO_H3600_CARD_RESET);

  /* Silently ignore Vpp, output enable, speaker enable. */

  restore_flags(flags);

  return 0;
}

struct pcmcia_low_level h3600_pcmcia_ops = { 
  h3600_pcmcia_init,
  h3600_pcmcia_shutdown,
  h3600_pcmcia_socket_state,
  h3600_pcmcia_get_irq_info,
  h3600_pcmcia_configure_socket
};

