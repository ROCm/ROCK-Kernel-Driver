/*
 * arch/arm/kernel/hw-sa1100.c
 *
 * SA1100-dependent machine specifics
 *
 * Copyright (C) 2000 Nicolas Pitre <nico@cam.org>
 *
 * This will certainly contain more stuff with time... like power management,
 * special hardware autodetection, etc.
 *
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>

#include <asm/delay.h>
#include <asm/hardware.h>
#include <asm/mach-types.h>

/*
 * SA1100 GPIO edge detection for IRQs:
 * IRQs are generated on Falling-Edge, Rising-Edge, or both.
 * This must be called *before* the appropriate IRQ is registered.
 * Use this instead of directly setting GRER/GFER.
 */

int GPIO_IRQ_rising_edge;
int GPIO_IRQ_falling_edge;

void set_GPIO_IRQ_edge( int gpio_mask, int edge )
{
	if( edge & GPIO_FALLING_EDGE )
		GPIO_IRQ_falling_edge |= gpio_mask;
	else
		GPIO_IRQ_falling_edge &= ~gpio_mask;
	if( edge & GPIO_RISING_EDGE )
		GPIO_IRQ_rising_edge |= gpio_mask;
	else
		GPIO_IRQ_rising_edge &= ~gpio_mask;
}

EXPORT_SYMBOL(set_GPIO_IRQ_edge);


#ifdef CONFIG_SA1100_ASSABET

unsigned long BCR_value = BCR_DB1110;
unsigned long SCR_value = SCR_INIT;
EXPORT_SYMBOL(BCR_value);
EXPORT_SYMBOL(SCR_value);

/*
 * Read System Configuration "Register"
 * (taken from "Intel StrongARM SA-1110 Microprocessor Development Board
 * User's Guide", section 4.4.1)
 *
 * This same scan is performed in arch/arm/boot/compressed/head-sa1100.S
 * to set up the serial port for decompression status messages. We 
 * repeat it here because the kernel may not be loaded as a zImage, and
 * also because it's a hassle to communicate the SCR value to the kernel
 * from the decompressor.
 */

void __init get_assabet_scr(void)
{
	unsigned long flags, scr, i;

	local_irq_save(flags);
	GPDR |= 0x3fc;			/* Configure GPIO 9:2 as outputs */
	GPSR = 0x3fc;			/* Write 0xFF to GPIO 9:2 */
	GPDR &= ~(0x3fc);		/* Configure GPIO 9:2 as inputs */
	for(i = 100; i--; scr = GPLR);	/* Read GPIO 9:2 */
	GPDR |= 0x3fc;			/*  restore correct pin direction */
	local_irq_restore(flags);
	scr &= 0x3fc;			/* save as system configuration byte. */

	SCR_value = scr;
}

#endif  /* CONFIG_SA1100_ASSABET */


#if defined(CONFIG_SA1100_BITSY)
/*
 * Bitsy has extended, write-only memory-mapped GPIO's
 */
static int bitsy_egpio = EGPIO_BITSY_RS232_ON;
void clr_bitsy_egpio(unsigned long x) 
{
  bitsy_egpio &= ~x;
  *(volatile int *)0xdc000000 = bitsy_egpio;
}
void set_bitsy_egpio(unsigned long x) 
{
  bitsy_egpio |= x;
  *(volatile int *)0xdc000000 = bitsy_egpio;
}
EXPORT_SYMBOL(clr_bitsy_egpio);
EXPORT_SYMBOL(set_bitsy_egpio);
#endif


#ifdef CONFIG_SA1111

static void __init sa1111_init(void){
  unsigned long id=SKID;

  if((id & SKID_ID_MASK) == SKID_SA1111_ID)
    printk(KERN_INFO "SA-1111 Microprocessor Companion Chip: "
	   "silicon revision %x, metal revision %x\n",
	   (id & SKID_SIREV_MASK)>>4, (id & SKID_MTREV_MASK));
  else {
    printk(KERN_ERR "Could not detect SA-1111!\n");
    return;
  }

  /* First, set up the 3.6864MHz clock on GPIO 27 for the SA-1111:
   * (SA-1110 Developer's Manual, section 9.1.2.1)
   */
  GAFR |= GPIO_GPIO27;
  GPDR |= GPIO_GPIO27;
  TUCR = TUCR_3_6864MHz;

  /* Now, set up the PLL and RCLK in the SA-1111: */
  SKCR = SKCR_PLL_BYPASS | SKCR_RDYEN | SKCR_OE_EN;
  udelay(100);
  SKCR = SKCR_PLL_BYPASS | SKCR_RCLKEN | SKCR_RDYEN | SKCR_OE_EN;

  /* SA-1111 Register Access Bus should now be available. Clocks for
   * any other SA-1111 functional blocks must be enabled separately
   * using the SKPCR.
   */

  {
  /*
   * SA1111 DMA bus master setup 
   */
	int cas;

	/* SA1111 side */
	switch ( (MDCNFG>>12) & 0x03 ) {
	case 0x02:
		cas = 0; break;
	case 0x03:
		cas = 1; break;
	default:
		cas = 1; break;
	}
	SMCR = 1		/* 1: memory is SDRAM */
		| ( 1 << 1 )	/* 1:MBGNT is enable */
		| ( ((MDCNFG >> 4) & 0x07) << 2 )	/* row address lines */
		| ( cas << 5 );	/* CAS latency */

	/* SA1110 side */
	GPDR |= 1<<21;
	GPDR &= ~(1<<22);
	GAFR |= ( (1<<21) | (1<<22) );

	TUCR |= (1<<10);
  }
}

#else
#define sa1111_init()  printk( "Warning: missing SA1111 support\n" )
#endif


static int __init hw_sa1100_init(void)
{
	if( machine_is_assabet() ){
		if(machine_has_neponset()){
#ifdef CONFIG_ASSABET_NEPONSET
			LEDS = WHOAMI;
			sa1111_init();
#else
			printk( "Warning: Neponset detected but full support "
				"hasn't been configured in the kernel\n" );
#endif
		}
	} else if (machine_is_xp860()) {
		sa1111_init();
	}
	return 0;
}

module_init(hw_sa1100_init);
