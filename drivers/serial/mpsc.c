/*
 * drivers/serial/mpsc.c
 *
 * Generic driver for the MPSC (UART mode) on Marvell parts (e.g., GT64240,
 * GT64260, MV64340, MV64360, GT96100, ... ).
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * Based on an old MPSC driver that was in the linuxppc tree.  It appears to
 * have been created by Chris Zankel (formerly of MontaVista) but there
 * is no proper Copyright so I'm not sure.  Apparently, parts were also
 * taken from PPCBoot (now U-Boot).  Also based on drivers/serial/8250.c
 * by Russell King.
 *
 * 2004 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
/*
 * The MPSC interface is much like a typical network controller's interface.
 * That is, you set up separate rings of descriptors for transmitting and
 * receiving data.  There is also a pool of buffers with (one buffer per
 * descriptor) that incoming data are dma'd into or outgoing data are dma'd
 * out of.
 *
 * The MPSC requires two other controllers to be able to work.  The Baud Rate
 * Generator (BRG) provides a clock at programmable frequencies which determines
 * the baud rate.  The Serial DMA Controller (SDMA) takes incoming data from the
 * MPSC and DMA's it into memory or DMA's outgoing data and passes it to the
 * MPSC.  It is actually the SDMA interrupt that the driver uses to keep the
 * transmit and receive "engines" going (i.e., indicate data has been
 * transmitted or received).
 *
 * NOTES:
 *
 * 1) Some chips have an erratum where several regs cannot be
 * read.  To work around that, we keep a local copy of those regs in
 * 'mpsc_port_info' and use the *_M or *_S macros when accessing those regs.
 *
 * 2) Some chips have an erratum where the ctlr will hang when the SDMA ctlr
 * accesses system mem in a cache coherent region.  This *should* be a
 * show-stopper when coherency is turned on but it seems to work okay as
 * long as there are no snoop hits.  Therefore, the ring buffer entries and
 * the buffers themselves are allocated via 'dma_alloc_noncoherent()' and
 * 'dma_cache_sync()' is used.  Also, since most PPC platforms are coherent
 * which makes 'dma_cache_sync()' a no-op, explicit cache management macros
 * have been added ensuring there are no snoop hits when coherency is on.
 *
 * 3) There is an erratum (on PPC) where you can't use the instruction to do
 * a DMA_TO_DEVICE/cache clean so DMA_BIDIRECTIONAL/flushes are used in places
 * where a DMA_TO_DEVICE/clean would have [otherwise] sufficed.
 *
 * 4) AFAICT, hardware flow control isn't supported by the controller --MAG.
 */

#include <linux/mv64xxx.h>
#include "mpsc.h"

/*
 * Define how this driver is known to the outside (we've been assigned a
 * range on the "Low-density serial ports" major).
 */
#define MPSC_MAJOR		204
#define MPSC_MINOR_START	44
#define	MPSC_DRIVER_NAME	"MPSC"
#define	MPSC_DEVFS_NAME		"ttymm/"
#define	MPSC_DEV_NAME		"ttyMM"
#define	MPSC_VERSION		"1.00"

static struct mpsc_port_info mpsc_ports[MPSC_NUM_CTLRS];
static struct mpsc_shared_regs mpsc_shared_regs;

/*
 ******************************************************************************
 *
 * Baud Rate Generator Routines (BRG)
 *
 ******************************************************************************
 */
static void
mpsc_brg_init(struct mpsc_port_info *pi, u32 clk_src)
{
	if (pi->brg_can_tune)
		MPSC_MOD_FIELD_M(pi, brg, BRG_BCR, 1, 25, 0);

	MPSC_MOD_FIELD_M(pi, brg, BRG_BCR, 4, 18, clk_src);
	MPSC_MOD_FIELD(pi, brg, BRG_BTR, 16, 0, 0);
	return;
}

static void
mpsc_brg_enable(struct mpsc_port_info *pi)
{
	MPSC_MOD_FIELD_M(pi, brg, BRG_BCR, 1, 16, 1);
	return;
}

static void
mpsc_brg_disable(struct mpsc_port_info *pi)
{
	MPSC_MOD_FIELD_M(pi, brg, BRG_BCR, 1, 16, 0);
	return;
}

static inline void
mpsc_set_baudrate(struct mpsc_port_info *pi, u32 baud)
{
	/*
	 * To set the baud, we adjust the CDV field in the BRG_BCR reg.
	 * From manual: Baud = clk / ((CDV+1)*2) ==> CDV = (clk / (baud*2)) - 1.
	 * However, the input clock is divided by 16 in the MPSC b/c of how
	 * 'MPSC_MMCRH' was set up so we have to divide the 'clk' used in our
	 * calculation by 16 to account for that.  So the real calculation
	 * that accounts for the way the mpsc is set up is:
	 * CDV = (clk / (baud*2*16)) - 1 ==> CDV = (clk / (baud << 5)) - 1.
	 */
	u32 cdv = (pi->port.uartclk / (baud << 5)) - 1;

	mpsc_brg_disable(pi);
	MPSC_MOD_FIELD_M(pi, brg, BRG_BCR, 16, 0, cdv);
	mpsc_brg_enable(pi);

	return;
}

/*
 ******************************************************************************
 *
 * Serial DMA Routines (SDMA)
 *
 ******************************************************************************
 */

static void
mpsc_sdma_burstsize(struct mpsc_port_info *pi, u32 burst_size)
{
	u32 v;

	pr_debug("mpsc_sdma_burstsize[%d]: burst_size: %d\n",
	    pi->port.line, burst_size);

	burst_size >>= 3; /* Divide by 8 b/c reg values are 8-byte chunks */

	if (burst_size < 2)
		v = 0x0;	/* 1 64-bit word */
	else if (burst_size < 4)
		v = 0x1;	/* 2 64-bit words */
	else if (burst_size < 8)
		v = 0x2;	/* 4 64-bit words */
	else
		v = 0x3;	/* 8 64-bit words */

	MPSC_MOD_FIELD(pi, sdma, SDMA_SDC, 2, 12, v);
	return;
}

static void
mpsc_sdma_init(struct mpsc_port_info *pi, u32 burst_size)
{
	pr_debug("mpsc_sdma_init[%d]: burst_size: %d\n", pi->port.line,
		burst_size);

	MPSC_MOD_FIELD(pi, sdma, SDMA_SDC, 10, 0, 0x03f);
	mpsc_sdma_burstsize(pi, burst_size);
	return;
}

static inline u32
mpsc_sdma_intr_mask(struct mpsc_port_info *pi, u32 mask)
{
	u32 old, v;

	pr_debug("mpsc_sdma_intr_mask[%d]: mask: 0x%x\n", pi->port.line, mask);

	old = v = MPSC_READ_S(pi, sdma_intr, SDMA_INTR_MASK);
	mask &= 0xf;
	if (pi->port.line)
		mask <<= 8;
	v &= ~mask;
	MPSC_WRITE_S(pi, sdma_intr, SDMA_INTR_MASK, v);

	if (pi->port.line)
		old >>= 8;
	return old & 0xf;
}

static inline void
mpsc_sdma_intr_unmask(struct mpsc_port_info *pi, u32 mask)
{
	u32 v;

	pr_debug("mpsc_sdma_intr_unmask[%d]: mask: 0x%x\n", pi->port.line,mask);

	v = MPSC_READ_S(pi, sdma_intr, SDMA_INTR_MASK);
	mask &= 0xf;
	if (pi->port.line)
		mask <<= 8;
	v |= mask;
	MPSC_WRITE_S(pi, sdma_intr, SDMA_INTR_MASK, v);
	return;
}

static inline void
mpsc_sdma_intr_ack(struct mpsc_port_info *pi)
{
	pr_debug("mpsc_sdma_intr_ack[%d]: Acknowledging IRQ\n", pi->port.line);
	MPSC_WRITE_S(pi, sdma_intr, SDMA_INTR_CAUSE, 0);
	return;
}

static inline void
mpsc_sdma_set_rx_ring(struct mpsc_port_info *pi, struct mpsc_rx_desc *rxre_p)
{
	pr_debug("mpsc_sdma_set_rx_ring[%d]: rxre_p: 0x%x\n",
		pi->port.line, (u32) rxre_p);

	MPSC_WRITE(pi, sdma, SDMA_SCRDP, (u32) rxre_p);
	return;
}

static inline void
mpsc_sdma_set_tx_ring(struct mpsc_port_info *pi, struct mpsc_tx_desc *txre_p)
{
	MPSC_WRITE(pi, sdma, SDMA_SFTDP, (u32) txre_p);
	MPSC_WRITE(pi, sdma, SDMA_SCTDP, (u32) txre_p);
	return;
}

static inline void
mpsc_sdma_cmd(struct mpsc_port_info *pi, u32 val)
{
	u32 v;

	v = MPSC_READ(pi, sdma, SDMA_SDCM);
	if (val)
		v |= val;
	else
		v = 0;
	wmb();
	MPSC_WRITE(pi, sdma, SDMA_SDCM, v);
	wmb();
	return;
}

static inline uint
mpsc_sdma_tx_active(struct mpsc_port_info *pi)
{
	return MPSC_READ(pi, sdma, SDMA_SDCM) & SDMA_SDCM_TXD;
}

static inline void
mpsc_sdma_start_tx(struct mpsc_port_info *pi)
{
	struct mpsc_tx_desc *txre, *txre_p;

	/* If tx isn't running & there's a desc ready to go, start it */
	if (!mpsc_sdma_tx_active(pi)) {
		txre = (struct mpsc_tx_desc *)(pi->txr +
			(pi->txr_tail * MPSC_TXRE_SIZE));
		dma_cache_sync((void *) txre, MPSC_TXRE_SIZE, DMA_FROM_DEVICE);
		MPSC_CACHE_INVALIDATE(pi, (u32)txre, (u32)txre+MPSC_TXRE_SIZE);

		if (be32_to_cpu(txre->cmdstat) & SDMA_DESC_CMDSTAT_O) {
			txre_p = (struct mpsc_tx_desc *)(pi->txr_p +
							 (pi->txr_tail *
							  MPSC_TXRE_SIZE));

			mpsc_sdma_set_tx_ring(pi, txre_p);
			mpsc_sdma_cmd(pi, SDMA_SDCM_STD | SDMA_SDCM_TXD);
		}
	}

	return;
}

static inline void
mpsc_sdma_stop(struct mpsc_port_info *pi)
{
	pr_debug("mpsc_sdma_stop[%d]: Stopping SDMA\n", pi->port.line);

	/* Abort any SDMA transfers */
	mpsc_sdma_cmd(pi, 0);
	mpsc_sdma_cmd(pi, SDMA_SDCM_AR | SDMA_SDCM_AT);

	/* Clear the SDMA current and first TX and RX pointers */
	mpsc_sdma_set_tx_ring(pi, 0);
	mpsc_sdma_set_rx_ring(pi, 0);

	/* Disable interrupts */
	mpsc_sdma_intr_mask(pi, 0xf);
	mpsc_sdma_intr_ack(pi);

	return;
}

/*
 ******************************************************************************
 *
 * Multi-Protocol Serial Controller Routines (MPSC)
 *
 ******************************************************************************
 */

static void
mpsc_hw_init(struct mpsc_port_info *pi)
{
	pr_debug("mpsc_hw_init[%d]: Initializing hardware\n", pi->port.line);

	/* Set up clock routing */
	MPSC_MOD_FIELD_S(pi, mpsc_routing, MPSC_MRR, 3, 0, 0);
	MPSC_MOD_FIELD_S(pi, mpsc_routing, MPSC_MRR, 3, 6, 0);
	MPSC_MOD_FIELD_S(pi, mpsc_routing, MPSC_RCRR, 4, 0, 0);
	MPSC_MOD_FIELD_S(pi, mpsc_routing, MPSC_RCRR, 4, 8, 1);
	MPSC_MOD_FIELD_S(pi, mpsc_routing, MPSC_TCRR, 4, 0, 0);
	MPSC_MOD_FIELD_S(pi, mpsc_routing, MPSC_TCRR, 4, 8, 1);

	/* Put MPSC in UART mode & enabel Tx/Rx egines */
	MPSC_WRITE(pi, mpsc, MPSC_MMCRL, 0x000004c4);

	/* No preamble, 16x divider, low-latency,  */
	MPSC_WRITE(pi, mpsc, MPSC_MMCRH, 0x04400400);

	MPSC_WRITE_M(pi, mpsc, MPSC_CHR_1, 0);
	MPSC_WRITE_M(pi, mpsc, MPSC_CHR_2, 0);
	MPSC_WRITE(pi, mpsc, MPSC_CHR_3, pi->mpsc_max_idle);
	MPSC_WRITE(pi, mpsc, MPSC_CHR_4, 0);
	MPSC_WRITE(pi, mpsc, MPSC_CHR_5, 0);
	MPSC_WRITE(pi, mpsc, MPSC_CHR_6, 0);
	MPSC_WRITE(pi, mpsc, MPSC_CHR_7, 0);
	MPSC_WRITE(pi, mpsc, MPSC_CHR_8, 0);
	MPSC_WRITE(pi, mpsc, MPSC_CHR_9, 0);
	MPSC_WRITE(pi, mpsc, MPSC_CHR_10, 0);

	return;
}

static inline void
mpsc_enter_hunt(struct mpsc_port_info *pi)
{
	u32 v;

	pr_debug("mpsc_enter_hunt[%d]: Hunting...\n", pi->port.line);

	MPSC_MOD_FIELD_M(pi, mpsc, MPSC_CHR_2, 1, 31, 1);

	/* If erratum prevents reading CHR_2, just delay for a while */
	if (pi->mirror_regs)
		udelay(100);
	else
		do {
			v = MPSC_READ_M(pi, mpsc, MPSC_CHR_2);
		} while (v & MPSC_CHR_2_EH);

	return;
}

static inline void
mpsc_freeze(struct mpsc_port_info *pi)
{
	pr_debug("mpsc_freeze[%d]: Freezing\n", pi->port.line);

	MPSC_MOD_FIELD_M(pi, mpsc, MPSC_MPCR, 1, 9, 1);
	return;
}

static inline void
mpsc_unfreeze(struct mpsc_port_info *pi)
{
	MPSC_MOD_FIELD_M(pi, mpsc, MPSC_MPCR, 1, 9, 0);

	pr_debug("mpsc_unfreeze[%d]: Unfrozen\n", pi->port.line);
	return;
}

static inline void
mpsc_set_char_length(struct mpsc_port_info *pi, u32 len)
{
	pr_debug("mpsc_set_char_length[%d]: char len: %d\n", pi->port.line,len);

	MPSC_MOD_FIELD_M(pi, mpsc, MPSC_MPCR, 2, 12, len);
	return;
}

static inline void
mpsc_set_stop_bit_length(struct mpsc_port_info *pi, u32 len)
{
	pr_debug("mpsc_set_stop_bit_length[%d]: stop bits: %d\n",
		pi->port.line, len);

	MPSC_MOD_FIELD_M(pi, mpsc, MPSC_MPCR, 1, 14, len);
	return;
}

static inline void
mpsc_set_parity(struct mpsc_port_info *pi, u32 p)
{
	pr_debug("mpsc_set_parity[%d]: parity bits: 0x%x\n", pi->port.line, p);

	MPSC_MOD_FIELD_M(pi, mpsc, MPSC_CHR_2, 2, 2, p);	/* TPM */
	MPSC_MOD_FIELD_M(pi, mpsc, MPSC_CHR_2, 2, 18, p);	/* RPM */
	return;
}

/*
 ******************************************************************************
 *
 * Driver Init Routines
 *
 ******************************************************************************
 */

static void
mpsc_init_hw(struct mpsc_port_info *pi)
{
	pr_debug("mpsc_init_hw[%d]: Initializing\n", pi->port.line);

	mpsc_brg_init(pi, pi->brg_clk_src);
	mpsc_brg_enable(pi);
	mpsc_sdma_init(pi, dma_get_cache_alignment());	/* burst a cacheline */
	mpsc_sdma_stop(pi);
	mpsc_hw_init(pi);

	return;
}

static int
mpsc_alloc_ring_mem(struct mpsc_port_info *pi)
{
	int rc = 0;
	static void mpsc_free_ring_mem(struct mpsc_port_info *pi);

	pr_debug("mpsc_alloc_ring_mem[%d]: Allocating ring mem\n",
		pi->port.line);

	if (!pi->dma_region) {
		if (!dma_supported(pi->port.dev, 0xffffffff)) {
			printk(KERN_ERR "MPSC: Inadequate DMA support\n");
			rc = -ENXIO;
		}
		else if ((pi->dma_region = dma_alloc_noncoherent(pi->port.dev,
			MPSC_DMA_ALLOC_SIZE, &pi->dma_region_p, GFP_KERNEL))
			== NULL) {

			printk(KERN_ERR "MPSC: Can't alloc Desc region\n");
			rc = -ENOMEM;
		}
	}

	return rc;
}

static void
mpsc_free_ring_mem(struct mpsc_port_info *pi)
{
	pr_debug("mpsc_free_ring_mem[%d]: Freeing ring mem\n", pi->port.line);

	if (pi->dma_region) {
		dma_free_noncoherent(pi->port.dev, MPSC_DMA_ALLOC_SIZE,
			  pi->dma_region, pi->dma_region_p);
		pi->dma_region = NULL;
		pi->dma_region_p = (dma_addr_t) NULL;
	}

	return;
}

static void
mpsc_init_rings(struct mpsc_port_info *pi)
{
	struct mpsc_rx_desc *rxre;
	struct mpsc_tx_desc *txre;
	dma_addr_t dp, dp_p;
	u8 *bp, *bp_p;
	int i;

	pr_debug("mpsc_init_rings[%d]: Initializing rings\n", pi->port.line);

	BUG_ON(pi->dma_region == NULL);

	memset(pi->dma_region, 0, MPSC_DMA_ALLOC_SIZE);

	/*
	 * Descriptors & buffers are multiples of cacheline size and must be
	 * cacheline aligned.
	 */
	dp = ALIGN((u32) pi->dma_region, dma_get_cache_alignment());
	dp_p = ALIGN((u32) pi->dma_region_p, dma_get_cache_alignment());

	/*
	 * Partition dma region into rx ring descriptor, rx buffers,
	 * tx ring descriptors, and tx buffers.
	 */
	pi->rxr = dp;
	pi->rxr_p = dp_p;
	dp += MPSC_RXR_SIZE;
	dp_p += MPSC_RXR_SIZE;

	pi->rxb = (u8 *) dp;
	pi->rxb_p = (u8 *) dp_p;
	dp += MPSC_RXB_SIZE;
	dp_p += MPSC_RXB_SIZE;

	pi->rxr_posn = 0;

	pi->txr = dp;
	pi->txr_p = dp_p;
	dp += MPSC_TXR_SIZE;
	dp_p += MPSC_TXR_SIZE;

	pi->txb = (u8 *) dp;
	pi->txb_p = (u8 *) dp_p;

	pi->txr_head = 0;
	pi->txr_tail = 0;

	/* Init rx ring descriptors */
	dp = pi->rxr;
	dp_p = pi->rxr_p;
	bp = pi->rxb;
	bp_p = pi->rxb_p;

	for (i = 0; i < MPSC_RXR_ENTRIES; i++) {
		rxre = (struct mpsc_rx_desc *)dp;

		rxre->bufsize = cpu_to_be16(MPSC_RXBE_SIZE);
		rxre->bytecnt = cpu_to_be16(0);
		rxre->cmdstat = cpu_to_be32(SDMA_DESC_CMDSTAT_O |
					    SDMA_DESC_CMDSTAT_EI |
					    SDMA_DESC_CMDSTAT_F |
					    SDMA_DESC_CMDSTAT_L);
		rxre->link = cpu_to_be32(dp_p + MPSC_RXRE_SIZE);
		rxre->buf_ptr = cpu_to_be32(bp_p);

		dp += MPSC_RXRE_SIZE;
		dp_p += MPSC_RXRE_SIZE;
		bp += MPSC_RXBE_SIZE;
		bp_p += MPSC_RXBE_SIZE;
	}
	rxre->link = cpu_to_be32(pi->rxr_p);	/* Wrap last back to first */

	/* Init tx ring descriptors */
	dp = pi->txr;
	dp_p = pi->txr_p;
	bp = pi->txb;
	bp_p = pi->txb_p;

	for (i = 0; i < MPSC_TXR_ENTRIES; i++) {
		txre = (struct mpsc_tx_desc *)dp;

		txre->link = cpu_to_be32(dp_p + MPSC_TXRE_SIZE);
		txre->buf_ptr = cpu_to_be32(bp_p);

		dp += MPSC_TXRE_SIZE;
		dp_p += MPSC_TXRE_SIZE;
		bp += MPSC_TXBE_SIZE;
		bp_p += MPSC_TXBE_SIZE;
	}
	txre->link = cpu_to_be32(pi->txr_p);	/* Wrap last back to first */

	dma_cache_sync((void *) pi->dma_region, MPSC_DMA_ALLOC_SIZE,
		DMA_BIDIRECTIONAL);
	MPSC_CACHE_FLUSH(pi, pi->dma_region,
		pi->dma_region + MPSC_DMA_ALLOC_SIZE);

	return;
}

static void
mpsc_uninit_rings(struct mpsc_port_info *pi)
{
	pr_debug("mpsc_uninit_rings[%d]: Uninitializing rings\n",pi->port.line);

	BUG_ON(pi->dma_region == NULL);

	pi->rxr = 0;
	pi->rxr_p = 0;
	pi->rxb = NULL;
	pi->rxb_p = NULL;
	pi->rxr_posn = 0;

	pi->txr = 0;
	pi->txr_p = 0;
	pi->txb = NULL;
	pi->txb_p = NULL;
	pi->txr_head = 0;
	pi->txr_tail = 0;

	return;
}

static int
mpsc_make_ready(struct mpsc_port_info *pi)
{
	int rc;

	pr_debug("mpsc_make_ready[%d]: Making cltr ready\n", pi->port.line);

	if (!pi->ready) {
		mpsc_init_hw(pi);
		if ((rc = mpsc_alloc_ring_mem(pi)))
			return rc;
		mpsc_init_rings(pi);
		pi->ready = 1;
	}

	return 0;
}

/*
 ******************************************************************************
 *
 * Interrupt Handling Routines
 *
 ******************************************************************************
 */

static inline int
mpsc_rx_intr(struct mpsc_port_info *pi, struct pt_regs *regs)
{
	struct mpsc_rx_desc *rxre;
	struct tty_struct *tty = pi->port.info->tty;
	u32	cmdstat, bytes_in, i;
	int	rc = 0;
	u8	*bp;
	char	flag = TTY_NORMAL;
	static void mpsc_start_rx(struct mpsc_port_info *pi);

	pr_debug("mpsc_rx_intr[%d]: Handling Rx intr\n", pi->port.line);

	rxre = (struct mpsc_rx_desc *)(pi->rxr + (pi->rxr_posn*MPSC_RXRE_SIZE));

	dma_cache_sync((void *)rxre, MPSC_RXRE_SIZE, DMA_FROM_DEVICE);
	MPSC_CACHE_INVALIDATE(pi, (u32) rxre, (u32) rxre + MPSC_RXRE_SIZE);

	/*
	 * Loop through Rx descriptors handling ones that have been completed.
	 */
	while (!((cmdstat = be32_to_cpu(rxre->cmdstat)) & SDMA_DESC_CMDSTAT_O)){
		bytes_in = be16_to_cpu(rxre->bytecnt);

		/* Following use of tty struct directly is deprecated */
		if (unlikely((tty->flip.count + bytes_in) >= TTY_FLIPBUF_SIZE)){
			if (tty->low_latency)
				tty_flip_buffer_push(tty);
			/*
			 * If this failed then we will throw awa the bytes
			 * but mst do so to clear interrupts.
			 */
		}

		bp = pi->rxb + (pi->rxr_posn * MPSC_RXBE_SIZE);
		dma_cache_sync((void *) bp, MPSC_RXBE_SIZE, DMA_FROM_DEVICE);
		MPSC_CACHE_INVALIDATE(pi, bp, bp + MPSC_RXBE_SIZE);

		/*
		 * Other than for parity error, the manual provides little
		 * info on what data will be in a frame flagged by any of
		 * these errors.  For parity error, it is the last byte in
		 * the buffer that had the error.  As for the rest, I guess
		 * we'll assume there is no data in the buffer.
		 * If there is...it gets lost.
		 */
		if (unlikely(cmdstat & (SDMA_DESC_CMDSTAT_BR |
			SDMA_DESC_CMDSTAT_FR | SDMA_DESC_CMDSTAT_OR))) {

			pi->port.icount.rx++;

			if (cmdstat & SDMA_DESC_CMDSTAT_BR) {	/* Break */
				pi->port.icount.brk++;

				if (uart_handle_break(&pi->port))
					goto next_frame;
			}
			else if (cmdstat & SDMA_DESC_CMDSTAT_FR)/* Framing */
				pi->port.icount.frame++;
			else if (cmdstat & SDMA_DESC_CMDSTAT_OR) /* Overrun */
				pi->port.icount.overrun++;

			cmdstat &= pi->port.read_status_mask;

			if (cmdstat & SDMA_DESC_CMDSTAT_BR)
				flag = TTY_BREAK;
			else if (cmdstat & SDMA_DESC_CMDSTAT_FR)
				flag = TTY_FRAME;
			else if (cmdstat & SDMA_DESC_CMDSTAT_OR)
				flag = TTY_OVERRUN;
			else if (cmdstat & SDMA_DESC_CMDSTAT_PE)
				flag = TTY_PARITY;
		}

		if (uart_handle_sysrq_char(&pi->port, *bp, regs)) {
			bp++;
			bytes_in--;
			goto next_frame;
		}

		if ((unlikely(cmdstat & (SDMA_DESC_CMDSTAT_BR |
			SDMA_DESC_CMDSTAT_FR | SDMA_DESC_CMDSTAT_OR))) &&
			!(cmdstat & pi->port.ignore_status_mask))

			tty_insert_flip_char(tty, *bp, flag);
		else {
			for (i=0; i<bytes_in; i++)
				tty_insert_flip_char(tty, *bp++, TTY_NORMAL);

			pi->port.icount.rx += bytes_in;
		}

next_frame:
		rxre->bytecnt = cpu_to_be16(0);
		wmb();
		rxre->cmdstat = cpu_to_be32(SDMA_DESC_CMDSTAT_O |
					    SDMA_DESC_CMDSTAT_EI |
					    SDMA_DESC_CMDSTAT_F |
					    SDMA_DESC_CMDSTAT_L);
		wmb();
		dma_cache_sync((void *)rxre, MPSC_RXRE_SIZE, DMA_BIDIRECTIONAL);
		MPSC_CACHE_FLUSH(pi, (u32) rxre, (u32) rxre + MPSC_RXRE_SIZE);

		/* Advance to next descriptor */
		pi->rxr_posn = (pi->rxr_posn + 1) & (MPSC_RXR_ENTRIES - 1);
		rxre = (struct mpsc_rx_desc *)(pi->rxr +
			(pi->rxr_posn * MPSC_RXRE_SIZE));
		dma_cache_sync((void *)rxre, MPSC_RXRE_SIZE, DMA_FROM_DEVICE);
		MPSC_CACHE_INVALIDATE(pi, (u32)rxre, (u32)rxre+MPSC_RXRE_SIZE);

		rc = 1;
	}

	/* Restart rx engine, if its stopped */
	if ((MPSC_READ(pi, sdma, SDMA_SDCM) & SDMA_SDCM_ERD) == 0)
		mpsc_start_rx(pi);

	tty_flip_buffer_push(tty);
	return rc;
}

static inline void
mpsc_setup_tx_desc(struct mpsc_port_info *pi, u32 count, u32 intr)
{
	struct mpsc_tx_desc *txre;

	txre = (struct mpsc_tx_desc *)(pi->txr +
		(pi->txr_head * MPSC_TXRE_SIZE));

	txre->bytecnt = cpu_to_be16(count);
	txre->shadow = txre->bytecnt;
	wmb();			/* ensure cmdstat is last field updated */
	txre->cmdstat = cpu_to_be32(SDMA_DESC_CMDSTAT_O | SDMA_DESC_CMDSTAT_F |
				    SDMA_DESC_CMDSTAT_L | ((intr) ?
							   SDMA_DESC_CMDSTAT_EI
							   : 0));
	wmb();
	dma_cache_sync((void *) txre, MPSC_TXRE_SIZE, DMA_BIDIRECTIONAL);
	MPSC_CACHE_FLUSH(pi, (u32) txre, (u32) txre + MPSC_TXRE_SIZE);

	return;
}

static inline void
mpsc_copy_tx_data(struct mpsc_port_info *pi)
{
	struct circ_buf *xmit = &pi->port.info->xmit;
	u8 *bp;
	u32 i;

	/* Make sure the desc ring isn't full */
	while (CIRC_CNT(pi->txr_head, pi->txr_tail, MPSC_TXR_ENTRIES) <
	       (MPSC_TXR_ENTRIES - 1)) {
		if (pi->port.x_char) {
			/*
			 * Ideally, we should use the TCS field in
			 * CHR_1 to put the x_char out immediately but
			 * errata prevents us from being able to read
			 * CHR_2 to know that its safe to write to
			 * CHR_1.  Instead, just put it in-band with
			 * all the other Tx data.
			 */
			bp = pi->txb + (pi->txr_head * MPSC_TXBE_SIZE);
			*bp = pi->port.x_char;
			pi->port.x_char = 0;
			i = 1;
		}
		else if (!uart_circ_empty(xmit) && !uart_tx_stopped(&pi->port)){
			i = min((u32) MPSC_TXBE_SIZE,
				(u32) uart_circ_chars_pending(xmit));
			i = min(i, (u32) CIRC_CNT_TO_END(xmit->head, xmit->tail,
				UART_XMIT_SIZE));
			bp = pi->txb + (pi->txr_head * MPSC_TXBE_SIZE);
			memcpy(bp, &xmit->buf[xmit->tail], i);
			xmit->tail = (xmit->tail + i) & (UART_XMIT_SIZE - 1);

			if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
				uart_write_wakeup(&pi->port);
		}
		else /* All tx data copied into ring bufs */
			return;

		dma_cache_sync((void *) bp, MPSC_TXBE_SIZE, DMA_BIDIRECTIONAL);
		MPSC_CACHE_FLUSH(pi, bp, bp + MPSC_TXBE_SIZE);
		mpsc_setup_tx_desc(pi, i, 1);

		/* Advance to next descriptor */
		pi->txr_head = (pi->txr_head + 1) & (MPSC_TXR_ENTRIES - 1);
	}

	return;
}

static inline int
mpsc_tx_intr(struct mpsc_port_info *pi)
{
	struct mpsc_tx_desc *txre;
	int rc = 0;

	if (!mpsc_sdma_tx_active(pi)) {
		txre = (struct mpsc_tx_desc *)(pi->txr +
			(pi->txr_tail * MPSC_TXRE_SIZE));

		dma_cache_sync((void *) txre, MPSC_TXRE_SIZE, DMA_FROM_DEVICE);
		MPSC_CACHE_INVALIDATE(pi, (u32) txre, (u32)txre+MPSC_TXRE_SIZE);

		while (!(be32_to_cpu(txre->cmdstat) & SDMA_DESC_CMDSTAT_O)) {
			rc = 1;
			pi->port.icount.tx += be16_to_cpu(txre->bytecnt);
			pi->txr_tail = (pi->txr_tail+1) & (MPSC_TXR_ENTRIES-1);

			/* If no more data to tx, fall out of loop */
			if (pi->txr_head == pi->txr_tail)
				break;

			txre = (struct mpsc_tx_desc *)(pi->txr +
				(pi->txr_tail * MPSC_TXRE_SIZE));
			dma_cache_sync((void *) txre, MPSC_TXRE_SIZE,
				DMA_FROM_DEVICE);
			MPSC_CACHE_INVALIDATE(pi, (u32) txre,
			      (u32) txre + MPSC_TXRE_SIZE);
		}

		mpsc_copy_tx_data(pi);
		mpsc_sdma_start_tx(pi);	/* start next desc if ready */
	}

	return rc;
}

/*
 * This is the driver's interrupt handler.  To avoid a race, we first clear
 * the interrupt, then handle any completed Rx/Tx descriptors.  When done
 * handling those descriptors, we restart the Rx/Tx engines if they're stopped.
 */
static irqreturn_t
mpsc_sdma_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct mpsc_port_info *pi = dev_id;
	ulong iflags;
	int rc = IRQ_NONE;

	pr_debug("mpsc_sdma_intr[%d]: SDMA Interrupt Received\n",pi->port.line);

	spin_lock_irqsave(&pi->port.lock, iflags);
	mpsc_sdma_intr_ack(pi);
	if (mpsc_rx_intr(pi, regs))
		rc = IRQ_HANDLED;
	if (mpsc_tx_intr(pi))
		rc = IRQ_HANDLED;
	spin_unlock_irqrestore(&pi->port.lock, iflags);

	pr_debug("mpsc_sdma_intr[%d]: SDMA Interrupt Handled\n", pi->port.line);
	return rc;
}

/*
 ******************************************************************************
 *
 * serial_core.c Interface routines
 *
 ******************************************************************************
 */
static uint
mpsc_tx_empty(struct uart_port *port)
{
	struct mpsc_port_info *pi = (struct mpsc_port_info *)port;
	ulong iflags;
	uint rc;

	spin_lock_irqsave(&pi->port.lock, iflags);
	rc = mpsc_sdma_tx_active(pi) ? 0 : TIOCSER_TEMT;
	spin_unlock_irqrestore(&pi->port.lock, iflags);

	return rc;
}

static void
mpsc_set_mctrl(struct uart_port *port, uint mctrl)
{
	/* Have no way to set modem control lines AFAICT */
	return;
}

static uint
mpsc_get_mctrl(struct uart_port *port)
{
	struct mpsc_port_info *pi = (struct mpsc_port_info *)port;
	u32 mflags, status;
	ulong iflags;

	spin_lock_irqsave(&pi->port.lock, iflags);
	status = MPSC_READ_M(pi, mpsc, MPSC_CHR_10);
	spin_unlock_irqrestore(&pi->port.lock, iflags);

	mflags = 0;
	if (status & 0x1)
		mflags |= TIOCM_CTS;
	if (status & 0x2)
		mflags |= TIOCM_CAR;

	return mflags | TIOCM_DSR;	/* No way to tell if DSR asserted */
}

static void
mpsc_stop_tx(struct uart_port *port, uint tty_start)
{
	struct mpsc_port_info *pi = (struct mpsc_port_info *)port;

	pr_debug("mpsc_stop_tx[%d]: tty_start: %d\n", port->line, tty_start);

	mpsc_freeze(pi);
	return;
}

static void
mpsc_start_tx(struct uart_port *port, uint tty_start)
{
	struct mpsc_port_info *pi = (struct mpsc_port_info *)port;

	mpsc_unfreeze(pi);
	mpsc_copy_tx_data(pi);
	mpsc_sdma_start_tx(pi);

	pr_debug("mpsc_start_tx[%d]: tty_start: %d\n", port->line, tty_start);
	return;
}

static void
mpsc_start_rx(struct mpsc_port_info *pi)
{
	pr_debug("mpsc_start_rx[%d]: Starting...\n", pi->port.line);

	if (pi->rcv_data) {
		mpsc_enter_hunt(pi);
		mpsc_sdma_cmd(pi, SDMA_SDCM_ERD);
	}
	return;
}

static void
mpsc_stop_rx(struct uart_port *port)
{
	struct mpsc_port_info *pi = (struct mpsc_port_info *)port;

	pr_debug("mpsc_stop_rx[%d]: Stopping...\n", port->line);

	mpsc_sdma_cmd(pi, SDMA_SDCM_AR);
	return;
}

static void
mpsc_enable_ms(struct uart_port *port)
{
	return;			/* Not supported */
}

static void
mpsc_break_ctl(struct uart_port *port, int ctl)
{
	struct mpsc_port_info *pi = (struct mpsc_port_info *)port;
	ulong flags;

	spin_lock_irqsave(&pi->port.lock, flags);
	if (ctl) /* Send as many BRK chars as we can */
		MPSC_WRITE_M(pi, mpsc, MPSC_CHR_1, 0x00ff0000);
	else /* Stop sending BRK chars */
		MPSC_WRITE_M(pi, mpsc, MPSC_CHR_1, 0);
	spin_unlock_irqrestore(&pi->port.lock, flags);

	return;
}

static int
mpsc_startup(struct uart_port *port)
{
	struct mpsc_port_info *pi = (struct mpsc_port_info *)port;
	u32 flag = 0;
	int rc;

	pr_debug("mpsc_startup[%d]: Starting up MPSC, irq: %d\n",
		port->line, pi->port.irq);

	if ((rc = mpsc_make_ready(pi)) == 0) {
		/* Setup IRQ handler */
		mpsc_sdma_intr_ack(pi);

		/* If irq's are shared, need to set flag */
		if (mpsc_ports[0].port.irq == mpsc_ports[1].port.irq)
			flag = SA_SHIRQ;

		if (request_irq(pi->port.irq, mpsc_sdma_intr, flag,
				"mpsc/sdma", pi))
			printk(KERN_ERR "MPSC: Can't get SDMA IRQ %d\n",
			       pi->port.irq);

		mpsc_sdma_intr_unmask(pi, 0xf);
		mpsc_sdma_set_rx_ring(pi, (struct mpsc_rx_desc *)(pi->rxr_p +
			(pi->rxr_posn * MPSC_RXRE_SIZE)));
	}

	return rc;
}

static void
mpsc_shutdown(struct uart_port *port)
{
	struct mpsc_port_info *pi = (struct mpsc_port_info *)port;
	static void mpsc_release_port(struct uart_port *port);

	pr_debug("mpsc_shutdown[%d]: Shutting down MPSC\n", port->line);

	mpsc_sdma_stop(pi);
	free_irq(pi->port.irq, pi);
	return;
}

static void
mpsc_set_termios(struct uart_port *port, struct termios *termios,
		 struct termios *old)
{
	struct mpsc_port_info *pi = (struct mpsc_port_info *)port;
	u32 baud;
	ulong flags;
	u32 chr_bits, stop_bits, par;

	pi->c_iflag = termios->c_iflag;
	pi->c_cflag = termios->c_cflag;

	switch (termios->c_cflag & CSIZE) {
	case CS5:
		chr_bits = MPSC_MPCR_CL_5;
		break;
	case CS6:
		chr_bits = MPSC_MPCR_CL_6;
		break;
	case CS7:
		chr_bits = MPSC_MPCR_CL_7;
		break;
	case CS8:
	default:
		chr_bits = MPSC_MPCR_CL_8;
		break;
	}

	if (termios->c_cflag & CSTOPB)
		stop_bits = MPSC_MPCR_SBL_2;
	else
		stop_bits = MPSC_MPCR_SBL_1;

	par = MPSC_CHR_2_PAR_EVEN;
	if (termios->c_cflag & PARENB)
		if (termios->c_cflag & PARODD)
			par = MPSC_CHR_2_PAR_ODD;
#ifdef	CMSPAR
		if (termios->c_cflag & CMSPAR) {
			if (termios->c_cflag & PARODD)
				par = MPSC_CHR_2_PAR_MARK;
			else
				par = MPSC_CHR_2_PAR_SPACE;
		}
#endif

	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk);

	spin_lock_irqsave(&pi->port.lock, flags);

	uart_update_timeout(port, termios->c_cflag, baud);

	mpsc_set_char_length(pi, chr_bits);
	mpsc_set_stop_bit_length(pi, stop_bits);
	mpsc_set_parity(pi, par);
	mpsc_set_baudrate(pi, baud);

	/* Characters/events to read */
	pi->rcv_data = 1;
	pi->port.read_status_mask = SDMA_DESC_CMDSTAT_OR;

	if (termios->c_iflag & INPCK)
		pi->port.read_status_mask |= SDMA_DESC_CMDSTAT_PE |
		    SDMA_DESC_CMDSTAT_FR;

	if (termios->c_iflag & (BRKINT | PARMRK))
		pi->port.read_status_mask |= SDMA_DESC_CMDSTAT_BR;

	/* Characters/events to ignore */
	pi->port.ignore_status_mask = 0;

	if (termios->c_iflag & IGNPAR)
		pi->port.ignore_status_mask |= SDMA_DESC_CMDSTAT_PE |
		    SDMA_DESC_CMDSTAT_FR;

	if (termios->c_iflag & IGNBRK) {
		pi->port.ignore_status_mask |= SDMA_DESC_CMDSTAT_BR;

		if (termios->c_iflag & IGNPAR)
			pi->port.ignore_status_mask |= SDMA_DESC_CMDSTAT_OR;
	}

	/* Ignore all chars if CREAD not set */
	if (!(termios->c_cflag & CREAD))
		pi->rcv_data = 0;
	else
		mpsc_start_rx(pi);

	spin_unlock_irqrestore(&pi->port.lock, flags);
	return;
}

static const char *
mpsc_type(struct uart_port *port)
{
	pr_debug("mpsc_type[%d]: port type: %s\n", port->line,MPSC_DRIVER_NAME);
	return MPSC_DRIVER_NAME;
}

static int
mpsc_request_port(struct uart_port *port)
{
	/* Should make chip/platform specific call */
	return 0;
}

static void
mpsc_release_port(struct uart_port *port)
{
	struct mpsc_port_info *pi = (struct mpsc_port_info *)port;

	if (pi->ready) {
		mpsc_uninit_rings(pi);
		mpsc_free_ring_mem(pi);
		pi->ready = 0;
	}

	return;
}

static void
mpsc_config_port(struct uart_port *port, int flags)
{
	return;
}

static int
mpsc_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	struct mpsc_port_info *pi = (struct mpsc_port_info *)port;
	int rc = 0;

	pr_debug("mpsc_verify_port[%d]: Verifying port data\n", pi->port.line);

	if (ser->type != PORT_UNKNOWN && ser->type != PORT_MPSC)
		rc = -EINVAL;
	else if (pi->port.irq != ser->irq)
		rc = -EINVAL;
	else if (ser->io_type != SERIAL_IO_MEM)
		rc = -EINVAL;
	else if (pi->port.uartclk / 16 != ser->baud_base) /* Not sure */
		rc = -EINVAL;
	else if ((void *)pi->port.mapbase != ser->iomem_base)
		rc = -EINVAL;
	else if (pi->port.iobase != ser->port)
		rc = -EINVAL;
	else if (ser->hub6 != 0)
		rc = -EINVAL;

	return rc;
}

static struct uart_ops mpsc_pops = {
	.tx_empty     = mpsc_tx_empty,
	.set_mctrl    = mpsc_set_mctrl,
	.get_mctrl    = mpsc_get_mctrl,
	.stop_tx      = mpsc_stop_tx,
	.start_tx     = mpsc_start_tx,
	.stop_rx      = mpsc_stop_rx,
	.enable_ms    = mpsc_enable_ms,
	.break_ctl    = mpsc_break_ctl,
	.startup      = mpsc_startup,
	.shutdown     = mpsc_shutdown,
	.set_termios  = mpsc_set_termios,
	.type         = mpsc_type,
	.release_port = mpsc_release_port,
	.request_port = mpsc_request_port,
	.config_port  = mpsc_config_port,
	.verify_port  = mpsc_verify_port,
};

/*
 ******************************************************************************
 *
 * Console Interface Routines
 *
 ******************************************************************************
 */

#ifdef CONFIG_SERIAL_MPSC_CONSOLE
static void
mpsc_console_write(struct console *co, const char *s, uint count)
{
	struct mpsc_port_info *pi = &mpsc_ports[co->index];
	u8 *bp, *dp, add_cr = 0;
	int i;

	while (mpsc_sdma_tx_active(pi))
		udelay(100);

	while (count > 0) {
		bp = dp = pi->txb + (pi->txr_head * MPSC_TXBE_SIZE);

		for (i = 0; i < MPSC_TXBE_SIZE; i++) {
			if (count == 0)
				break;

			if (add_cr) {
				*(dp++) = '\r';
				add_cr = 0;
			}
			else {
				*(dp++) = *s;

				if (*(s++) == '\n') { /* add '\r' after '\n' */
					add_cr = 1;
					count++;
				}
			}

			count--;
		}

		dma_cache_sync((void *) bp, MPSC_TXBE_SIZE, DMA_BIDIRECTIONAL);
		MPSC_CACHE_FLUSH(pi, bp, bp + MPSC_TXBE_SIZE);
		mpsc_setup_tx_desc(pi, i, 0);
		pi->txr_head = (pi->txr_head + 1) & (MPSC_TXR_ENTRIES - 1);
		mpsc_sdma_start_tx(pi);

		while (mpsc_sdma_tx_active(pi))
			udelay(100);

		pi->txr_tail = (pi->txr_tail + 1) & (MPSC_TXR_ENTRIES - 1);
	}

	return;
}

static int __init
mpsc_console_setup(struct console *co, char *options)
{
	struct mpsc_port_info *pi;
	int baud, bits, parity, flow;

	pr_debug("mpsc_console_setup[%d]: options: %s\n", co->index, options);

	if (co->index >= MPSC_NUM_CTLRS)
		co->index = 0;

	pi = &mpsc_ports[co->index];

	baud = pi->default_baud;
	bits = pi->default_bits;
	parity = pi->default_parity;
	flow = pi->default_flow;

	if (!pi->port.ops)
		return -ENODEV;

	spin_lock_init(&pi->port.lock);	/* Temporary fix--copied from 8250.c */

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(&pi->port, co, baud, parity, bits, flow);
}

extern struct uart_driver mpsc_reg;
static struct console mpsc_console = {
	.name   = MPSC_DEV_NAME,
	.write  = mpsc_console_write,
	.device = uart_console_device,
	.setup  = mpsc_console_setup,
	.flags  = CON_PRINTBUFFER,
	.index  = -1,
	.data   = &mpsc_reg,
};

static int __init
mpsc_late_console_init(void)
{
	pr_debug("mpsc_late_console_init: Enter\n");

	if (!(mpsc_console.flags & CON_ENABLED))
		register_console(&mpsc_console);
	return 0;
}

late_initcall(mpsc_late_console_init);

#define MPSC_CONSOLE	&mpsc_console
#else
#define MPSC_CONSOLE	NULL
#endif
/*
 ******************************************************************************
 *
 * Dummy Platform Driver to extract & map shared register regions
 *
 ******************************************************************************
 */
static void
mpsc_resource_err(char *s)
{
	printk(KERN_WARNING "MPSC: Platform device resource error in %s\n", s);
	return;
}

static int
mpsc_shared_map_regs(struct platform_device *pd)
{
	struct resource	*r;

	if ((r = platform_get_resource(pd, IORESOURCE_MEM,
		MPSC_ROUTING_BASE_ORDER)) && request_mem_region(r->start,
		MPSC_ROUTING_REG_BLOCK_SIZE, "mpsc_routing_regs")) {

		mpsc_shared_regs.mpsc_routing_base = (u32) ioremap(r->start,
			MPSC_ROUTING_REG_BLOCK_SIZE);
		mpsc_shared_regs.mpsc_routing_base_p = r->start;
	}
	else {
		mpsc_resource_err("MPSC routing base");
		return -ENOMEM;
	}

	if ((r = platform_get_resource(pd, IORESOURCE_MEM,
		MPSC_SDMA_INTR_BASE_ORDER)) && request_mem_region(r->start,
		MPSC_SDMA_INTR_REG_BLOCK_SIZE, "sdma_intr_regs")) {

		mpsc_shared_regs.sdma_intr_base = (u32) ioremap(r->start,
			MPSC_SDMA_INTR_REG_BLOCK_SIZE);
		mpsc_shared_regs.sdma_intr_base_p = r->start;
	}
	else {
		iounmap((void *)mpsc_shared_regs.mpsc_routing_base);
		release_mem_region(mpsc_shared_regs.mpsc_routing_base_p,
			MPSC_ROUTING_REG_BLOCK_SIZE);
		mpsc_resource_err("SDMA intr base");
		return -ENOMEM;
	}

	return 0;
}

static void
mpsc_shared_unmap_regs(void)
{
	if (!mpsc_shared_regs.mpsc_routing_base) {
		iounmap((void *)mpsc_shared_regs.mpsc_routing_base);
		release_mem_region(mpsc_shared_regs.mpsc_routing_base_p,
			MPSC_ROUTING_REG_BLOCK_SIZE);
	}
	if (!mpsc_shared_regs.sdma_intr_base) {
		iounmap((void *)mpsc_shared_regs.sdma_intr_base);
		release_mem_region(mpsc_shared_regs.sdma_intr_base_p,
			MPSC_SDMA_INTR_REG_BLOCK_SIZE);
	}

	mpsc_shared_regs.mpsc_routing_base = 0;
	mpsc_shared_regs.sdma_intr_base = 0;

	mpsc_shared_regs.mpsc_routing_base_p = 0;
	mpsc_shared_regs.sdma_intr_base_p = 0;

	return;
}

static int
mpsc_shared_drv_probe(struct device *dev)
{
	struct platform_device		*pd = to_platform_device(dev);
	struct mpsc_shared_pd_dd	*dd;
	int				 rc = -ENODEV;

	if (pd->id == 0) {
		if (!(rc = mpsc_shared_map_regs(pd)))  {
			dd = (struct mpsc_shared_pd_dd *)
				dev_get_drvdata(dev);

			mpsc_shared_regs.MPSC_MRR_m = dd->mrr_val;
			mpsc_shared_regs.MPSC_RCRR_m= dd->rcrr_val;
			mpsc_shared_regs.MPSC_TCRR_m= dd->tcrr_val;
			mpsc_shared_regs.SDMA_INTR_CAUSE_m =
				dd->intr_cause_val;
			mpsc_shared_regs.SDMA_INTR_MASK_m =
				dd->intr_mask_val;

			rc = 0;
		}
	}

	return rc;
}

static int
mpsc_shared_drv_remove(struct device *dev)
{
	struct platform_device	*pd = to_platform_device(dev);
	int	rc = -ENODEV;

	if (pd->id == 0) {
		mpsc_shared_unmap_regs();
		mpsc_shared_regs.MPSC_MRR_m = 0;
		mpsc_shared_regs.MPSC_RCRR_m = 0;
		mpsc_shared_regs.MPSC_TCRR_m = 0;
		mpsc_shared_regs.SDMA_INTR_CAUSE_m = 0;
		mpsc_shared_regs.SDMA_INTR_MASK_m = 0;
		rc = 0;
	}

	return rc;
}

static struct device_driver mpsc_shared_driver = {
	.name	= MPSC_SHARED_NAME,
	.bus	= &platform_bus_type,
	.probe	= mpsc_shared_drv_probe,
	.remove	= mpsc_shared_drv_remove,
};

/*
 ******************************************************************************
 *
 * Driver Interface Routines
 *
 ******************************************************************************
 */
static struct uart_driver mpsc_reg = {
	.owner       = THIS_MODULE,
	.driver_name = MPSC_DRIVER_NAME,
	.devfs_name  = MPSC_DEVFS_NAME,
	.dev_name    = MPSC_DEV_NAME,
	.major       = MPSC_MAJOR,
	.minor       = MPSC_MINOR_START,
	.nr          = MPSC_NUM_CTLRS,
	.cons        = MPSC_CONSOLE,
};

static int
mpsc_drv_map_regs(struct mpsc_port_info *pi, struct platform_device *pd)
{
	struct resource	*r;

	if ((r = platform_get_resource(pd, IORESOURCE_MEM, MPSC_BASE_ORDER)) &&
		request_mem_region(r->start, MPSC_REG_BLOCK_SIZE, "mpsc_regs")){

		pi->mpsc_base = (u32) ioremap(r->start, MPSC_REG_BLOCK_SIZE);
		pi->mpsc_base_p = r->start;
	}
	else {
		mpsc_resource_err("MPSC base");
		return -ENOMEM;
	}

	if ((r = platform_get_resource(pd, IORESOURCE_MEM,
		MPSC_SDMA_BASE_ORDER)) && request_mem_region(r->start,
		MPSC_SDMA_REG_BLOCK_SIZE, "sdma_regs")) {

		pi->sdma_base = (u32)ioremap(r->start,MPSC_SDMA_REG_BLOCK_SIZE);
		pi->sdma_base_p = r->start;
	}
	else {
		mpsc_resource_err("SDMA base");
		return -ENOMEM;
	}

	if ((r = platform_get_resource(pd,IORESOURCE_MEM,MPSC_BRG_BASE_ORDER))
		&& request_mem_region(r->start, MPSC_BRG_REG_BLOCK_SIZE,
		"brg_regs")) {

		pi->brg_base = (u32) ioremap(r->start, MPSC_BRG_REG_BLOCK_SIZE);
		pi->brg_base_p = r->start;
	}
	else {
		mpsc_resource_err("BRG base");
		return -ENOMEM;
	}

	return 0;
}

static void
mpsc_drv_unmap_regs(struct mpsc_port_info *pi)
{
	if (!pi->mpsc_base) {
		iounmap((void *)pi->mpsc_base);
		release_mem_region(pi->mpsc_base_p, MPSC_REG_BLOCK_SIZE);
	}
	if (!pi->sdma_base) {
		iounmap((void *)pi->sdma_base);
		release_mem_region(pi->sdma_base_p, MPSC_SDMA_REG_BLOCK_SIZE);
	}
	if (!pi->brg_base) {
		iounmap((void *)pi->brg_base);
		release_mem_region(pi->brg_base_p, MPSC_BRG_REG_BLOCK_SIZE);
	}

	pi->mpsc_base = 0;
	pi->sdma_base = 0;
	pi->brg_base = 0;

	pi->mpsc_base_p = 0;
	pi->sdma_base_p = 0;
	pi->brg_base_p = 0;

	return;
}

static void
mpsc_drv_get_platform_data(struct mpsc_port_info *pi,
	struct platform_device *pd, int num)
{
	struct mpsc_pd_dd	*dd;

	dd = (struct mpsc_pd_dd *)dev_get_drvdata(&pd->dev);

	pi->port.uartclk = dd->brg_clk_freq;
	pi->port.iotype = UPIO_MEM;
	pi->port.line = num;
	pi->port.type = PORT_MPSC;
	pi->port.fifosize = MPSC_TXBE_SIZE;
	pi->port.membase = (char *)pi->mpsc_base;
	pi->port.mapbase = (ulong) pi->mpsc_base;
	pi->port.ops = &mpsc_pops;

	pi->mirror_regs = dd->mirror_regs;
	pi->cache_mgmt = dd->cache_mgmt;
	pi->brg_can_tune = dd->brg_can_tune;
	pi->brg_clk_src = dd->brg_clk_src;
	pi->mpsc_max_idle = dd->max_idle;
	pi->default_baud = dd->default_baud;
	pi->default_bits = dd->default_bits;
	pi->default_parity = dd->default_parity;
	pi->default_flow = dd->default_flow;

	/* Initial values of mirrored regs */
	pi->MPSC_CHR_1_m = dd->chr_1_val;
	pi->MPSC_CHR_2_m = dd->chr_2_val;
	pi->MPSC_CHR_10_m = dd->chr_10_val;
	pi->MPSC_MPCR_m = dd->mpcr_val;
	pi->BRG_BCR_m = dd->bcr_val;

	pi->shared_regs = &mpsc_shared_regs;

	pi->port.irq = platform_get_irq(pd, 0);

	return;
}

static int
mpsc_drv_probe(struct device *dev)
{
	struct platform_device	*pd = to_platform_device(dev);
	struct mpsc_port_info	*pi;
	int			rc = -ENODEV;

	pr_debug("mpsc_drv_probe: Adding MPSC %d\n", pd->id);

	if (pd->id < MPSC_NUM_CTLRS) {
		pi = &mpsc_ports[pd->id];

		if (!(rc = mpsc_drv_map_regs(pi, pd))) {
			mpsc_drv_get_platform_data(pi, pd, pd->id);

			if (!(rc = mpsc_make_ready(pi)))
				if (!(rc = uart_add_one_port(&mpsc_reg,
					&pi->port)))
					rc = 0;
				else {
					mpsc_release_port(
						(struct uart_port *)pi);
					mpsc_drv_unmap_regs(pi);
				}
			else
				mpsc_drv_unmap_regs(pi);
		}
	}

	return rc;
}

static int
mpsc_drv_remove(struct device *dev)
{
	struct platform_device	*pd = to_platform_device(dev);

	pr_debug("mpsc_drv_exit: Removing MPSC %d\n", pd->id);

	if (pd->id < MPSC_NUM_CTLRS) {
		uart_remove_one_port(&mpsc_reg, &mpsc_ports[pd->id].port);
		mpsc_release_port((struct uart_port *)&mpsc_ports[pd->id].port);
		mpsc_drv_unmap_regs(&mpsc_ports[pd->id]);
		return 0;
	}
	else
		return -ENODEV;
}

static struct device_driver mpsc_driver = {
	.name	= MPSC_CTLR_NAME,
	.bus	= &platform_bus_type,
	.probe	= mpsc_drv_probe,
	.remove	= mpsc_drv_remove,
};

static int __init
mpsc_drv_init(void)
{
	int	rc;

	printk(KERN_INFO "Serial: MPSC driver $Revision: 1.00 $\n");

	memset(mpsc_ports, 0, sizeof(mpsc_ports));
	memset(&mpsc_shared_regs, 0, sizeof(mpsc_shared_regs));

	if (!(rc = uart_register_driver(&mpsc_reg))) {
		if (!(rc = driver_register(&mpsc_shared_driver))) {
			if ((rc = driver_register(&mpsc_driver))) {
				driver_unregister(&mpsc_shared_driver);
				uart_unregister_driver(&mpsc_reg);
			}
		}
		else
			uart_unregister_driver(&mpsc_reg);
	}

	return rc;

}

static void __exit
mpsc_drv_exit(void)
{
	driver_unregister(&mpsc_driver);
	driver_unregister(&mpsc_shared_driver);
	uart_unregister_driver(&mpsc_reg);
	memset(mpsc_ports, 0, sizeof(mpsc_ports));
	memset(&mpsc_shared_regs, 0, sizeof(mpsc_shared_regs));
	return;
}

module_init(mpsc_drv_init);
module_exit(mpsc_drv_exit);

MODULE_AUTHOR("Mark A. Greer <mgreer@mvista.com>");
MODULE_DESCRIPTION("Generic Marvell MPSC serial/UART driver $Revision: 1.00 $");
MODULE_VERSION(MPSC_VERSION);
MODULE_LICENSE("GPL");
MODULE_ALIAS_CHARDEV_MAJOR(MPSC_MAJOR);
