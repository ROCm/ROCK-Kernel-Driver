/*
 * linux/include/asm-arm/arch-sa1100/ide.h
 *
 * Copyright (c) 1998 Hugo Fiennes & Nicolas Pitre
 *
 * 18-aug-2000: Cleanup by Erik Mouw (J.A.K.Mouw@its.tudelft.nl)
 *              Get rid of the special ide_init_hwif_ports() functions
 *              and make a generalised function that can be used by all
 *              architectures.
 */

#include <linux/config.h>
#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/mach-types.h>


#define PCMCIA_IO_0_BASE 0xe0000000
#define PCMCIA_IO_1_BASE 0xe4000000


/*
 * Set up a hw structure for a specified data port, control port and IRQ.
 * This should follow whatever the default interface uses.
 */
static __inline__ void
ide_init_hwif_ports(hw_regs_t *hw, int data_port, int ctrl_port, int *irq)
{
	ide_ioreg_t reg;
	int i;
	int regincr = 1;
	
	/* The Empeg board has the first two address lines unused */
	if (machine_is_empeg())
		regincr = 1 << 2;

	/* The LART doesn't use A0 for IDE */
	if (machine_is_lart())
		regincr = 1 << 1;

	memset(hw, 0, sizeof(*hw));

	reg = (ide_ioreg_t)data_port;

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = reg;
		reg += regincr;
	}
	
	hw->io_ports[IDE_CONTROL_OFFSET] = (ide_ioreg_t) ctrl_port;
	
	if (irq)
		*irq = 0;
}




/*
 * This registers the standard ports for this architecture with the IDE
 * driver.
 */
static __inline__ void
ide_init_default_hwifs(void)
{
    if( machine_is_empeg() ){
#ifdef CONFIG_SA1100_EMPEG
	hw_regs_t hw;

	/* First, do the SA1100 setup */

	/* PCMCIA IO space */
	MECR=0x21062106;

        /* Issue 3 is much neater than issue 2 */
	GPDR&=~(EMPEG_IDE1IRQ|EMPEG_IDE2IRQ);

	/* Interrupts on rising edge: lines are inverted before they get to
           the SA */
	set_GPIO_IRQ_edge( (EMPEG_IDE1IRQ|EMPEG_IDE2IRQ), GPIO_FALLING_EDGE );

	/* Take hard drives out of reset */
	GPSR=(EMPEG_IDERESET);

	/* Sonja and her successors have two IDE ports. */
	/* MAC 23/4/1999, swap these round so that the left hand
	   hard disk is hda when viewed from the front. This
	   doesn't match the silkscreen however. */
	ide_init_hwif_ports(&hw, PCMCIA_IO_0_BASE + 0x40, PCMCIA_IO_0_BASE + 0x78, NULL);
	hw.irq = EMPEG_IRQ_IDE2;
	ide_register_hw(&hw, NULL);
	ide_init_hwif_ports(&hw, PCMCIA_IO_0_BASE + 0x00, PCMCIA_IO_0_BASE + 0x38, NULL);
	hw.irq = ,EMPEG_IRQ_IDE1;
	ide_register_hw(&hw, NULL);
#endif
    }

    else if( machine_is_victor() ){
#ifdef CONFIG_SA1100_VICTOR
	hw_regs_t hw;

	/* Enable appropriate GPIOs as interrupt lines */
	GPDR &= ~GPIO_GPIO7;
	set_GPIO_IRQ_edge( GPIO_GPIO7, GPIO_RISING_EDGE );

	/* set the pcmcia interface timing */
	MECR = 0x00060006;

	ide_init_hwif_ports(&hw, PCMCIA_IO_0_BASE + 0x1f0, PCMCIA_IO_0_BASE + 0x3f6, NULL);
	hw.irq = IRQ_GPIO7;
	ide_register_hw(&hw, NULL);
#endif
    }
    else if (machine_is_lart()) {
#ifdef CONFIG_SA1100_LART
        hw_regs_t hw;

        /* Enable GPIO as interrupt line */
        GPDR &= ~GPIO_GPIO1;
        set_GPIO_IRQ_edge(GPIO_GPIO1, GPIO_RISING_EDGE);
        
        /* set PCMCIA interface timing */
        MECR = 0x00060006;

        /* init the interface */
	ide_init_hwif_ports(&hw, PCMCIA_IO_0_BASE + 0x0000, PCMCIA_IO_0_BASE + 0x1000, NULL);
        hw.irq = IRQ_GPIO1;
        ide_register_hw(&hw, NULL);
#endif
    }
}


