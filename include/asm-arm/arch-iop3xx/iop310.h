/*
 * linux/include/asm/arch-iop3xx/iop310.h
 *
 * Intel IOP310 Companion Chip definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _IOP310_HW_H_
#define _IOP310_HW_H_

/*
 * This is needed for mixed drivers that need to work on all
 * IOP3xx variants but behave slightly differently on each.
 */
#ifndef __ASSEMBLY__
#define iop_is_310() ((processor_id & 0xffffe3f0) == 0x69052000)
#endif

/*
 * IOP310 I/O and Mem space regions for PCI autoconfiguration
 */
#define IOP310_PCISEC_LOWER_IO		0x90010000
#define IOP310_PCISEC_UPPER_IO		0x9001ffff
#define IOP310_PCISEC_LOWER_MEM		0x88000000
#define IOP310_PCISEC_UPPER_MEM		0x8bffffff

#define IOP310_PCIPRI_LOWER_IO		0x90000000
#define IOP310_PCIPRI_UPPER_IO		0x9000ffff
#define IOP310_PCIPRI_LOWER_MEM		0x80000000
#define IOP310_PCIPRI_UPPER_MEM		0x83ffffff

#define IOP310_PCI_WINDOW_SIZE		64 * 0x100000

/*
 * IOP310 chipset registers
 */
#define IOP310_VIRT_MEM_BASE 0xe8001000  /* chip virtual mem address*/
#define IOP310_PHY_MEM_BASE  0x00001000  /* chip physical memory address */
#define IOP310_REG_ADDR(reg) (IOP310_VIRT_MEM_BASE | IOP310_PHY_MEM_BASE | (reg))

/* PCI-to-PCI Bridge Unit 0x00001000 through 0x000010FF */
#define IOP310_VIDR    (volatile u16 *)IOP310_REG_ADDR(0x00001000)
#define IOP310_DIDR    (volatile u16 *)IOP310_REG_ADDR(0x00001002)
#define IOP310_PCR     (volatile u16 *)IOP310_REG_ADDR(0x00001004)
#define IOP310_PSR     (volatile u16 *)IOP310_REG_ADDR(0x00001006)
#define IOP310_RIDR    (volatile u8  *)IOP310_REG_ADDR(0x00001008)
#define IOP310_CCR     (volatile u32 *)IOP310_REG_ADDR(0x00001009)
#define IOP310_CLSR    (volatile u8  *)IOP310_REG_ADDR(0x0000100C)
#define IOP310_PLTR    (volatile u8  *)IOP310_REG_ADDR(0x0000100D)
#define IOP310_HTR     (volatile u8  *)IOP310_REG_ADDR(0x0000100E)
/* Reserved 0x0000100F through  0x00001017 */
#define IOP310_PBNR    (volatile u8  *)IOP310_REG_ADDR(0x00001018)
#define IOP310_SBNR    (volatile u8  *)IOP310_REG_ADDR(0x00001019)
#define IOP310_SUBBNR  (volatile u8  *)IOP310_REG_ADDR(0x0000101A)
#define IOP310_SLTR    (volatile u8  *)IOP310_REG_ADDR(0x0000101B)
#define IOP310_IOBR    (volatile u8  *)IOP310_REG_ADDR(0x0000101C)
#define IOP310_IOLR    (volatile u8  *)IOP310_REG_ADDR(0x0000101D)
#define IOP310_SSR     (volatile u16 *)IOP310_REG_ADDR(0x0000101E)
#define IOP310_MBR     (volatile u16 *)IOP310_REG_ADDR(0x00001020)
#define IOP310_MLR     (volatile u16 *)IOP310_REG_ADDR(0x00001022)
#define IOP310_PMBR    (volatile u16 *)IOP310_REG_ADDR(0x00001024)
#define IOP310_PMLR    (volatile u16 *)IOP310_REG_ADDR(0x00001026)
/* Reserved 0x00001028 through 0x00001033 */
#define IOP310_CAPR    (volatile u8  *)IOP310_REG_ADDR(0x00001034)
/* Reserved 0x00001035 through 0x0000103D */
#define IOP310_BCR     (volatile u16 *)IOP310_REG_ADDR(0x0000103E)
#define IOP310_EBCR    (volatile u16 *)IOP310_REG_ADDR(0x00001040)
#define IOP310_SISR    (volatile u16 *)IOP310_REG_ADDR(0x00001042)
#define IOP310_PBISR   (volatile u32 *)IOP310_REG_ADDR(0x00001044)
#define IOP310_SBISR   (volatile u32 *)IOP310_REG_ADDR(0x00001048)
#define IOP310_SACR    (volatile u32 *)IOP310_REG_ADDR(0x0000104C)
#define IOP310_PIRSR   (volatile u32 *)IOP310_REG_ADDR(0x00001050)
#define IOP310_SIOBR   (volatile u8  *)IOP310_REG_ADDR(0x00001054)
#define IOP310_SIOLR   (volatile u8  *)IOP310_REG_ADDR(0x00001055)
#define IOP310_SCDR    (volatile u8  *)IOP310_REG_ADDR(0x00001056)

#define IOP310_SMBR    (volatile u16 *)IOP310_REG_ADDR(0x00001058)
#define IOP310_SMLR    (volatile u16 *)IOP310_REG_ADDR(0x0000105A)
#define IOP310_SDER    (volatile u16 *)IOP310_REG_ADDR(0x0000105C)
#define IOP310_QCR     (volatile u16 *)IOP310_REG_ADDR(0x0000105E)
#define IOP310_CAPID   (volatile u8  *)IOP310_REG_ADDR(0x00001068)
#define IOP310_NIPTR   (volatile u8  *)IOP310_REG_ADDR(0x00001069)
#define IOP310_PMCR    (volatile u16 *)IOP310_REG_ADDR(0x0000106A)
#define IOP310_PMCSR   (volatile u16 *)IOP310_REG_ADDR(0x0000106C)
#define IOP310_PMCSRBSE (volatile u8 *)IOP310_REG_ADDR(0x0000106E)
/* Reserved 0x00001064 through 0x000010FFH */

/* Performance monitoring unit  0x00001100 through 0x000011FF*/
#define IOP310_PMONGTMR    (volatile u32 *)IOP310_REG_ADDR(0x00001100)
#define IOP310_PMONESR     (volatile u32 *)IOP310_REG_ADDR(0x00001104)
#define IOP310_PMONEMISR   (volatile u32 *)IOP310_REG_ADDR(0x00001108)
#define IOP310_PMONGTSR    (volatile u32 *)IOP310_REG_ADDR(0x00001110)
#define IOP310_PMONPECR1   (volatile u32 *)IOP310_REG_ADDR(0x00001114)
#define IOP310_PMONPECR2   (volatile u32 *)IOP310_REG_ADDR(0x00001118)
#define IOP310_PMONPECR3   (volatile u32 *)IOP310_REG_ADDR(0x0000111C)
#define IOP310_PMONPECR4   (volatile u32 *)IOP310_REG_ADDR(0x00001120)
#define IOP310_PMONPECR5   (volatile u32 *)IOP310_REG_ADDR(0x00001124)
#define IOP310_PMONPECR6   (volatile u32 *)IOP310_REG_ADDR(0x00001128)
#define IOP310_PMONPECR7   (volatile u32 *)IOP310_REG_ADDR(0x0000112C)
#define IOP310_PMONPECR8   (volatile u32 *)IOP310_REG_ADDR(0x00001130)
#define IOP310_PMONPECR9   (volatile u32 *)IOP310_REG_ADDR(0x00001134)
#define IOP310_PMONPECR10  (volatile u32 *)IOP310_REG_ADDR(0x00001138)
#define IOP310_PMONPECR11  (volatile u32 *)IOP310_REG_ADDR(0x0000113C)
#define IOP310_PMONPECR12  (volatile u32 *)IOP310_REG_ADDR(0x00001140)
#define IOP310_PMONPECR13  (volatile u32 *)IOP310_REG_ADDR(0x00001144)
#define IOP310_PMONPECR14  (volatile u32 *)IOP310_REG_ADDR(0x00001148)

/* Address Translation Unit 0x00001200 through 0x000012FF */
#define IOP310_ATUVID     (volatile u16 *)IOP310_REG_ADDR(0x00001200)
#define IOP310_ATUDID     (volatile u16 *)IOP310_REG_ADDR(0x00001202)
#define IOP310_PATUCMD    (volatile u16 *)IOP310_REG_ADDR(0x00001204)
#define IOP310_PATUSR     (volatile u16 *)IOP310_REG_ADDR(0x00001206)
#define IOP310_ATURID     (volatile u8  *)IOP310_REG_ADDR(0x00001208)
#define IOP310_ATUCCR     (volatile u32 *)IOP310_REG_ADDR(0x00001209)
#define IOP310_ATUCLSR    (volatile u8  *)IOP310_REG_ADDR(0x0000120C)
#define IOP310_ATULT      (volatile u8  *)IOP310_REG_ADDR(0x0000120D)
#define IOP310_ATUHTR     (volatile u8  *)IOP310_REG_ADDR(0x0000120E)

#define IOP310_PIABAR     (volatile u32 *)IOP310_REG_ADDR(0x00001210)
/* Reserved 0x00001214 through 0x0000122B */
#define IOP310_ASVIR      (volatile u16 *)IOP310_REG_ADDR(0x0000122C)
#define IOP310_ASIR       (volatile u16 *)IOP310_REG_ADDR(0x0000122E)
#define IOP310_ERBAR      (volatile u32 *)IOP310_REG_ADDR(0x00001230)
#define IOP310_ATUCAPPTR  (volatile u8  *)IOP310_REG_ADDR(0x00001234)
/* Reserved 0x00001235 through 0x0000123B */
#define IOP310_ATUILR     (volatile u8  *)IOP310_REG_ADDR(0x0000123C)
#define IOP310_ATUIPR     (volatile u8  *)IOP310_REG_ADDR(0x0000123D)
#define IOP310_ATUMGNT    (volatile u8  *)IOP310_REG_ADDR(0x0000123E)
#define IOP310_ATUMLAT    (volatile u8  *)IOP310_REG_ADDR(0x0000123F)
#define IOP310_PIALR      (volatile u32 *)IOP310_REG_ADDR(0x00001240)
#define IOP310_PIATVR     (volatile u32 *)IOP310_REG_ADDR(0x00001244)
#define IOP310_SIABAR     (volatile u32 *)IOP310_REG_ADDR(0x00001248)
#define IOP310_SIALR      (volatile u32 *)IOP310_REG_ADDR(0x0000124C)
#define IOP310_SIATVR     (volatile u32 *)IOP310_REG_ADDR(0x00001250)
#define IOP310_POMWVR     (volatile u32 *)IOP310_REG_ADDR(0x00001254)
/* Reserved 0x00001258 through 0x0000125B */
#define IOP310_POIOWVR    (volatile u32 *)IOP310_REG_ADDR(0x0000125C)
#define IOP310_PODWVR     (volatile u32 *)IOP310_REG_ADDR(0x00001260)
#define IOP310_POUDR      (volatile u32 *)IOP310_REG_ADDR(0x00001264)
#define IOP310_SOMWVR     (volatile u32 *)IOP310_REG_ADDR(0x00001268)
#define IOP310_SOIOWVR    (volatile u32 *)IOP310_REG_ADDR(0x0000126C)
/* Reserved 0x00001270 through 0x00001273*/
#define IOP310_ERLR       (volatile u32 *)IOP310_REG_ADDR(0x00001274)
#define IOP310_ERTVR      (volatile u32 *)IOP310_REG_ADDR(0x00001278)
/* Reserved 0x00001279 through 0x0000127C*/
#define IOP310_ATUCAPID   (volatile u8  *)IOP310_REG_ADDR(0x00001280)
#define IOP310_ATUNIPTR   (volatile u8  *)IOP310_REG_ADDR(0x00001281)
#define IOP310_APMCR      (volatile u16 *)IOP310_REG_ADDR(0x00001282)
#define IOP310_APMCSR     (volatile u16 *)IOP310_REG_ADDR(0x00001284)
/* Reserved 0x00001286 through 0x00001287 */
#define IOP310_ATUCR      (volatile u32 *)IOP310_REG_ADDR(0x00001288)
/* Reserved 0x00001289  through 0x0000128C*/
#define IOP310_PATUISR    (volatile u32 *)IOP310_REG_ADDR(0x00001290)
#define IOP310_SATUISR    (volatile u32 *)IOP310_REG_ADDR(0x00001294)
#define IOP310_SATUCMD    (volatile u16 *)IOP310_REG_ADDR(0x00001298)
#define IOP310_SATUSR     (volatile u16 *)IOP310_REG_ADDR(0x0000129A)
#define IOP310_SODWVR     (volatile u32 *)IOP310_REG_ADDR(0x0000129C)
#define IOP310_SOUDR      (volatile u32 *)IOP310_REG_ADDR(0x000012A0)
#define IOP310_POCCAR     (volatile u32 *)IOP310_REG_ADDR(0x000012A4)
#define IOP310_SOCCAR     (volatile u32 *)IOP310_REG_ADDR(0x000012A8)
#define IOP310_POCCDR     (volatile u32 *)IOP310_REG_ADDR(0x000012AC)
#define IOP310_SOCCDR     (volatile u32 *)IOP310_REG_ADDR(0x000012B0)
#define IOP310_PAQCR      (volatile u32 *)IOP310_REG_ADDR(0x000012B4)
#define IOP310_SAQCR      (volatile u32 *)IOP310_REG_ADDR(0x000012B8)
#define IOP310_PATUIMR    (volatile u32 *)IOP310_REG_ADDR(0x000012BC)
#define IOP310_SATUIMR    (volatile u32 *)IOP310_REG_ADDR(0x000012C0)
/* Reserved 0x000012C4 through 0x000012FF */
/* Messaging Unit 0x00001300 through 0x000013FF */
#define IOP310_MUIMR0       (volatile u32 *)IOP310_REG_ADDR(0x00001310)
#define IOP310_MUIMR1       (volatile u32 *)IOP310_REG_ADDR(0x00001314)
#define IOP310_MUOMR0       (volatile u32 *)IOP310_REG_ADDR(0x00001318)
#define IOP310_MUOMR1       (volatile u32 *)IOP310_REG_ADDR(0x0000131C)
#define IOP310_MUIDR        (volatile u32 *)IOP310_REG_ADDR(0x00001320)
#define IOP310_MUIISR       (volatile u32 *)IOP310_REG_ADDR(0x00001324)
#define IOP310_MUIIMR       (volatile u32 *)IOP310_REG_ADDR(0x00001328)
#define IOP310_MUODR        (volatile u32 *)IOP310_REG_ADDR(0x0000132C)
#define IOP310_MUOISR       (volatile u32 *)IOP310_REG_ADDR(0x00001330)
#define IOP310_MUOIMR       (volatile u32 *)IOP310_REG_ADDR(0x00001334)
#define IOP310_MUMUCR       (volatile u32 *)IOP310_REG_ADDR(0x00001350)
#define IOP310_MUQBAR       (volatile u32 *)IOP310_REG_ADDR(0x00001354)
#define IOP310_MUIFHPR      (volatile u32 *)IOP310_REG_ADDR(0x00001360)
#define IOP310_MUIFTPR      (volatile u32 *)IOP310_REG_ADDR(0x00001364)
#define IOP310_MUIPHPR      (volatile u32 *)IOP310_REG_ADDR(0x00001368)
#define IOP310_MUIPTPR      (volatile u32 *)IOP310_REG_ADDR(0x0000136C)
#define IOP310_MUOFHPR      (volatile u32 *)IOP310_REG_ADDR(0x00001370)
#define IOP310_MUOFTPR      (volatile u32 *)IOP310_REG_ADDR(0x00001374)
#define IOP310_MUOPHPR      (volatile u32 *)IOP310_REG_ADDR(0x00001378)
#define IOP310_MUOPTPR      (volatile u32 *)IOP310_REG_ADDR(0x0000137C)
#define IOP310_MUIAR        (volatile u32 *)IOP310_REG_ADDR(0x00001380)
/* DMA Controller 0x00001400 through 0x000014FF */
#define IOP310_DMA0CCR     (volatile u32 *)IOP310_REG_ADDR(0x00001400)
#define IOP310_DMA0CSR     (volatile u32 *)IOP310_REG_ADDR(0x00001404)
/* Reserved 0x001408 through 0x00140B */
#define IOP310_DMA0DAR     (volatile u32 *)IOP310_REG_ADDR(0x0000140C)
#define IOP310_DMA0NDAR    (volatile u32 *)IOP310_REG_ADDR(0x00001410)
#define IOP310_DMA0PADR    (volatile u32 *)IOP310_REG_ADDR(0x00001414)
#define IOP310_DMA0PUADR   (volatile u32 *)IOP310_REG_ADDR(0x00001418)
#define IOP310_DMA0LADR    (volatile u32 *)IOP310_REG_ADDR(0x0000141C)
#define IOP310_DMA0BCR     (volatile u32 *)IOP310_REG_ADDR(0x00001420)
#define IOP310_DMA0DCR     (volatile u32 *)IOP310_REG_ADDR(0x00001424)
/* Reserved 0x00001428 through 0x0000143F */
#define IOP310_DMA1CCR     (volatile u32 *)IOP310_REG_ADDR(0x00001440)
#define IOP310_DMA1CSR     (volatile u32 *)IOP310_REG_ADDR(0x00001444)
/* Reserved 0x00001448 through 0x0000144B */
#define IOP310_DMA1DAR     (volatile u32 *)IOP310_REG_ADDR(0x0000144C)
#define IOP310_DMA1NDAR    (volatile u32 *)IOP310_REG_ADDR(0x00001450)
#define IOP310_DMA1PADR    (volatile u32 *)IOP310_REG_ADDR(0x00001454)
#define IOP310_DMA1PUADR   (volatile u32 *)IOP310_REG_ADDR(0x00001458)
#define IOP310_DMA1LADR    (volatile u32 *)IOP310_REG_ADDR(0x0000145C)
#define IOP310_DMA1BCR     (volatile u32 *)IOP310_REG_ADDR(0x00001460)
#define IOP310_DMA1DCR     (volatile u32 *)IOP310_REG_ADDR(0x00001464)
/* Reserved 0x00001468 through 0x0000147F */
#define IOP310_DMA2CCR     (volatile u32 *)IOP310_REG_ADDR(0x00001480)
#define IOP310_DMA2CSR     (volatile u32 *)IOP310_REG_ADDR(0x00001484)
/* Reserved 0x00001488 through 0x0000148B */
#define IOP310_DMA2DAR     (volatile u32 *)IOP310_REG_ADDR(0x0000148C)
#define IOP310_DMA2NDAR    (volatile u32 *)IOP310_REG_ADDR(0x00001490)
#define IOP310_DMA2PADR    (volatile u32 *)IOP310_REG_ADDR(0x00001494)
#define IOP310_DMA2PUADR   (volatile u32 *)IOP310_REG_ADDR(0x00001498)
#define IOP310_DMA2LADR    (volatile u32 *)IOP310_REG_ADDR(0x0000149C)
#define IOP310_DMA2BCR     (volatile u32 *)IOP310_REG_ADDR(0x000014A0)
#define IOP310_DMA2DCR     (volatile u32 *)IOP310_REG_ADDR(0x000014A4)

/* Memory controller 0x00001500 through 0x0015FF */

/* core interface unit 0x00001640 - 0x0000167F */
#define IOP310_CIUISR     (volatile u32 *)IOP310_REG_ADDR(0x00001644)

/* PCI and Peripheral Interrupt Controller 0x00001700 - 0x0000171B */
#define IOP310_IRQISR     (volatile u32 *)IOP310_REG_ADDR(0x00001700)
#define IOP310_FIQ2ISR    (volatile u32 *)IOP310_REG_ADDR(0x00001704)
#define IOP310_FIQ1ISR    (volatile u32 *)IOP310_REG_ADDR(0x00001708)
#define IOP310_PDIDR      (volatile u32 *)IOP310_REG_ADDR(0x00001710)

/* AAU registers. DJ 0x00001800 - 0x00001838 */
#define IOP310_AAUACR    (volatile u32 *)IOP310_REG_ADDR(0x00001800)
#define IOP310_AAUASR    (volatile u32 *)IOP310_REG_ADDR(0x00001804)
#define IOP310_AAUADAR   (volatile u32 *)IOP310_REG_ADDR(0x00001808)
#define IOP310_AAUANDAR  (volatile u32 *)IOP310_REG_ADDR(0x0000180C)
#define IOP310_AAUSAR1   (volatile u32 *)IOP310_REG_ADDR(0x00001810)
#define IOP310_AAUSAR2   (volatile u32 *)IOP310_REG_ADDR(0x00001814)
#define IOP310_AAUSAR3   (volatile u32 *)IOP310_REG_ADDR(0x00001818)
#define IOP310_AAUSAR4   (volatile u32 *)IOP310_REG_ADDR(0x0000181C)
#define IOP310_AAUDAR    (volatile u32 *)IOP310_REG_ADDR(0x00001820)
#define IOP310_AAUABCR   (volatile u32 *)IOP310_REG_ADDR(0x00001824)
#define IOP310_AAUADCR   (volatile u32 *)IOP310_REG_ADDR(0x00001828)
#define IOP310_AAUSAR5   (volatile u32 *)IOP310_REG_ADDR(0x0000182C)
#define IOP310_AAUSAR6   (volatile u32 *)IOP310_REG_ADDR(0x00001830)
#define IOP310_AAUSAR7   (volatile u32 *)IOP310_REG_ADDR(0x00001834)
#define IOP310_AAUSAR8   (volatile u32 *)IOP310_REG_ADDR(0x00001838)

#endif // _IOP310_HW_H_
