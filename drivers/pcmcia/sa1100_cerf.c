/*
 * drivers/pcmcia/sa1100_cerf.c
 *
 * PCMCIA implementation routines for CerfBoard
 * Based off the Assabet.
 *
 */
#include <linux/kernel.h>
#include <linux/sched.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/arch/pcmcia.h>


static int cerf_pcmcia_init(struct pcmcia_init *init){
  int irq, res;

  /* Enable CF bus: */
//  BCR_clear(BCR_CF_BUS_OFF);

  /* All those are inputs */
  GPDR &= ~(GPIO_CF_CD | GPIO_CF_BVD2 | GPIO_CF_BVD1 | GPIO_CF_IRQ);

  /* Set transition detect */
  set_GPIO_IRQ_edge( GPIO_CF_CD|GPIO_CF_BVD2|GPIO_CF_BVD1, GPIO_BOTH_EDGES );
  set_GPIO_IRQ_edge( GPIO_CF_IRQ, GPIO_FALLING_EDGE );

  /* Register interrupts */
  irq = IRQ_GPIO_CF_CD;
  res = request_irq( irq, init->handler, SA_INTERRUPT, "CF_CD", NULL );
  if( res < 0 ) goto irq_err;
  irq = IRQ_GPIO_CF_BVD2;
  res = request_irq( irq, init->handler, SA_INTERRUPT, "CF_BVD2", NULL );
  if( res < 0 ) goto irq_err;
  irq = IRQ_GPIO_CF_BVD1;
  res = request_irq( irq, init->handler, SA_INTERRUPT, "CF_BVD1", NULL );
  if( res < 0 ) goto irq_err;

  /* There's only one slot, but it's "Slot 1": */
  return 2;

irq_err:
  printk( KERN_ERR "%s: Request for IRQ %lu failed\n", __FUNCTION__, irq );
  return -1;
}

static int cerf_pcmcia_shutdown(void)
{
  /* disable IRQs */
  free_irq( IRQ_GPIO_CF_CD, NULL );
  free_irq( IRQ_GPIO_CF_BVD2, NULL );
  free_irq( IRQ_GPIO_CF_BVD1, NULL );
  
  /* Disable CF bus: */
//  BCR_set(BCR_CF_BUS_OFF);

  return 0;
}

static int cerf_pcmcia_socket_state(struct pcmcia_state_array
				       *state_array){
  unsigned long levels;

  if(state_array->size<2) return -1;

  memset(state_array->state, 0, 
	 (state_array->size)*sizeof(struct pcmcia_state));

  levels=GPLR;

  state_array->state[1].detect=((levels & GPIO_CF_CD)==0)?1:0;

  state_array->state[1].ready=(levels & GPIO_CF_IRQ)?1:0;

  state_array->state[1].bvd1=(levels & GPIO_CF_BVD1)?1:0;

  state_array->state[1].bvd2=(levels & GPIO_CF_BVD2)?1:0;

  state_array->state[1].wrprot=0; /* Not available on Assabet. */

  state_array->state[1].vs_3v=1;  /* Can only apply 3.3V on Assabet. */

  state_array->state[1].vs_Xv=0;

  return 1;
}

static int cerf_pcmcia_get_irq_info(struct pcmcia_irq_info *info){

  if(info->sock>1) return -1;

  if(info->sock==1)
    info->irq=IRQ_GPIO_CF_IRQ;

  return 0;
}

static int cerf_pcmcia_configure_socket(const struct pcmcia_configure
					   *configure)
{
  unsigned long value, flags;

  if(configure->sock>1) return -1;

  if(configure->sock==0) return 0;

  save_flags_cli(flags);

//  value = BCR_value;

  switch(configure->vcc){
  case 0:
//    value &= ~BCR_CF_PWR;
    break;

  case 50:
    printk(KERN_WARNING "%s(): CS asked for 5V, applying 3.3V...\n",
	   __FUNCTION__);

  case 33:  /* Can only apply 3.3V to the CF slot. */
//    value |= BCR_CF_PWR;
    break;

  default:
    printk(KERN_ERR "%s(): unrecognized Vcc %u\n", __FUNCTION__,
	   configure->vcc);
    restore_flags(flags);
    return -1;
  }

//  value = (configure->reset) ? (value | BCR_CF_RST) : (value & ~BCR_CF_RST);

  /* Silently ignore Vpp, output enable, speaker enable. */

//  BCR = BCR_value = value;

  restore_flags(flags);

  return 0;
}

struct pcmcia_low_level cerf_pcmcia_ops = { 
  cerf_pcmcia_init,
  cerf_pcmcia_shutdown,
  cerf_pcmcia_socket_state,
  cerf_pcmcia_get_irq_info,
  cerf_pcmcia_configure_socket
};

