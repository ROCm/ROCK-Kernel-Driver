/* $Id: sgihpc.h,v 1.2 1999/12/06 23:13:21 ralf Exp $
 *
 * sgihpc.h: Various HPC I/O controller defines.  The HPC is basically
 *           the approximate functional equivalent of the Sun SYSIO
 *           on SGI INDY machines.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1998 Ralf Baechle (ralf@gnu.org)
 */
#ifndef _MIPS_SGIHPC_H
#define _MIPS_SGIHPC_H

#include <asm/page.h>

extern int sgi_has_ioc2; /* to know if we have older ioc1 or ioc2. */
extern int sgi_guiness;  /* GUINESS or FULLHOUSE machine. */
extern int sgi_boardid;  /* Board revision. */

/* An HPC dma descriptor. */
struct hpc_dma_desc {
	unsigned int pbuf;      /* physical address of data buffer */
	unsigned int cntinfo;   /* counter and info bits */
#define HPCDMA_EOX    0x80000000 /* last desc in chain for tx */
#define HPCDMA_EOR    0x80000000 /* last desc in chain for rx */
#define HPCDMA_EOXP   0x40000000 /* end of packet for tx */
#define HPCDMA_EORP   0x40000000 /* end of packet for rx */
#define HPCDMA_XIE    0x20000000 /* irq generated when at end of this desc */
#define HPCDMA_XIU    0x01000000 /* Tx buffer in use by CPU. */
#define HPCDMA_EIPC   0x00ff0000 /* SEEQ ethernet special xternal bytecount */
#define HPCDMA_ETXD   0x00008000 /* set to one by HPC when packet tx'd */
#define HPCDMA_OWN    0x00004000 /* Denotes ring buffer ownership on rx */
#define HPCDMA_BCNT   0x00003fff /* size in bytes of this dma buffer */

	unsigned int pnext;     /* paddr of next hpc_dma_desc if any */
};

typedef volatile unsigned int hpcreg;

/* HPC1 stuff. */

/* HPC3 stuff. */

/* The set of regs for each HPC3 pbus dma channel. */
struct hpc3_pbus_dmacregs {
	hpcreg pbdma_bptr; /* pbus dma channel buffer ptr */
	hpcreg pbdma_dptr; /* pbus dma channel desc ptr */
	char _unused1[PAGE_SIZE - (2 * sizeof(hpcreg))]; /* padding */
	hpcreg pbdma_ctrl; /* pbus dma channel control reg */
#define HPC3_PDMACTRL_SEL  0x00000002 /* little endian transfer */
#define HPC3_PDMACTRL_RCV  0x00000004 /* direction is receive */
#define HPC3_PDMACTRL_FLSH 0x00000008 /* enable flush for receive DMA */
#define HPC3_PDMACTRL_ACT  0x00000010 /* start dma transfer */
#define HPC3_PDMACTRL_LD   0x00000020 /* load enable for ACT */
#define HPC3_PDMACTRL_RT   0x00000040 /* Use realtime GIO bus servicing */
#define HPC3_PDMACTRL_HW   0x0000ff00 /* DMA High-water mark */
#define HPC3_PDMACTRL_FB   0x003f0000 /* Ptr to beginning of fifo */
#define HPC3_PDMACTRL_FE   0x3f000000 /* Ptr to end of fifo */

	char _unused2[PAGE_SIZE - (sizeof(hpcreg))]; /* padding */
};

/* The HPC3 scsi registers, this does not include external ones. */
struct hpc3_scsiregs {
	hpcreg cbptr;   /* current dma buffer ptr, diagnostic use only */
	hpcreg ndptr;   /* next dma descriptor ptr */
	char _unused1[PAGE_SIZE - (2 * sizeof(hpcreg))]; /* padding */
	hpcreg bcd;     /* byte count info */
#define HPC3_SBCD_BCNTMSK 0x00003fff /* bytes to transfer from/to memory */
#define HPC3_SBCD_XIE     0x00004000 /* Send IRQ when done with cur buf */
#define HPC3_SBCD_EOX     0x00008000 /* Indicates this is last buf in chain */

	hpcreg ctrl;    /* control register */
#define HPC3_SCTRL_IRQ    0x01 /* IRQ asserted, either dma done or parity */
#define HPC3_SCTRL_ENDIAN 0x02 /* DMA endian mode, 0=big 1=little */
#define HPC3_SCTRL_DIR    0x04 /* DMA direction, 1=dev2mem 0=mem2dev */
#define HPC3_SCTRL_FLUSH  0x08 /* Tells HPC3 to flush scsi fifos */
#define HPC3_SCTRL_ACTIVE 0x10 /* SCSI DMA channel is active */
#define HPC3_SCTRL_AMASK  0x20 /* DMA active inhibits PIO */
#define HPC3_SCTRL_CRESET 0x40 /* Resets dma channel and external controller */
#define HPC3_SCTRL_PERR   0x80 /* Bad parity on HPC3 iface to scsi controller */

	hpcreg gfptr;   /* current GIO fifo ptr */
	hpcreg dfptr;   /* current device fifo ptr */
	hpcreg dconfig; /* DMA configuration register */
#define HPC3_SDCFG_HCLK 0x00001 /* Enable DMA half clock mode */
#define HPC3_SDCFG_D1   0x00006 /* Cycles to spend in D1 state */
#define HPC3_SDCFG_D2   0x00038 /* Cycles to spend in D2 state */
#define HPC3_SDCFG_D3   0x001c0 /* Cycles to spend in D3 state */
#define HPC3_SDCFG_HWAT 0x00e00 /* DMA high water mark */
#define HPC3_SDCFG_HW   0x01000 /* Enable 16-bit halfword DMA accesses to scsi */
#define HPC3_SDCFG_SWAP 0x02000 /* Byte swap all DMA accesses */
#define HPC3_SDCFG_EPAR 0x04000 /* Enable parity checking for DMA */
#define HPC3_SDCFG_POLL 0x08000 /* hd_dreq polarity control */
#define HPC3_SDCFG_ERLY 0x30000 /* hd_dreq behavior control bits */

	hpcreg pconfig; /* PIO configuration register */
#define HPC3_SPCFG_P3   0x0003 /* Cycles to spend in P3 state */
#define HPC3_SPCFG_P2W  0x001c /* Cycles to spend in P2 state for writes */
#define HPC3_SPCFG_P2R  0x01e0 /* Cycles to spend in P2 state for reads */
#define HPC3_SPCFG_P1   0x0e00 /* Cycles to spend in P1 state */
#define HPC3_SPCFG_HW   0x1000 /* Enable 16-bit halfword PIO accesses to scsi */
#define HPC3_SPCFG_SWAP 0x2000 /* Byte swap all PIO accesses */
#define HPC3_SPCFG_EPAR 0x4000 /* Enable parity checking for PIO */
#define HPC3_SPCFG_FUJI 0x8000 /* Fujitsu scsi controller mode for faster dma/pio */

	char _unused2[PAGE_SIZE - (6 * sizeof(hpcreg))]; /* padding */
};

/* SEEQ ethernet HPC3 registers, only one seeq per HPC3. */
struct hpc3_ethregs {
	/* Receiver registers. */
	hpcreg rx_cbptr;   /* current dma buffer ptr, diagnostic use only */
	hpcreg rx_ndptr;   /* next dma descriptor ptr */
	char _unused1[PAGE_SIZE - (2 * sizeof(hpcreg))]; /* padding */
	hpcreg rx_bcd;     /* byte count info */
#define HPC3_ERXBCD_BCNTMSK 0x00003fff /* bytes to be sent to memory */
#define HPC3_ERXBCD_XIE     0x20000000 /* HPC3 interrupts cpu at end of this buf */
#define HPC3_ERXBCD_EOX     0x80000000 /* flags this as end of descriptor chain */

	hpcreg rx_ctrl;    /* control register */
#define HPC3_ERXCTRL_STAT50 0x0000003f /* Receive status reg bits of Seeq8003 */
#define HPC3_ERXCTRL_STAT6  0x00000040 /* Rdonly irq status */
#define HPC3_ERXCTRL_STAT7  0x00000080 /* Rdonlt old/new status bit from Seeq */
#define HPC3_ERXCTRL_ENDIAN 0x00000100 /* Endian for dma channel, little=1 big=0 */
#define HPC3_ERXCTRL_ACTIVE 0x00000200 /* Tells if DMA transfer is in progress */
#define HPC3_ERXCTRL_AMASK  0x00000400 /* Tells if ACTIVE inhibits PIO's to hpc3 */
#define HPC3_ERXCTRL_RBO    0x00000800 /* Receive buffer overflow if set to 1 */

	hpcreg rx_gfptr;   /* current GIO fifo ptr */
	hpcreg rx_dfptr;   /* current device fifo ptr */
	hpcreg _unused2;   /* padding */
	hpcreg rx_reset;   /* reset register */
#define HPC3_ERXRST_CRESET 0x1 /* Reset dma channel and external controller */
#define HPC3_ERXRST_CLRIRQ 0x2 /* Clear channel interrupt */
#define HPC3_ERXRST_LBACK  0x4 /* Enable diagnostic loopback mode of Seeq8003 */

	hpcreg rx_dconfig; /* DMA configuration register */
#define HPC3_ERXDCFG_D1    0x0000f /* Cycles to spend in D1 state for PIO */
#define HPC3_ERXDCFG_D2    0x000f0 /* Cycles to spend in D2 state for PIO */
#define HPC3_ERXDCFG_D3    0x00f00 /* Cycles to spend in D3 state for PIO */
#define HPC3_ERXDCFG_WCTRL 0x01000 /* Enable writes of desc into ex ctrl port */
#define HPC3_ERXDCFG_FRXDC 0x02000 /* Clear eop stat bits upon rxdc, hw seeq fix */
#define HPC3_ERXDCFG_FEOP  0x04000 /* Bad packet marker timeout enable */
#define HPC3_ERXDCFG_FIRQ  0x08000 /* Another bad packet timeout enable */
#define HPC3_ERXDCFG_PTO   0x30000 /* Programmed timeout value for above two */

	hpcreg rx_pconfig; /* PIO configuration register */
#define HPC3_ERXPCFG_P1    0x000f /* Cycles to spend in P1 state for PIO */
#define HPC3_ERXPCFG_P2    0x00f0 /* Cycles to spend in P2 state for PIO */
#define HPC3_ERXPCFG_P3    0x0f00 /* Cycles to spend in P3 state for PIO */
#define HPC3_ERXPCFG_TST   0x1000 /* Diagnistic ram test feature bit */

	char _unused3[PAGE_SIZE - (8 * sizeof(hpcreg))]; /* padding */

	/* Transmitter registers. */
	hpcreg tx_cbptr;   /* current dma buffer ptr, diagnostic use only */
	hpcreg tx_ndptr;   /* next dma descriptor ptr */
	char _unused4[PAGE_SIZE - (2 * sizeof(hpcreg))]; /* padding */
	hpcreg tx_bcd;     /* byte count info */
#define HPC3_ETXBCD_BCNTMSK 0x00003fff /* bytes to be read from memory */
#define HPC3_ETXBCD_ESAMP   0x10000000 /* if set, too late to add descriptor */
#define HPC3_ETXBCD_XIE     0x20000000 /* Interrupt cpu at end of cur desc */
#define HPC3_ETXBCD_EOP     0x40000000 /* Last byte of cur buf is end of packet */
#define HPC3_ETXBCD_EOX     0x80000000 /* This buf is the end of desc chain */

	hpcreg tx_ctrl;    /* control register */
#define HPC3_ETXCTRL_STAT30 0x0000000f /* Rdonly copy of seeq tx stat reg */
#define HPC3_ETXCTRL_STAT4  0x00000010 /* Indicate late collision occurred */
#define HPC3_ETXCTRL_STAT75 0x000000e0 /* Rdonly irq status from seeq */
#define HPC3_ETXCTRL_ENDIAN 0x00000100 /* Dma channel endian mode, 1=little 0=big */
#define HPC3_ETXCTRL_ACTIVE 0x00000200 /* DMA tx channel is active */
#define HPC3_ETXCTRL_AMASK  0x00000400 /* Indicates ACTIVE inhibits PIO's */

	hpcreg tx_gfptr;   /* current GIO fifo ptr */
	hpcreg tx_dfptr;   /* current device fifo ptr */
	char _unused5[PAGE_SIZE - (4 * sizeof(hpcreg))]; /* padding */
};

struct hpc3_regs {
	/* First regs for the PBUS 8 dma channels. */
	struct hpc3_pbus_dmacregs pbdma[8];

	/* Now the HPC scsi registers, we get two scsi reg sets. */
	struct hpc3_scsiregs scsi_chan0, scsi_chan1;

	/* The SEEQ hpc3 ethernet dma/control registers. */
	struct hpc3_ethregs ethregs;

	/* Here are where the hpc3 fifo's can be directly accessed
	 * via PIO accesses.  Under normal operation we never stick
	 * our grubby paws in here so it's just padding.
	 */
	char _unused1[PAGE_SIZE * 24];

	/* HPC3 irq status regs.  Due to a peculiar bug you need to
	 * look at two different register addresses to get at all of
	 * the status bits.  The first reg can only reliably report
	 * bits 4:0 of the status, and the second reg can only
	 * reliably report bits 9:5 of the hpc3 irq status.  I told
	 * you it was a peculiar bug. ;-)
	 */
	hpcreg istat0;      /* Irq status, only bits <4:0> reliable. */
#define HPC3_ISTAT_PBIMASK  0x0ff  /* irq bits for pbus devs 0 --> 7 */
#define HPC3_ISTAT_SC0MASK  0x100  /* irq bit for scsi channel 0 */
#define HPC3_ISTAT_SC1MASK  0x200  /* irq bit for scsi channel 1 */

	hpcreg gio64_misc;  /* GIO64 misc control bits. */
#define HPC3_GIOMISC_ERTIME   0x1    /* Enable external timer real time. */
#define HPC3_GIOMISC_DENDIAN  0x2    /* dma descriptor endian, 1=lit 0=big */

	hpcreg eeprom_data; /* EEPROM data reg. */
#define HPC3_EEPROM_EPROT     0x01   /* Protect register enable */
#define HPC3_EEPROM_CSEL      0x02   /* Chip select */
#define HPC3_EEPROM_ECLK      0x04   /* EEPROM clock */
#define HPC3_EEPROM_DATO      0x08   /* Data out */
#define HPC3_EEPROM_DATI      0x10   /* Data in */

	hpcreg istat1;      /* Irq status, only bits <9:5> reliable. */
	hpcreg gio64_estat; /* GIO64 error interrupt status reg. */
#define HPC3_GIOESTAT_BLMASK  0x000ff  /* Bus lane where bad parity occurred */
#define HPC3_GIOESTAT_CTYPE   0x00100  /* Bus cycle type, 0=PIO 1=DMA */
#define HPC3_GIOESTAT_PIDMSK  0x3f700  /* DMA channel parity identifier */

	/* Now direct PIO per-HPC3 peripheral access to external regs. */
	char _unused2[0x13fec]; /* Trust me... */
	hpcreg scsi0_ext[256];  /* SCSI channel 0 external regs */
	char _unused3[0x07c00]; /* Trust me... */
	hpcreg scsi1_ext[256];  /* SCSI channel 1 external regs */
	char _unused4[0x07c00]; /* It'll only hurt a little... */

	/* Did DaveM forget the ethernet external regs?
	 * Anyhow, they're not here and we need some padding instead.
	 */
	char _unused5[0x04000]; /* It'll hurt a lot if you leave this out */

	/* Per-peripheral device external registers and dma/pio control. */
	hpcreg pbus_extregs[16][256]; /* 2nd indice indexes controller */
	hpcreg pbus_dmacfgs[8][128]; /* 2nd indice indexes controller */
#define HPC3_PIODCFG_D3R    0x00000001 /* Cycles to spend in D3 for reads */
#define HPC3_PIODCFG_D4R    0x0000001e /* Cycles to spend in D4 for reads */
#define HPC3_PIODCFG_D5R    0x000001e0 /* Cycles to spend in D5 for reads */
#define HPC3_PIODCFG_D3W    0x00000200 /* Cycles to spend in D3 for writes */
#define HPC3_PIODCFG_D4W    0x00003c00 /* Cycles to spend in D4 for writes */
#define HPC3_PIODCFG_D5W    0x0003c000 /* Cycles to spend in D5 for writes */
#define HPC3_PIODCFG_HWORD  0x00040000 /* Enable 16-bit dma access mode */
#define HPC3_PIODCFG_EHI    0x00080000 /* Places halfwords on high 16 bits of bus */
#define HPC3_PIODCFG_RTIME  0x00200000 /* Make this device real time on GIO bus */
#define HPC3_PIODCFG_BURST  0x07c00000 /* 5 bit burst count for DMA device */
#define HPC3_PIODCFG_DRQLV  0x08000000 /* Use live pbus_dreq unsynchronized signal */

	hpcreg pbus_piocfgs[64][10]; /* 2nd indice indexes controller */
#define HPC3_PIOPCFG_RP2    0x00001  /* Cycles to spend in P2 state for reads */
#define HPC3_PIOPCFG_RP3    0x0001e  /* Cycles to spend in P3 state for reads */
#define HPC3_PIOPCFG_RP4    0x001e0  /* Cycles to spend in P4 state for reads */
#define HPC3_PIOPCFG_WP2    0x00200  /* Cycles to spend in P2 state for writes */
#define HPC3_PIOPCFG_WP3    0x03c00  /* Cycles to spend in P3 state for writes */
#define HPC3_PIOPCFG_WP4    0x3c000  /* Cycles to spend in P4 state for writes */
#define HPC3_PIOPCFG_HW     0x40000  /* Enable 16-bit PIO accesses */
#define HPC3_PIOPCFG_EHI    0x80000  /* Place even address bits in bits <15:8> */

	/* PBUS PROM control regs. */
	hpcreg pbus_promwe;     /* PROM write enable register */
#define HPC3_PROM_WENAB     0x1 /* Enable writes to the PROM */

	char _unused6[0x800 - sizeof(hpcreg)];
	hpcreg pbus_promswap;   /* Chip select swap reg */
#define HPC3_PROM_SWAP      0x1 /* invert GIO addr bit to select prom0 or prom1 */

	char _unused7[0x800 - sizeof(hpcreg)];
	hpcreg pbus_gout;       /* PROM general purpose output reg */
#define HPC3_PROM_STAT      0x1 /* General purpose status bit in gout */

	char _unused8[0x1000 - sizeof(hpcreg)];
	hpcreg pbus_promram[16384]; /* 64k of PROM battery backed ram */
};

/* It is possible to have two HPC3's within the address space on
 * one machine, though only having one is more likely on an INDY.
 * Controller 0 lives at physical address 0x1fb80000 and the controller
 * 1 if present lives at address 0x1fb00000.
 */
extern struct hpc3_regs *hpc3c0, *hpc3c1;
#define HPC3_CHIP0_PBASE   0x1fb80000 /* physical */
#define HPC3_CHIP1_PBASE   0x1fb00000 /* physical */

/* Control and misc status information, these live in pbus channel 6. */
struct hpc3_miscregs {
	hpcreg pdata, pctrl, pstat, pdmactrl, pistat, pimask;
	hpcreg ptimer1, ptimer2, ptimer3, ptimer4;
	hpcreg _unused1[2];
	hpcreg ser1cmd, ser1data;
	hpcreg ser0cmd, ser0data;
	hpcreg kbdmouse0, kbdmouse1;
	hpcreg gcsel, genctrl, panel;
	hpcreg _unused2;
	hpcreg sysid;
	hpcreg _unused3;
	hpcreg read, _unused4;
	hpcreg dselect;
#define HPC3_DSELECT_SCLK10MHZ   0x00 /* use 10MHZ serial clock */
#define HPC3_DSELECT_ISDNB       0x01 /* enable isdn B */
#define HPC3_DSELECT_ISDNA       0x02 /* enable isdn A */
#define HPC3_DSELECT_LPR         0x04 /* use parallel DMA */
#define HPC3_DSELECT_SCLK667MHZ  0x10 /* use 6.67MHZ serial clock */
#define HPC3_DSELECT_SCLKEXT     0x20 /* use external serial clock */

	hpcreg _unused5;
	hpcreg write1;
#define HPC3_WRITE1_PRESET    0x01  /* 0=LPR_RESET, 1=NORMAL */
#define HPC3_WRITE1_KMRESET   0x02  /* 0=KBDMOUSE_RESET, 1=NORMAL */
#define HPC3_WRITE1_ERESET    0x04  /* 0=EISA_RESET, 1=NORMAL */
#define HPC3_WRITE1_GRESET    0x08  /* 0=MAGIC_GIO_RESET, 1=NORMAL */
#define HPC3_WRITE1_LC0OFF    0x10  /* turn led off (guiness=red, else green) */
#define HPC3_WRITE1_LC1OFF    0x20  /* turn led off (guiness=green, else amber) */

	hpcreg _unused6;
	hpcreg write2;
#define HPC3_WRITE2_NTHRESH  0x01 /* use 4.5db threshhold */
#define HPC3_WRITE2_TPSPEED  0x02 /* use 100ohm TP speed */
#define HPC3_WRITE2_EPSEL    0x04 /* force cable mode: 1=AUI 0=TP */
#define HPC3_WRITE2_EASEL    0x08 /* 1=autoselect 0=manual cable selection */
#define HPC3_WRITE2_U1AMODE  0x10 /* 1=PC 0=MAC UART mode */
#define HPC3_WRITE2_U0AMODE  0x20 /* 1=PC 0=MAC UART mode */
#define HPC3_WRITE2_MLO      0x40 /* 1=4.75V 0=+5V */
#define HPC3_WRITE2_MHI      0x80 /* 1=5.25V 0=+5V */
};
extern struct hpc3_miscregs *hpc3mregs;
#define HPC3_MREGS_PBASE   0x1fbd9800 /* physical */

/* We need software copies of these because they are write only. */
extern unsigned int sgi_hpc_write1, sgi_hpc_write2;

struct hpc_keyb {
#ifdef __MIPSEB__
	unsigned char _unused0[3];
	volatile unsigned char data;
	unsigned char _unused1[3];
	volatile unsigned char command;
#else
	volatile unsigned char data;
	unsigned char _unused0[3];
	volatile unsigned char command;
	unsigned char _unused1[3];
#endif
};

/* Indy RTC  */

/* The layout of registers for the INDY Dallas 1286 clock chipset. */
struct indy_clock {
	volatile unsigned int hsec;
	volatile unsigned int sec;
	volatile unsigned int min;
	volatile unsigned int malarm;
	volatile unsigned int hr;
	volatile unsigned int halarm;
	volatile unsigned int day;
	volatile unsigned int dalarm;
	volatile unsigned int date;
	volatile unsigned int month;
	volatile unsigned int year;
	volatile unsigned int cmd;
	volatile unsigned int whsec;
	volatile unsigned int wsec;
	volatile unsigned int _unused0[50];
};

#define INDY_CLOCK_REGS (KSEG1ADDR(0x1fbe0000))

extern void sgihpc_init(void);

#endif /* !(_MIPS_SGIHPC_H) */
