/*
 * BK Id: SCCS/s.ppc4xx.h 1.3 05/17/01 18:14:25 cort
 */
/*
 *
 *    Copyright (c) 1999 Grant Erickson <grant@lcse.umn.edu>
 *
 *    Module name: ppc4xx.h
 *
 *    Description:
 *	A generic include file which pulls in appropriate include files
 *      for specific board types based on configuration settings.
 *
 */

#ifdef __KERNEL__
#ifndef __PPC4XX_H__
#define	__PPC4XX_H__

#include <linux/config.h>

#ifndef __ASSEMBLY__

#if defined(CONFIG_OAK)
#include <asm/oak.h>
#endif

#if defined(CONFIG_WALNUT)
#include <asm/walnut.h>
#endif

/* IO_BASE is for PCI I/O.
 * ISA not supported, just here to resolve copilation.
 */

#define _IO_BASE	0xe8000000	/* The PCI address window */
#define _ISA_MEM_BASE	0
#define PCI_DRAM_OFFSET	0

extern unsigned long isa_io_base;

/*
 * The "residual" board information structure the boot loader passes
 * into the kernel.
 */
extern unsigned char __res[];

/* I don't know if this is general to 4xx, or unique to a specific
 * processor or board.  In any case it is easy to move.
 */
#define PPC4xx_PCI_IO_ADDR	((uint)0xe8000000)
#define PPC4xx_PCI_IO_SIZE	((uint)64*1024)
#define PPC4xx_PCI_CFG_ADDR	((uint)0xeec00000)
#define PPC4xx_PCI_CFG_SIZE	((uint)4*1024)
#define PPC4xx_PCI_LCFG_ADDR	((uint)0xef400000)
#define PPC4xx_PCI_LCFG_SIZE	((uint)4*1024)
#define PPC4xx_ONB_IO_ADDR	((uint)0xef600000)
#define PPC4xx_ONB_IO_SIZE	((uint)4*1024)

#endif /* __ASSEMBLY__ */

/* Device Control Registers unique to 4xx */

#define	DCRN_BEAR	0x090	/* Bus Error Address Register */
#define	DCRN_BESR	0x091	/* Bus Error Syndrome Register */
#define	  BESR_DSES    	0x80000000	/* Data-Side Error Status */
#define	  BESR_DMES	0x40000000	/* DMA Error Status */
#define	  BESR_RWS	0x20000000	/* Read/Write Status */
#define	  BESR_ETMASK	0x1C000000	/* Error Type */
#define	    ET_PROT	0
#define	    ET_PARITY	1
#define	    ET_NCFG	2
#define	    ET_BUSERR	4
#define	    ET_BUSTO	6
#define DCRN_CHCR0	0x0B1	/* Chip Control Register 1                    */
#define DCRN_CHCR1	0x0B2	/* Chip Control Register 2                    */
#define DCRN_CHPSR	0x0B4	/* Chip Pin Strapping                         */
#define DCRN_CPMER	0x0B9	/* CPM Enable                                 */
#define DCRN_CPMFR	0x0BA	/* CPM Force                                  */
#define   CPM_IIC	0x80000000  /* IIC interface                          */
#define   CPM_PCI	0x40000000  /* PCI bridge                             */
#define   CPM_CPU	0x20000000  /* processor core                         */
#define   CPM_DMA	0x10000000  /* DMA controller                         */
#define   CPM_BRG	0x08000000  /* PLB to OPB bridge                      */
#define   CPM_DCP	0x04000000  /* CodePack                               */
#define   CPM_EBC	0x02000000  /* ROM/SRAM peripheral controller         */
#define   CPM_SDRAM	0x01000000  /* SDRAM memory controller                */
#define   CPM_PLB	0x00800000  /* PLB bus arbiter                        */
#define   CPM_GPIO	0x00400000  /* General Purpose IO (??)                */
#define   CPM_UART0	0x00200000  /* serial port 0                          */
#define   CPM_UART1	0x00100000  /* serial port 1                          */
#define   CPM_UIC	0x00080000  /* Universal Interrupt Controller         */
#define   CPM_TMRCLK	0x00040000  /* CPU timers                             */
#define   CPM_EMAC_MM	0x00020000  /* on-chip ethernet MM unit               */
#define   CPM_EMAC_RM	0x00010000  /* on-chip ethernet RM unit               */
#define   CPM_EMAC_TM	0x00008000  /* on-chip ethernet TM unit               */
#define DCRN_CPMSR	0x0B8	/* CPM Status                                 */

#define	DCRN_DMACR0	0x100	/* DMA Channel Control Register 0             */
#define	DCRN_DMACT0	0x101	/* DMA Count Register 0                       */
#define	DCRN_DMADA0	0x102	/* DMA Destination Address Register 0         */
#define	DCRN_DMASA0	0x103	/* DMA Source Address Register 0              */
#define DCRN_ASG0	0x104	/* DMA Scatter/Gather Descriptor Addr 0       */

#define	DCRN_DMACR1	0x108	/* DMA Channel Control Register 1             */
#define	DCRN_DMACT1	0x109	/* DMA Count Register 1                       */
#define	DCRN_DMADA1	0x10A	/* DMA Destination Address Register 1         */
#define	DCRN_DMASA1	0x10B	/* DMA Source Address Register 1              */
#define DCRN_ASG1	0x10C	/* DMA Scatter/Gather Descriptor Addr 1       */

#define	DCRN_DMACR2	0x110	/* DMA Channel Control Register 2             */
#define	DCRN_DMACT2	0x111	/* DMA Count Register 2                       */
#define	DCRN_DMADA2	0x112	/* DMA Destination Address Register 2         */
#define	DCRN_DMASA2	0x113	/* DMA Source Address Register 2              */
#define DCRN_ASG2	0x114	/* DMA Scatter/Gather Descriptor Addr 2       */

#define	DCRN_DMACR3	0x118	/* DMA Channel Control Register 3             */
#define	DCRN_DMACT3	0x119	/* DMA Count Register 3                       */
#define	DCRN_DMADA3	0x11A	/* DMA Destination Address Register 3         */
#define	DCRN_DMASA3	0x11B	/* DMA Source Address Register 3              */
#define DCRN_ASG3	0x11C	/* DMA Scatter/Gather Descriptor Addr 3       */

#define	DCRN_DMASR	0x120	/* DMA Status Register                        */
#define DCRN_ASGC	0x123	/* DMA Scatter/Gather Command                 */
#define DCRN_ADR	0x124	/* DMA Address Decode                         */

#define DCRN_SLP        0x125   /* DMA Sleep Register                         */
#define DCRN_POL        0x126   /* DMA Polarity Register                      */


#define DCRN_EBCCFGADR	0x012	/* Peripheral Controller Address              */
#define DCRN_EBCCFGDATA	0x013	/* Peripheral Controller Data                 */
#define	DCRN_EXISR	0x040    /* External Interrupt Status Register */
#define	DCRN_EXIER	0x042    /* External Interrupt Enable Register */
#define	  EXIER_CIE	0x80000000	/* Critical Interrupt Enable */
#define	  EXIER_SRIE	0x08000000	/* Serial Port Rx Int. Enable */
#define	  EXIER_STIE	0x04000000	/* Serial Port Tx Int. Enable */
#define	  EXIER_JRIE	0x02000000	/* JTAG Serial Port Rx Int. Enable */
#define	  EXIER_JTIE	0x01000000	/* JTAG Serial Port Tx Int. Enable */
#define	  EXIER_D0IE	0x00800000	/* DMA Channel 0 Interrupt Enable */
#define	  EXIER_D1IE	0x00400000	/* DMA Channel 1 Interrupt Enable */
#define	  EXIER_D2IE	0x00200000	/* DMA Channel 2 Interrupt Enable */
#define	  EXIER_D3IE	0x00100000	/* DMA Channel 3 Interrupt Enable */
#define	  EXIER_E0IE	0x00000010	/* External Interrupt 0 Enable */
#define	  EXIER_E1IE	0x00000008	/* External Interrupt 1 Enable */
#define	  EXIER_E2IE	0x00000004	/* External Interrupt 2 Enable */
#define	  EXIER_E3IE	0x00000002	/* External Interrupt 3 Enable */
#define	  EXIER_E4IE	0x00000001	/* External Interrupt 4 Enable */
#define	DCRN_IOCR	0x0A0    /* Input/Output Configuration Register */
#define	  IOCR_E0TE	0x80000000
#define	  IOCR_E0LP	0x40000000
#define	  IOCR_E1TE	0x20000000
#define	  IOCR_E1LP	0x10000000
#define	  IOCR_E2TE	0x08000000
#define	  IOCR_E2LP	0x04000000
#define	  IOCR_E3TE	0x02000000
#define	  IOCR_E3LP	0x01000000
#define	  IOCR_E4TE	0x00800000
#define	  IOCR_E4LP	0x00400000
#define	  IOCR_EDT     	0x00080000
#define	  IOCR_SOR     	0x00040000
#define	  IOCR_EDO	0x00008000
#define	  IOCR_2XC	0x00004000
#define	  IOCR_ATC	0x00002000
#define	  IOCR_SPD	0x00001000
#define	  IOCR_BEM	0x00000800
#define	  IOCR_PTD	0x00000400
#define	  IOCR_ARE	0x00000080
#define	  IOCR_DRC	0x00000020
#define	  IOCR_RDM(x)	(((x) & 0x3) << 3)
#define	  IOCR_TCS	0x00000004
#define	  IOCR_SCS	0x00000002
#define	  IOCR_SPC	0x00000001
#define DCRN_KIAR	0x014	/* Decompression Controller Address           */
#define DCRN_KIDR	0x015	/* Decompression Controller Data              */
#define DCRN_MALCR	0x180	/* MAL Configuration                          */
#define   MALCR_MMSR    0x80000000    /* MAL Software reset                   */
#define   MALCR_PLBP_1  0x00400000    /* MAL reqest priority:                 */
#define   MALCR_PLBP_2  0x00800000    /*   lowsest is 00                      */
#define   MALCR_PLBP_3  0x00C00000    /*   highest                            */
#define   MALCR_GA      0x00200000    /* Guarded Active Bit                   */
#define   MALCR_OA      0x00100000    /* Ordered Active Bit                   */
#define   MALCR_PLBLE   0x00080000    /* PLB Lock Error Bit                   */
#define   MALCR_PLBLT_1 0x00040000    /* PLB Latency Timer                    */
#define   MALCR_PLBLT_2 0x00020000
#define   MALCR_PLBLT_3 0x00010000
#define   MALCR_PLBLT_4 0x00008000
#define   MALCR_PLBLT_DEFAULT 0x00078000 /* JSP: Is this a valid default??    */
#define   MALCR_PLBB    0x00004000    /* PLB Burst Deactivation Bit           */
#define   MALCR_OPBBL   0x00000080    /* OPB Lock Bit                         */
#define   MALCR_EOPIE   0x00000004    /* End Of Packet Interrupt Enable       */
#define   MALCR_LEA     0x00000002    /* Locked Error Active                  */
#define   MALCR_MSD     0x00000001    /* MAL Scroll Descriptor Bit            */
#define DCRN_MALDBR     0x183   /* Debug Register                             */
#define DCRN_MALESR	0x181	/* Error Status                               */
#define   MALESR_EVB    0x80000000    /* Error Valid Bit                      */
#define   MALESR_CID    0x40000000    /* Channel ID Bit  for channel 0        */
#define   MALESR_DE     0x00100000    /* Descriptor Error                     */
#define   MALESR_OEN    0x00080000    /* OPB Non-Fullword Error               */
#define   MALESR_OTE    0x00040000    /* OPB Timeout Error                    */
#define   MALESR_OSE    0x00020000    /* OPB Slave Error                      */
#define   MALESR_PEIN   0x00010000    /* PLB Bus Error Indication             */
#define   MALESR_DEI    0x00000010    /* Descriptor Error Interrupt           */
#define   MALESR_ONEI   0x00000008    /* OPB Non-Fullword Error Interrupt     */
#define   MALESR_OTEI   0x00000004    /* OPB Timeout Error Interrupt          */
#define   MALESR_OSEI   0x00000002    /* OPB Slace Error Interrupt            */
#define   MALESR_PBEI   0x00000001    /* PLB Bus Error Interrupt              */
#define DCRN_MALIER	0x182	/* Interrupt Enable                           */
#define   MALIER_DE     0x00000010    /* Descriptor Error Interrupt Enable    */
#define   MALIER_NE     0x00000008    /* OPB Non-word Transfer Int Enable     */
#define   MALIER_TE     0x00000004    /* OPB Time Out Error Interrupt Enable  */
#define   MALIER_OPBE   0x00000002    /* OPB Slave Error Interrupt Enable     */
#define   MALIER_PLBE   0x00000001    /* PLB Error Interrupt Enable           */
#define DCRN_MALTXCARR  0x185   /* TX Channed Active Reset Register           */
#define DCRN_MALTXCASR  0x184   /* TX Channel Active Set Register             */
#define DCRN_MALTXDEIR	0x187	/* Tx Descriptor Error Interrupt              */
#define DCRN_MALTXEOBISR    0x186   /* Tx End of Buffer Interrupt Status      */
#define   MALOBISR_CH0  0x80000000    /* EOB channel 1 bit                    */
#define   MALOBISR_CH2  0x40000000    /* EOB channel 2 bit                    */
#define DCRN_MALRXCARR  0x191   /* RX Channed Active Reset Register           */
#define DCRN_MALRXCASR  0x190   /* RX Channel Active Set Register             */
#define DCRN_MALRXDEIR	0x193	/* Rx Descriptor Error Interrupt              */
#define DCRN_MALRXEOBISR    0x192   /* Rx End of Buffer Interrupt Status      */
#define DCRN_MALRXCTP0R	0x1C0	/* Channel Rx 0 Channel Table Pointer         */
#define DCRN_MALTXCTP0R	0x1A0	/* Channel Tx 0 Channel Table Pointer         */
#define DCRN_MALTXCTP1R	0x1A1	/* Channel Tx 1 Channel Table Pointer         */
#define DCRN_MALRCBS0	0x1E0	/* Channel Rx 0 Channel Buffer Size           */
#define DCRN_MEMCFGADR	0x010	/* Memory Controller Address                  */
#define DCRN_MEMCFGDATA	0x011	/* Memory Controller Data                     */
#define DCRN_OCMISARC	0x018	/* OCM Instr Side Addr Range Compare          */
#define DCRN_OCMISCR	0x019	/* OCM Instr Side Control                     */
#define DCRN_OCMDSARC	0x01A	/* OCM Data Side Addr Range Compare           */
#define DCRN_OCMDSCR	0x01B	/* OCM Data Side Control                      */
#define DCRN_PLB0_ACR	0x087	/* PLB Arbiter Control                        */
#define DCRN_PLB0_BEAR	0x086	/* PLB Error Address                          */
#define DCRN_PLB0_BESR	0x084	/* PLB Error Status                           */
#define DCRN_PLLMR	0x0B0	/* PLL Mode                                   */
#define DCRN_POB0_BEAR	0x0A2	/* PLB to OPB Error Address                   */
#define DCRN_POB0_BESR0	0x0A0	/* PLB to OPB Error Status Register 1         */
#define DCRN_POB0_BESR1	0x0A4	/* PLB to OPB Error Status Register 1         */
#define DCRN_UICCR	0x0C3	/* UIC Critical                               */
#define DCRN_UICER	0x0C2	/* UIC Enable                                 */
#define DCRN_UICPR	0x0C4	/* UIC Polarity                               */
#define DCRN_UICSR	0x0C0	/* UIC Status                                 */
#define DCRN_UICTR	0x0C5	/* UIC Triggering                             */
#define DCRN_UICMSR	0x0C6	/* UIC Masked Status                          */
#define DCRN_UICVR	0x0C7	/* UIC Vector                                 */
#define DCRN_UICVCR	0x0C8	/* UIC Vector Configuration                   */
#define   UIC_U0	0x80000000  /* UART0                                  */
#define   UIC_U1	0x40000000  /* UART1                                  */
#define   UIC_IIC	0x20000000  /* IIC                                    */
#define   UIC_EM	0x10000000  /* External Master                        */
#define   UIC_PCI	0x08000000  /* PCI                                    */
#define   UIC_D0	0x04000000  /* DMA Channel 0                          */
#define   UIC_D1	0x02000000  /* DMA Channel 1                          */
#define   UIC_D2	0x01000000  /* DMA Channel 2                          */
#define   UIC_D3	0x00800000  /* DMA Channel 3                          */
#define   UIC_EW	0x00400000  /* Ethernet Wake-up                       */
#define   UIC_MS	0x00200000  /* MAL SERR                               */
#define   UIC_MTE	0x00100000  /* MAL TX EOB                             */
#define   UIC_MRE	0x00080000  /* MAL RX EOB                             */
#define   UIC_MTD	0x00040000  /* MAL TX DE                              */
#define   UIC_MRD	0x00020000  /* MAL RX DE                              */
#define   UIC_E		0x00010000  /* Ethernet                               */
#define   UIC_EPS	0x00008000  /* External PCI SERR                      */
#define   UIC_EC	0x00004000  /* ECC Correctable Error                  */
#define   UIC_PPM	0x00002000  /* PCI Power Management                   */
/*
**			0x00001000  reserved
**			0x00000800  reserved
**			0x00000400  reserved
**			0x00000200  reserved
**			0x00000100  reserved
**			0x00000080  reserved
*/
#define   UIC_EIR0	0x00000040  /* External IRQ 0                         */
#define   UIC_EIR1	0x00000020  /* External IRQ 0                         */
#define   UIC_EIR2	0x00000010  /* External IRQ 0                         */
#define   UIC_EIR3	0x00000008  /* External IRQ 0                         */
#define   UIC_EIR4	0x00000004  /* External IRQ 0                         */
#define   UIC_EIR5	0x00000002  /* External IRQ 0                         */
#define   UIC_EIR6	0x00000001  /* External IRQ 0                         */

#endif /* __PPC4XX_H__ */
#endif /* __KERNEL__ */
