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
#include <linux/module.h>
#include <asm/irq.h>
#include <asm/mpc8xx.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/8xx_immap.h>
#include <asm/commproc.h>
#include <asm/io.h>
#include <asm/rheap.h>

extern int get_pteptr(struct mm_struct *mm, unsigned long addr, pte_t **ptep);

static void m8xx_cpm_dpinit(void);
static	uint	host_buffer;	/* One page of host buffer */
static	uint	host_end;	/* end + 1 */
cpm8xx_t	*cpmp;		/* Pointer to comm processor space */

/* CPM interrupt vector functions.
*/
struct	cpm_action {
	void	(*handler)(void *, struct pt_regs * regs);
	void	*dev_id;
};
static	struct	cpm_action cpm_vecs[CPMVEC_NR];
static	void	cpm_interrupt(int irq, void * dev, struct pt_regs * regs);
static	void	cpm_error_interrupt(void *, struct pt_regs * regs);
static	void	alloc_host_memory(void);

#if 1
void
m8xx_cpm_reset(void)
{
	volatile immap_t	 *imp;
	volatile cpm8xx_t	*commproc;

	imp = (immap_t *)IMAP_ADDR;
	commproc = (cpm8xx_t *)&imp->im_cpm;

#ifdef CONFIG_UCODE_PATCH
	/* Perform a reset.
	*/
	commproc->cp_cpcr = (CPM_CR_RST | CPM_CR_FLG);

	/* Wait for it.
	*/
	while (commproc->cp_cpcr & CPM_CR_FLG);

	cpm_load_patch(imp);
#endif

	/* Set SDMA Bus Request priority 5.
	 * On 860T, this also enables FEC priority 6.  I am not sure
	 * this is what we realy want for some applications, but the
	 * manual recommends it.
	 * Bit 25, FAM can also be set to use FEC aggressive mode (860T).
	 */
	imp->im_siu_conf.sc_sdcr = 1;

	/* Reclaim the DP memory for our use. */
	m8xx_cpm_dpinit();

	/* Tell everyone where the comm processor resides.
	*/
	cpmp = (cpm8xx_t *)commproc;
}

/* We used to do this earlier, but have to postpone as long as possible
 * to ensure the kernel VM is now running.
 */
static void
alloc_host_memory()
{
	uint	physaddr;

	/* Set the host page for allocation.
	*/
	host_buffer = (uint)consistent_alloc(GFP_KERNEL, PAGE_SIZE, &physaddr);
	host_end = host_buffer + PAGE_SIZE;
}
#else
void
m8xx_cpm_reset(uint host_page_addr)
{
	volatile immap_t	 *imp;
	volatile cpm8xx_t	*commproc;
	pte_t			*pte;

	imp = (immap_t *)IMAP_ADDR;
	commproc = (cpm8xx_t *)&imp->im_cpm;

#ifdef CONFIG_UCODE_PATCH
	/* Perform a reset.
	*/
	commproc->cp_cpcr = (CPM_CR_RST | CPM_CR_FLG);

	/* Wait for it.
	*/
	while (commproc->cp_cpcr & CPM_CR_FLG);

	cpm_load_patch(imp);
#endif

	/* Set SDMA Bus Request priority 5.
	 * On 860T, this also enables FEC priority 6.  I am not sure
	 * this is what we realy want for some applications, but the
	 * manual recommends it.
	 * Bit 25, FAM can also be set to use FEC aggressive mode (860T).
	*/
	imp->im_siu_conf.sc_sdcr = 1;

	/* Reclaim the DP memory for our use. */
	m8xx_cpm_dpinit();

	/* Set the host page for allocation.
	*/
	host_buffer = host_page_addr;	/* Host virtual page address */
	host_end = host_page_addr + PAGE_SIZE;

	/* We need to get this page early, so I have to do it the
	 * hard way.
	 */
	if (get_pteptr(&init_mm, host_page_addr, &pte)) {
		pte_val(*pte) |= _PAGE_NO_CACHE;
		flush_tlb_page(init_mm.mmap, host_buffer);
	}
	else {
		panic("Huh?  No CPM host page?");
	}

	/* Tell everyone where the comm processor resides.
	*/
	cpmp = (cpm8xx_t *)commproc;
}
#endif

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
		(*cpm_vecs[vec].handler)(cpm_vecs[vec].dev_id, regs);
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
cpm_error_interrupt(void *dev, struct pt_regs *regs)
{
}

/* Install a CPM interrupt handler.
*/
void
cpm_install_handler(int vec, void (*handler)(void *, struct pt_regs *regs),
		    void *dev_id)
{

	/* If null handler, assume we are trying to free the IRQ.
	*/
	if (!handler) {
		cpm_free_handler(vec);
		return;
	}

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

/* We also own one page of host buffer space for the allocation of
 * UART "fifos" and the like.
 */
uint
m8xx_cpm_hostalloc(uint size)
{
	uint	retloc;

#if 1
	if (host_buffer == 0)
		alloc_host_memory();
#endif

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
#define BRG_INT_CLK		(((bd_t *)__res)->bi_intfreq)
#define BRG_UART_CLK		(BRG_INT_CLK/16)
#define BRG_UART_CLK_DIV16	(BRG_UART_CLK/16)

void
cpm_setbrg(uint brg, uint rate)
{
	volatile uint	*bp;

	/* This is good enough to get SMCs running.....
	*/
	bp = (uint *)&cpmp->cp_brgc1;
	bp += brg;
	/* The BRG has a 12-bit counter.  For really slow baud rates (or
	 * really fast processors), we may have to further divide by 16.
	 */
	if (((BRG_UART_CLK / rate) - 1) < 4096)
		*bp = (((BRG_UART_CLK / rate) - 1) << 1) | CPM_BRG_EN;
	else
		*bp = (((BRG_UART_CLK_DIV16 / rate) - 1) << 1) |
						CPM_BRG_EN | CPM_BRG_DIV16;
}

/*
 * dpalloc / dpfree bits.
 */
static spinlock_t cpm_dpmem_lock;
/*
 * 16 blocks should be enough to satisfy all requests
 * until the memory subsystem goes up...
 */
static rh_block_t cpm_boot_dpmem_rh_block[16];
static rh_info_t cpm_dpmem_info;

#define CPM_DPMEM_ALIGNMENT	8

void m8xx_cpm_dpinit(void)
{
	cpm8xx_t *cp = &((immap_t *)IMAP_ADDR)->im_cpm;

	spin_lock_init(&cpm_dpmem_lock);

	/* Initialize the info header */
	rh_init(&cpm_dpmem_info, CPM_DPMEM_ALIGNMENT,
			sizeof(cpm_boot_dpmem_rh_block) /
			sizeof(cpm_boot_dpmem_rh_block[0]),
			cpm_boot_dpmem_rh_block);

	/*
	 * Attach the usable dpmem area.
	 * XXX: This is actually crap.  CPM_DATAONLY_BASE and
	 * CPM_DATAONLY_SIZE are a subset of the available dparm.  It varies
	 * with the processor and the microcode patches applied / activated.
	 * But the following should be at least safe.
	 */
	rh_attach_region(&cpm_dpmem_info, (void *)CPM_DATAONLY_BASE, CPM_DATAONLY_SIZE);
}

/*
 * Allocate the requested size worth of DP memory.
 * This function used to return an index into the DPRAM area.
 * Now it returns the actuall physical address of that area.
 * use m8xx_cpm_dpram_offset() to get the index
 */
uint cpm_dpalloc(uint size, uint align)
{
	void *start;
	unsigned long flags;

	spin_lock_irqsave(&cpm_dpmem_lock, flags);
	cpm_dpmem_info.alignment = align;
	start = rh_alloc(&cpm_dpmem_info, size, "commproc");
	spin_unlock_irqrestore(&cpm_dpmem_lock, flags);

	return (uint)start;
}
EXPORT_SYMBOL(cpm_dpalloc);

int cpm_dpfree(uint offset)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&cpm_dpmem_lock, flags);
	ret = rh_free(&cpm_dpmem_info, (void *)offset);
	spin_unlock_irqrestore(&cpm_dpmem_lock, flags);

	return ret;
}
EXPORT_SYMBOL(cpm_dpfree);

uint cpm_dpalloc_fixed(uint offset, uint size, uint align)
{
	void *start;
	unsigned long flags;

	spin_lock_irqsave(&cpm_dpmem_lock, flags);
	cpm_dpmem_info.alignment = align;
	start = rh_alloc_fixed(&cpm_dpmem_info, (void *)offset, size, "commproc");
	spin_unlock_irqrestore(&cpm_dpmem_lock, flags);

	return (uint)start;
}
EXPORT_SYMBOL(cpm_dpalloc_fixed);

void cpm_dpdump(void)
{
	rh_dump(&cpm_dpmem_info);
}
EXPORT_SYMBOL(cpm_dpdump);

void *cpm_dpram_addr(uint offset)
{
	return ((immap_t *)IMAP_ADDR)->im_cpm.cp_dpmem + offset;
}
EXPORT_SYMBOL(cpm_dpram_addr);
