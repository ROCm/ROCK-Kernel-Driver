/*
 * arch/ppc/syslib/bestcomm/bestcomm.h
 *
 * Driver for MPC52xx processor BestComm peripheral controller
 *
 * Author: Dale Farnsworth <dfarnsworth@mvista.com>
 *
 * 2003-2004 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * HISTORY:
 *
 * 2005-08-14	Converted to platform driver by
 *		Andrey Volkov <avolkov@varma-el.com>, Varma Electronics Oy
 */

#ifndef __BESTCOMM_BESTCOMM_H__
#define __BESTCOMM_BESTCOMM_H__

#include "mpc52xx_pic.h"

/* Buffer Descriptor definitions */
struct sdma_bd {
	u32 status;
	void *data;
};

struct sdma_bd2 {
	u32 status;
	void *data1;
	void *data2;
};

struct sdma_io {
	unsigned long			base_reg_addr;
	struct mpc52xx_sdma __iomem	*io;
	unsigned long			base_sram_addr;
	void __iomem			*sram;
	size_t				sram_size;

	struct sdma_tdt __iomem  	*tdt;
	u32 __iomem			*var;
};
extern struct sdma_io sdma;

#define	sdma_sram_pa(virt)	(((unsigned long)(((void __iomem *)(virt))-sdma.sram))+sdma.base_sram_addr)
#define	sdma_sram_va(pa)	((void __iomem *)((((unsigned long)(pa))-sdma.base_sram_addr)+((unsigned long)sdma.sram)))

#define	sdma_io_pa(virt)	(((unsigned long)(((void __iomem *)(virt))-((void __iomem *)sdma.io)))+sdma.base_reg_addr)
#define	sdma_io_va(pa)	((void __iomem *)((((unsigned long)(pa))-sdma.base_reg_addr)+((unsigned long)sdma.io)))

#define SDMA_LEN_BITS		26
#define SDMA_LEN_MASK		((1 << SDMA_LEN_BITS) - 1)

#define SDMA_BD_READY		0x40000000UL

#define SDMA_FEC_TX_BD_TFD	0x08000000UL	/* transmit frame done */
#define SDMA_FEC_TX_BD_INT	0x04000000UL	/* Interrupt */
#define SDMA_FEC_TX_BD_TFD_INIT	(SDMA_BD_READY | SDMA_FEC_TX_BD_TFD | \
							SDMA_FEC_TX_BD_INT)

struct sdma {
	union {
		struct sdma_bd *bd;
		struct sdma_bd2 *bd2;
	};
	void **cookie;
	u16 index;
	u16 outdex;
	u16 num_bd;
	s16 tasknum;
	u32 flags;
	struct device_node *node;
};

#define SDMA_FLAGS_NONE		0x0000
#define SDMA_FLAGS_ENABLE_TASK	0x0001
#define SDMA_FLAGS_BD2		0x0002

/* Task Descriptor Table Entry */
struct sdma_tdt {
	u32 start;
	u32 stop;
	u32 var;
	u32 fdt;
	u32 exec_status; /* used internally by SmartComm engine */
	u32 mvtp;		 /* used internally by SmartComm engine */
	u32 context;
	u32 litbase;
};

//extern struct sdma_tdt *sdma_tdt;

#define SDMA_MAX_TASKS		16
#define SDMA_MAX_VAR		24
#define SDMA_MAX_INC		8
#define SDMA_MAX_FDT		64
#define SDMA_MAX_CONTEXT	20
#define SDMA_CONTEXT_SIZE	SDMA_MAX_CONTEXT * sizeof(u32)
#define SDMA_CONTEXT_ALIGN	0x100
#define SDMA_VAR_SIZE		SDMA_MAX_VAR * sizeof(u32)
#define SDMA_VAR_ALIGN		0x80
#define SDMA_INC_SIZE		SDMA_MAX_INC * sizeof(u32)
#define SDMA_FDT_SIZE		SDMA_MAX_FDT * sizeof(u32)
#define SDMA_FDT_ALIGN		0x100
#define SDMA_BD_ALIGN		0x10

#define TASK_ENABLE		0x8000

#ifndef DPRINK
	#ifdef CONFIG_BESTCOMM_DEBUG
	#define DPRINTK(a,b...)	printk(KERN_DEBUG "sdma: %s: " a, __FUNCTION__ , ## b)
	#else
	#define DPRINTK(a,b...)
	#endif
#endif

static inline void sdma_enable_task(int task)
{
	u16 reg;

	DPRINTK("***DMA enable task (%d): tdt = %p\n",task, sdma.tdt);
	DPRINTK("***tdt->start   = %08x\n",sdma.tdt[task].start);
	DPRINTK("***tdt->stop    = %08x\n",sdma.tdt[task].stop);
	DPRINTK("***tdt->var     = %08x\n",sdma.tdt[task].var);
	DPRINTK("***tdt->fdt     = %08x\n",sdma.tdt[task].fdt);
	DPRINTK("***tdt->status  = %08x\n",sdma.tdt[task].exec_status);
	DPRINTK("***tdt->mvtp    = %08x\n",sdma.tdt[task].mvtp);
	DPRINTK("***tdt->context = %08x\n",sdma.tdt[task].context);
	DPRINTK("***tdt->litbase = %08x\n",sdma.tdt[task].litbase);
	DPRINTK("***--------------\n");

	reg = in_be16(&sdma.io->tcr[task]);
	DPRINTK("***enable task: &sdma.io->tcr=%p, reg = %04x\n", &sdma.io->tcr, reg);
	out_be16(&sdma.io->tcr[task],  reg | TASK_ENABLE);
}

static inline void sdma_disable_task(int task)
{
	u16 reg = in_be16(&sdma.io->tcr[task]);
	DPRINTK("***disable task(%d): reg = %04x\n", task, reg);
	out_be16(&sdma.io->tcr[task], reg & ~TASK_ENABLE);
}

static inline int sdma_irq(struct sdma *s)
{
	return irq_of_parse_and_map(s->node, s->tasknum);
}

static inline void sdma_enable(struct sdma *s)
{
	sdma_enable_task(s->tasknum);
}

static inline void sdma_disable(struct sdma *s)
{
	sdma_disable_task(s->tasknum);
}

static inline int sdma_queue_empty(struct sdma *s)
{
	return s->index == s->outdex;
}

static inline void sdma_clear_irq(struct sdma *s)
{
	out_be32(&sdma.io->IntPend, 1 << s->tasknum);
}

static inline int sdma_next_index(struct sdma *s)
{
	return ((s->index + 1) == s->num_bd) ? 0 : s->index + 1;
}

static inline int sdma_next_outdex(struct sdma *s)
{
	return ((s->outdex + 1) == s->num_bd) ? 0 : s->outdex + 1;
}

static inline int sdma_queue_full(struct sdma *s)
{
	return s->outdex == sdma_next_index(s);
}

static inline int sdma_buffer_done(struct sdma *s)
{
#ifdef CONFIG_BESTCOMM_DEBUG
	BUG_ON(s->flags & SDMA_FLAGS_BD2);
#endif
	if (sdma_queue_empty(s))
		return 0;
	return (s->bd[s->outdex].status & SDMA_BD_READY) == 0;
}

static inline int sdma_buffer2_done(struct sdma *s)
{
#ifdef CONFIG_BESTCOMM_DEBUG
	BUG_ON(!(s->flags & SDMA_FLAGS_BD2));
#endif
	if (sdma_queue_empty(s))
		return 0;

	return (s->bd2[s->outdex].status & SDMA_BD_READY) == 0;
}

static inline u32 *sdma_task_desc(int task)
{
	return sdma_sram_va(sdma.tdt[task].start);
}

static inline u32 sdma_task_num_descs(int task)
{
	return (sdma.tdt[task].stop - sdma.tdt[task].start)/sizeof(u32) + 1;
}

static inline u32 *sdma_task_var(int task)
{
	return sdma_sram_va(sdma.tdt[task].var);
}

static inline u32 *sdma_task_inc(int task)
{
	return &sdma_task_var(task)[SDMA_MAX_VAR];
}

static inline void sdma_set_tcr_initiator(int task, int initiator) {
	u16 *tcr = &sdma.io->tcr[task];
	out_be16(tcr, (in_be16(tcr) & ~0x1f00) | (initiator << 8));
}

#define SDMA_DRD_INITIATOR_SHIFT	21

static inline int sdma_desc_initiator(u32 desc)
{
	return (desc >> SDMA_DRD_INITIATOR_SHIFT) & 0x1f;
}

static inline void sdma_set_desc_initiator(u32 *desc, int initiator)
{
	*desc = (*desc & ~(0x1f << SDMA_DRD_INITIATOR_SHIFT)) |
			((initiator << SDMA_DRD_INITIATOR_SHIFT) & 0x1f);
}

static inline void sdma_submit_buffer(struct sdma *s, void *cookie, void *data,
								int length)
{
#ifdef CONFIG_BESTCOMM_DEBUG
	BUG_ON(s->flags & SDMA_FLAGS_BD2);
#endif
	s->cookie[s->index] = cookie;
	s->bd[s->index].data = data;
	s->bd[s->index].status = SDMA_BD_READY | length;
	s->index = sdma_next_index(s);
	if (s->flags & SDMA_FLAGS_ENABLE_TASK)
		sdma_enable_task(s->tasknum);
}

/*
 * Special submit_buffer function to submit last buffer of a frame to
 * the FEC tx task.  tfd means "transmit frame done".
 */
static inline void sdma_fec_tfd_submit_buffer(struct sdma *s, void *cookie,
							void *data, int length)
{
#ifdef CONFIG_BESTCOMM_DEBUG
	BUG_ON(s->flags & SDMA_FLAGS_BD2);
#endif
	s->cookie[s->index] = cookie;
	s->bd[s->index].data = data;
	s->bd[s->index].status = SDMA_FEC_TX_BD_TFD_INIT | length;
	s->index = sdma_next_index(s);
	sdma_enable_task(s->tasknum);
}

static inline void *sdma_retrieve_buffer(struct sdma *s, int *length)
{
	void *cookie = s->cookie[s->outdex];

#ifdef CONFIG_BESTCOMM_DEBUG
	BUG_ON(s->flags & SDMA_FLAGS_BD2);
#endif
	if (length)
		*length = s->bd[s->outdex].status & SDMA_LEN_MASK;
	s->outdex = sdma_next_outdex(s);
	return cookie;
}

static inline void sdma_submit_buffer2(struct sdma *s, void *cookie,
					void *data1, void *data2, int length)
{
#ifdef CONFIG_BESTCOMM_DEBUG
	BUG_ON(!(s->flags & SDMA_FLAGS_BD2));
#endif
	s->cookie[s->index] = cookie;
	s->bd2[s->index].data1 = data1;
	s->bd2[s->index].data2 = data2;
	s->bd2[s->index].status = SDMA_BD_READY | length;
	s->index = sdma_next_index(s);
	if (s->flags & SDMA_FLAGS_ENABLE_TASK)
		sdma_enable_task(s->tasknum);
}

static inline void *sdma_retrieve_buffer2(struct sdma *s, int *length)
{
	void *cookie = s->cookie[s->outdex];

#ifdef CONFIG_BESTCOMM_DEBUG
	BUG_ON(!(s->flags & SDMA_FLAGS_BD2));
#endif
	if (length)
		*length = s->bd2[s->outdex].status & SDMA_LEN_MASK;
	s->outdex = sdma_next_outdex(s);
	return cookie;
}

#define SDMA_TASK_MAGIC		0x4243544B	/* 'BCTK' */

/* the size fields are given in number of 32-bit words */
struct sdma_task_header {
	u32	magic;
	u8	desc_size;
	u8	var_size;
	u8	inc_size;
	u8	first_var;
	u8	reserved[8];
};

#define SDMA_DESC_NOP		0x000001f8
#define SDMA_LCD_MASK		0x80000000
#define SDMA_DRD_EXTENDED	0x40000000

#define sdma_drd_is_extended(desc) ((desc) & SDMA_DRD_EXTENDED)

static inline int sdma_desc_is_drd(u32 desc) {
	return !(desc & SDMA_LCD_MASK) && desc != SDMA_DESC_NOP;
};

#define SDMA_PRAGMA_BIT_RSV		7	/* reserved pragma bit */
#define SDMA_PRAGMA_BIT_PRECISE_INC	6	/* increment 0=when possible, */
						/*	1=iter end */
#define SDMA_PRAGMA_BIT_RST_ERROR_NO	5	/* don't reset errors on */
						/* task enable */
#define SDMA_PRAGMA_BIT_PACK		4	/* pack data enable */
#define SDMA_PRAGMA_BIT_INTEGER		3	/* data alignment */
						/* 0=frac(msb), 1=int(lsb) */
#define SDMA_PRAGMA_BIT_SPECREAD	2	/* XLB speculative read */
#define SDMA_PRAGMA_BIT_CW		1	/* write line buffer enable */
#define SDMA_PRAGMA_BIT_RL		0	/* read line buffer enable */

#define SDMA_STD_PRAGMA		((0 << SDMA_PRAGMA_BIT_RSV)		| \
				 (0 << SDMA_PRAGMA_BIT_PRECISE_INC)	| \
				 (0 << SDMA_PRAGMA_BIT_RST_ERROR_NO)	| \
				 (0 << SDMA_PRAGMA_BIT_PACK)		| \
				 (0 << SDMA_PRAGMA_BIT_INTEGER)		| \
				 (1 << SDMA_PRAGMA_BIT_SPECREAD)	| \
				 (1 << SDMA_PRAGMA_BIT_CW)		| \
				 (1 << SDMA_PRAGMA_BIT_RL))

#define SDMA_PCI_PRAGMA		((0 << SDMA_PRAGMA_BIT_RSV)		| \
				 (0 << SDMA_PRAGMA_BIT_PRECISE_INC)	| \
				 (0 << SDMA_PRAGMA_BIT_RST_ERROR_NO)	| \
				 (0 << SDMA_PRAGMA_BIT_PACK)		| \
				 (1 << SDMA_PRAGMA_BIT_INTEGER)		| \
				 (1 << SDMA_PRAGMA_BIT_SPECREAD)	| \
				 (1 << SDMA_PRAGMA_BIT_CW)		| \
				 (1 << SDMA_PRAGMA_BIT_RL))

#define SDMA_ATA_PRAGMA		SDMA_STD_PRAGMA
#define SDMA_CRC16_DP_0_PRAGMA	SDMA_STD_PRAGMA
#define SDMA_CRC16_DP_1_PRAGMA	SDMA_STD_PRAGMA
#define SDMA_FEC_RX_BD_PRAGMA	SDMA_STD_PRAGMA
#define SDMA_FEC_TX_BD_PRAGMA	SDMA_STD_PRAGMA
#define SDMA_GEN_DP_0_PRAGMA	SDMA_STD_PRAGMA
#define SDMA_GEN_DP_1_PRAGMA	SDMA_STD_PRAGMA
#define SDMA_GEN_DP_2_PRAGMA	SDMA_STD_PRAGMA
#define SDMA_GEN_DP_3_PRAGMA	SDMA_STD_PRAGMA
#define SDMA_GEN_DP_BD_0_PRAGMA	SDMA_STD_PRAGMA
#define SDMA_GEN_DP_BD_1_PRAGMA	SDMA_STD_PRAGMA
#define SDMA_GEN_RX_BD_PRAGMA	SDMA_STD_PRAGMA
#define SDMA_GEN_TX_BD_PRAGMA	SDMA_STD_PRAGMA
#define SDMA_GEN_LPC_PRAGMA	SDMA_STD_PRAGMA
#define SDMA_PCI_RX_PRAGMA	SDMA_PCI_PRAGMA
#define SDMA_PCI_TX_PRAGMA	SDMA_PCI_PRAGMA

static inline void sdma_set_task_pragma(int task, int pragma)
{
	u32 *fdt = &sdma.tdt[task].fdt;
	*fdt = (*fdt & ~0xff) | pragma;
}

static inline void sdma_set_task_auto_start(int task, int next_task)
{
	u16 *tcr = &sdma.io->tcr[task];
	out_be16(tcr, (in_be16(tcr) & ~0xff) | 0x00c0 | next_task);
}

#define SDMA_INITIATOR_ALWAYS	 0
#define SDMA_INITIATOR_SCTMR_0	 1
#define SDMA_INITIATOR_SCTMR_1	 2
#define SDMA_INITIATOR_FEC_RX	 3
#define SDMA_INITIATOR_FEC_TX	 4
#define SDMA_INITIATOR_ATA_RX	 5
#define SDMA_INITIATOR_ATA_TX	 6
#define SDMA_INITIATOR_SCPCI_RX	 7
#define SDMA_INITIATOR_SCPCI_TX	 8
#define SDMA_INITIATOR_PSC3_RX	 9
#define SDMA_INITIATOR_PSC3_TX	10
#define SDMA_INITIATOR_PSC2_RX	11
#define SDMA_INITIATOR_PSC2_TX	12
#define SDMA_INITIATOR_PSC1_RX	13
#define SDMA_INITIATOR_PSC1_TX	14
#define SDMA_INITIATOR_SCTMR_2	15
#define SDMA_INITIATOR_SCLPC	16
#define SDMA_INITIATOR_PSC5_RX	17
#define SDMA_INITIATOR_PSC5_TX	18
#define SDMA_INITIATOR_PSC4_RX	19
#define SDMA_INITIATOR_PSC4_TX	20
#define SDMA_INITIATOR_I2C2_RX	21
#define SDMA_INITIATOR_I2C2_TX	22
#define SDMA_INITIATOR_I2C1_RX	23
#define SDMA_INITIATOR_I2C1_TX	24
#define SDMA_INITIATOR_PSC6_RX	25
#define SDMA_INITIATOR_PSC6_TX	26
#define SDMA_INITIATOR_IRDA_RX	25
#define SDMA_INITIATOR_IRDA_TX	26
#define SDMA_INITIATOR_SCTMR_3	27
#define SDMA_INITIATOR_SCTMR_4	28
#define SDMA_INITIATOR_SCTMR_5	29
#define SDMA_INITIATOR_SCTMR_6	30
#define SDMA_INITIATOR_SCTMR_7	31

#define SDMA_IPR_ALWAYS	7
#define SDMA_IPR_SCTMR_0 	2
#define SDMA_IPR_SCTMR_1 	2
#define SDMA_IPR_FEC_RX 	6
#define SDMA_IPR_FEC_TX 	5
#define SDMA_IPR_ATA_RX 	4
#define SDMA_IPR_ATA_TX 	3
#define SDMA_IPR_SCPCI_RX	2
#define SDMA_IPR_SCPCI_TX	2
#define SDMA_IPR_PSC3_RX	2
#define SDMA_IPR_PSC3_TX	2
#define SDMA_IPR_PSC2_RX	2
#define SDMA_IPR_PSC2_TX	2
#define SDMA_IPR_PSC1_RX	2
#define SDMA_IPR_PSC1_TX	2
#define SDMA_IPR_SCTMR_2	2
#define SDMA_IPR_SCLPC		2
#define SDMA_IPR_PSC5_RX	2
#define SDMA_IPR_PSC5_TX	2
#define SDMA_IPR_PSC4_RX	2
#define SDMA_IPR_PSC4_TX	2
#define SDMA_IPR_I2C2_RX	2
#define SDMA_IPR_I2C2_TX	2
#define SDMA_IPR_I2C1_RX	2
#define SDMA_IPR_I2C1_TX	2
#define SDMA_IPR_PSC6_RX	2
#define SDMA_IPR_PSC6_TX	2
#define SDMA_IPR_IRDA_RX	2
#define SDMA_IPR_IRDA_TX	2
#define SDMA_IPR_SCTMR_3	2
#define SDMA_IPR_SCTMR_4	2
#define SDMA_IPR_SCTMR_5	2
#define SDMA_IPR_SCTMR_6	2
#define SDMA_IPR_SCTMR_7	2

extern struct sdma *sdma_alloc(int request_queue_size);
extern void sdma_free(struct sdma *sdma_struct);
extern int sdma_load_task(u32 *task_image);
extern void *sdma_sram_alloc(int size, int alignment, u32 *dma_handle);
extern void sdma_init_bd(struct sdma *s);
extern void sdma_init_bd2(struct sdma *s);

#define FIELD_OFFSET(s,f) ((unsigned long)(&(((struct s*)0)->f)))

#endif  /* __BESTCOMM_BESTCOMM_H__ */
