/* 
 * File...........: linux/include/asm-s390x/idals.h
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *		    Martin Schwidefsky <schwidefsky@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 2000a
 
 * History of changes
 * 07/24/00 new file
 * 05/04/02 code restructuring.
 */

#ifndef _S390_IDALS_H
#define _S390_IDALS_H

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <asm/cio.h>
#include <asm/uaccess.h>

#ifdef CONFIG_ARCH_S390X
#define IDA_SIZE_LOG 12 /* 11 for 2k , 12 for 4k */
#else
#define IDA_SIZE_LOG 11 /* 11 for 2k , 12 for 4k */
#endif
#define IDA_BLOCK_SIZE (1L<<IDA_SIZE_LOG)

/*
 * Test if an address/length pair needs an idal list.
 */
static inline int
idal_is_needed(void *vaddr, unsigned int length)
{
#if defined(CONFIG_ARCH_S390X)
	return ((__pa(vaddr) + length) >> 31) != 0;
#else
	return 0;
#endif
}


/*
 * Return the number of idal words needed for an address/length pair.
 */
static inline unsigned int
idal_nr_words(void *vaddr, unsigned int length)
{
#if defined(CONFIG_ARCH_S390X)
	if (idal_is_needed(vaddr, length))
		return ((__pa(vaddr) & (IDA_BLOCK_SIZE-1)) + length + 
			(IDA_BLOCK_SIZE-1)) >> IDA_SIZE_LOG;
#endif
	return 0;
}

/*
 * Create the list of idal words for an address/length pair.
 */
static inline unsigned long *
idal_create_words(unsigned long *idaws, void *vaddr, unsigned int length)
{
#if defined(CONFIG_ARCH_S390X)
	unsigned long paddr;
	unsigned int cidaw;

	paddr = __pa(vaddr);
	cidaw = ((paddr & (IDA_BLOCK_SIZE-1)) + length + 
		 (IDA_BLOCK_SIZE-1)) >> IDA_SIZE_LOG;
	*idaws++ = paddr;
	paddr &= -IDA_BLOCK_SIZE;
	while (--cidaw > 0) {
		paddr += IDA_BLOCK_SIZE;
		*idaws++ = paddr;
	}
#endif
	return idaws;
}

/*
 * Sets the address of the data in CCW.
 * If necessary it allocates an IDAL and sets the appropriate flags.
 */
static inline int
set_normalized_cda(struct ccw1 * ccw, void *vaddr)
{
#if defined (CONFIG_ARCH_S390X)
	unsigned int nridaws;
	unsigned long *idal;

	if (ccw->flags & CCW_FLAG_IDA)
		return -EINVAL;
	nridaws = idal_nr_words(vaddr, ccw->count);
	if (nridaws > 0) {
		idal = kmalloc(nridaws * sizeof(unsigned long),
			       GFP_ATOMIC | GFP_DMA );
		if (idal == NULL)
			return -ENOMEM;
		idal_create_words(idal, vaddr, ccw->count);
		ccw->flags |= CCW_FLAG_IDA;
		vaddr = idal;
	}
#endif
	ccw->cda = (__u32)(unsigned long) vaddr;
	return 0;
}

/*
 * Releases any allocated IDAL related to the CCW.
 */
static inline void
clear_normalized_cda(struct ccw1 * ccw)
{
#if defined(CONFIG_ARCH_S390X)
	if (ccw->flags & CCW_FLAG_IDA) {
		kfree((void *)(unsigned long) ccw->cda);
		ccw->flags &= ~CCW_FLAG_IDA;
	}
#endif
	ccw->cda = 0;
}

#endif
