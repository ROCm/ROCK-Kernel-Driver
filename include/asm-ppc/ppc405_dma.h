/*
 * Author: Pete Popov <ppopov@mvista.com>
 *
 * 2000 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Data structures specific to the IBM PowerPC 405 on-chip DMA controller
 * and API.
 */

#ifdef __KERNEL__
#ifndef __ASMPPC_405_DMA_H
#define __ASMPPC_405_DMA_H

#include <linux/types.h>

/* #define DEBUG_405DMA */

#define TRUE  1
#define FALSE 0

#define SGL_LIST_SIZE 4096
/* #define PCI_ALLOC_IS_NONCONSISTENT */

#define MAX_405GP_DMA_CHANNELS	4

/* The maximum address that we can perform a DMA transfer to on this platform */
/* Doesn't really apply... */
#define MAX_DMA_ADDRESS		0xFFFFFFFF

extern unsigned long ISA_DMA_THRESHOLD;

#define dma_outb	outb
#define dma_inb		inb


/*
 * Function return status codes
 * These values are used to indicate whether or not the function
 * call was successful, or a bad/invalid parameter was passed.
 */
#define DMA_STATUS_GOOD			0
#define DMA_STATUS_BAD_CHANNEL		1
#define DMA_STATUS_BAD_HANDLE		2
#define DMA_STATUS_BAD_MODE		3
#define DMA_STATUS_NULL_POINTER		4
#define DMA_STATUS_OUT_OF_MEMORY	5
#define DMA_STATUS_SGL_LIST_EMPTY	6
#define DMA_STATUS_GENERAL_ERROR	7


/*
 * These indicate status as returned from the DMA Status Register.
 */
#define DMA_STATUS_NO_ERROR	0
#define DMA_STATUS_CS		1	/* Count Status        */
#define DMA_STATUS_TS		2	/* Transfer Status     */
#define DMA_STATUS_DMA_ERROR	3	/* DMA Error Occurred  */
#define DMA_STATUS_DMA_BUSY	4	/* The channel is busy */


/*
 * Transfer Modes
 * These modes are defined in a way that makes it possible to
 * simply "or" in the value in the control register.
 */
#define DMA_MODE_READ		DMA_TD                /* Peripheral to Memory */
#define DMA_MODE_WRITE		0                     /* Memory to Peripheral */
#define DMA_MODE_MM		(SET_DMA_TM(TM_S_MM)) /* memory to memory */

				/* Device-paced memory to memory, */
				/* device is at source address    */
#define DMA_MODE_MM_DEVATSRC	(DMA_TD | SET_DMA_TM(TM_D_MM))

				/* Device-paced memory to memory,      */
				/* device is at destination address    */
#define DMA_MODE_MM_DEVATDST	(SET_DMA_TM(TM_D_MM))


/*
 * DMA Polarity Configuration Register
 */
#define DMAReq0_ActiveLow (1<<31)
#define DMAAck0_ActiveLow (1<<30)
#define EOT0_ActiveLow    (1<<29)           /* End of Transfer      */

#define DMAReq1_ActiveLow (1<<28)
#define DMAAck1_ActiveLow (1<<27)
#define EOT1_ActiveLow    (1<<26)

#define DMAReq2_ActiveLow (1<<25)
#define DMAAck2_ActiveLow (1<<24)
#define EOT2_ActiveLow    (1<<23)

#define DMAReq3_ActiveLow (1<<22)
#define DMAAck3_ActiveLow (1<<21)
#define EOT3_ActiveLow    (1<<20)

/*
 * DMA Sleep Mode Register
 */
#define SLEEP_MODE_ENABLE (1<<21)


/*
 * DMA Status Register
 */
#define DMA_CS0           (1<<31) /* Terminal Count has been reached */
#define DMA_CS1           (1<<30)
#define DMA_CS2           (1<<29)
#define DMA_CS3           (1<<28)

#define DMA_TS0           (1<<27) /* End of Transfer has been requested */
#define DMA_TS1           (1<<26)
#define DMA_TS2           (1<<25)
#define DMA_TS3           (1<<24)

#define DMA_CH0_ERR       (1<<23) /* DMA Chanel 0 Error */
#define DMA_CH1_ERR       (1<<22)
#define DMA_CH2_ERR       (1<<21)
#define DMA_CH3_ERR       (1<<20)

#define DMA_IN_DMA_REQ0   (1<<19) /* Internal DMA Request is pending */
#define DMA_IN_DMA_REQ1   (1<<18)
#define DMA_IN_DMA_REQ2   (1<<17)
#define DMA_IN_DMA_REQ3   (1<<16)

#define DMA_EXT_DMA_REQ0  (1<<15) /* External DMA Request is pending */
#define DMA_EXT_DMA_REQ1  (1<<14)
#define DMA_EXT_DMA_REQ2  (1<<13)
#define DMA_EXT_DMA_REQ3  (1<<12)

#define DMA_CH0_BUSY      (1<<11) /* DMA Channel 0 Busy */
#define DMA_CH1_BUSY      (1<<10)
#define DMA_CH2_BUSY       (1<<9)
#define DMA_CH3_BUSY       (1<<8)

#define DMA_SG0            (1<<7) /* DMA Channel 0 Scatter/Gather in progress */
#define DMA_SG1            (1<<6)
#define DMA_SG2            (1<<5)
#define DMA_SG3            (1<<4)



/*
 * DMA Channel Control Registers
 */
#define DMA_CH_ENABLE         (1<<31)     /* DMA Channel Enable */
#define SET_DMA_CH_ENABLE(x)  (((x)&0x1)<<31)
#define GET_DMA_CH_ENABLE(x)  (((x)&DMA_CH_ENABLE)>>31)

#define DMA_CIE_ENABLE        (1<<30)     /* DMA Channel Interrupt Enable */
#define SET_DMA_CIE_ENABLE(x) (((x)&0x1)<<30)
#define GET_DMA_CIE_ENABLE(x) (((x)&DMA_CIE_ENABLE)>>30)

#define DMA_TD                (1<<29)
#define SET_DMA_TD(x)         (((x)&0x1)<<29)
#define GET_DMA_TD(x)         (((x)&DMA_TD)>>29)

#define DMA_PL                (1<<28)     /* Peripheral Location */
#define SET_DMA_PL(x)         (((x)&0x1)<<28)
#define GET_DMA_PL(x)         (((x)&DMA_PL)>>28)

#define EXTERNAL_PERIPHERAL    0
#define INTERNAL_PERIPHERAL    1


#define SET_DMA_PW(x)     (((x)&0x3)<<26) /* Peripheral Width */
#define DMA_PW_MASK       SET_DMA_PW(3)
#define   PW_8                 0
#define   PW_16                1
#define   PW_32                2
#define   PW_64                3
#define GET_DMA_PW(x)     (((x)&DMA_PW_MASK)>>26)

#define DMA_DAI           (1<<25)         /* Destination Address Increment */
#define SET_DMA_DAI(x)    (((x)&0x1)<<25)

#define DMA_SAI           (1<<24)         /* Source Address Increment */
#define SET_DMA_SAI(x)    (((x)&0x1)<<24)

#define DMA_BEN           (1<<23)         /* Buffer Enable */
#define SET_DMA_BEN(x)    (((x)&0x1)<<23)

#define SET_DMA_TM(x)     (((x)&0x3)<<21) /* Transfer Mode */
#define DMA_TM_MASK       SET_DMA_TM(3)
#define   TM_PERIPHERAL        0          /* Peripheral */
#define   TM_RESERVED          1          /* Reserved */
#define   TM_S_MM              2          /* Memory to Memory */
#define   TM_D_MM              3          /* Device Paced Memory to Memory */
#define GET_DMA_TM(x)     (((x)&DMA_TM_MASK)>>21)

#define SET_DMA_PSC(x)    (((x)&0x3)<<19) /* Peripheral Setup Cycles */
#define DMA_PSC_MASK      SET_DMA_PSC(3)
#define GET_DMA_PSC(x)    (((x)&DMA_PSC_MASK)>>19)

#define SET_DMA_PWC(x)    (((x)&0x3F)<<13) /* Peripheral Wait Cycles */
#define DMA_PWC_MASK      SET_DMA_PWC(0x3F)
#define GET_DMA_PWC(x)    (((x)&DMA_PWC_MASK)>>13)

#define SET_DMA_PHC(x)    (((x)&0x7)<<10) /* Peripheral Hold Cycles */
#define DMA_PHC_MASK      SET_DMA_PHC(0x7)
#define GET_DMA_PHC(x)    (((x)&DMA_PHC_MASK)>>10)

#define DMA_ETD_OUTPUT     (1<<9)         /* EOT pin is a TC output */
#define SET_DMA_ETD(x)     (((x)&0x1)<<9)

#define DMA_TCE_ENABLE     (1<<8)
#define SET_DMA_TCE(x)     (((x)&0x1)<<8)

#define SET_DMA_PRIORITY(x)   (((x)&0x3)<<6)   /* DMA Channel Priority */
#define DMA_PRIORITY_MASK SET_DMA_PRIORITY(3)
#define   PRIORITY_LOW         0
#define   PRIORITY_MID_LOW     1
#define   PRIORITY_MID_HIGH    2
#define   PRIORITY_HIGH        3
#define GET_DMA_PRIORITY(x) (((x)&DMA_PRIORITY_MASK)>>6)

#define SET_DMA_PREFETCH(x)   (((x)&0x3)<<4)  /* Memory Read Prefetch */
#define DMA_PREFETCH_MASK      SET_DMA_PREFETCH(3)
#define   PREFETCH_1           0              /* Prefetch 1 Double Word */
#define   PREFETCH_2           1
#define   PREFETCH_4           2
#define GET_DMA_PREFETCH(x) (((x)&DMA_PREFETCH_MASK)>>4)

#define DMA_PCE            (1<<3)         /* Parity Check Enable */
#define SET_DMA_PCE(x)     (((x)&0x1)<<3)
#define GET_DMA_PCE(x)     (((x)&DMA_PCE)>>3)

#define DMA_DEC            (1<<2)         /* Address Decrement */
#define SET_DMA_DEC(x)     (((x)&0x1)<<2)
#define GET_DMA_DEC(x)     (((x)&DMA_DEC)>>2)

/*
 * DMA SG Command Register
 */
#define SSG0_ENABLE        (1<<31)        /* Start Scatter Gather */
#define SSG1_ENABLE        (1<<30)
#define SSG2_ENABLE        (1<<29)
#define SSG3_ENABLE        (1<<28)
#define SSG0_MASK_ENABLE   (1<<15)        /* Enable writing to SSG0 bit */
#define SSG1_MASK_ENABLE   (1<<14)
#define SSG2_MASK_ENABLE   (1<<13)
#define SSG3_MASK_ENABLE   (1<<12)


/*
 * DMA Scatter/Gather Descriptor Bit fields
 */
#define SG_LINK            (1<<31)        /* Link */
#define SG_TCI_ENABLE      (1<<29)        /* Enable Terminal Count Interrupt */
#define SG_ETI_ENABLE      (1<<28)        /* Enable End of Transfer Interrupt */
#define SG_ERI_ENABLE      (1<<27)        /* Enable Error Interrupt */
#define SG_COUNT_MASK       0xFFFF        /* Count Field */




typedef uint32_t sgl_handle_t;

typedef struct {

	/*
	 * Valid polarity settings:
	 *   DMAReq0_ActiveLow
	 *   DMAAck0_ActiveLow
	 *   EOT0_ActiveLow
	 *
	 *   DMAReq1_ActiveLow
	 *   DMAAck1_ActiveLow
	 *   EOT1_ActiveLow
	 *
	 *   DMAReq2_ActiveLow
	 *   DMAAck2_ActiveLow
	 *   EOT2_ActiveLow
	 *
	 *   DMAReq3_ActiveLow
	 *   DMAAck3_ActiveLow
	 *   EOT3_ActiveLow
	 */
	unsigned int polarity;

	char buffer_enable;      /* Boolean: buffer enable            */
	char tce_enable;         /* Boolean: terminal count enable    */
	char etd_output;         /* Boolean: eot pin is a tc output   */
	char pce;                /* Boolean: parity check enable      */

	/*
	 * Peripheral location:
	 * INTERNAL_PERIPHERAL (UART0 on the 405GP)
	 * EXTERNAL_PERIPHERAL
	 */
	char pl;                 /* internal/external peripheral      */

	/*
	 * Valid pwidth settings:
	 *   PW_8
	 *   PW_16
	 *   PW_32
	 *   PW_64
	 */
	unsigned int pwidth;

	char dai;                /* Boolean: dst address increment   */
	char sai;                /* Boolean: src address increment   */

	/*
	 * Valid psc settings: 0-3
	 */
	unsigned int psc;        /* Peripheral Setup Cycles         */

	/*
	 * Valid pwc settings:
	 * 0-63
	 */
	unsigned int pwc;        /* Peripheral Wait Cycles          */

	/*
	 * Valid phc settings:
	 * 0-7
	 */
	unsigned int phc;        /* Peripheral Hold Cycles          */

	/*
	 * Valid cp (channel priority) settings:
	 *   PRIORITY_LOW
	 *   PRIORITY_MID_LOW
	 *   PRIORITY_MID_HIGH
	 *   PRIORITY_HIGH
	 */
	unsigned int cp;         /* channel priority                */

	/*
	 * Valid pf (memory read prefetch) settings:
	 *
	 *   PREFETCH_1
	 *   PREFETCH_2
	 *   PREFETCH_4
	 */
	unsigned int pf;         /* memory read prefetch            */

	/*
	 * Boolean: channel interrupt enable
	 * NOTE: for sgl transfers, only the last descriptor will be setup to
	 * interrupt.
	 */
	char int_enable;

	char shift;              /* easy access to byte_count shift, based on */
	                         /* the width of the channel                  */

	uint32_t control;        /* channel control word                      */


	/* These variabled are used ONLY in single dma transfers              */
	unsigned int mode;       /* transfer mode                     */
	dma_addr_t addr;

} ppc_dma_ch_t;


typedef struct {
	uint32_t control;
	uint32_t src_addr;
	uint32_t dst_addr;
	uint32_t control_count;
	uint32_t next;
} ppc_sgl_t;



typedef struct {
	unsigned int dmanr;
	uint32_t control;     /* channel ctrl word; loaded from each descrptr */
	uint32_t sgl_control; /* LK, TCI, ETI, and ERI bits in sgl descriptor */
	dma_addr_t dma_addr;  /* dma (physical) address of this list          */
	ppc_sgl_t *phead;
	ppc_sgl_t *ptail;

} sgl_list_info_t;


typedef struct {
	unsigned int *src_addr;
	unsigned int *dst_addr;
	dma_addr_t dma_src_addr;
	dma_addr_t dma_dst_addr;
} pci_alloc_desc_t;


extern ppc_dma_ch_t dma_channels[];

/*
 *
 * DMA API inline functions
 * These functions are implemented here as inline functions for
 * performance reasons.
 *
 */

static __inline__ int get_405gp_dma_status(void)
{
	return (mfdcr(DCRN_DMASR));
}


static __inline__ int enable_405gp_dma(unsigned int dmanr)
{
	unsigned int control;
	ppc_dma_ch_t *p_dma_ch = &dma_channels[dmanr];

#ifdef DEBUG_405DMA
	if (dmanr >= MAX_405GP_DMA_CHANNELS) {
		printk("enable_dma: bad channel: %d\n", dmanr);
		return DMA_STATUS_BAD_CHANNEL;
	}
#endif


	switch (dmanr) {
	case 0:
		if (p_dma_ch->mode == DMA_MODE_READ) {
			/* peripheral to memory */
			mtdcr(DCRN_DMASA0, NULL);
			mtdcr(DCRN_DMADA0, p_dma_ch->addr);
			}
		else if (p_dma_ch->mode == DMA_MODE_WRITE) {
			/* memory to peripheral */
			mtdcr(DCRN_DMASA0, p_dma_ch->addr);
			mtdcr(DCRN_DMADA0, NULL);
		}
		/* for other xfer modes, the addresses are already set */
		control = mfdcr(DCRN_DMACR0);
		control &= ~(DMA_TM_MASK | DMA_TD);   /* clear all mode bits */
		control |= (p_dma_ch->mode | DMA_CH_ENABLE);
		mtdcr(DCRN_DMACR0, control);
		break;
	case 1:
		if (p_dma_ch->mode == DMA_MODE_READ) {
			mtdcr(DCRN_DMASA1, NULL);
			mtdcr(DCRN_DMADA1, p_dma_ch->addr);
		} else if (p_dma_ch->mode == DMA_MODE_WRITE) {
			mtdcr(DCRN_DMASA1, p_dma_ch->addr);
			mtdcr(DCRN_DMADA1, NULL);
		}
		control = mfdcr(DCRN_DMACR1);
		control &= ~(DMA_TM_MASK | DMA_TD);
		control |= (p_dma_ch->mode | DMA_CH_ENABLE);
		mtdcr(DCRN_DMACR1, control);
		break;
	case 2:
		if (p_dma_ch->mode == DMA_MODE_READ) {
			mtdcr(DCRN_DMASA2, NULL);
			mtdcr(DCRN_DMADA2, p_dma_ch->addr);
		} else if (p_dma_ch->mode == DMA_MODE_WRITE) {
			mtdcr(DCRN_DMASA2, p_dma_ch->addr);
			mtdcr(DCRN_DMADA2, NULL);
		}
		control = mfdcr(DCRN_DMACR2);
		control &= ~(DMA_TM_MASK | DMA_TD);
		control |= (p_dma_ch->mode | DMA_CH_ENABLE);
		mtdcr(DCRN_DMACR2, control);
		break;
	case 3:
		if (p_dma_ch->mode == DMA_MODE_READ) {
			mtdcr(DCRN_DMASA3, NULL);
			mtdcr(DCRN_DMADA3, p_dma_ch->addr);
		} else if (p_dma_ch->mode == DMA_MODE_WRITE) {
			mtdcr(DCRN_DMASA3, p_dma_ch->addr);
			mtdcr(DCRN_DMADA3, NULL);
		}
		control = mfdcr(DCRN_DMACR3);
		control &= ~(DMA_TM_MASK | DMA_TD);
		control |= (p_dma_ch->mode | DMA_CH_ENABLE);
		mtdcr(DCRN_DMACR3, control);
		break;
	default:
		return DMA_STATUS_BAD_CHANNEL;
	}
	return DMA_STATUS_GOOD;
}



static __inline__ void disable_405gp_dma(unsigned int dmanr)
{
	unsigned int control;

	switch (dmanr) {
	case 0:
		control = mfdcr(DCRN_DMACR0);
		control &= ~DMA_CH_ENABLE;
		mtdcr(DCRN_DMACR0, control);
		break;
	case 1:
		control = mfdcr(DCRN_DMACR1);
		control &= ~DMA_CH_ENABLE;
		mtdcr(DCRN_DMACR1, control);
		break;
	case 2:
		control = mfdcr(DCRN_DMACR2);
		control &= ~DMA_CH_ENABLE;
		mtdcr(DCRN_DMACR2, control);
		break;
	case 3:
		control = mfdcr(DCRN_DMACR3);
		control &= ~DMA_CH_ENABLE;
		mtdcr(DCRN_DMACR3, control);
		break;
	default:
#ifdef DEBUG_405DMA
		printk("disable_dma: bad channel: %d\n", dmanr);
#endif
	}
}



/*
 * Sets the dma mode for single DMA transfers only.
 * For scatter/gather transfers, the mode is passed to the
 * alloc_dma_handle() function as one of the parameters.
 *
 * The mode is simply saved and used later.  This allows
 * the driver to call set_dma_mode() and set_dma_addr() in
 * any order.
 *
 * Valid mode values are:
 *
 * DMA_MODE_READ          peripheral to memory
 * DMA_MODE_WRITE         memory to peripheral
 * DMA_MODE_MM            memory to memory
 * DMA_MODE_MM_DEVATSRC   device-paced memory to memory, device at src
 * DMA_MODE_MM_DEVATDST   device-paced memory to memory, device at dst
 */
static __inline__ int set_405gp_dma_mode(unsigned int dmanr, unsigned int mode)
{
	ppc_dma_ch_t *p_dma_ch = &dma_channels[dmanr];

#ifdef DEBUG_405DMA
	switch (mode) {
	case DMA_MODE_READ:
	case DMA_MODE_WRITE:
	case DMA_MODE_MM:
	case DMA_MODE_MM_DEVATSRC:
	case DMA_MODE_MM_DEVATDST:
		break;
	default:
		printk("set_dma_mode: bad mode 0x%x\n", mode);
		return DMA_STATUS_BAD_MODE;
	}
	if (dmanr >= MAX_405GP_DMA_CHANNELS) {
		printk("set_dma_mode: bad channel 0x%x\n", dmanr);
		return DMA_STATUS_BAD_CHANNEL;
	}
#endif

	p_dma_ch->mode = mode;
	return DMA_STATUS_GOOD;
}



/*
 * Sets the DMA Count register. Note that 'count' is in bytes.
 * However, the DMA Count register counts the number of "transfers",
 * where each transfer is equal to the bus width.  Thus, count
 * MUST be a multiple of the bus width.
 */
static __inline__ void
set_405gp_dma_count(unsigned int dmanr, unsigned int count)
{
	ppc_dma_ch_t *p_dma_ch = &dma_channels[dmanr];

#ifdef DEBUG_405DMA
	{
	int error = 0;
	switch(p_dma_ch->pwidth) {
	case PW_8:
		break;
	case PW_16:
		if (count & 0x1)
		error = 1;
		break;
	case PW_32:
		if (count & 0x3)
		error = 1;
		break;
	case PW_64:
		if (count & 0x7)
		error = 1;
		break;
	default:
		printk("set_dma_count: invalid bus width: 0x%x\n",
			p_dma_ch->pwidth);
		return;
	}
	if (error)
		printk("Warning: set_dma_count count 0x%x bus width %d\n",
			count, p_dma_ch->pwidth);
	}
#endif

	count = count >> p_dma_ch->shift;
	switch (dmanr) {
	case 0:
		mtdcr(DCRN_DMACT0, count);
		break;
	case 1:
		mtdcr(DCRN_DMACT1, count);
		break;
	case 2:
		mtdcr(DCRN_DMACT2, count);
		break;
	case 3:
		mtdcr(DCRN_DMACT3, count);
		break;
	default:
#ifdef DEBUG_405DMA
		printk("set_dma_count: bad channel: %d\n", dmanr);
#endif
	}
}



/*
 *   Returns the number of bytes left to be transfered.
 *   After a DMA transfer, this should return zero.
 *   Reading this while a DMA transfer is still in progress will return
 *   unpredictable results.
 */
static __inline__ int get_405gp_dma_residue(unsigned int dmanr)
{
	unsigned int count;
	ppc_dma_ch_t *p_dma_ch = &dma_channels[dmanr];

	switch (dmanr) {
	case 0:
		count = mfdcr(DCRN_DMACT0);
		break;
	case 1:
		count = mfdcr(DCRN_DMACT1);
		break;
	case 2:
		count = mfdcr(DCRN_DMACT2);
		break;
	case 3:
		count = mfdcr(DCRN_DMACT3);
		break;
	default:
#ifdef DEBUG_405DMA
		printk("get_dma_residue: bad channel: %d\n", dmanr);
#endif
	    return 0;
	}

	return (count << p_dma_ch->shift);
}



/*
 * Sets the DMA address for a memory to peripheral or peripheral
 * to memory transfer.  The address is just saved in the channel
 * structure for now and used later in enable_dma().
 */
static __inline__ void set_405gp_dma_addr(unsigned int dmanr, dma_addr_t addr)
{
	ppc_dma_ch_t *p_dma_ch = &dma_channels[dmanr];
#ifdef DEBUG_405DMA
	{
	int error = 0;
	switch(p_dma_ch->pwidth) {
	case PW_8:
		break;
	case PW_16:
		if ((unsigned)addr & 0x1)
		error = 1;
		break;
	case PW_32:
		if ((unsigned)addr & 0x3)
		error = 1;
		break;
	case PW_64:
		if ((unsigned)addr & 0x7)
		error = 1;
		break;
	default:
		printk("set_dma_addr: invalid bus width: 0x%x\n",
			p_dma_ch->pwidth);
		return;
	}
	if (error)
		printk("Warning: set_dma_addr addr 0x%x bus width %d\n",
			addr, p_dma_ch->pwidth);
	}
#endif

	/* save dma address and program it later after we know the xfer mode */
	p_dma_ch->addr = addr;
}




/*
 * Sets both DMA addresses for a memory to memory transfer.
 * For memory to peripheral or peripheral to memory transfers
 * the function set_dma_addr() should be used instead.
 */
static __inline__ void
set_405gp_dma_addr2(unsigned int dmanr, dma_addr_t src_dma_addr,
	dma_addr_t dst_dma_addr)
{
#ifdef DEBUG_405DMA
	{
	ppc_dma_ch_t *p_dma_ch = &dma_channels[dmanr];
	int error = 0;
	switch(p_dma_ch->pwidth) {
	case PW_8:
		break;
	case PW_16:
		if (((unsigned)src_dma_addr & 0x1) ||
		    ((unsigned)dst_dma_addr & 0x1)
		   )
			error = 1;
		break;
	case PW_32:
		if (((unsigned)src_dma_addr & 0x3) ||
		    ((unsigned)dst_dma_addr & 0x3)
		   )
			error = 1;
		break;
	case PW_64:
		if (((unsigned)src_dma_addr & 0x7) ||
		    ((unsigned)dst_dma_addr & 0x7)
		   )
			error = 1;
		break;
	default:
		printk("set_dma_addr2: invalid bus width: 0x%x\n",
			p_dma_ch->pwidth);
		return;
	}
	if (error)
		printk("Warning: set_dma_addr2 src 0x%x dst 0x%x bus width %d\n",
			src_dma_addr, dst_dma_addr, p_dma_ch->pwidth);
	}
#endif

	switch (dmanr) {
	case 0:
		mtdcr(DCRN_DMASA0, src_dma_addr);
		mtdcr(DCRN_DMADA0, dst_dma_addr);
		break;
	case 1:
		mtdcr(DCRN_DMASA1, src_dma_addr);
		mtdcr(DCRN_DMADA1, dst_dma_addr);
		break;
	case 2:
		mtdcr(DCRN_DMASA2, src_dma_addr);
		mtdcr(DCRN_DMADA2, dst_dma_addr);
		break;
	case 3:
		mtdcr(DCRN_DMASA3, src_dma_addr);
		mtdcr(DCRN_DMADA3, dst_dma_addr);
		break;
	default:
#ifdef DEBUG_405DMA
		printk("set_dma_addr2: bad channel: %d\n", dmanr);
#endif
	}
}



/*
 * Enables the channel interrupt.
 *
 * If performing a scatter/gatter transfer, this function
 * MUST be called before calling alloc_dma_handle() and building
 * the sgl list.  Otherwise, interrupts will not be enabled, if
 * they were previously disabled.
 */
static __inline__ int
enable_405gp_dma_interrupt(unsigned int dmanr)
{
	unsigned int control;
	ppc_dma_ch_t *p_dma_ch = &dma_channels[dmanr];

	p_dma_ch->int_enable = TRUE;
	switch (dmanr) {
	case 0:
		control = mfdcr(DCRN_DMACR0);
		control|= DMA_CIE_ENABLE;        /* Channel Interrupt Enable */
		mtdcr(DCRN_DMACR0, control);
		break;
	case 1:
		control = mfdcr(DCRN_DMACR1);
		control|= DMA_CIE_ENABLE;
		mtdcr(DCRN_DMACR1, control);
		break;
	case 2:
		control = mfdcr(DCRN_DMACR2);
		control|= DMA_CIE_ENABLE;
		mtdcr(DCRN_DMACR2, control);
		break;
	case 3:
		control = mfdcr(DCRN_DMACR3);
		control|= DMA_CIE_ENABLE;
		mtdcr(DCRN_DMACR3, control);
		break;
	default:
#ifdef DEBUG_405DMA
		printk("enable_dma_interrupt: bad channel: %d\n", dmanr);
#endif
		return DMA_STATUS_BAD_CHANNEL;
	}
	return DMA_STATUS_GOOD;
}



/*
 * Disables the channel interrupt.
 *
 * If performing a scatter/gatter transfer, this function
 * MUST be called before calling alloc_dma_handle() and building
 * the sgl list.  Otherwise, interrupts will not be disabled, if
 * they were previously enabled.
 */
static __inline__ int
disable_405gp_dma_interrupt(unsigned int dmanr)
{
	unsigned int control;
	ppc_dma_ch_t *p_dma_ch = &dma_channels[dmanr];

	p_dma_ch->int_enable = TRUE;
	switch (dmanr) {
	case 0:
		control = mfdcr(DCRN_DMACR0);
		control &= ~DMA_CIE_ENABLE;       /* Channel Interrupt Enable */
		mtdcr(DCRN_DMACR0, control);
		break;
	case 1:
		control = mfdcr(DCRN_DMACR1);
		control &= ~DMA_CIE_ENABLE;
		mtdcr(DCRN_DMACR1, control);
		break;
	case 2:
		control = mfdcr(DCRN_DMACR2);
		control &= ~DMA_CIE_ENABLE;
		mtdcr(DCRN_DMACR2, control);
		break;
	case 3:
		control = mfdcr(DCRN_DMACR3);
		control &= ~DMA_CIE_ENABLE;
		mtdcr(DCRN_DMACR3, control);
		break;
	default:
#ifdef DEBUG_405DMA
		printk("enable_dma_interrupt: bad channel: %d\n", dmanr);
#endif
		return DMA_STATUS_BAD_CHANNEL;
	}
	return DMA_STATUS_GOOD;
}


#ifdef DCRNCAP_DMA_SG

/*
 *   Add a new sgl descriptor to the end of a scatter/gather list
 *   which was created by alloc_dma_handle().
 *
 *   For a memory to memory transfer, both dma addresses must be
 *   valid. For a peripheral to memory transfer, one of the addresses
 *   must be set to NULL, depending on the direction of the transfer:
 *   memory to peripheral: set dst_addr to NULL,
 *   peripheral to memory: set src_addr to NULL.
 */
static __inline__ int
add_405gp_dma_sgl(sgl_handle_t handle, dma_addr_t src_addr, dma_addr_t dst_addr,
	unsigned int count)
{
	sgl_list_info_t *psgl = (sgl_list_info_t *)handle;
	ppc_dma_ch_t *p_dma_ch;

	if (!handle) {
#ifdef DEBUG_405DMA
		printk("add_dma_sgl: null handle\n");
#endif
		return DMA_STATUS_BAD_HANDLE;
	}

#ifdef DEBUG_405DMA
	if (psgl->dmanr >= MAX_405GP_DMA_CHANNELS) {
		printk("add_dma_sgl error: psgl->dmanr == %d\n", psgl->dmanr);
		return DMA_STATUS_BAD_CHANNEL;
	}
#endif

	p_dma_ch = &dma_channels[psgl->dmanr];

#ifdef DEBUG_405DMA
	{
	int error = 0;
	unsigned int aligned = (unsigned)src_addr | (unsigned)dst_addr | count;
	switch(p_dma_ch->pwidth) {
	case PW_8:
		break;
	case PW_16:
		if (aligned & 0x1)
		error = 1;
		break;
	case PW_32:
		if (aligned & 0x3)
			error = 1;
		break;
	case PW_64:
		if (aligned & 0x7)
			error = 1;
		break;
	default:
		printk("add_dma_sgl: invalid bus width: 0x%x\n",
			p_dma_ch->pwidth);
		return DMA_STATUS_GENERAL_ERROR;
	}
	if (error)
		printk("Alignment warning: add_dma_sgl src 0x%x dst 0x%x count 0x%x bus width var %d\n",
			src_addr, dst_addr, count, p_dma_ch->pwidth);

	}
#endif

	if ((unsigned)(psgl->ptail + 1) >= ((unsigned)psgl + SGL_LIST_SIZE)) {
#ifdef DEBUG_405DMA
		printk("sgl handle out of memory \n");
#endif
		return DMA_STATUS_OUT_OF_MEMORY;
	}


	if (!psgl->ptail) {
		psgl->phead = (ppc_sgl_t *)
			      ((unsigned)psgl + sizeof(sgl_list_info_t));
		psgl->ptail = psgl->phead;
	} else {
		psgl->ptail->next = virt_to_bus(psgl->ptail + 1);
		psgl->ptail++;
	}

	psgl->ptail->control       = psgl->control;
	psgl->ptail->src_addr      = src_addr;
	psgl->ptail->dst_addr      = dst_addr;
	psgl->ptail->control_count = (count >> p_dma_ch->shift) |
				     psgl->sgl_control;
	psgl->ptail->next          = (uint32_t)NULL;

	return DMA_STATUS_GOOD;
}



/*
 * Enable (start) the DMA described by the sgl handle.
 */
static __inline__ void enable_405gp_dma_sgl(sgl_handle_t handle)
{
	sgl_list_info_t *psgl = (sgl_list_info_t *)handle;
	ppc_dma_ch_t *p_dma_ch;
	uint32_t sg_command;

#ifdef DEBUG_405DMA
	if (!handle) {
		printk("enable_dma_sgl: null handle\n");
		return;
	} else if (psgl->dmanr > (MAX_405GP_DMA_CHANNELS - 1)) {
		printk("enable_dma_sgl: bad channel in handle %d\n",
			psgl->dmanr);
		return;
	} else if (!psgl->phead) {
		printk("enable_dma_sgl: sg list empty\n");
		return;
	}
#endif

	p_dma_ch = &dma_channels[psgl->dmanr];
	psgl->ptail->control_count &= ~SG_LINK; /* make this the last dscrptr */
	sg_command = mfdcr(DCRN_ASGC);

	switch(psgl->dmanr) {
	case 0:
		mtdcr(DCRN_ASG0, virt_to_bus(psgl->phead));
		sg_command |= SSG0_ENABLE;
		break;
	case 1:
		mtdcr(DCRN_ASG1, virt_to_bus(psgl->phead));
		sg_command |= SSG1_ENABLE;
		break;
	case 2:
		mtdcr(DCRN_ASG2, virt_to_bus(psgl->phead));
		sg_command |= SSG2_ENABLE;
		break;
	case 3:
		mtdcr(DCRN_ASG3, virt_to_bus(psgl->phead));
		sg_command |= SSG3_ENABLE;
		break;
	default:
#ifdef DEBUG_405DMA
		printk("enable_dma_sgl: bad channel: %d\n", psgl->dmanr);
#endif
	}

#if 0 /* debug */
	printk("\n\nenable_dma_sgl at dma_addr 0x%x\n",
		virt_to_bus(psgl->phead));
	{
	ppc_sgl_t *pnext, *sgl_addr;

	pnext = psgl->phead;
	while (pnext) {
		printk("dma descriptor at 0x%x, dma addr 0x%x\n",
			(unsigned)pnext, (unsigned)virt_to_bus(pnext));
		printk("control 0x%x src 0x%x dst 0x%x c_count 0x%x, next 0x%x\n",
			(unsigned)pnext->control, (unsigned)pnext->src_addr,
			(unsigned)pnext->dst_addr,
			(unsigned)pnext->control_count, (unsigned)pnext->next);

		(unsigned)pnext = bus_to_virt(pnext->next);
	}
	printk("sg_command 0x%x\n", sg_command);
	}
#endif

#ifdef PCI_ALLOC_IS_NONCONSISTENT
	/*
	* This is temporary only, until pci_alloc_consistent() really does
	* return "consistent" memory.
	*/
	flush_dcache_range((unsigned)handle, (unsigned)handle + SGL_LIST_SIZE);
#endif

	mtdcr(DCRN_ASGC, sg_command);             /* start transfer */
}



/*
 * Halt an active scatter/gather DMA operation.
 */
static __inline__ void disable_405gp_dma_sgl(sgl_handle_t handle)
{
	sgl_list_info_t *psgl = (sgl_list_info_t *)handle;
	uint32_t sg_command;

#ifdef DEBUG_405DMA
	if (!handle) {
		printk("enable_dma_sgl: null handle\n");
		return;
	} else if (psgl->dmanr > (MAX_405GP_DMA_CHANNELS - 1)) {
		printk("enable_dma_sgl: bad channel in handle %d\n",
			psgl->dmanr);
		return;
	}
#endif
	sg_command = mfdcr(DCRN_ASGC);
	switch(psgl->dmanr) {
	case 0:
		sg_command &= ~SSG0_ENABLE;
		break;
	case 1:
		sg_command &= ~SSG1_ENABLE;
		break;
	case 2:
		sg_command &= ~SSG2_ENABLE;
		break;
	case 3:
		sg_command &= ~SSG3_ENABLE;
		break;
	default:
#ifdef DEBUG_405DMA
		printk("enable_dma_sgl: bad channel: %d\n", psgl->dmanr);
#endif
	}

	mtdcr(DCRN_ASGC, sg_command);             /* stop transfer */
}



/*
 *  Returns number of bytes left to be transferred from the entire sgl list.
 *  *src_addr and *dst_addr get set to the source/destination address of
 *  the sgl descriptor where the DMA stopped.
 *
 *  An sgl transfer must NOT be active when this function is called.
 */
static __inline__ int
get_405gp_dma_sgl_residue(sgl_handle_t handle, dma_addr_t *src_addr,
	dma_addr_t *dst_addr)
{
	sgl_list_info_t *psgl = (sgl_list_info_t *)handle;
	ppc_dma_ch_t *p_dma_ch;
	ppc_sgl_t *pnext, *sgl_addr;
	uint32_t count_left;

#ifdef DEBUG_405DMA
	if (!handle) {
		printk("get_dma_sgl_residue: null handle\n");
		return DMA_STATUS_BAD_HANDLE;
	} else if (psgl->dmanr > (MAX_405GP_DMA_CHANNELS - 1)) {
		printk("get_dma_sgl_residue: bad channel in handle %d\n",
			psgl->dmanr);
		return DMA_STATUS_BAD_CHANNEL;
	}
#endif

	switch(psgl->dmanr) {
	case 0:
		sgl_addr = (ppc_sgl_t *)bus_to_virt(mfdcr(DCRN_ASG0));
		count_left = mfdcr(DCRN_DMACT0);
		break;
	case 1:
		sgl_addr = (ppc_sgl_t *)bus_to_virt(mfdcr(DCRN_ASG1));
		count_left = mfdcr(DCRN_DMACT1);
		break;
	case 2:
		sgl_addr = (ppc_sgl_t *)bus_to_virt(mfdcr(DCRN_ASG2));
		count_left = mfdcr(DCRN_DMACT2);
		break;
	case 3:
		sgl_addr = (ppc_sgl_t *)bus_to_virt(mfdcr(DCRN_ASG3));
		count_left = mfdcr(DCRN_DMACT3);
		break;
	default:
#ifdef DEBUG_405DMA
		printk("get_dma_sgl_residue: bad channel: %d\n", psgl->dmanr);
#endif
		goto error;
	}

	if (!sgl_addr) {
#ifdef DEBUG_405DMA
		printk("get_dma_sgl_residue: sgl addr register is null\n");
#endif
		goto error;
	}

	pnext = psgl->phead;
	while (pnext &&
		((unsigned)pnext < ((unsigned)psgl + SGL_LIST_SIZE) &&
		(pnext != sgl_addr))
	      ) {
		pnext = pnext++;
	}

	if (pnext == sgl_addr) {           /* found the sgl descriptor */

		*src_addr = pnext->src_addr;
		*dst_addr = pnext->dst_addr;

		/*
		 * Now search the remaining descriptors and add their count.
		 * We already have the remaining count from this descriptor in
		 * count_left.
		 */
		pnext++;

		while ((pnext != psgl->ptail) &&
			((unsigned)pnext < ((unsigned)psgl + SGL_LIST_SIZE))
		      ) {
			count_left += pnext->control_count & SG_COUNT_MASK;
		}

		if (pnext != psgl->ptail) { /* should never happen */
#ifdef DEBUG_405DMA
			printk("get_dma_sgl_residue error (1) psgl->ptail 0x%x handle 0x%x\n",
				(unsigned int)psgl->ptail,
				(unsigned int)handle);
#endif
			goto error;
		}

		/* success */
		p_dma_ch = &dma_channels[psgl->dmanr];
		return (count_left << p_dma_ch->shift);  /* count in bytes */

	} else {
	/* this shouldn't happen */
#ifdef DEBUG_405DMA
		printk("get_dma_sgl_residue, unable to match current address 0x%x, handle 0x%x\n",
			(unsigned int)sgl_addr, (unsigned int)handle);

#endif
	}


error:
	*src_addr = (dma_addr_t)NULL;
	*dst_addr = (dma_addr_t)NULL;
	return 0;
}




/*
 * Returns the address(es) of the buffer(s) contained in the head element of
 * the scatter/gather list.  The element is removed from the scatter/gather
 * list and the next element becomes the head.
 *
 * This function should only be called when the DMA is not active.
 */
static __inline__ int
delete_405gp_dma_sgl_element(sgl_handle_t handle, dma_addr_t *src_dma_addr,
	dma_addr_t *dst_dma_addr)
{
	sgl_list_info_t *psgl = (sgl_list_info_t *)handle;

#ifdef DEBUG_405DMA
	if (!handle) {
		printk("delete_sgl_element: null handle\n");
		return DMA_STATUS_BAD_HANDLE;
	} else if (psgl->dmanr > (MAX_405GP_DMA_CHANNELS - 1)) {
		printk("delete_sgl_element: bad channel in handle %d\n",
			psgl->dmanr);
		return DMA_STATUS_BAD_CHANNEL;
	}
#endif

	if (!psgl->phead) {
#ifdef DEBUG_405DMA
		printk("delete_sgl_element: sgl list empty\n");
#endif
		*src_dma_addr = (dma_addr_t)NULL;
		*dst_dma_addr = (dma_addr_t)NULL;
		return DMA_STATUS_SGL_LIST_EMPTY;
	}

	*src_dma_addr = (dma_addr_t)psgl->phead->src_addr;
	*dst_dma_addr = (dma_addr_t)psgl->phead->dst_addr;

	if (psgl->phead == psgl->ptail) {
		/* last descriptor on the list */
		psgl->phead = NULL;
		psgl->ptail = NULL;
	} else {
		psgl->phead++;
	}

	return DMA_STATUS_GOOD;
}

#endif /* DCRNCAP_DMA_SG */

/*
 * The rest of the DMA API, in ppc405_dma.c
 */
extern int hw_init_dma_channel(unsigned int,  ppc_dma_ch_t *);
extern int get_channel_config(unsigned int, ppc_dma_ch_t *);
extern int set_channel_priority(unsigned int, unsigned int);
extern unsigned int get_peripheral_width(unsigned int);
extern int alloc_dma_handle(sgl_handle_t *, unsigned int, unsigned int);
extern void free_dma_handle(sgl_handle_t);

#endif
#endif /* __KERNEL__ */
