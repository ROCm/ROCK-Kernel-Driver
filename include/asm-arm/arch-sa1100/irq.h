/*
 * linux/include/asm-arm/arch-sa1100/irq.h
 *
 * Copyright (C) 1996-1999 Russell king
 * Copyright (C) 1999 Hugo Fiennes
 *
 * Changelog:
 *   22-08-1998	RMK	Restructured IRQ routines
 *   06-01-1999	HBF	SA1100 twiddles
 *   12-02-1999	NP	added ICCR
 *   17-02-1999	NP	empeg henry ugly hacks now in a separate file ;)
 *   11-08-1999	PD	SA1101 support added
 *   25-09-1999	RMK	Merged into main ARM tree, cleaned up
 *   12-05-2000 NP	IRQ dispatcher handler for GPIO 11 to 27.
 *   26-05-2000 JD	SA-1111 support added
 */
#include <linux/config.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

#define fixup_irq(x)	(x)

/* 
 * We don't need to ACK IRQs on the SA1100 unless they're GPIOs
 * this is for internal IRQs i.e. from 11 to 31.
 */

static void sa1100_mask_irq(unsigned int irq)
{
	ICMR &= ~(1 << irq);
}

static void sa1100_unmask_irq(unsigned int irq)
{
	ICMR |= (1 << irq);
}

/*
 * SA1100 GPIO edge detection for IRQs.
 */
extern int GPIO_IRQ_rising_edge;
extern int GPIO_IRQ_falling_edge;

/*
 * GPIO IRQs must be acknoledged.  This is for IRQs from 0 to 10.
 */

static void sa1100_mask_and_ack_GPIO0_10_irq(unsigned int irq)
{
	ICMR &= ~(1 << irq);
	GEDR = (1 << irq);
}

static void sa1100_mask_GPIO0_10_irq(unsigned int irq)
{
	ICMR &= ~(1 << irq);
}

static void sa1100_unmask_GPIO0_10_irq(unsigned int irq)
{
	GRER = (GRER & ~(1 << irq)) | (GPIO_IRQ_rising_edge & (1 << irq));
	GFER = (GFER & ~(1 << irq)) | (GPIO_IRQ_falling_edge & (1 << irq));
	ICMR |= (1 << irq);
}

/* 
 * Install handler for GPIO 11-27 edge detect interrupts
 */

void do_IRQ(int irq, struct pt_regs * regs);

static int GPIO_11_27_enabled;		/* enabled i.e. unmasked GPIO IRQs */
static int GPIO_11_27_spurious;		/* GPIOs that triggered when masked */

static void sa1100_GPIO11_27_demux(int irq, void *dev_id, 
				   struct pt_regs *regs)
{
	int i, spurious;

	while( (irq = (GEDR & 0xfffff800)) ){
		/*
		 * We don't want to clear GRER/GFER when the corresponding
		 * IRQ is masked because we could miss a level transition
		 * i.e. an IRQ which need servicing as soon as it is 
		 * unmasked.  However, such situation should happen only
		 * during the loop below.  Thus all IRQs which aren't 
		 * enabled at this point are considered spurious.  Those 
		 * are cleared but only de-activated if they happened twice.
		 */
		spurious = irq & ~GPIO_11_27_enabled;
		if (spurious) {
			GEDR = spurious;
			GRER &= ~(spurious & GPIO_11_27_spurious);
			GFER &= ~(spurious & GPIO_11_27_spurious);
			GPIO_11_27_spurious |= spurious;
			irq ^= spurious;
			if (!irq) continue;
		}

		for (i = 11; i <= 27; ++i) {
			if (irq & (1<<i)) {
				do_IRQ( IRQ_GPIO_11_27(i), regs );
			}
		}
	}
}

static struct irqaction GPIO11_27_irq = {
	name:		"GPIO 11-27",
	handler:	sa1100_GPIO11_27_demux,
	flags:		SA_INTERRUPT
};                                                                              

static void sa1100_mask_and_ack_GPIO11_27_irq(unsigned int irq)
{
	int mask = (1 << GPIO_11_27_IRQ(irq));
	GPIO_11_27_enabled &= ~mask;
	GEDR = mask;
}

static void sa1100_mask_GPIO11_27_irq(unsigned int irq)
{
	GPIO_11_27_enabled &= ~(1 << GPIO_11_27_IRQ(irq));
}

static void sa1100_unmask_GPIO11_27_irq(unsigned int irq)
{
	int mask = (1 << GPIO_11_27_IRQ(irq));
	GPIO_11_27_enabled |= mask;
	GPIO_11_27_spurious &= ~mask;
	GRER = (GRER & ~mask) | (GPIO_IRQ_rising_edge & mask);
	GFER = (GFER & ~mask) | (GPIO_IRQ_falling_edge & mask);
}


#if defined(CONFIG_SA1111)

/* 
 * Install handler for SA1111 IRQ handler.
 */

static void sa1111_IRQ_demux( int irq, void *dev_id, struct pt_regs *regs )
{
	int i;
	unsigned long stat0, stat1;

	for(;;) {
		stat0 = INTSTATCLR0, stat1 = INTSTATCLR1;
		if( !stat0 && !stat1 ) break;
		if( stat0 )
			for( i = 0; i < 32; i++ )
				if( stat0 & (1<<i) )
					do_IRQ( SA1111_IRQ(i), regs );

		if( stat1 )
			for( i = 32; i < 55; i++ )
				if( stat1 & (1<<(i-32)) )
					do_IRQ( SA1111_IRQ(i), regs );
	}
}

static struct irqaction sa1111_irq = {
	name:		"SA1111",
	handler:	sa1111_IRQ_demux,
	flags:		SA_INTERRUPT
};

static void sa1111_mask_and_ack_lowirq(unsigned int irq)
{
	unsigned int mask = 1 << (irq - SA1111_IRQ(0));

	//INTEN0 &= ~mask;
	INTSTATCLR0 = mask;
}

static void sa1111_mask_and_ack_highirq(unsigned int irq)
{
	unsigned int mask = 1 << (irq - SA1111_IRQ(32));

	//INTEN1 &= ~mask;
	INTSTATCLR1 = mask;
}

static void sa1111_mask_lowirq(unsigned int irq)
{
	//INTEN0 &= ~(1 << (irq - SA1111_IRQ(0)));
}

static void sa1111_mask_highirq(unsigned int irq)
{
	//INTEN1 &= ~(1 << (irq - SA1111_IRQ(32)));
}

static void sa1111_unmask_lowirq(unsigned int irq)
{
	INTEN0 |= 1 << (irq - SA1111_IRQ(0));
}

static void sa1111_unmask_highirq(unsigned int irq)
{
	INTEN1 |= 1 << ((irq - SA1111_IRQ(32)));
}

#endif  /* CONFIG_SA1111 */


#ifdef CONFIG_ASSABET_NEPONSET

/* 
 * Install handler for Neponset IRQ.  Yes, yes... we are way down the IRQ
 * cascade which is not good for IRQ latency, but the hardware has been
 * designed that way...
 */

static void neponset_IRQ_demux( int irq, void *dev_id, struct pt_regs *regs )
{
	int irr;

	for(;;){
		irr = IRR & (IRR_ETHERNET | IRR_USAR | IRR_SA1111);
		/* Let's have all active IRQ bits high.
		 * Note: there is a typo in the Neponset user's guide 
		 * for the SA1111 IRR level.
		 */
		irr ^= (IRR_ETHERNET | IRR_USAR);
		if (!irr) break;

		if( irr & IRR_ETHERNET )
			do_IRQ(NEPONSET_ETHERNET_IRQ, regs);

		if( irr & IRR_USAR )
			do_IRQ(NEPONSET_USAR_IRQ, regs);

		if( irr & IRR_SA1111 )	
			sa1111_IRQ_demux(irq, dev_id, regs);
	}
}

static struct irqaction neponset_irq = {
	name:		"Neponset",
	handler:	neponset_IRQ_demux,
	flags:		SA_INTERRUPT
};                                                                              

#endif


#if defined(CONFIG_SA1100_GRAPHICSCLIENT) || defined(CONFIG_SA1100_THINCLIENT)

/*
 * IRQ handler for the ThinClient/GraphicsClient external IRQ controller
 */

static void ADS_IRQ_demux( int irq, void *dev_id, struct pt_regs *regs )
{
	int irq, i;

	while( (irq = ADS_INT_ST1 | (ADS_INT_ST2 << 8)) ){
		for( i = 0; i < 16; i++ )
			if( irq & (1<<i) )
				do_IRQ( ADS_EXT_IRQ(i), regs );
	}
}

static struct irqaction ADS_ext_irq = {
	name:		"ADS_ext_IRQ",
	handler:	ADS_IRQ_demux,
	flags:		SA_INTERRUPT
};

static void ADS_mask_and_ack_irq0(unsigned int irq)
{
	int mask = (1 << (irq - ADS_EXT_IRQ(0)));
	ADS_INT_EN1 &= ~mask;
	ADS_INT_ST1 = mask;
}

static void ADS_mask_irq0(unsigned int irq)
{
	ADS_INT_ST1 = (1 << (irq - ADS_EXT_IRQ(0)));
}

static void ADS_unmask_irq0(unsigned int irq)
{
	ADS_INT_EN1 |= (1 << (irq - ADS_EXT_IRQ(0)));
}

static void ADS_mask_and_ack_irq1(unsigned int irq)
{
	int mask = (1 << (irq - ADS_EXT_IRQ(8)));
	ADS_INT_EN2 &= ~mask;
	ADS_INT_ST2 = mask;
}

static void ADS_mask_irq1(unsigned int irq)
{
	ADS_INT_ST2 = (1 << (irq - ADS_EXT_IRQ(8)));
}

static void ADS_unmask_irq1(unsigned int irq)
{
	ADS_INT_EN2 |= (1 << (irq - ADS_EXT_IRQ(8)));
}

#endif


static __inline__ void irq_init_irq(void)
{
	int irq;

	/* disable all IRQs */
	ICMR = 0;

	/* all IRQs are IRQ, not FIQ */
	ICLR = 0;

	/* clear all GPIO edge detects */
	GFER = 0;
	GRER = 0;
	GEDR = -1;

	/*
	 * Whatever the doc says, this has to be set for the wait-on-irq
	 * instruction to work... on a SA1100 rev 9 at least.
	 */
	ICCR = 1;

	for (irq = 0; irq <= 10; irq++) {
		irq_desc[irq].valid	= 1;
		irq_desc[irq].probe_ok	= 1;
		irq_desc[irq].mask_ack	= sa1100_mask_and_ack_GPIO0_10_irq;
		irq_desc[irq].mask	= sa1100_mask_GPIO0_10_irq;
		irq_desc[irq].unmask	= sa1100_unmask_GPIO0_10_irq;
	}

	for (irq = 11; irq <= 31; irq++) {
		irq_desc[irq].valid	= 1;
		irq_desc[irq].probe_ok	= 0;
		irq_desc[irq].mask_ack	= sa1100_mask_irq;
		irq_desc[irq].mask	= sa1100_mask_irq;
		irq_desc[irq].unmask	= sa1100_unmask_irq;
	}

	for (irq = 32; irq <= 48; irq++) {
		irq_desc[irq].valid	= 1;
		irq_desc[irq].probe_ok	= 1;
		irq_desc[irq].mask_ack	= sa1100_mask_and_ack_GPIO11_27_irq;
		irq_desc[irq].mask	= sa1100_mask_GPIO11_27_irq;
		irq_desc[irq].unmask	= sa1100_unmask_GPIO11_27_irq;
	}
	setup_arm_irq( IRQ_GPIO11_27, &GPIO11_27_irq );

#ifdef CONFIG_SA1111
	if( machine_is_assabet() && machine_has_neponset() ){

		/* disable all IRQs */
		INTEN0 = 0;
		INTEN1 = 0;

		/* detect on rising edge */
		INTPOL0 = 0;
		INTPOL1 = 0;

		/* clear all IRQs */
		INTSTATCLR0 = -1;
		INTSTATCLR1 = -1;

		for (irq = SA1111_IRQ(0); irq <= SA1111_IRQ(26); irq++) {
			irq_desc[irq].valid	= 1;
			irq_desc[irq].probe_ok	= 1;
			irq_desc[irq].mask_ack	= sa1111_mask_and_ack_lowirq;
			irq_desc[irq].mask	= sa1111_mask_lowirq;
			irq_desc[irq].unmask	= sa1111_unmask_lowirq;
		}
		for (irq = SA1111_IRQ(32); irq <= SA1111_IRQ(54); irq++) {
		  	irq_desc[irq].valid	= 1;
			irq_desc[irq].probe_ok	= 1;
			irq_desc[irq].mask_ack	= sa1111_mask_and_ack_highirq;
			irq_desc[irq].mask	= sa1111_mask_highirq;
			irq_desc[irq].unmask	= sa1111_unmask_highirq;
		}

		if( machine_has_neponset() ){
			/* setup extra Neponset IRQs */
			irq = NEPONSET_ETHERNET_IRQ;
		  	irq_desc[irq].valid	= 1;
			irq_desc[irq].probe_ok	= 1;
			irq = NEPONSET_USAR_IRQ;
		  	irq_desc[irq].valid	= 1;
			irq_desc[irq].probe_ok	= 1;
			set_GPIO_IRQ_edge( GPIO_NEP_IRQ, GPIO_RISING_EDGE );
			setup_arm_irq( IRQ_GPIO_NEP_IRQ, &neponset_irq );
		}else{
			/* for pure SA1111 designs to come (currently unused) */
			set_GPIO_IRQ_edge( 0, GPIO_RISING_EDGE );
			setup_arm_irq( -1, &sa1111_irq );
		}
	}
#endif

#if defined(CONFIG_SA1100_GRAPHICSCLIENT) || defined(CONFIG_SA1100_THINCLIENT)
	if( machine_is_graphicsclient()  || machine_is_thinclient() ){
		/* disable all IRQs */
		ADS_INT_EN1 = 0;
		ADS_INT_EN2 = 0;
		/* clear all IRQs */
		ADS_INT_ST1 = 0xff;
		ADS_INT_ST2 = 0xff;

		for (irq = ADS_EXT_IRQ(0); irq <= ADS_EXT_IRQ(7); irq++) {
			irq_desc[irq].valid	= 1;
			irq_desc[irq].probe_ok	= 1;
			irq_desc[irq].mask_ack	= ADS_mask_and_ack_irq0;
			irq_desc[irq].mask	= ADS_mask_irq0;
			irq_desc[irq].unmask	= ADS_unmask_irq0;
		}
		for (irq = ADS_EXT_IRQ(8); irq <= ADS_EXT_IRQ(15); irq++) {
			irq_desc[irq].valid	= 1;
			irq_desc[irq].probe_ok	= 1;
			irq_desc[irq].mask_ack	= ADS_mask_and_ack_irq1;
			irq_desc[irq].mask	= ADS_mask_irq1;
			irq_desc[irq].unmask	= ADS_unmask_irq1;
		}
		GPDR &= ~GPIO_GPIO0;
		set_GPIO_IRQ_edge(GPIO_GPIO0, GPIO_FALLING_EDGE);
		setup_arm_irq( IRQ_GPIO0, &ADS_ext_irq );
	}
#endif

}
