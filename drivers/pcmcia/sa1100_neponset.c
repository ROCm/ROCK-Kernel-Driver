/*
 * drivers/pcmcia/sa1100_neponset.c
 *
 * Neponset PCMCIA specific routines
 *
 */
#include <linux/kernel.h>
#include <linux/sched.h>

#include <asm/delay.h>
#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/arch/pcmcia.h>
#include <asm/arch/assabet.h>

static int neponset_pcmcia_init(struct pcmcia_init *init){
  int return_val=0;

  /* Set GPIO_A<3:0> to be outputs for PCMCIA/CF power controller: */
  PA_DDR &= ~(GPIO_GPIO0 | GPIO_GPIO1 | GPIO_GPIO2 | GPIO_GPIO3);
  
  /* MAX1600 to standby mode: */
  PA_DWR &= ~(GPIO_GPIO0 | GPIO_GPIO1 | GPIO_GPIO2 | GPIO_GPIO3);
  NCR_0 &= ~(NCR_A0VPP | NCR_A1VPP);

  INTPOL1 |= 
    (1 << (S0_READY_NINT - SA1111_IRQ(32))) |
    (1 << (S1_READY_NINT - SA1111_IRQ(32))) |
    (1 << (S0_CD_VALID - SA1111_IRQ(32))) |
    (1 << (S1_CD_VALID - SA1111_IRQ(32))) |
    (1 << (S0_BVD1_STSCHG - SA1111_IRQ(32))) |
    (1 << (S1_BVD1_STSCHG - SA1111_IRQ(32)));

  return_val+=request_irq(S0_CD_VALID, init->handler, SA_INTERRUPT,
			  "Neponset PCMCIA (0) CD", NULL);
  return_val+=request_irq(S1_CD_VALID, init->handler, SA_INTERRUPT,
			  "Neponset CF (1) CD", NULL);
  return_val+=request_irq(S0_BVD1_STSCHG, init->handler, SA_INTERRUPT,
			  "Neponset PCMCIA (0) BVD1", NULL);
  return_val+=request_irq(S1_BVD1_STSCHG, init->handler, SA_INTERRUPT,
			  "Neponset CF (1) BVD1", NULL);

  return (return_val<0) ? -1 : 2;
}

static int neponset_pcmcia_shutdown(void){

  free_irq(S0_CD_VALID, NULL);
  free_irq(S1_CD_VALID, NULL);
  free_irq(S0_BVD1_STSCHG, NULL);
  free_irq(S1_BVD1_STSCHG, NULL);

  INTPOL1 &= 
    ~((1 << (S0_CD_VALID - SA1111_IRQ(32))) |
      (1 << (S1_CD_VALID - SA1111_IRQ(32))) |
      (1 << (S0_BVD1_STSCHG - SA1111_IRQ(32))) |
      (1 << (S1_BVD1_STSCHG - SA1111_IRQ(32))));

  return 0;
}

static int neponset_pcmcia_socket_state(struct pcmcia_state_array
					*state_array){
  unsigned long status;
  int return_val=1;

  if(state_array->size<2) return -1;

  memset(state_array->state, 0, 
	 (state_array->size)*sizeof(struct pcmcia_state));

  status=PCSR;

  state_array->state[0].detect=((status & PCSR_S0_DETECT)==0)?1:0;

  state_array->state[0].ready=((status & PCSR_S0_READY)==0)?0:1;

  state_array->state[0].bvd1=((status & PCSR_S0_BVD1)==0)?0:1;

  state_array->state[0].bvd2=((status & PCSR_S0_BVD2)==0)?0:1;

  state_array->state[0].wrprot=((status & PCSR_S0_WP)==0)?0:1;

  state_array->state[0].vs_3v=((status & PCSR_S0_VS1)==0)?1:0;

  state_array->state[0].vs_Xv=((status & PCSR_S0_VS2)==0)?1:0;

  state_array->state[1].detect=((status & PCSR_S1_DETECT)==0)?1:0;

  state_array->state[1].ready=((status & PCSR_S1_READY)==0)?0:1;

  state_array->state[1].bvd1=((status & PCSR_S1_BVD1)==0)?0:1;

  state_array->state[1].bvd2=((status & PCSR_S1_BVD2)==0)?0:1;

  state_array->state[1].wrprot=((status & PCSR_S1_WP)==0)?0:1;

  state_array->state[1].vs_3v=((status & PCSR_S1_VS1)==0)?1:0;

  state_array->state[1].vs_Xv=((status & PCSR_S1_VS2)==0)?1:0;

  return return_val;
}

static int neponset_pcmcia_get_irq_info(struct pcmcia_irq_info *info){

  switch(info->sock){
  case 0:
    info->irq=S0_READY_NINT;
    break;

  case 1:
    info->irq=S1_READY_NINT;
    break;

  default:
    return -1;
  }

  return 0;
}

static int neponset_pcmcia_configure_socket(const struct pcmcia_configure
					    *configure){
  unsigned long pccr=PCCR, ncr=NCR_0, gpio=PA_DWR;

  /* Neponset uses the Maxim MAX1600, with the following connections:
   *
   *   MAX1600      Neponset
   *
   *    A0VCC        SA-1111 GPIO A<1>
   *    A1VCC        SA-1111 GPIO A<0>
   *    A0VPP        CPLD NCR A0VPP
   *    A1VPP        CPLD NCR A1VPP
   *    B0VCC        SA-1111 GPIO A<2>
   *    B1VCC        SA-1111 GPIO A<3>
   *    B0VPP        ground (slot B is CF)
   *    B1VPP        ground (slot B is CF)
   *
   *     VX          VCC (5V)
   *     VY          VCC3_3 (3.3V)
   *     12INA       12V
   *     12INB       ground (slot B is CF)
   *
   * The MAX1600 CODE pin is tied to ground, placing the device in 
   * "Standard Intel code" mode. Refer to the Maxim data sheet for
   * the corresponding truth table.
   */

  switch(configure->sock){
  case 0:
    
    switch(configure->vcc){
    case 0:
      pccr=(pccr & ~PCCR_S0_FLT);
      gpio&=~(GPIO_GPIO0 | GPIO_GPIO1);
      break;

    case 33:
      pccr=(pccr & ~PCCR_S0_PSE) | PCCR_S0_FLT | PCCR_S0_PWAITEN;
      gpio=(gpio & ~(GPIO_GPIO0 | GPIO_GPIO1)) | GPIO_GPIO1;
      break;

    case 50:
      pccr=(pccr | PCCR_S0_PSE | PCCR_S0_FLT | PCCR_S0_PWAITEN);
      gpio=(gpio & ~(GPIO_GPIO0 | GPIO_GPIO1)) | GPIO_GPIO0;
      break;

    default:
      printk(KERN_ERR "%s(): unrecognized Vcc %u\n", __FUNCTION__,
	     configure->vcc);
      return -1;
    }

    switch(configure->vpp){
    case 0:
      ncr&=~(NCR_A0VPP | NCR_A1VPP);
      break;

    case 120:
      ncr=(ncr & ~(NCR_A0VPP | NCR_A1VPP)) | NCR_A1VPP;
      break;

    default:
      if(configure->vpp == configure->vcc)
	ncr=(ncr & ~(NCR_A0VPP | NCR_A1VPP)) | NCR_A0VPP;
      else {
	printk(KERN_ERR "%s(): unrecognized Vpp %u\n", __FUNCTION__,
	       configure->vpp);
	return -1;
      }
    }

    pccr=(configure->reset)?(pccr | PCCR_S0_RST):(pccr & ~PCCR_S0_RST);

    break;

  case 1:
    switch(configure->vcc){
    case 0:
      pccr=(pccr & ~PCCR_S1_FLT);
      gpio&=~(GPIO_GPIO2 | GPIO_GPIO3);
      break;

    case 33:
      pccr=(pccr & ~PCCR_S1_PSE) | PCCR_S1_FLT | PCCR_S1_PWAITEN;
      gpio=(gpio & ~(GPIO_GPIO2 | GPIO_GPIO3)) | GPIO_GPIO2;
      break;

    case 50:
      pccr=(pccr | PCCR_S1_PSE | PCCR_S1_FLT | PCCR_S1_PWAITEN);
      gpio=(gpio & ~(GPIO_GPIO2 | GPIO_GPIO3)) | GPIO_GPIO3;
      break;

    default:
      printk(KERN_ERR "%s(): unrecognized Vcc %u\n", __FUNCTION__,
	     configure->vcc);
      return -1;
    }

    if(configure->vpp!=configure->vcc && configure->vpp!=0){
      printk(KERN_ERR "%s(): CF slot cannot support Vpp %u\n", __FUNCTION__,
	     configure->vpp);
      return -1;
    }

    pccr=(configure->reset)?(pccr | PCCR_S1_RST):(pccr & ~PCCR_S1_RST);

    break;

  default:
    return -1;
  }

  PCCR = pccr;
  NCR_0 = ncr;
  PA_DWR = gpio;

  return 0;
}

struct pcmcia_low_level neponset_pcmcia_ops = { 
  neponset_pcmcia_init,
  neponset_pcmcia_shutdown,
  neponset_pcmcia_socket_state,
  neponset_pcmcia_get_irq_info,
  neponset_pcmcia_configure_socket
};

