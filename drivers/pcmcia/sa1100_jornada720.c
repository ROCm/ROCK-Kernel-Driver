/*
 * drivers/pcmcia/sa1100_jornada720.c
 *
 * Jornada720 PCMCIA specific routines
 *
 */
#include <linux/kernel.h>
#include <linux/sched.h>

#include <asm/delay.h>
#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/arch/pcmcia.h>

#define SOCKET0_POWER   GPIO_GPIO0
#define SOCKET0_3V      GPIO_GPIO2
#define SOCKET1_POWER   (GPIO_GPIO1 | GPIO_GPIO3)
#define SOCKET1_3V   GPIO_GPIO3

static int jornada720_pcmcia_init(struct pcmcia_init *init)
{
  int return_val=0;

  GRER |= 0x00000002;
  /* Set GPIO_A<3:1> to be outputs for PCMCIA/CF power controller: */
  PA_DDR = 0;
  PA_DWR = 0;
  PA_SDR = 0;
  PA_SSR = 0;

  PB_DDR = 0;
  PB_DWR = 0x01;
  PB_SDR = 0;
  PB_SSR = 0;

  PC_DDR = 0x88;
  PC_DWR = 0x20;
  PC_SDR = 0;
  PC_SSR = 0;

  INTPOL1 |=
    (1 << (S0_READY_NINT - SA1111_IRQ(32))) |
    (1 << (S1_READY_NINT - SA1111_IRQ(32))) |
    (1 << (S0_CD_VALID - SA1111_IRQ(32))) |
    (1 << (S1_CD_VALID - SA1111_IRQ(32))) |
    (1 << (S0_BVD1_STSCHG - SA1111_IRQ(32))) |
    (1 << (S1_BVD1_STSCHG - SA1111_IRQ(32)));

  return_val+=request_irq(S0_CD_VALID, init->handler, SA_INTERRUPT,
			  "Jornada720 PCMCIA (0) CD", NULL);
  return_val+=request_irq(S1_CD_VALID, init->handler, SA_INTERRUPT,
			  "Jornada720 CF (1) CD", NULL);
  return_val+=request_irq(S0_BVD1_STSCHG, init->handler, SA_INTERRUPT,
			  "Jornada720 PCMCIA (0) BVD1", NULL);
  return_val+=request_irq(S1_BVD1_STSCHG, init->handler, SA_INTERRUPT,
			  "Jornada720 CF (1) BVD1", NULL);

  return (return_val<0) ? -1 : 2;
}

static int jornada720_pcmcia_shutdown(void)
{
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

static int jornada720_pcmcia_socket_state(struct pcmcia_state_array
					*state_array)
{
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

static int jornada720_pcmcia_get_irq_info(struct pcmcia_irq_info *info)
{
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

static int jornada720_pcmcia_configure_socket(const struct pcmcia_configure
					    *configure)
{
  unsigned long pccr=PCCR, gpio=PA_DWR;

printk("%s(): config socket %d vcc %d vpp %d\n", __FUNCTION__,
	       configure->sock, configure->vcc, configure->vpp);
  switch(configure->sock){
  case 0:
    switch(configure->vcc){
    case 0:
      pccr = (pccr & ~PCCR_S0_FLT);
      gpio&=~(SOCKET0_POWER | SOCKET0_3V);
      break;

    case 33:
      pccr = (pccr & ~PCCR_S0_PSE) | PCCR_S0_FLT | PCCR_S0_PWAITEN;
      gpio |= SOCKET0_POWER | SOCKET0_3V;
      break;

    case 50:
      pccr = (pccr | PCCR_S0_PSE | PCCR_S0_FLT | PCCR_S0_PWAITEN);
      gpio = (gpio & ~SOCKET0_3V) | SOCKET0_POWER;
      break;

    default:
      printk(KERN_ERR "%s(): unrecognized Vcc %u\n", __FUNCTION__,
	     configure->vcc);
      return -1;
    }
    switch(configure->vpp){
    case 0:
      break;
    case 50:
	printk(KERN_ERR "%s(): 5.0 Vpp %u\n", __FUNCTION__,
	       configure->vpp);
      break;
    case 120:
	printk(KERN_ERR "%s(): 12 Vpp %u\n", __FUNCTION__,
	       configure->vpp);
      break;
    default:
	printk(KERN_ERR "%s(): unrecognized Vpp %u\n", __FUNCTION__,
	       configure->vpp);
	return -1;
    }
    pccr=(configure->reset)?(pccr | PCCR_S0_RST):(pccr & ~PCCR_S0_RST);
    break;

  case 1:
    switch(configure->vcc){
    case 0:
      pccr = (pccr & ~PCCR_S1_FLT);
      gpio &= ~(SOCKET1_POWER);
      break;

    case 33:
      pccr = (pccr & ~PCCR_S1_PSE) | PCCR_S1_FLT | PCCR_S1_PWAITEN;
      gpio |= SOCKET1_POWER;
      break;

    case 50:
      pccr = (pccr | PCCR_S1_PSE | PCCR_S1_FLT | PCCR_S1_PWAITEN);
      gpio = (gpio & ~(SOCKET1_POWER)) | SOCKET1_POWER;
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
  PA_DWR = gpio;
  return 0;
}

struct pcmcia_low_level jornada720_pcmcia_ops = {
  jornada720_pcmcia_init,
  jornada720_pcmcia_shutdown,
  jornada720_pcmcia_socket_state,
  jornada720_pcmcia_get_irq_info,
  jornada720_pcmcia_configure_socket
};

