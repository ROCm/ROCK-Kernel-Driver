

/* Minimal serial functions needed to send messages out the serial
 * port on SMC1.
 */
#include <linux/types.h>
#include "asm/mpc8260.h"
#include "asm/cpm_8260.h"

uint	no_print;
extern char	*params[];
extern int	nparams;
static		u_char	cons_hold[128], *sgptr;
static		int	cons_hold_cnt;

void
serial_init(bd_t *bd)
{
	volatile smc_t		*sp;
	volatile smc_uart_t	*up;
	volatile cbd_t	*tbdf, *rbdf;
	volatile immap_t	*ip;
	volatile iop8260_t	*io;
	volatile cpm8260_t	*cp;
	uint	dpaddr, memaddr;

	ip = (immap_t *)IMAP_ADDR;

	sp = (smc_t*)&(ip->im_smc[0]);
	*(ushort *)(&ip->im_dprambase[PROFF_SMC1_BASE]) = PROFF_SMC1;
	up = (smc_uart_t *)&ip->im_dprambase[PROFF_SMC1];

	cp = &ip->im_cpm;
	io = &ip->im_ioport;

	/* Disable transmitter/receiver.
	*/
	sp->smc_smcmr &= ~(SMCMR_REN | SMCMR_TEN);

	/* Use Port D for SMC1 instead of other functions.
	*/
	io->iop_ppard |= 0x00c00000;
	io->iop_pdird |= 0x00400000;
	io->iop_pdird &= ~0x00800000;
	io->iop_psord &= ~0x00c00000;

	/* Allocate space for two buffer descriptors in the DP ram.
	 * For now, this address seems OK, but it may have to
	 * change with newer versions of the firmware.
	 */
	dpaddr = 0x0800;

	/* Grab a few bytes from the top of memory.
	 */
#if 1
	memaddr = (bd->bi_memsize - 256) & ~15;
#else
	memaddr = 0x0f002c00;
#endif

	/* Set the physical address of the host memory buffers in
	 * the buffer descriptors.
	 */
	rbdf = (cbd_t *)&ip->im_dprambase[dpaddr];
	rbdf->cbd_bufaddr = memaddr;
	rbdf->cbd_sc = 0;
	tbdf = rbdf + 1;
	tbdf->cbd_bufaddr = memaddr+128;
	tbdf->cbd_sc = 0;

	/* Set up the uart parameters in the parameter ram.
	*/
	up->smc_rbase = dpaddr;
	up->smc_tbase = dpaddr+sizeof(cbd_t);
	up->smc_rfcr = CPMFCR_EB;
	up->smc_tfcr = CPMFCR_EB;
	up->smc_brklen = 0;
	up->smc_brkec = 0;
	up->smc_brkcr = 0;

	/* Set UART mode, 8 bit, no parity, one stop.
	 * Enable receive and transmit.
	 */
	sp->smc_smcmr = smcr_mk_clen(9) |  SMCMR_SM_UART;

	/* Mask all interrupts and remove anything pending.
	*/
	sp->smc_smcm = 0;
	sp->smc_smce = 0xff;

	/* Set up the baud rate generator.
	 */
	ip->im_clkrst.car_sccr = 0;	/* DIV 4 BRG */
	ip->im_cpmux.cmx_smr = 0;
	ip->im_brgc1 =
		((((bd->bi_brgfreq * 1000000)/16) / bd->bi_baudrate) << 1) |
								CPM_BRG_EN;

	/* Make the first buffer the only buffer.
	*/
	tbdf->cbd_sc |= BD_SC_WRAP;
	rbdf->cbd_sc |= BD_SC_EMPTY | BD_SC_WRAP;

#if 0
	/* Single character receive.
	*/
	up->smc_mrblr = 1;
	up->smc_maxidl = 0;
#else
	up->smc_mrblr = 128;
	up->smc_maxidl = 8;
#endif

	/* Initialize Tx/Rx parameters.
	*/
	cp->cp_cpcr = mk_cr_cmd(CPM_CR_SMC1_PAGE, CPM_CR_SMC1_SBLOCK, 0, CPM_CR_INIT_TRX) | CPM_CR_FLG;
	while (cp->cp_cpcr & CPM_CR_FLG);

	/* Enable transmitter/receiver.
	*/
	sp->smc_smcmr |= SMCMR_REN | SMCMR_TEN;
}

void
serial_putchar(const char c)
{
	volatile cbd_t		*tbdf;
	volatile char		*buf;
	volatile smc_uart_t	*up;
	volatile immap_t	*ip;
	extern bd_t		*board_info;

	ip = (immap_t *)IMAP_ADDR;
	up = (smc_uart_t *)&(ip->im_dprambase[PROFF_SMC1]);
	tbdf = (cbd_t *)&ip->im_dprambase[up->smc_tbase];

	/* Wait for last character to go.
	*/
	buf = (char *)tbdf->cbd_bufaddr;
	while (tbdf->cbd_sc & BD_SC_READY);

	*buf = c;
	tbdf->cbd_datlen = 1;
	tbdf->cbd_sc |= BD_SC_READY;
}

char
serial_getc()
{
	char	c;

	if (cons_hold_cnt <= 0) {
		cons_hold_cnt = serial_readbuf(cons_hold);
		sgptr = cons_hold;
	}
	c = *sgptr++;
	cons_hold_cnt--;

	return(c);
}

int
serial_readbuf(u_char *cbuf)
{
	volatile cbd_t		*rbdf;
	volatile char		*buf;
	volatile smc_uart_t	*up;
	volatile immap_t	*ip;
	int	i, nc;

	ip = (immap_t *)IMAP_ADDR;

	up = (smc_uart_t *)&(ip->im_dprambase[PROFF_SMC1]);
	rbdf = (cbd_t *)&ip->im_dprambase[up->smc_rbase];

	/* Wait for character to show up.
	*/
	buf = (char *)rbdf->cbd_bufaddr;
	while (rbdf->cbd_sc & BD_SC_EMPTY);
	nc = rbdf->cbd_datlen;
	for (i=0; i<nc; i++)
		*cbuf++ = *buf++;
	rbdf->cbd_sc |= BD_SC_EMPTY;

	return(nc);
}

int
serial_tstc()
{
	volatile cbd_t		*rbdf;
	volatile smc_uart_t	*up;
	volatile immap_t	*ip;

	ip = (immap_t *)IMAP_ADDR;
	up = (smc_uart_t *)&(ip->im_dprambase[PROFF_SMC1]);
	rbdf = (cbd_t *)&ip->im_dprambase[up->smc_rbase];

	return(!(rbdf->cbd_sc & BD_SC_EMPTY));
}

