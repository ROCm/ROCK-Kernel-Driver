/*
 * pSeries_lpar.c
 * Copyright (C) 2001 Todd Inglett, IBM Corporation
 *
 * pSeries LPAR support.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <asm/processor.h>
#include <asm/mmu.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/machdep.h>
#include <asm/abs_addr.h>
#include <asm/mmu_context.h>
#include <asm/ppcdebug.h>
#include <asm/pci_dma.h>
#include <linux/pci.h>
#include <asm/naca.h>
#include <asm/tlbflush.h>

/* Status return values */
#define H_Success	0
#define H_Busy		1	/* Hardware busy -- retry later */
#define H_Hardware	-1	/* Hardware error */
#define H_Function	-2	/* Function not supported */
#define H_Privilege	-3	/* Caller not privileged */
#define H_Parameter	-4	/* Parameter invalid, out-of-range or conflicting */
#define H_Bad_Mode	-5	/* Illegal msr value */
#define H_PTEG_Full	-6	/* PTEG is full */
#define H_Not_Found	-7	/* PTE was not found" */
#define H_Reserved_DABR	-8	/* DABR address is reserved by the hypervisor on this processor" */

/* Flags */
#define H_LARGE_PAGE		(1UL<<(63-16))
#define H_EXACT		    (1UL<<(63-24))	/* Use exact PTE or return H_PTEG_FULL */
#define H_R_XLATE		(1UL<<(63-25))	/* include a valid logical page num in the pte if the valid bit is set */
#define H_READ_4		(1UL<<(63-26))	/* Return 4 PTEs */
#define H_AVPN			(1UL<<(63-32))	/* An avpn is provided as a sanity test */
#define H_ANDCOND		(1UL<<(63-33))
#define H_ICACHE_INVALIDATE	(1UL<<(63-40))	/* icbi, etc.  (ignored for IO pages) */
#define H_ICACHE_SYNCHRONIZE	(1UL<<(63-41))	/* dcbst, icbi, etc (ignored for IO pages */
#define H_ZERO_PAGE		(1UL<<(63-48))	/* zero the page before mapping (ignored for IO pages) */
#define H_COPY_PAGE		(1UL<<(63-49))
#define H_N			(1UL<<(63-61))
#define H_PP1			(1UL<<(63-62))
#define H_PP2			(1UL<<(63-63))



/* pSeries hypervisor opcodes */
#define H_REMOVE		0x04
#define H_ENTER			0x08
#define H_READ			0x0c
#define H_CLEAR_MOD		0x10
#define H_CLEAR_REF		0x14
#define H_PROTECT		0x18
#define H_GET_TCE		0x1c
#define H_PUT_TCE		0x20
#define H_SET_SPRG0		0x24
#define H_SET_DABR		0x28
#define H_PAGE_INIT		0x2c
#define H_SET_ASR		0x30
#define H_ASR_ON		0x34
#define H_ASR_OFF		0x38
#define H_LOGICAL_CI_LOAD	0x3c
#define H_LOGICAL_CI_STORE	0x40
#define H_LOGICAL_CACHE_LOAD	0x44
#define H_LOGICAL_CACHE_STORE	0x48
#define H_LOGICAL_ICBI		0x4c
#define H_LOGICAL_DCBF		0x50
#define H_GET_TERM_CHAR		0x54
#define H_PUT_TERM_CHAR		0x58
#define H_REAL_TO_LOGICAL	0x5c
#define H_HYPERVISOR_DATA	0x60
#define H_EOI			0x64
#define H_CPPR			0x68
#define H_IPI			0x6c
#define H_IPOLL			0x70
#define H_XIRR			0x74

#define HSC			".long 0x44000022\n"
#define H_ENTER_r3		"li	3, 0x08\n"

/* plpar_hcall() -- Generic call interface using above opcodes
 *
 * The actual call interface is a hypervisor call instruction with
 * the opcode in R3 and input args in R4-R7.
 * Status is returned in R3 with variable output values in R4-R11.
 * Only H_PTE_READ with H_READ_4 uses R6-R11 so we ignore it for now
 * and return only two out args which MUST ALWAYS BE PROVIDED.
 */
long plpar_hcall(unsigned long opcode,
		 unsigned long arg1,
		 unsigned long arg2,
		 unsigned long arg3,
		 unsigned long arg4,
		 unsigned long *out1,
		 unsigned long *out2,
		 unsigned long *out3);

/* Same as plpar_hcall but for those opcodes that return no values
 * other than status.  Slightly more efficient.
 */
long plpar_hcall_norets(unsigned long opcode, ...);


long plpar_pte_enter(unsigned long flags,
		     unsigned long ptex,
		     unsigned long new_pteh, unsigned long new_ptel,
		     unsigned long *old_pteh_ret, unsigned long *old_ptel_ret)
{
	unsigned long dummy, ret;
	ret = plpar_hcall(H_ENTER, flags, ptex, new_pteh, new_ptel,
			   old_pteh_ret, old_ptel_ret, &dummy);
	return(ret);
}

long plpar_pte_remove(unsigned long flags,
		      unsigned long ptex,
		      unsigned long avpn,
		      unsigned long *old_pteh_ret, unsigned long *old_ptel_ret)
{
	unsigned long dummy;
	return plpar_hcall(H_REMOVE, flags, ptex, avpn, 0,
			   old_pteh_ret, old_ptel_ret, &dummy);
}

long plpar_pte_read(unsigned long flags,
		    unsigned long ptex,
		    unsigned long *old_pteh_ret, unsigned long *old_ptel_ret)
{
	unsigned long dummy;
	return plpar_hcall(H_READ, flags, ptex, 0, 0,
			   old_pteh_ret, old_ptel_ret, &dummy);
}

long plpar_pte_protect(unsigned long flags,
		       unsigned long ptex,
		       unsigned long avpn)
{
	return plpar_hcall_norets(H_PROTECT, flags, ptex);
}

long plpar_tce_get(unsigned long liobn,
		   unsigned long ioba,
		   unsigned long *tce_ret)
{
	unsigned long dummy;
	return plpar_hcall(H_GET_TCE, liobn, ioba, 0, 0,
			   tce_ret, &dummy, &dummy);
}


long plpar_tce_put(unsigned long liobn,
		   unsigned long ioba,
		   unsigned long tceval)
{
	return plpar_hcall_norets(H_PUT_TCE, liobn, ioba, tceval);
}

long plpar_get_term_char(unsigned long termno,
			 unsigned long *len_ret,
			 char *buf_ret)
{
	unsigned long *lbuf = (unsigned long *)buf_ret;  /* ToDo: alignment? */
	return plpar_hcall(H_GET_TERM_CHAR, termno, 0, 0, 0,
			   len_ret, lbuf+0, lbuf+1);
}

long plpar_put_term_char(unsigned long termno,
			 unsigned long len,
			 const char *buffer)
{
	unsigned long dummy;
	unsigned long *lbuf = (unsigned long *)buffer;  /* ToDo: alignment? */
	return plpar_hcall(H_PUT_TERM_CHAR, termno, len,
			   lbuf[0], lbuf[1], &dummy, &dummy, &dummy);
}

long plpar_eoi(unsigned long xirr)
{
	return plpar_hcall_norets(H_EOI, xirr);
}

long plpar_cppr(unsigned long cppr)
{
	return plpar_hcall_norets(H_CPPR, cppr);
}

long plpar_ipi(unsigned long servernum,
	       unsigned long mfrr)
{
	return plpar_hcall_norets(H_IPI, servernum, mfrr);
}

long plpar_xirr(unsigned long *xirr_ret)
{
	unsigned long dummy;
	return plpar_hcall(H_XIRR, 0, 0, 0, 0,
			   xirr_ret, &dummy, &dummy);
}

static void tce_build_pSeriesLP(struct TceTable *tbl, long tcenum, 
				unsigned long uaddr, int direction )
{
	u64 set_tce_rc;
	union Tce tce;
	
	PPCDBG(PPCDBG_TCE, "build_tce: uaddr = 0x%lx\n", uaddr);
	PPCDBG(PPCDBG_TCE, "\ttcenum = 0x%lx, tbl = 0x%lx, index=%lx\n", 
	       tcenum, tbl, tbl->index);

	tce.wholeTce = 0;
	tce.tceBits.rpn = (virt_to_absolute(uaddr)) >> PAGE_SHIFT;

	tce.tceBits.readWrite = 1;
	if ( direction != PCI_DMA_TODEVICE ) tce.tceBits.pciWrite = 1;

	set_tce_rc = plpar_tce_put((u64)tbl->index, 
				 (u64)tcenum << 12, 
				 tce.wholeTce );

	if(set_tce_rc) {
		printk("tce_build_pSeriesLP: plpar_tce_put failed. rc=%ld\n", set_tce_rc);
		printk("\tindex   = 0x%lx\n", (u64)tbl->index);
		printk("\ttcenum  = 0x%lx\n", (u64)tcenum);
		printk("\ttce val = 0x%lx\n", tce.wholeTce );
	}
}

static void tce_free_one_pSeriesLP(struct TceTable *tbl, long tcenum)
{
	u64 set_tce_rc;
	union Tce tce;

	tce.wholeTce = 0;
	set_tce_rc = plpar_tce_put((u64)tbl->index, 
				 (u64)tcenum << 12,
				 tce.wholeTce );
	if ( set_tce_rc ) {
		printk("tce_free_one_pSeriesLP: plpar_tce_put failed\n");
		printk("\trc      = %ld\n", set_tce_rc);
		printk("\tindex   = 0x%lx\n", (u64)tbl->index);
		printk("\ttcenum  = 0x%lx\n", (u64)tcenum);
		printk("\ttce val = 0x%lx\n", tce.wholeTce );
	}

}

/* PowerPC Interrupts for lpar. */
/* NOTE: this typedef is duplicated (for now) from xics.c! */
typedef struct {
	int (*xirr_info_get)(int cpu);
	void (*xirr_info_set)(int cpu, int val);
	void (*cppr_info)(int cpu, u8 val);
	void (*qirr_info)(int cpu, u8 val);
} xics_ops;
static int pSeriesLP_xirr_info_get(int n_cpu)
{
	unsigned long lpar_rc;
	unsigned long return_value; 

	lpar_rc = plpar_xirr(&return_value);
	if (lpar_rc != H_Success) {
		panic(" bad return code xirr - rc = %lx \n", lpar_rc); 
	}
	return ((int)(return_value));
}

static void pSeriesLP_xirr_info_set(int n_cpu, int value)
{
	unsigned long lpar_rc;
	unsigned long val64 = value & 0xffffffff;

	lpar_rc = plpar_eoi(val64);
	if (lpar_rc != H_Success) {
		panic(" bad return code EOI - rc = %ld, value=%lx \n", lpar_rc, val64); 
	}
}

static void pSeriesLP_cppr_info(int n_cpu, u8 value)
{
	unsigned long lpar_rc;

	lpar_rc = plpar_cppr(value);
	if (lpar_rc != H_Success) {
		panic(" bad return code cppr - rc = %lx \n", lpar_rc); 
	}
}

static void pSeriesLP_qirr_info(int n_cpu , u8 value)
{
	unsigned long lpar_rc;

	lpar_rc = plpar_ipi(get_hard_smp_processor_id(n_cpu),value);
	if (lpar_rc != H_Success) {
		panic(" bad return code qirr -ipi  - rc = %lx \n", lpar_rc); 
	}
}

xics_ops pSeriesLP_ops = {
	pSeriesLP_xirr_info_get,
	pSeriesLP_xirr_info_set,
	pSeriesLP_cppr_info,
	pSeriesLP_qirr_info
};
/* end TAI-LPAR */


int vtermno;	/* virtual terminal# for udbg  */

static void udbg_putcLP(unsigned char c)
{
	char buf[16];
	unsigned long rc;

	if (c == '\n')
		udbg_putcLP('\r');

	buf[0] = c;
	do {
		rc = plpar_put_term_char(vtermno, 1, buf);
	} while(rc == H_Busy);
}

/* Buffered chars getc */
static long inbuflen;
static long inbuf[2];	/* must be 2 longs */

static int udbg_getc_pollLP(void)
{
	/* The interface is tricky because it may return up to 16 chars.
	 * We save them statically for future calls to udbg_getc().
	 */
	char ch, *buf = (char *)inbuf;
	int i;
	long rc;
	if (inbuflen == 0) {
		/* get some more chars. */
		inbuflen = 0;
		rc = plpar_get_term_char(vtermno, &inbuflen, buf);
		if (rc != H_Success)
			inbuflen = 0;	/* otherwise inbuflen is garbage */
	}
	if (inbuflen <= 0 || inbuflen > 16) {
		/* Catch error case as well as other oddities (corruption) */
		inbuflen = 0;
		return -1;
	}
	ch = buf[0];
	for (i = 1; i < inbuflen; i++)	/* shuffle them down. */
		buf[i-1] = buf[i];
	inbuflen--;
	return ch;
}

static unsigned char udbg_getcLP(void)
{
	int ch;
	for (;;) {
		ch = udbg_getc_pollLP();
		if (ch == -1) {
			/* This shouldn't be needed...but... */
			volatile unsigned long delay;
			for (delay=0; delay < 2000000; delay++)
				;
		} else {
			return ch;
		}
	}
}

void pSeries_lpar_mm_init(void);

/* This is called early in setup.c.
 * Use it to setup page table ppc_md stuff as well as udbg.
 */
void pSeriesLP_init_early(void)
{
	pSeries_lpar_mm_init();

	ppc_md.tce_build	 = tce_build_pSeriesLP;
	ppc_md.tce_free_one	 = tce_free_one_pSeriesLP;

#ifdef CONFIG_SMP
	smp_init_pSeries();
#endif
	pSeries_pcibios_init_early();

	/* The keyboard is not useful in the LPAR environment.
	 * Leave all the interfaces NULL.
	 */

	if (naca->serialPortAddr) {
		void *comport = (void *)__ioremap(naca->serialPortAddr, 16, _PAGE_NO_CACHE);
		udbg_init_uart(comport);
		ppc_md.udbg_putc = udbg_putc;
		ppc_md.udbg_getc = udbg_getc;
		ppc_md.udbg_getc_poll = udbg_getc_poll;
	} else {
		/* lookup the first virtual terminal number in case we don't have a com port.
		 * Zero is probably correct in case someone calls udbg before the init.
		 * The property is a pair of numbers.  The first is the starting termno (the
		 * one we use) and the second is the number of terminals.
		 */
		u32 *termno;
		struct device_node *np = find_path_device("/rtas");
		if (np) {
			termno = (u32 *)get_property(np, "ibm,termno", 0);
			if (termno)
				vtermno = termno[0];
		}
		ppc_md.udbg_putc = udbg_putcLP;
		ppc_md.udbg_getc = udbg_getcLP;
		ppc_md.udbg_getc_poll = udbg_getc_pollLP;
	}
}

int hvc_get_chars(int index, char *buf, int count)
{
	unsigned long got;

	if (plpar_hcall(H_GET_TERM_CHAR, index, 0, 0, 0, &got,
		(unsigned long *)buf, (unsigned long *)buf+1) == H_Success) {
		/*
		 * Work around a HV bug where it gives us a null
		 * after every \r.  -- paulus
		 */
		if (got > 0) {
			int i;
			for (i = 1; i < got; ++i) {
				if (buf[i] == 0 && buf[i-1] == '\r') {
					--got;
					if (i < got)
						memmove(&buf[i], &buf[i+1],
							got - i);
				}
			}
		}
		return got;
	}
	return 0;
}

int hvc_put_chars(int index, const char *buf, int count)
{
	unsigned long dummy;
	unsigned long *lbuf = (unsigned long *) buf;
	long ret;

	ret = plpar_hcall(H_PUT_TERM_CHAR, index, count, lbuf[0], lbuf[1],
			  &dummy, &dummy, &dummy);
	if (ret == H_Success)
		return count;
	if (ret == H_Busy)
		return 0;
	return -1;
}

int hvc_count(int *start_termno)
{
	u32 *termno;
	struct device_node *dn;

	if ((dn = find_path_device("/rtas")) != NULL) {
		if ((termno = (u32 *)get_property(dn, "ibm,termno", 0)) != NULL) {
			if (start_termno)
				*start_termno = termno[0];
			return termno[1];
		}
	}
	return 0;
}






/*
 * Create a pte - LPAR .  Used during initialization only.
 * We assume the PTE will fit in the primary PTEG.
 */
void pSeries_lpar_make_pte(HPTE *htab, unsigned long va, unsigned long pa,
			   int mode, unsigned long hash_mask, int large)
{
	HPTE local_hpte;
	unsigned long hash, slot, flags, lpar_rc, vpn;
	unsigned long dummy1, dummy2;

	if (large)
		vpn = va >> LARGE_PAGE_SHIFT;
	else
		vpn = va >> PAGE_SHIFT;

	hash = hpt_hash(vpn, large);

	slot = ((hash & hash_mask)*HPTES_PER_GROUP);

	local_hpte.dw1.dword1 = pa | mode;
	local_hpte.dw0.dword0 = 0;
	local_hpte.dw0.dw0.avpn = va >> 23;
	local_hpte.dw0.dw0.bolted = 1;		/* bolted */
	if (large) {
		local_hpte.dw0.dw0.l = 1;	/* large page */
		local_hpte.dw0.dw0.avpn &= ~0x1UL;
	}
	local_hpte.dw0.dw0.v = 1;

	/* Set CEC cookie to 0                   */
	/* Zero page = 0                         */
	/* I-cache Invalidate = 0                */
	/* I-cache synchronize = 0               */
	/* Exact = 0 - modify any entry in group */
	flags = 0;

	lpar_rc =  plpar_pte_enter(flags, slot, local_hpte.dw0.dword0,
				   local_hpte.dw1.dword1, &dummy1, &dummy2);

	if (lpar_rc == H_PTEG_Full) {
		while(1)
			;
	}

	/*
	 * NOTE: we explicitly do not check return status here because it is
	 * "normal" for early boot code to map io regions for which a partition
	 * has no access.  However, we will die if we actually fault on these
	 * "permission denied" pages.
	 */
}

static long pSeries_lpar_insert_hpte(unsigned long hpte_group,
				     unsigned long vpn, unsigned long prpn,
				     int secondary, unsigned long hpteflags,
				     int bolted, int large)
{
	unsigned long avpn = vpn >> 11;
	unsigned long arpn = physRpn_to_absRpn(prpn);
	unsigned long lpar_rc;
	unsigned long flags;
	unsigned long slot;
	HPTE lhpte;

	/* Fill in the local HPTE with absolute rpn, avpn and flags */
	lhpte.dw1.dword1      = 0;
	lhpte.dw1.dw1.rpn     = arpn;
	lhpte.dw1.flags.flags = hpteflags;

	lhpte.dw0.dword0      = 0;
	lhpte.dw0.dw0.avpn    = avpn;
	lhpte.dw0.dw0.h       = secondary;
	lhpte.dw0.dw0.bolted  = bolted;
	lhpte.dw0.dw0.v       = 1;

	if (large)
		lhpte.dw0.dw0.l = 1;

	/* Now fill in the actual HPTE */
	/* Set CEC cookie to 0         */
	/* Large page = 0              */
	/* Zero page = 0               */
	/* I-cache Invalidate = 0      */
	/* I-cache synchronize = 0     */
	/* Exact = 0                   */
	flags = 0;

	/* XXX why is this here? - Anton */
	if (hpteflags & (_PAGE_GUARDED|_PAGE_NO_CACHE))
		lhpte.dw1.flags.flags &= ~_PAGE_COHERENT;

	__asm__ __volatile__ (
		H_ENTER_r3
		"mr    4, %2\n"
                "mr    5, %3\n"
                "mr    6, %4\n"
                "mr    7, %5\n"
                HSC    
                "mr    %0, 3\n"
                "mr    %1, 4\n"
		: "=r" (lpar_rc), "=r" (slot)
		: "r" (flags), "r" (hpte_group), "r" (lhpte.dw0.dword0),
		"r" (lhpte.dw1.dword1)
		: "r3", "r4", "r5", "r6", "r7", "cc");

	if (lpar_rc == H_PTEG_Full)
		return -1;

	if (lpar_rc != H_Success)
		panic("Bad return code from pte enter rc = %lx\n", lpar_rc);

	return slot;
}

static spinlock_t pSeries_lpar_tlbie_lock = SPIN_LOCK_UNLOCKED;

static long pSeries_lpar_remove_hpte(unsigned long hpte_group)
{
	unsigned long slot_offset;
	unsigned long lpar_rc;
	int i;
	unsigned long dummy1, dummy2;

	/* pick a random slot to start at */
	slot_offset = mftb() & 0x7;

	for (i = 0; i < HPTES_PER_GROUP; i++) {

		/* dont remove a bolted entry */
		lpar_rc = plpar_pte_remove(H_ANDCOND, hpte_group + slot_offset,
					   (0x1UL << 4), &dummy1, &dummy2);

		if (lpar_rc == H_Success)
			return i;

		if (lpar_rc != H_Not_Found)
			panic("Bad return code from pte remove rc = %lx\n",
			      lpar_rc);

		slot_offset++;
		slot_offset &= 0x7;
	}

	return -1;
}

/*
 * NOTE: for updatepp ops we are fortunate that the linux "newpp" bits and
 * the low 3 bits of flags happen to line up.  So no transform is needed.
 * We can probably optimize here and assume the high bits of newpp are
 * already zero.  For now I am paranoid.
 */
static long pSeries_lpar_hpte_updatepp(unsigned long slot, unsigned long newpp,
				       unsigned long va, int large)
{
	unsigned long lpar_rc;
	unsigned long flags;
	flags = (newpp & 3) | H_AVPN;
	unsigned long vpn = va >> PAGE_SHIFT;

	udbg_printf("updatepp\n");

	lpar_rc = plpar_pte_protect(flags, slot, (vpn >> 4) & ~0x7fUL);

	if (lpar_rc == H_Not_Found) {
		udbg_printf("updatepp missed\n");
		return -1;
	}

	if (lpar_rc != H_Success)
		panic("bad return code from pte protect rc = %lx\n", lpar_rc);

	return 0;
}

static unsigned long pSeries_lpar_hpte_getword0(unsigned long slot)
{
	unsigned long dword0;
	unsigned long lpar_rc;
	unsigned long dummy_word1;
	unsigned long flags;

	/* Read 1 pte at a time                        */
	/* Do not need RPN to logical page translation */
	/* No cross CEC PFT access                     */
	flags = 0;
	
	lpar_rc = plpar_pte_read(flags, slot, &dword0, &dummy_word1);

	if (lpar_rc != H_Success)
		panic("Error on pte read in get_hpte0 rc = %lx\n", lpar_rc);

	return dword0;
}

static long pSeries_lpar_hpte_find(unsigned long vpn)
{
	unsigned long hash;
	unsigned long i, j;
	long slot;
	union {
		unsigned long dword0;
		Hpte_dword0 dw0;
	} hpte_dw0;
	Hpte_dword0 dw0;

	hash = hpt_hash(vpn, 0);

	for (j = 0; j < 2; j++) {
		slot = (hash & htab_data.htab_hash_mask) * HPTES_PER_GROUP;
		for (i = 0; i < HPTES_PER_GROUP; i++) {
			hpte_dw0.dword0 = pSeries_lpar_hpte_getword0(slot);
			dw0 = hpte_dw0.dw0;

			if ((dw0.avpn == (vpn >> 11)) && dw0.v &&
			    (dw0.h == j)) {
				/* HPTE matches */
				if (j)
					slot = -slot;
				return slot;
			}
			++slot;
		}
		hash = ~hash;
	}

	return -1;
} 

static void pSeries_lpar_hpte_updateboltedpp(unsigned long newpp,
					     unsigned long ea)
{
	unsigned long lpar_rc;
	unsigned long vsid, va, vpn, flags;
	long slot;

	vsid = get_kernel_vsid(ea);
	va = (vsid << 28) | (ea & 0x0fffffff);
	vpn = va >> PAGE_SHIFT;

	slot = pSeries_lpar_hpte_find(vpn);
	if (slot == -1)
		panic("updateboltedpp: Could not find page to bolt\n");

	flags = newpp & 3;
	lpar_rc = plpar_pte_protect(flags, slot, 0);

	if (lpar_rc != H_Success)
		panic("Bad return code from pte bolted protect rc = %lx\n",
		      lpar_rc); 
}

static void pSeries_lpar_hpte_invalidate(unsigned long slot, unsigned long va,
					 int large, int local)
{
	unsigned long vpn, avpn;
	unsigned long lpar_rc;
	unsigned long dummy1, dummy2;

	if (large)
		vpn = va >> LARGE_PAGE_SHIFT;
	else
		vpn = va >> PAGE_SHIFT;

	avpn = vpn >> 11;

	lpar_rc = plpar_pte_remove(H_AVPN, slot, (vpn >> 4) & ~0x7fUL, &dummy1,
				   &dummy2);

	if (lpar_rc == H_Not_Found) {
		udbg_printf("invalidate missed\n");
		return;
	}

	if (lpar_rc != H_Success)
		panic("Bad return code from invalidate rc = %lx\n", lpar_rc);
}

/*
 * Take a spinlock around flushes to avoid bouncing the hypervisor tlbie
 * lock.
 */
void pSeries_lpar_flush_hash_range(unsigned long context, unsigned long number,
				   int local)
{
	int i;
	struct tlb_batch_data *ptes =
		&tlb_batch_array[smp_processor_id()][0];
	unsigned long flags;

	spin_lock_irqsave(&pSeries_lpar_tlbie_lock, flags);
	for (i = 0; i < number; i++) {
		flush_hash_page(context, ptes->addr, ptes->pte, local);
		ptes++;
	}
	spin_unlock_irqrestore(&pSeries_lpar_tlbie_lock, flags);
}

void pSeries_lpar_mm_init(void)
{
	ppc_md.hpte_invalidate  = pSeries_lpar_hpte_invalidate;
	ppc_md.hpte_updatepp    = pSeries_lpar_hpte_updatepp;
	ppc_md.hpte_updateboltedpp = pSeries_lpar_hpte_updateboltedpp;
	ppc_md.insert_hpte      = pSeries_lpar_insert_hpte;
	ppc_md.remove_hpte      = pSeries_lpar_remove_hpte;
	ppc_md.make_pte         = pSeries_lpar_make_pte;
	ppc_md.flush_hash_range	= pSeries_lpar_flush_hash_range;
}
