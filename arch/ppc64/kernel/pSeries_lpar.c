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
#include <asm/tlb.h>
#include <asm/hvcall.h>

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
	return plpar_hcall_norets(H_PROTECT, flags, ptex, avpn);
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
	unsigned long *lbuf = (unsigned long *)buffer;  /* ToDo: alignment? */
	return plpar_hcall_norets(H_PUT_TERM_CHAR, termno, len, lbuf[0],
				  lbuf[1]);
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
	struct device_node *np;

	pSeries_lpar_mm_init();

	ppc_md.tce_build	 = tce_build_pSeriesLP;
	ppc_md.tce_free_one	 = tce_free_one_pSeriesLP;

#ifdef CONFIG_SMP
	smp_init_pSeries();
#endif

	/* The keyboard is not useful in the LPAR environment.
	 * Leave all the interfaces NULL.
	 */

	/* lookup the first virtual terminal number in case we don't have a
	 * com port. Zero is probably correct in case someone calls udbg
	 * before the init. The property is a pair of numbers.  The first
	 * is the starting termno (the one we use) and the second is the
	 * number of terminals.
	 */
	np = find_path_device("/rtas");
	if (np) {
		u32 *termno = (u32 *)get_property(np, "ibm,termno", 0);
		if (termno)
			vtermno = termno[0];
	}
	ppc_md.udbg_putc = udbg_putcLP;
	ppc_md.udbg_getc = udbg_getcLP;
	ppc_md.udbg_getc_poll = udbg_getc_pollLP;
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
	unsigned long *lbuf = (unsigned long *) buf;
	long ret;

	ret = plpar_hcall_norets(H_PUT_TERM_CHAR, index, count, lbuf[0],
				 lbuf[1]);
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




long pSeries_lpar_hpte_insert(unsigned long hpte_group,
			      unsigned long va, unsigned long prpn,
			      int secondary, unsigned long hpteflags,
			      int bolted, int large)
{
	unsigned long arpn = physRpn_to_absRpn(prpn);
	unsigned long lpar_rc;
	unsigned long flags;
	unsigned long slot;
	HPTE lhpte;
	unsigned long dummy0, dummy1;

	/* Fill in the local HPTE with absolute rpn, avpn and flags */
	lhpte.dw1.dword1      = 0;
	lhpte.dw1.dw1.rpn     = arpn;
	lhpte.dw1.flags.flags = hpteflags;

	lhpte.dw0.dword0      = 0;
	lhpte.dw0.dw0.avpn    = va >> 23;
	lhpte.dw0.dw0.h       = secondary;
	lhpte.dw0.dw0.bolted  = bolted;
	lhpte.dw0.dw0.v       = 1;

	if (large) {
		lhpte.dw0.dw0.l = 1;
		lhpte.dw0.dw0.avpn &= ~0x1UL;
	}

	/* Now fill in the actual HPTE */
	/* Set CEC cookie to 0         */
	/* Zero page = 0               */
	/* I-cache Invalidate = 0      */
	/* I-cache synchronize = 0     */
	/* Exact = 0                   */
	flags = 0;

	/* XXX why is this here? - Anton */
	if (hpteflags & (_PAGE_GUARDED|_PAGE_NO_CACHE))
		lhpte.dw1.flags.flags &= ~_PAGE_COHERENT;

	lpar_rc = plpar_hcall(H_ENTER, flags, hpte_group, lhpte.dw0.dword0,
			      lhpte.dw1.dword1, &slot, &dummy0, &dummy1);

	if (lpar_rc == H_PTEG_Full)
		return -1;

	/*
	 * Since we try and ioremap PHBs we don't own, the pte insert
	 * will fail. However we must catch the failure in hash_page
	 * or we will loop forever, so return -2 in this case.
	 */
	if (lpar_rc != H_Success)
		return -2;

	return slot;
}

static spinlock_t pSeries_lpar_tlbie_lock = SPIN_LOCK_UNLOCKED;

static long pSeries_lpar_hpte_remove(unsigned long hpte_group)
{
	unsigned long slot_offset;
	unsigned long lpar_rc;
	int i;
	unsigned long dummy1, dummy2;

	/* pick a random slot to start at */
	slot_offset = mftb() & 0x7;

	for (i = 0; i < HPTES_PER_GROUP; i++) {

		/* don't remove a bolted entry */
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
				       unsigned long va, int large, int local)
{
	unsigned long lpar_rc;
	unsigned long flags = (newpp & 7) | H_AVPN;
	unsigned long avpn = va >> 23;

	if (large)
		avpn &= ~0x1UL;

	lpar_rc = plpar_pte_protect(flags, slot, (avpn << 7));

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
	unsigned long avpn = va >> 23;
	unsigned long lpar_rc;
	unsigned long dummy1, dummy2;

	if (large)
		avpn &= ~0x1UL;

	lpar_rc = plpar_pte_remove(H_AVPN, slot, (avpn << 7), &dummy1,
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
	unsigned long flags;
	struct ppc64_tlb_batch *batch = &ppc64_tlb_batch[smp_processor_id()];

	spin_lock_irqsave(&pSeries_lpar_tlbie_lock, flags);

	for (i = 0; i < number; i++)
		flush_hash_page(context, batch->addr[i], batch->pte[i], local);

	spin_unlock_irqrestore(&pSeries_lpar_tlbie_lock, flags);
}

void pSeries_lpar_mm_init(void)
{
	ppc_md.hpte_invalidate	= pSeries_lpar_hpte_invalidate;
	ppc_md.hpte_updatepp	= pSeries_lpar_hpte_updatepp;
	ppc_md.hpte_updateboltedpp = pSeries_lpar_hpte_updateboltedpp;
	ppc_md.hpte_insert	= pSeries_lpar_hpte_insert;
	ppc_md.hpte_remove	= pSeries_lpar_hpte_remove;
	ppc_md.flush_hash_range	= pSeries_lpar_flush_hash_range;
}
