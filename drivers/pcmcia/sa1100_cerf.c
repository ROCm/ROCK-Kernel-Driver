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

  GPDR &= ~(GPIO_CF_CD | GPIO_CF_BVD2 | GPIO_CF_BVD1 | GPIO_CF_IRQ);
  GPDR |= (GPIO_CF_RESET);

  set_GPIO_IRQ_edge( GPIO_CF_CD|GPIO_CF_BVD2|GPIO_CF_BVD1, GPIO_BOTH_EDGES );
  set_GPIO_IRQ_edge( GPIO_CF_IRQ, GPIO_FALLING_EDGE );

  irq = IRQ_GPIO_CF_CD;
  res = request_irq( irq, init->handler, SA_INTERRUPT, "CF_CD", NULL );
  if( res < 0 ) goto irq_err;
  irq = IRQ_GPIO_CF_BVD2;
  res = request_irq( irq, init->handler, SA_INTERRUPT, "CF_BVD2", NULL );
  if( res < 0 ) goto irq_err;
  irq = IRQ_GPIO_CF_BVD1;
  res = request_irq( irq, init->handler, SA_INTERRUPT, "CF_BVD1", NULL );
  if( res < 0 ) goto irq_err;

  return 2;

irq_err:
  printk( KERN_ERR "%s: Request for IRQ %lu failed\n", __FUNCTION__, irq );
  return -1;
}

static int cerf_pcmcia_shutdown(void)
{
  free_irq( IRQ_GPIO_CF_CD, NULL );
  free_irq( IRQ_GPIO_CF_BVD2, NULL );
  free_irq( IRQ_GPIO_CF_BVD1, NULL );

  return 0;
}

static int cerf_pcmcia_socket_state(struct pcmcia_state_array
				       *state_array){
  unsigned long levels;
#ifdef CONFIG_SA1100_CERF_CPLD
  int i = 0;
#else
  int i = 1;
#endif

  if(state_array->size<2) return -1;

  memset(state_array->state, 0, 
	 (state_array->size)*sizeof(struct pcmcia_state));

  levels=GPLR;

  state_array->state[i].detect=((levels & GPIO_CF_CD)==0)?1:0;

  state_array->state[i].ready=(levels & GPIO_CF_IRQ)?1:0;

  state_array->state[i].bvd1=(levels & GPIO_CF_BVD1)?1:0;

  state_array->state[i].bvd2=(levels & GPIO_CF_BVD2)?1:0;

  state_array->state[i].wrprot=0;

  state_array->state[i].vs_3v=1;

  state_array->state[i].vs_Xv=0;

  return 1;
}

static int cerf_pcmcia_get_irq_info(struct pcmcia_irq_info *info){

  if(info->sock>1) return -1;

#ifdef CONFIG_SA1100_CERF_CPLD
  if(info->sock==0)
#else
  if(info->sock==1)
#endif
    info->irq=IRQ_GPIO_CF_IRQ;

  return 0;
}

static int cerf_pcmcia_configure_socket(const struct pcmcia_configure
					   *configure)
{
  unsigned long flags;

  if(configure->sock>1)
    return -1;

#ifdef CONFIG_SA1100_CERF_CPLD
  if(configure->sock==1)
#else
  if(configure->sock==0)
#endif
    return 0;

  save_flags_cli(flags);

  switch(configure->vcc){
  case 0:
    break;

  case 50:
  case 33:
#ifdef CONFIG_SA1100_CERF_CPLD
     GPDR |= GPIO_PWR_SHUTDOWN;
     GPCR |= GPIO_PWR_SHUTDOWN;
#endif
     break;

  default:
    printk(KERN_ERR "%s(): unrecognized Vcc %u\n", __FUNCTION__,
	   configure->vcc);
    restore_flags(flags);
    return -1;
  }

  if(configure->reset)
  {
#ifdef CONFIG_SA1100_CERF_CPLD
    GPDR |= GPIO_CF_RESET;
    GPSR |= GPIO_CF_RESET;
#endif
  }
  else
  {
#ifdef CONFIG_SA1100_CERF_CPLD
    GPDR |= GPIO_CF_RESET;
    GPCR |= GPIO_CF_RESET;
#endif
  }

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

