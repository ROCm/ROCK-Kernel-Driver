/*
 * include/asm-ppc/mpc52xx.h
 * 
 * Prototypes, etc. for the Freescale MPC52xx embedded cpu chips
 * May need to be cleaned as the port goes on ...
 *
 *
 * Maintainer : Sylvain Munaut <tnt@246tNt.com>
 *
 * Originally written by Dale Farnsworth <dfarnsworth@mvista.com> 
 * for the 2.4 kernel.
 *
 * Copyright (C) 2004 Sylvain Munaut <tnt@246tNt.com>
 * Copyright (C) 2003 MontaVista, Software, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef __ASM_MPC52xx_H__
#define __ASM_MPC52xx_H__

#ifndef __ASSEMBLY__
#include <asm/ppcboot.h>
#include <asm/types.h>

struct pt_regs;
struct ocp_def;
#endif /* __ASSEMBLY__ */


/* ======================================================================== */
/* Main registers/struct addresses                                          */
/* ======================================================================== */
/* Theses are PHYSICAL addresses !                                          */
/* TODO : There should be no static mapping, but it's not yet the case, so  */
/*        we require a 1:1 mapping                                          */

#define MPC52xx_MBAR		0xf0000000	/* Phys address */
#define MPC52xx_MBAR_SIZE	0x00010000
#define MPC52xx_MBAR_VIRT	0xf0000000	/* Virt address */

#define MPC52xx_MMAP_CTL	(MPC52xx_MBAR + 0x0000)
#define MPC52xx_CDM		(MPC52xx_MBAR + 0x0200)
#define MPC52xx_SFTRST		(MPC52xx_MBAR + 0x0220)
#define MPC52xx_SFTRST_BIT	0x01000000
#define MPC52xx_INTR		(MPC52xx_MBAR + 0x0500)
#define MPC52xx_GPTx(x)		(MPC52xx_MBAR + 0x0600 + ((x)<<4))
#define MPC52xx_RTC		(MPC52xx_MBAR + 0x0800)
#define MPC52xx_MSCAN1		(MPC52xx_MBAR + 0x0900)
#define MPC52xx_MSCAN2		(MPC52xx_MBAR + 0x0980)
#define MPC52xx_GPIO		(MPC52xx_MBAR + 0x0b00)
#define MPC52xx_PCI		(MPC52xx_MBAR + 0x0d00)
#define MPC52xx_USB_OHCI	(MPC52xx_MBAR + 0x1000)
#define MPC52xx_SDMA		(MPC52xx_MBAR + 0x1200)
#define MPC52xx_XLB		(MPC52xx_MBAR + 0x1f00)
#define MPC52xx_PSCx(x)		(MPC52xx_MBAR + 0x2000 + ((x)<<9))
#define MPC52xx_PSC1		(MPC52xx_MBAR + 0x2000)
#define MPC52xx_PSC2		(MPC52xx_MBAR + 0x2200)
#define MPC52xx_PSC3		(MPC52xx_MBAR + 0x2400)
#define MPC52xx_PSC4		(MPC52xx_MBAR + 0x2600)
#define MPC52xx_PSC5		(MPC52xx_MBAR + 0x2800)
#define MPC52xx_PSC6		(MPC52xx_MBAR + 0x2C00)
#define MPC52xx_FEC		(MPC52xx_MBAR + 0x3000)
#define MPC52xx_ATA		(MPC52xx_MBAR + 0x3a00)
#define MPC52xx_I2C1		(MPC52xx_MBAR + 0x3d00)
#define MPC52xx_I2C_MICR	(MPC52xx_MBAR + 0x3d20)
#define MPC52xx_I2C2		(MPC52xx_MBAR + 0x3d40)

/* SRAM used for SDMA */
#define MPC52xx_SRAM		(MPC52xx_MBAR + 0x8000)
#define MPC52xx_SRAM_SIZE	(16*1024)
#define MPC52xx_SDMA_MAX_TASKS	16

	/* Memory allocation block size */
#define MPC52xx_SDRAM_UNIT	0x8000		/* 32K byte */


/* ======================================================================== */
/* IRQ mapping                                                              */
/* ======================================================================== */
/* Be sure to look at mpc52xx_pic.h if you wish for whatever reason to change
 * this
 */

#define MPC52xx_CRIT_IRQ_NUM	4
#define MPC52xx_MAIN_IRQ_NUM	17
#define MPC52xx_SDMA_IRQ_NUM	17
#define MPC52xx_PERP_IRQ_NUM	23

#define MPC52xx_CRIT_IRQ_BASE	0
#define MPC52xx_MAIN_IRQ_BASE	(MPC52xx_CRIT_IRQ_BASE + MPC52xx_CRIT_IRQ_NUM)
#define MPC52xx_SDMA_IRQ_BASE	(MPC52xx_MAIN_IRQ_BASE + MPC52xx_MAIN_IRQ_NUM)
#define MPC52xx_PERP_IRQ_BASE	(MPC52xx_SDMA_IRQ_BASE + MPC52xx_SDMA_IRQ_NUM)

#define MPC52xx_IRQ0			(MPC52xx_CRIT_IRQ_BASE + 0)
#define MPC52xx_SLICE_TIMER_0_IRQ	(MPC52xx_CRIT_IRQ_BASE + 1)
#define MPC52xx_HI_INT_IRQ		(MPC52xx_CRIT_IRQ_BASE + 2)
#define MPC52xx_CCS_IRQ			(MPC52xx_CRIT_IRQ_BASE + 3)

#define MPC52xx_IRQ1			(MPC52xx_MAIN_IRQ_BASE + 1)
#define MPC52xx_IRQ2			(MPC52xx_MAIN_IRQ_BASE + 2)
#define MPC52xx_IRQ3			(MPC52xx_MAIN_IRQ_BASE + 3)

#define MPC52xx_SDMA_IRQ		(MPC52xx_PERP_IRQ_BASE + 0)
#define MPC52xx_PSC1_IRQ		(MPC52xx_PERP_IRQ_BASE + 1)
#define MPC52xx_PSC2_IRQ		(MPC52xx_PERP_IRQ_BASE + 2)
#define MPC52xx_PSC3_IRQ		(MPC52xx_PERP_IRQ_BASE + 3)
#define MPC52xx_PSC6_IRQ		(MPC52xx_PERP_IRQ_BASE + 4)
#define MPC52xx_IRDA_IRQ		(MPC52xx_PERP_IRQ_BASE + 4)
#define MPC52xx_FEC_IRQ			(MPC52xx_PERP_IRQ_BASE + 5)
#define MPC52xx_USB_IRQ			(MPC52xx_PERP_IRQ_BASE + 6)
#define MPC52xx_ATA_IRQ			(MPC52xx_PERP_IRQ_BASE + 7)
#define MPC52xx_PCI_CNTRL_IRQ		(MPC52xx_PERP_IRQ_BASE + 8)
#define MPC52xx_PCI_SCIRX_IRQ		(MPC52xx_PERP_IRQ_BASE + 9)
#define MPC52xx_PCI_SCITX_IRQ		(MPC52xx_PERP_IRQ_BASE + 10)
#define MPC52xx_PSC4_IRQ		(MPC52xx_PERP_IRQ_BASE + 11)
#define MPC52xx_PSC5_IRQ		(MPC52xx_PERP_IRQ_BASE + 12)
#define MPC52xx_SPI_MODF_IRQ		(MPC52xx_PERP_IRQ_BASE + 13)
#define MPC52xx_SPI_SPIF_IRQ		(MPC52xx_PERP_IRQ_BASE + 14)
#define MPC52xx_I2C1_IRQ		(MPC52xx_PERP_IRQ_BASE + 15)
#define MPC52xx_I2C2_IRQ		(MPC52xx_PERP_IRQ_BASE + 16)
#define MPC52xx_CAN1_IRQ		(MPC52xx_PERP_IRQ_BASE + 17)
#define MPC52xx_CAN2_IRQ		(MPC52xx_PERP_IRQ_BASE + 18)
#define MPC52xx_IR_RX_IRQ		(MPC52xx_PERP_IRQ_BASE + 19)
#define MPC52xx_IR_TX_IRQ		(MPC52xx_PERP_IRQ_BASE + 20)
#define MPC52xx_XLB_ARB_IRQ		(MPC52xx_PERP_IRQ_BASE + 21)



/* ======================================================================== */
/* Structures mapping of some unit register set                             */
/* ======================================================================== */

#ifndef __ASSEMBLY__

/* Memory Mapping Control */
struct mpc52xx_mmap_ctl {
	volatile u32	mbar;		/* MMAP_CTRL + 0x00 */

	volatile u32	cs0_start;	/* MMAP_CTRL + 0x04 */
	volatile u32	cs0_stop;	/* MMAP_CTRL + 0x08 */
	volatile u32	cs1_start;	/* MMAP_CTRL + 0x0c */
	volatile u32	cs1_stop;	/* MMAP_CTRL + 0x10 */
	volatile u32	cs2_start;	/* MMAP_CTRL + 0x14 */
	volatile u32	cs2_stop;	/* MMAP_CTRL + 0x18 */
	volatile u32	cs3_start;	/* MMAP_CTRL + 0x1c */
	volatile u32	cs3_stop;	/* MMAP_CTRL + 0x20 */
	volatile u32	cs4_start;	/* MMAP_CTRL + 0x24 */
	volatile u32	cs4_stop;	/* MMAP_CTRL + 0x28 */
	volatile u32	cs5_start;	/* MMAP_CTRL + 0x2c */
	volatile u32	cs5_stop;	/* MMAP_CTRL + 0x30 */

	volatile u32	sdram0;		/* MMAP_CTRL + 0x34 */
	volatile u32	sdram1;		/* MMAP_CTRL + 0X38 */

	volatile u32	reserved[4];	/* MMAP_CTRL + 0x3c .. 0x48 */

	volatile u32	boot_start;	/* MMAP_CTRL + 0x4c */
	volatile u32	boot_stop;	/* MMAP_CTRL + 0x50 */
	
	volatile u32	ipbi_ws_ctrl;	/* MMAP_CTRL + 0x54 */
	
	volatile u32	cs6_start;	/* MMAP_CTRL + 0x58 */
	volatile u32	cs6_stop;	/* MMAP_CTRL + 0x5c */
	volatile u32	cs7_start;	/* MMAP_CTRL + 0x60 */
	volatile u32	cs7_stop;	/* MMAP_CTRL + 0x60 */
};

/* Interrupt controller */
struct mpc52xx_intr {
	volatile u32	per_mask;	/* INTR + 0x00 */
	volatile u32	per_pri1;	/* INTR + 0x04 */
	volatile u32	per_pri2;	/* INTR + 0x08 */
	volatile u32	per_pri3;	/* INTR + 0x0c */
	volatile u32	ctrl;		/* INTR + 0x10 */
	volatile u32	main_mask;	/* INTR + 0x14 */
	volatile u32	main_pri1;	/* INTR + 0x18 */
	volatile u32	main_pri2;	/* INTR + 0x1c */
	volatile u32	reserved1;	/* INTR + 0x20 */
	volatile u32	enc_status;	/* INTR + 0x24 */
	volatile u32	crit_status;	/* INTR + 0x28 */
	volatile u32	main_status;	/* INTR + 0x2c */
	volatile u32	per_status;	/* INTR + 0x30 */
	volatile u32	reserved2;	/* INTR + 0x34 */
	volatile u32	per_error;	/* INTR + 0x38 */
};

/* SDMA */
struct mpc52xx_sdma {
	volatile u32	taskBar;	/* SDMA + 0x00 */
	volatile u32	currentPointer;	/* SDMA + 0x04 */
	volatile u32	endPointer;	/* SDMA + 0x08 */
	volatile u32	variablePointer;/* SDMA + 0x0c */

	volatile u8	IntVect1;	/* SDMA + 0x10 */
	volatile u8	IntVect2;	/* SDMA + 0x11 */
	volatile u16	PtdCntrl;	/* SDMA + 0x12 */

	volatile u32	IntPend;	/* SDMA + 0x14 */
	volatile u32	IntMask;	/* SDMA + 0x18 */
	
	volatile u16	tcr[16];	/* SDMA + 0x1c .. 0x3a */

	volatile u8	ipr[31];	/* SDMA + 0x3c .. 5b */

	volatile u32	res1;		/* SDMA + 0x5c */
	volatile u32	task_size0;	/* SDMA + 0x60 */
	volatile u32	task_size1;	/* SDMA + 0x64 */
	volatile u32	MDEDebug;	/* SDMA + 0x68 */
	volatile u32	ADSDebug;	/* SDMA + 0x6c */
	volatile u32	Value1;		/* SDMA + 0x70 */
	volatile u32	Value2;		/* SDMA + 0x74 */
	volatile u32	Control;	/* SDMA + 0x78 */
	volatile u32	Status;		/* SDMA + 0x7c */
};

/* GPT */
struct mpc52xx_gpt {
	volatile u32	mode;		/* GPTx + 0x00 */
	volatile u32	count;		/* GPTx + 0x04 */
	volatile u32	pwm;		/* GPTx + 0x08 */
	volatile u32	status;		/* GPTx + 0X0c */
};

/* RTC */
struct mpc52xx_rtc {
	volatile u32	time_set;	/* RTC + 0x00 */
	volatile u32	date_set;	/* RTC + 0x04 */
	volatile u32	stopwatch;	/* RTC + 0x08 */
	volatile u32	int_enable;	/* RTC + 0x0c */
	volatile u32	time;		/* RTC + 0x10 */
	volatile u32	date;		/* RTC + 0x14 */
	volatile u32	stopwatch_intr;	/* RTC + 0x18 */
	volatile u32	bus_error;	/* RTC + 0x1c */
	volatile u32	dividers;	/* RTC + 0x20 */
};

/* GPIO */
struct mpc52xx_gpio {
	volatile u32	port_config;	/* GPIO + 0x00 */
	volatile u32	simple_gpioe;	/* GPIO + 0x04 */
	volatile u32	simple_ode;	/* GPIO + 0x08 */
	volatile u32	simple_ddr;	/* GPIO + 0x0c */
	volatile u32	simple_dvo;	/* GPIO + 0x10 */
	volatile u32	simple_ival;	/* GPIO + 0x14 */
	volatile u8	outo_gpioe;	/* GPIO + 0x18 */
	volatile u8	reserved1[3];	/* GPIO + 0x19 */
	volatile u8	outo_dvo;	/* GPIO + 0x1c */
	volatile u8	reserved2[3];	/* GPIO + 0x1d */
	volatile u8	sint_gpioe;	/* GPIO + 0x20 */
	volatile u8	reserved3[3];	/* GPIO + 0x21 */
	volatile u8	sint_ode;	/* GPIO + 0x24 */
	volatile u8	reserved4[3];	/* GPIO + 0x25 */
	volatile u8	sint_ddr;	/* GPIO + 0x28 */
	volatile u8	reserved5[3];	/* GPIO + 0x29 */
	volatile u8	sint_dvo;	/* GPIO + 0x2c */
	volatile u8	reserved6[3];	/* GPIO + 0x2d */
	volatile u8	sint_inten;	/* GPIO + 0x30 */
	volatile u8	reserved7[3];	/* GPIO + 0x31 */
	volatile u16	sint_itype;	/* GPIO + 0x34 */
	volatile u16	reserved8;	/* GPIO + 0x36 */
	volatile u8	gpio_control;	/* GPIO + 0x38 */
	volatile u8	reserved9[3];	/* GPIO + 0x39 */
	volatile u8	sint_istat;	/* GPIO + 0x3c */
	volatile u8	sint_ival;	/* GPIO + 0x3d */
	volatile u8	bus_errs;	/* GPIO + 0x3e */
	volatile u8	reserved10;	/* GPIO + 0x3f */
};

#define MPC52xx_GPIO_PSC_CONFIG_UART_WITHOUT_CD	4
#define MPC52xx_GPIO_PSC_CONFIG_UART_WITH_CD	5
#define MPC52xx_GPIO_PCI_DIS			(1<<15)

/* XLB Bus control */
struct mpc52xx_xlb {
	volatile u8 reserved[0x40];
	volatile u32 config;		/* XLB + 0x40 */
	volatile u32 version;		/* XLB + 0x44 */
	volatile u32 status;		/* XLB + 0x48 */
	volatile u32 int_enable;	/* XLB + 0x4c */
	volatile u32 addr_capture;	/* XLB + 0x50 */
	volatile u32 bus_sig_capture;	/* XLB + 0x54 */
	volatile u32 addr_timeout;	/* XLB + 0x58 */
	volatile u32 data_timeout;	/* XLB + 0x5c */
	volatile u32 bus_act_timeout;	/* XLB + 0x60 */
	volatile u32 master_pri_enable;	/* XLB + 0x64 */
	volatile u32 master_priority;	/* XLB + 0x68 */
	volatile u32 base_address;	/* XLB + 0x6c */
	volatile u32 snoop_window;	/* XLB + 0x70 */
};


/* Clock Distribution control */
struct mpc52xx_cdm {
	volatile u32	jtag_id;	/* MBAR_CDM + 0x00  reg0 read only */
	volatile u32	rstcfg;		/* MBAR_CDM + 0x04  reg1 read only */
	volatile u32	breadcrumb;	/* MBAR_CDM + 0x08  reg2 */

	volatile u8	mem_clk_sel;	/* MBAR_CDM + 0x0c  reg3 byte0 */
	volatile u8	xlb_clk_sel;	/* MBAR_CDM + 0x0d  reg3 byte1 read only */
	volatile u8	ipb_clk_sel;	/* MBAR_CDM + 0x0e  reg3 byte2 */
	volatile u8	pci_clk_sel;	/* MBAR_CDM + 0x0f  reg3 byte3 */

	volatile u8	ext_48mhz_en;	/* MBAR_CDM + 0x10  reg4 byte0 */
	volatile u8	fd_enable;	/* MBAR_CDM + 0x11  reg4 byte1 */
	volatile u16	fd_counters;	/* MBAR_CDM + 0x12  reg4 byte2,3 */

	volatile u32	clk_enables;	/* MBAR_CDM + 0x14  reg5 */

	volatile u8	osc_disable;	/* MBAR_CDM + 0x18  reg6 byte0 */
	volatile u8	reserved0[3];	/* MBAR_CDM + 0x19  reg6 byte1,2,3 */

	volatile u8	ccs_sleep_enable;/* MBAR_CDM + 0x1c  reg7 byte0 */
	volatile u8	osc_sleep_enable;/* MBAR_CDM + 0x1d  reg7 byte1 */
	volatile u8	reserved1;	/* MBAR_CDM + 0x1e  reg7 byte2 */
	volatile u8	ccs_qreq_test;	/* MBAR_CDM + 0x1f  reg7 byte3 */

	volatile u8	soft_reset;	/* MBAR_CDM + 0x20  u8 byte0 */
	volatile u8	no_ckstp;	/* MBAR_CDM + 0x21  u8 byte0 */
	volatile u8	reserved2[2];	/* MBAR_CDM + 0x22  u8 byte1,2,3 */

	volatile u8	pll_lock;	/* MBAR_CDM + 0x24  reg9 byte0 */
	volatile u8	pll_looselock;	/* MBAR_CDM + 0x25  reg9 byte1 */
	volatile u8	pll_sm_lockwin;	/* MBAR_CDM + 0x26  reg9 byte2 */
	volatile u8	reserved3;	/* MBAR_CDM + 0x27  reg9 byte3 */

	volatile u16	reserved4;	/* MBAR_CDM + 0x28  reg10 byte0,1 */
	volatile u16	mclken_div_psc1;/* MBAR_CDM + 0x2a  reg10 byte2,3 */
    
	volatile u16	reserved5;	/* MBAR_CDM + 0x2c  reg11 byte0,1 */
	volatile u16	mclken_div_psc2;/* MBAR_CDM + 0x2e  reg11 byte2,3 */
		
	volatile u16	reserved6;	/* MBAR_CDM + 0x30  reg12 byte0,1 */
	volatile u16	mclken_div_psc3;/* MBAR_CDM + 0x32  reg12 byte2,3 */
    
	volatile u16	reserved7;	/* MBAR_CDM + 0x34  reg13 byte0,1 */
	volatile u16	mclken_div_psc6;/* MBAR_CDM + 0x36  reg13 byte2,3 */
};

#endif /* __ASSEMBLY__ */


/* ========================================================================= */
/* Prototypes for MPC52xx syslib                                             */
/* ========================================================================= */

#ifndef __ASSEMBLY__

extern void mpc52xx_init_irq(void);
extern int mpc52xx_get_irq(struct pt_regs *regs);

extern unsigned long mpc52xx_find_end_of_memory(void);
extern void mpc52xx_set_bat(void);
extern void mpc52xx_map_io(void);
extern void mpc52xx_restart(char *cmd);
extern void mpc52xx_halt(void);
extern void mpc52xx_power_off(void);
extern void mpc52xx_progress(char *s, unsigned short hex);
extern void mpc52xx_calibrate_decr(void);
extern void mpc52xx_add_board_devices(struct ocp_def board_ocp[]);

#endif /* __ASSEMBLY__ */


/* ========================================================================= */
/* Platform configuration                                                    */
/* ========================================================================= */

/* The U-Boot platform information struct */
extern bd_t __res;

/* Platform options */
#if defined(CONFIG_LITE5200)
#include <platforms/lite5200.h>
#endif


#endif /* __ASM_MPC52xx_H__ */
