/*
 * drivers/pcmcia/sa1100_pangolin.c
 *
 * PCMCIA implementation routines for simpad
 *
 */
#include <linux/kernel.h>
#include <linux/sched.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/arch/pcmcia.h>

static int simpad_pcmcia_init(struct pcmcia_init *init){
  int irq, res;

  /* set GPIO_CF_CD & GPIO_CF_IRQ as inputs */
  GPDR &= ~(GPIO_CF_CD|GPIO_CF_IRQ);
  //init_simpad_cs3();
  printk("\nCS3:%x\n",cs3_shadow);
  PCMCIA_setbit(PCMCIA_RESET);
  PCMCIA_clearbit(PCMCIA_BUFF_DIS);

  /* Set transition detect */
  set_GPIO_IRQ_edge( GPIO_CF_CD, GPIO_BOTH_EDGES );
  set_GPIO_IRQ_edge( GPIO_CF_IRQ, GPIO_FALLING_EDGE );

  /* Register interrupts */
  irq = IRQ_GPIO_CF_CD;
  res = request_irq( irq, init->handler, SA_INTERRUPT, "CF_CD", NULL );
  if( res < 0 ) goto irq_err;

  /* There's only one slot, but it's "Slot 1": */
  return 2;

irq_err:
  printk( KERN_ERR "%s: Request for IRQ %lu failed\n", __FUNCTION__, irq );
  return -1;
}

static int simpad_pcmcia_shutdown(void)
{
  /* disable IRQs */
  free_irq( IRQ_GPIO_CF_CD, NULL );

  /* Disable CF bus: */

  PCMCIA_setbit(PCMCIA_BUFF_DIS);
  PCMCIA_clearbit(PCMCIA_RESET);
  return 0;
}

static int simpad_pcmcia_socket_state(struct pcmcia_state_array
				       *state_array)
{
  unsigned long levels;

  if(state_array->size<2) return -1;

  memset(state_array->state, 0,
	 (state_array->size)*sizeof(struct pcmcia_state));

  levels=GPLR;

  state_array->state[1].detect=((levels & GPIO_CF_CD)==0)?1:0;

  state_array->state[1].ready=(levels & GPIO_CF_IRQ)?1:0;

  state_array->state[1].bvd1=1; /* Not available on Simpad. */

  state_array->state[1].bvd2=1; /* Not available on Simpad. */

  state_array->state[1].wrprot=0; /* Not available on Simpad. */

  state_array->state[1].vs_3v=1;  /* Can only apply 3.3V on Simpad. */

  state_array->state[1].vs_Xv=0;

  return 1;
}

static int simpad_pcmcia_get_irq_info(struct pcmcia_irq_info *info){

  if(info->sock>1) return -1;

  if(info->sock==1)
    info->irq=IRQ_GPIO_CF_IRQ;

  return 0;
}

static int simpad_pcmcia_configure_socket(const struct pcmcia_configure
					   *configure)
{
  unsigned long value, flags;

  if(configure->sock>1) return -1;

  if(configure->sock==0) return 0;

  save_flags_cli(flags);

  /* Murphy: BUS_ON different from POWER ? */

  switch(configure->vcc){
  case 0:
    PCMCIA_setbit(PCMCIA_BUFF_DIS);
    break;

  case 33:
  case 50:
    PCMCIA_setbit(PCMCIA_BUFF_DIS);
    break;

  default:
    printk(KERN_ERR "%s(): unrecognized Vcc %u\n", __FUNCTION__,
	   configure->vcc);
    restore_flags(flags);
    return -1;
  }

  /* Silently ignore Vpp, output enable, speaker enable. */

  restore_flags(flags);

  return 0;
}

struct pcmcia_low_level simpad_pcmcia_ops = {
  simpad_pcmcia_init,
  simpad_pcmcia_shutdown,
  simpad_pcmcia_socket_state,
  simpad_pcmcia_get_irq_info,
  simpad_pcmcia_configure_socket
};

