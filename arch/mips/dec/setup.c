/*
 * Setup the interrupt stuff.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998 Harald Koerfgen
 */
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/mc146818rtc.h>
#include <linux/param.h>
#include <asm/mipsregs.h>
#include <asm/bootinfo.h>
#include <linux/init.h>
#include <asm/irq.h>
#include <asm/reboot.h>

#include <asm/dec/interrupts.h>
#include <asm/dec/kn01.h>
#include <asm/dec/kn02.h>
#include <asm/dec/kn02xa.h>
#include <asm/dec/kn03.h>
#include <asm/dec/ioasic_ints.h>

extern asmlinkage void decstation_handle_int(void);

void dec_init_kn01(void);
void dec_init_kn230(void);
void dec_init_kn02(void);
void dec_init_kn02ba(void);
void dec_init_kn02ca(void);
void dec_init_kn03(void);

char *dec_rtc_base = (char *) KN01_RTC_BASE;	/* Assume DS2100/3100 initially */

decint_t dec_interrupt[NR_INTS];

/* 
 * Information regarding the IRQ Controller
 *
 * isr and imr are also hardcoded for different machines in int_handler.S
 */

volatile unsigned int *isr = 0L;	/* address of the interrupt status register     */
volatile unsigned int *imr = 0L;	/* address of the interrupt mask register       */

extern void dec_machine_restart(char *command);
extern void dec_machine_halt(void);
extern void dec_machine_power_off(void);

extern void wbflush_setup(void);

extern struct rtc_ops dec_rtc_ops;

extern void intr_halt(void);

extern int setup_dec_irq(int, struct irqaction *);

void (*board_time_init) (struct irqaction * irq);

static void __init dec_irq_setup(void)
{
    switch (mips_machtype) {
    case MACH_DS23100:
	dec_init_kn01();
	break;
    case MACH_DS5100:		/*  DS5100 MIPSMATE */
	dec_init_kn230();
	break;
    case MACH_DS5000_200:	/* DS5000 3max */
	dec_init_kn02();
	break;
    case MACH_DS5000_1XX:	/* DS5000/100 3min */
	dec_init_kn02ba();
	break;
    case MACH_DS5000_2X0:	/* DS5000/240 3max+ */
	dec_init_kn03();
	break;
    case MACH_DS5000_XX:	/* Personal DS5000/2x */
	dec_init_kn02ca();
	break;
    case MACH_DS5800:		/* DS5800 Isis */
	panic("Don't know how to set this up!");
	break;
    case MACH_DS5400:		/* DS5400 MIPSfair */
	panic("Don't know how to set this up!");
	break;
    case MACH_DS5500:		/* DS5500 MIPSfair-2 */
	panic("Don't know how to set this up!");
	break;
    }
    set_except_vector(0, decstation_handle_int);
}

/*
 * enable the periodic interrupts
 */
static void __init dec_time_init(struct irqaction *irq)
{
    /*
     * Here we go, enable periodic rtc interrupts.
     */

#ifndef LOG_2_HZ
#  define LOG_2_HZ 7
#endif

    CMOS_WRITE(RTC_REF_CLCK_32KHZ | (16 - LOG_2_HZ), RTC_REG_A);
    CMOS_WRITE(CMOS_READ(RTC_REG_B) | RTC_PIE, RTC_REG_B);
    setup_dec_irq(CLOCK, irq);
}

void __init decstation_setup(void)
{
    irq_setup = dec_irq_setup;
    board_time_init = dec_time_init;

    wbflush_setup();

    _machine_restart = dec_machine_restart;
    _machine_halt = dec_machine_halt;
    _machine_power_off = dec_machine_power_off;

    rtc_ops = &dec_rtc_ops;
}

/*
 * Machine-specific initialisation for kn01, aka Pmax, aka DS2100, DS3100,
 * and possibly also the DS5100.
 */
void __init dec_init_kn01(void)
{
    /*
     * Setup some memory addresses.
     */
    dec_rtc_base = (char *) KN01_RTC_BASE;

    /*
     * Setup interrupt structure
     */
    dec_interrupt[CLOCK].cpu_mask = IE_IRQ3;
    dec_interrupt[CLOCK].iemask = 0;
    cpu_mask_tbl[0] = IE_IRQ3;
    cpu_irq_nr[0] = CLOCK;

    dec_interrupt[SCSI_INT].cpu_mask = IE_IRQ0;
    dec_interrupt[SCSI_INT].iemask = 0;
    cpu_mask_tbl[1] = IE_IRQ0;
    cpu_irq_nr[1] = SCSI_INT;

    dec_interrupt[ETHER].cpu_mask = IE_IRQ1;
    dec_interrupt[ETHER].iemask = 0;
    cpu_mask_tbl[2] = IE_IRQ1;
    cpu_irq_nr[2] = ETHER;

    dec_interrupt[SERIAL].cpu_mask = IE_IRQ2;
    dec_interrupt[SERIAL].iemask = 0;
    cpu_mask_tbl[3] = IE_IRQ2;
    cpu_irq_nr[3] = SERIAL;

    dec_interrupt[MEMORY].cpu_mask = IE_IRQ4;
    dec_interrupt[MEMORY].iemask = 0;
    cpu_mask_tbl[4] = IE_IRQ4;
    cpu_irq_nr[4] = MEMORY;

    dec_interrupt[FPU].cpu_mask = IE_IRQ5;
    dec_interrupt[FPU].iemask = 0;
    cpu_mask_tbl[5] = IE_IRQ5;
    cpu_irq_nr[5] = FPU;
}				/* dec_init_kn01 */

/*
 * Machine-specific initialisation for kn230, aka MIPSmate, aka DS5100
 *
 * There are a lot of experiments to do, this is definitely incomplete.
 */
void __init dec_init_kn230(void)
{
    /*
     * Setup some memory addresses.
     */
    dec_rtc_base = (char *) KN01_RTC_BASE;

    /*
     * Setup interrupt structure
     */
    dec_interrupt[CLOCK].cpu_mask = IE_IRQ2;
    dec_interrupt[CLOCK].iemask = 0;
    cpu_mask_tbl[0] = IE_IRQ2;
    cpu_irq_nr[0] = CLOCK;

    dec_interrupt[FPU].cpu_mask = IE_IRQ5;
    dec_interrupt[FPU].iemask = 0;
    cpu_mask_tbl[5] = IE_IRQ5;
    cpu_irq_nr[5] = FPU;
}				/* dec_init_kn230 */

/*
 * Machine-specific initialisation for kn02, aka 3max, aka DS5000/2xx.
 */
void __init dec_init_kn02(void)
{
    /*
     * Setup some memory addresses. FIXME: probably incomplete!
     */
    dec_rtc_base = (char *) KN02_RTC_BASE;
    isr = (volatile unsigned int *) KN02_CSR_ADDR;
    imr = (volatile unsigned int *) KN02_CSR_ADDR;

    /*
     * Setup IOASIC interrupt
     */
    cpu_ivec_tbl[1] = kn02_io_int;
    cpu_mask_tbl[1] = IE_IRQ0;
    cpu_irq_nr[1] = -1;
    *imr = *imr & 0xff00ff00;

    /*
     * Setup interrupt structure
     */
    dec_interrupt[CLOCK].cpu_mask = IE_IRQ1;
    dec_interrupt[CLOCK].iemask = 0;
    cpu_mask_tbl[0] = IE_IRQ1;
    cpu_irq_nr[0] = CLOCK;

    dec_interrupt[SCSI_INT].cpu_mask = IE_IRQ0;
    dec_interrupt[SCSI_INT].iemask = KN02_SLOT5;
    asic_mask_tbl[0] = KN02_SLOT5;
    asic_irq_nr[0] = SCSI_INT;

    dec_interrupt[ETHER].cpu_mask = IE_IRQ0;
    dec_interrupt[ETHER].iemask = KN02_SLOT6;
    asic_mask_tbl[1] = KN02_SLOT6;
    asic_irq_nr[1] = ETHER;

    dec_interrupt[SERIAL].cpu_mask = IE_IRQ0;
    dec_interrupt[SERIAL].iemask = KN02_SLOT7;
    asic_mask_tbl[2] = KN02_SLOT7;
    asic_irq_nr[2] = SERIAL;

    dec_interrupt[TC0].cpu_mask = IE_IRQ0;
    dec_interrupt[TC0].iemask = KN02_SLOT0;
    asic_mask_tbl[3] = KN02_SLOT0;
    asic_irq_nr[3] = TC0;

    dec_interrupt[TC1].cpu_mask = IE_IRQ0;
    dec_interrupt[TC1].iemask = KN02_SLOT1;
    asic_mask_tbl[4] = KN02_SLOT1;
    asic_irq_nr[4] = TC1;

    dec_interrupt[TC2].cpu_mask = IE_IRQ0;
    dec_interrupt[TC2].iemask = KN02_SLOT2;
    asic_mask_tbl[5] = KN02_SLOT2;
    asic_irq_nr[5] = TC2;

    dec_interrupt[MEMORY].cpu_mask = IE_IRQ3;
    dec_interrupt[MEMORY].iemask = 0;
    cpu_mask_tbl[2] = IE_IRQ3;
    cpu_irq_nr[2] = MEMORY;

    dec_interrupt[FPU].cpu_mask = IE_IRQ5;
    dec_interrupt[FPU].iemask = 0;
    cpu_mask_tbl[3] = IE_IRQ5;
    cpu_irq_nr[3] = FPU;

}				/* dec_init_kn02 */

/*
 * Machine-specific initialisation for kn02ba, aka 3min, aka DS5000/1xx.
 */
void __init dec_init_kn02ba(void)
{
    /*
     * Setup some memory addresses.
     */
    dec_rtc_base = (char *) KN02XA_RTC_BASE;
    isr = (volatile unsigned int *) KN02XA_SIR_ADDR;
    imr = (volatile unsigned int *) KN02XA_SIRM_ADDR;

    /*
     * Setup IOASIC interrupt
     */
    cpu_mask_tbl[0] = IE_IRQ3;
    cpu_irq_nr[0] = -1;
    cpu_ivec_tbl[0] = kn02ba_io_int;
    *imr = 0;

    /*
     * Setup interrupt structure
     */
    dec_interrupt[CLOCK].cpu_mask = IE_IRQ3;
    dec_interrupt[CLOCK].iemask = KMIN_CLOCK;
    asic_mask_tbl[0] = KMIN_CLOCK;
    asic_irq_nr[0] = CLOCK;

    dec_interrupt[SCSI_DMA_INT].cpu_mask = IE_IRQ3;
    dec_interrupt[SCSI_DMA_INT].iemask = SCSI_DMA_INTS;
    asic_mask_tbl[1] = SCSI_DMA_INTS;
    asic_irq_nr[1] = SCSI_DMA_INT;

    dec_interrupt[SCSI_INT].cpu_mask = IE_IRQ3;
    dec_interrupt[SCSI_INT].iemask = SCSI_CHIP;
    asic_mask_tbl[2] = SCSI_CHIP;
    asic_irq_nr[2] = SCSI_INT;

    dec_interrupt[ETHER].cpu_mask = IE_IRQ3;
    dec_interrupt[ETHER].iemask = LANCE_INTS;
    asic_mask_tbl[3] = LANCE_INTS;
    asic_irq_nr[3] = ETHER;

    dec_interrupt[SERIAL].cpu_mask = IE_IRQ3;
    dec_interrupt[SERIAL].iemask = SERIAL_INTS;
    asic_mask_tbl[4] = SERIAL_INTS;
    asic_irq_nr[4] = SERIAL;

    dec_interrupt[MEMORY].cpu_mask = IE_IRQ3;
    dec_interrupt[MEMORY].iemask = KMIN_TIMEOUT;
    asic_mask_tbl[5] = KMIN_TIMEOUT;
    asic_irq_nr[5] = MEMORY;

    dec_interrupt[TC0].cpu_mask = IE_IRQ0;
    dec_interrupt[TC0].iemask = 0;
    cpu_mask_tbl[1] = IE_IRQ0;
    cpu_irq_nr[1] = TC0;

    dec_interrupt[TC1].cpu_mask = IE_IRQ1;
    dec_interrupt[TC1].iemask = 0;
    cpu_mask_tbl[2] = IE_IRQ1;
    cpu_irq_nr[2] = TC1;

    dec_interrupt[TC2].cpu_mask = IE_IRQ2;
    dec_interrupt[TC2].iemask = 0;
    cpu_mask_tbl[3] = IE_IRQ2;
    cpu_irq_nr[3] = TC2;

    dec_interrupt[HALT].cpu_mask = IE_IRQ4;
    dec_interrupt[HALT].iemask = 0;
    cpu_mask_tbl[4] = IE_IRQ4;
    cpu_irq_nr[4] = HALT;

    dec_interrupt[FPU].cpu_mask = IE_IRQ5;
    dec_interrupt[FPU].iemask = 0;
    cpu_mask_tbl[5] = IE_IRQ5;
    cpu_irq_nr[5] = FPU;

}				/* dec_init_kn02ba */

/*
 * Machine-specific initialisation for kn02ca, aka maxine, aka DS5000/2x.
 */
void __init dec_init_kn02ca(void)
{
    /*
     * Setup some memory addresses. FIXME: probably incomplete!
     */
    dec_rtc_base = (char *) KN02XA_RTC_BASE;
    isr = (volatile unsigned int *) KN02XA_SIR_ADDR;
    imr = (volatile unsigned int *) KN02XA_SIRM_ADDR;

    /*
     * Setup IOASIC interrupt
     */
    cpu_ivec_tbl[1] = kn02ba_io_int;
    cpu_irq_nr[1] = -1;
    cpu_mask_tbl[1] = IE_IRQ3;
    *imr = 0;

    /*
     * Setup interrupt structure
     */
    dec_interrupt[CLOCK].cpu_mask = IE_IRQ1;
    dec_interrupt[CLOCK].iemask = 0;
    cpu_mask_tbl[0] = IE_IRQ1;
    cpu_irq_nr[0] = CLOCK;

    dec_interrupt[SCSI_DMA_INT].cpu_mask = IE_IRQ3;
    dec_interrupt[SCSI_DMA_INT].iemask = SCSI_DMA_INTS;
    asic_mask_tbl[0] = SCSI_DMA_INTS;
    asic_irq_nr[0] = SCSI_DMA_INT;

    dec_interrupt[SCSI_INT].cpu_mask = IE_IRQ3;
    dec_interrupt[SCSI_INT].iemask = SCSI_CHIP;
    asic_mask_tbl[1] = SCSI_CHIP;
    asic_irq_nr[1] = SCSI_INT;

    dec_interrupt[ETHER].cpu_mask = IE_IRQ3;
    dec_interrupt[ETHER].iemask = LANCE_INTS;
    asic_mask_tbl[2] = LANCE_INTS;
    asic_irq_nr[2] = ETHER;

    dec_interrupt[SERIAL].cpu_mask = IE_IRQ3;
    dec_interrupt[SERIAL].iemask = XINE_SERIAL_INTS;
    asic_mask_tbl[3] = XINE_SERIAL_INTS;
    asic_irq_nr[3] = SERIAL;

    dec_interrupt[TC0].cpu_mask = IE_IRQ3;
    dec_interrupt[TC0].iemask = MAXINE_TC0;
    asic_mask_tbl[4] = MAXINE_TC0;
    asic_irq_nr[4] = TC0;

    dec_interrupt[TC1].cpu_mask = IE_IRQ3;
    dec_interrupt[TC1].iemask = MAXINE_TC1;
    asic_mask_tbl[5] = MAXINE_TC1;
    asic_irq_nr[5] = TC1;

    dec_interrupt[MEMORY].cpu_mask = IE_IRQ2;
    dec_interrupt[MEMORY].iemask = 0;
    cpu_mask_tbl[2] = IE_IRQ2;
    cpu_irq_nr[2] = MEMORY;

    dec_interrupt[HALT].cpu_mask = IE_IRQ4;
    dec_interrupt[HALT].iemask = 0;
    cpu_mask_tbl[3] = IE_IRQ4;
    cpu_irq_nr[3] = HALT;

    dec_interrupt[FPU].cpu_mask = IE_IRQ5;
    dec_interrupt[FPU].iemask = 0;
    cpu_mask_tbl[4] = IE_IRQ5;
    cpu_irq_nr[4] = FPU;

}				/* dec_init_kn02ca */

/*
 * Machine-specific initialisation for kn03, aka 3max+, aka DS5000/240.
 */
void __init dec_init_kn03(void)
{
    /*
     * Setup some memory addresses. FIXME: probably incomplete!
     */
    dec_rtc_base = (char *) KN03_RTC_BASE;
    isr = (volatile unsigned int *) KN03_SIR_ADDR;
    imr = (volatile unsigned int *) KN03_SIRM_ADDR;

    /*
     * Setup IOASIC interrupt
     */
    cpu_ivec_tbl[1] = kn03_io_int;
    cpu_mask_tbl[1] = IE_IRQ0;
    cpu_irq_nr[1] = -1;
    *imr = 0;

    /*
     * Setup interrupt structure
     */
    dec_interrupt[CLOCK].cpu_mask = IE_IRQ1;
    dec_interrupt[CLOCK].iemask = 0;
    cpu_mask_tbl[0] = IE_IRQ1;
    cpu_irq_nr[0] = CLOCK;

    dec_interrupt[SCSI_DMA_INT].cpu_mask = IE_IRQ0;
    dec_interrupt[SCSI_DMA_INT].iemask = SCSI_DMA_INTS;
    asic_mask_tbl[0] = SCSI_DMA_INTS;
    asic_irq_nr[0] = SCSI_DMA_INT;

    dec_interrupt[SCSI_INT].cpu_mask = IE_IRQ0;
    dec_interrupt[SCSI_INT].iemask = SCSI_CHIP;
    asic_mask_tbl[1] = SCSI_CHIP;
    asic_irq_nr[1] = SCSI_INT;

    dec_interrupt[ETHER].cpu_mask = IE_IRQ0;
    dec_interrupt[ETHER].iemask = LANCE_INTS;
    asic_mask_tbl[2] = LANCE_INTS;
    asic_irq_nr[2] = ETHER;

    dec_interrupt[SERIAL].cpu_mask = IE_IRQ0;
    dec_interrupt[SERIAL].iemask = SERIAL_INTS;
    asic_mask_tbl[3] = SERIAL_INTS;
    asic_irq_nr[3] = SERIAL;

    dec_interrupt[TC0].cpu_mask = IE_IRQ0;
    dec_interrupt[TC0].iemask = KN03_TC0;
    asic_mask_tbl[4] = KN03_TC0;
    asic_irq_nr[4] = TC0;

    dec_interrupt[TC1].cpu_mask = IE_IRQ0;
    dec_interrupt[TC1].iemask = KN03_TC1;
    asic_mask_tbl[5] = KN03_TC1;
    asic_irq_nr[5] = TC1;

    dec_interrupt[TC2].cpu_mask = IE_IRQ0;
    dec_interrupt[TC2].iemask = KN03_TC2;
    asic_mask_tbl[6] = KN03_TC2;
    asic_irq_nr[6] = TC2;

    dec_interrupt[MEMORY].cpu_mask = IE_IRQ3;
    dec_interrupt[MEMORY].iemask = 0;
    cpu_mask_tbl[2] = IE_IRQ3;
    cpu_irq_nr[2] = MEMORY;

    dec_interrupt[HALT].cpu_mask = IE_IRQ4;
    dec_interrupt[HALT].iemask = 0;
    cpu_mask_tbl[3] = IE_IRQ4;
    cpu_irq_nr[3] = HALT;

    dec_interrupt[FPU].cpu_mask = IE_IRQ5;
    dec_interrupt[FPU].iemask = 0;
    cpu_mask_tbl[4] = IE_IRQ5;
    cpu_irq_nr[4] = FPU;

}				/* dec_init_kn03 */
