
/*
 * General Purpose functions for the global management of the
 * Communication Processor Module.
 * Copyright (c) 1997 Dan Malek (dmalek@jlc.net)
 *
 * In addition to the individual control of the communication
 * channels, there are a few functions that globally affect the
 * communication processor.
 *
 * Buffer descriptors must be allocated from the dual ported memory
 * space.  The allocator for that is here.  When the communication
 * process is reset, we reclaim the memory available.  There is
 * currently no deallocator for this memory.
 * The amount of space available is platform dependent.  On the
 * MBX, the EPPC software loads additional microcode into the
 * communication processor, and uses some of the DP ram for this
 * purpose.  Current, the first 512 bytes and the last 256 bytes of
 * memory are used.  Right now I am conservative and only use the
 * memory that can never be used for microcode.  If there are
 * applications that require more DP ram, we can expand the boundaries
 * but then we have to be careful of any downloaded microcode.
 */
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <asm/irq.h>
#include <asm/mpc8xx.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/8xx_immap.h>
#include "commproc.h"

static	uint	dp_alloc_base;	/* Starting offset in DP ram */
static	uint	dp_alloc_top;	/* Max offset + 1 */
static	uint	host_buffer;	/* One page of host buffer */
static	uint	host_end;	/* end + 1 */
cpm8xx_t	*cpmp;		/* Pointer to comm processor space */

/* CPM interrupt vector functions.
*/
struct	cpm_action {
	void	(*handler)(void *);
	void	*dev_id;
};
static	struct	cpm_action cpm_vecs[CPMVEC_NR];
static	void	cpm_interrupt(int irq, void * dev, struct pt_regs * regs);
static	void	cpm_error_interrupt(void *);

void
m8xx_cpm_reset(uint host_page_addr)
{
	volatile immap_t	 *imp;
	volatile cpm8xx_t	*commproc;
	pte_t			*pte;

	imp = (immap_t *)IMAP_ADDR;
	commproc = (cpm8xx_t *)&imp->im_cpm;

#ifdef notdef
	/* We can't do this.  It seems to blow away the microcode
	 * patch that EPPC-Bug loaded for us.  EPPC-Bug uses SCC1 for
	 * Ethernet, SMC1 for the console, and I2C for serial EEPROM.
	 * Our own drivers quickly reset all of these.
	 */

	/* Perform a reset.
	*/
	commproc->cp_cpcr = (CPM_CR_RST | CPM_CR_FLG);

	/* Wait for it.
	*/
	while (commproc->cp_cpcr & CPM_CR_FLG);
#endif

	/* Set SDMA Bus Request priority 5.
	 * On 860T, this also enables FEC priority 6.  I am not sure
	 * this is what we realy want for some applications, but the
	 * manual recommends it.
	 * Bit 25, FAM can also be set to use FEC aggressive mode (860T).
	*/
	imp->im_siu_conf.sc_sdcr = 1;

	/* Reclaim the DP memory for our use.
	*/
	dp_alloc_base = CPM_DATAONLY_BASE;
	dp_alloc_top = dp_alloc_base + CPM_DATAONLY_SIZE;

	/* Set the host page for allocation.
	*/
	host_buffer = host_page_addr;	/* Host virtual page address */
	host_end = host_page_addr + PAGE_SIZE;
	pte = va_to_pte(host_page_addr);
	pte_val(*pte) |= _PAGE_NO_CACHE;
	flush_tlb_page(init_mm.mmap, host_buffer);

	/* Tell everyone where the comm processor resides.
	*/
	cpmp = (cpm8xx_t *)commproc;
}

/* This is called during init_IRQ.  We used to do it above, but this
 * was too early since init_IRQ was not yet called.
 */
void
cpm_interrupt_init(void)
{
	/* Initialize the CPM interrupt controller.
	*/
	((immap_t *)IMAP_ADDR)->im_cpic.cpic_cicr =
	    (CICR_SCD_SCC4 | CICR_SCC_SCC3 | CICR_SCB_SCC2 | CICR_SCA_SCC1) |
		((CPM_INTERRUPT/2) << 13) | CICR_HP_MASK;
	((immap_t *)IMAP_ADDR)->im_cpic.cpic_cimr = 0;

	/* Set our interrupt handler with the core CPU.
	*/
	if (request_8xxirq(CPM_INTERRUPT, cpm_interrupt, 0, "cpm", NULL) != 0)
		panic("Could not allocate CPM IRQ!");

	/* Install our own error handler.
	*/
	cpm_install_handler(CPMVEC_ERROR, cpm_error_interrupt, NULL);
	((immap_t *)IMAP_ADDR)->im_cpic.cpic_cicr |= CICR_IEN;
}

/* CPM interrupt controller interrupt.
*/
static	void
cpm_interrupt(int irq, void * dev, struct pt_regs * regs)
{
	uint	vec;

	/* Get the vector by setting the ACK bit and then reading
	 * the register.
	 */
	((volatile immap_t *)IMAP_ADDR)->im_cpic.cpic_civr = 1;
	vec = ((volatile immap_t *)IMAP_ADDR)->im_cpic.cpic_civr;
	vec >>= 11;

	if (cpm_vecs[vec].handler != 0)
		(*cpm_vecs[vec].handler)(cpm_vecs[vec].dev_id);
	else
		((immap_t *)IMAP_ADDR)->im_cpic.cpic_cimr &= ~(1 << vec);

	/* After servicing the interrupt, we have to remove the status
	 * indicator.
	 */
	((immap_t *)IMAP_ADDR)->im_cpic.cpic_cisr = (1 << vec);
	
}

/* The CPM can generate the error interrupt when there is a race condition
 * between generating and masking interrupts.  All we have to do is ACK it
 * and return.  This is a no-op function so we don't need any special
 * tests in the interrupt handler.
 */
static	void
cpm_error_interrupt(void *dev)
{
}

/* Install a CPM interrupt handler.
*/
void
cpm_install_handler(int vec, void (*handler)(void *), void *dev_id)
{
	if (cpm_vecs[vec].handler != 0)
		printk("CPM interrupt %x replacing %x\n",
			(uint)handler, (uint)cpm_vecs[vec].handler);
	cpm_vecs[vec].handler = handler;
	cpm_vecs[vec].dev_id = dev_id;
	((immap_t *)IMAP_ADDR)->im_cpic.cpic_cimr |= (1 << vec);
}

/* Free a CPM interrupt handler.
*/
void
cpm_free_handler(int vec)
{
	cpm_vecs[vec].handler = NULL;
	cpm_vecs[vec].dev_id = NULL;
	((immap_t *)IMAP_ADDR)->im_cpic.cpic_cimr &= ~(1 << vec);
}

/* Allocate some memory from the dual ported ram.  We may want to
 * enforce alignment restrictions, but right now everyone is a good
 * citizen.
 */
uint
m8xx_cpm_dpalloc(uint size)
{
	uint	retloc;

	if ((dp_alloc_base + size) >= dp_alloc_top)
		return(CPM_DP_NOSPACE);

	retloc = dp_alloc_base;
	dp_alloc_base += size;

	return(retloc);
}

/* We also own one page of host buffer space for the allocation of
 * UART "fifos" and the like.
 */
uint
m8xx_cpm_hostalloc(uint size)
{
	uint	retloc;

	if ((host_buffer + size) >= host_end)
		return(0);

	retloc = host_buffer;
	host_buffer += size;

	return(retloc);
}

/* Set a baud rate generator.  This needs lots of work.  There are
 * four BRGs, any of which can be wired to any channel.
 * The internal baud rate clock is the system clock divided by 16.
 * This assumes the baudrate is 16x oversampled by the uart.
 */
#define BRG_INT_CLK	(((bd_t *)__res)->bi_intfreq * 1000000)
#define BRG_UART_CLK	(BRG_INT_CLK/16)

void
m8xx_cpm_setbrg(uint brg, uint rate)
{
	volatile uint	*bp;

	/* This is good enough to get SMCs running.....
	*/
	bp = (uint *)&cpmp->cp_brgc1;
	bp += brg;
	*bp = ((BRG_UART_CLK / rate) << 1) | CPM_BRG_EN;
}

