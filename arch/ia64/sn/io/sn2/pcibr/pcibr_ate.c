/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001-2003 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <asm/sn/sgi.h>
#include <asm/sn/pci/pciio.h>
#include <asm/sn/pci/pcibr.h>
#include <asm/sn/pci/pcibr_private.h>
#include <asm/sn/pci/pci_defs.h>

/*
 * functions
 */
int		pcibr_ate_alloc(pcibr_soft_t, int, struct resource *);
void		pcibr_ate_free(pcibr_soft_t, int, int, struct resource *);
bridge_ate_t	pcibr_flags_to_ate(pcibr_soft_t, unsigned);
bridge_ate_p	pcibr_ate_addr(pcibr_soft_t, int);
void		ate_write(pcibr_soft_t, int, int, bridge_ate_t);

int pcibr_invalidate_ate;  /* by default don't invalidate ATE on free */

/*
 * Allocate "count" contiguous Bridge Address Translation Entries
 * on the specified bridge to be used for PCI to XTALK mappings.
 * Indices in rm map range from 1..num_entries.  Indicies returned
 * to caller range from 0..num_entries-1.
 *
 * Return the start index on success, -1 on failure.
 */
int
pcibr_ate_alloc(pcibr_soft_t pcibr_soft, int count, struct resource *res)
{
    int			    status = 0;
    unsigned long	    flag;

    memset(res, 0, sizeof(struct resource));
    flag = pcibr_lock(pcibr_soft);
    status = allocate_resource( &pcibr_soft->bs_int_ate_resource, res,
				count, pcibr_soft->bs_int_ate_resource.start, 
				pcibr_soft->bs_int_ate_resource.end, 1,
				NULL, NULL);
    if (status) {
	/* Failed to allocate */
	pcibr_unlock(pcibr_soft, flag);
	return -1;
    }

    /* Save the resource for freeing */
    pcibr_unlock(pcibr_soft, flag);

    return res->start;
}

void
pcibr_ate_free(pcibr_soft_t pcibr_soft, int index, int count, struct resource *res)
{

    bridge_ate_t ate;
    int status = 0;
    unsigned long flags;

    if (pcibr_invalidate_ate) {
	/* For debugging purposes, clear the valid bit in the ATE */
	ate = *pcibr_ate_addr(pcibr_soft, index);
	ate_write(pcibr_soft, index, count, (ate & ~ATE_V));
    }

    flags = pcibr_lock(pcibr_soft);
    status = release_resource(res);
    pcibr_unlock(pcibr_soft, flags);
    if (status)
	BUG(); /* Ouch .. */

}

/*
 * Convert PCI-generic software flags and Bridge-specific software flags
 * into Bridge-specific Address Translation Entry attribute bits.
 */
bridge_ate_t
pcibr_flags_to_ate(pcibr_soft_t pcibr_soft, unsigned flags)
{
    bridge_ate_t            attributes;

    /* default if nothing specified:
     * NOBARRIER
     * NOPREFETCH
     * NOPRECISE
     * COHERENT
     * Plus the valid bit
     */
    attributes = ATE_CO | ATE_V;

    /* Generic macro flags
     */
    if (flags & PCIIO_DMA_DATA) {	/* standard data channel */
	attributes &= ~ATE_BAR;		/* no barrier */
	attributes |= ATE_PREF;		/* prefetch on */
    }
    if (flags & PCIIO_DMA_CMD) {	/* standard command channel */
	attributes |= ATE_BAR;		/* barrier bit on */
	attributes &= ~ATE_PREF;	/* disable prefetch */
    }
    /* Generic detail flags
     */
    if (flags & PCIIO_PREFETCH)
	attributes |= ATE_PREF;
    if (flags & PCIIO_NOPREFETCH)
	attributes &= ~ATE_PREF;

    /* Provider-specific flags
     */
    if (flags & PCIBR_BARRIER)
	attributes |= ATE_BAR;
    if (flags & PCIBR_NOBARRIER)
	attributes &= ~ATE_BAR;

    if (flags & PCIBR_PREFETCH)
	attributes |= ATE_PREF;
    if (flags & PCIBR_NOPREFETCH)
	attributes &= ~ATE_PREF;

    if (flags & PCIBR_PRECISE)
	attributes |= ATE_PREC;
    if (flags & PCIBR_NOPRECISE)
	attributes &= ~ATE_PREC;

    /* In PCI-X mode, Prefetch & Precise not supported */
    if (IS_PCIX(pcibr_soft)) {
	attributes &= ~(ATE_PREC | ATE_PREF);
    }

    return (attributes);
}

/*
 * Setup an Address Translation Entry as specified.  Use either the Bridge
 * internal maps or the external map RAM, as appropriate.
 */
bridge_ate_p
pcibr_ate_addr(pcibr_soft_t pcibr_soft,
	       int ate_index)
{
    if (ate_index < pcibr_soft->bs_int_ate_size) {
	return (pcireg_int_ate_addr(pcibr_soft, ate_index));
    } else {
	printk("pcibr_ate_addr(): INVALID ate_index 0x%x", ate_index);
	return (bridge_ate_p)0;
    }
}

/*
 * Write the ATE.
 */
void
ate_write(pcibr_soft_t pcibr_soft, int ate_index, int count, bridge_ate_t ate)
{
    while (count-- > 0) {
	if (ate_index < pcibr_soft->bs_int_ate_size) {
	    pcireg_int_ate_set(pcibr_soft, ate_index, ate);
	    PCIBR_DEBUG((PCIBR_DEBUG_DMAMAP, pcibr_soft->bs_vhdl,
			"ate_write(): ate_index=0x%x, ate=0x%lx\n",
			ate_index, (uint64_t)ate));
	} else {
	    printk("ate_write(): INVALID ate_index 0x%x", ate_index);
	    return;
	}
	ate_index++;
	ate += IOPGSIZE;
    }

    pcireg_tflush_get(pcibr_soft);	/* wait until Bridge PIO complete */
}
