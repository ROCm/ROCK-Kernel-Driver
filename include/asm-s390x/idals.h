/* 
   * File...........: linux/include/asm-s390x/idals.h
   * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
   * Bugreports.to..: <Linux390@de.ibm.com>
   * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 2000a
   
   * History of changes
   * 07/24/00 new file
 */
#include <linux/config.h>
#include <asm/irq.h>

#define IDA_SIZE_LOG 12 /* 11 for 2k , 12 for 4k */
#define IDA_BLOCK_SIZE (1L<<IDA_SIZE_LOG)

static inline addr_t *
idal_alloc ( int nridaws )
{
	if ( nridaws > 33 )
		BUG();
	return kmalloc(nridaws * sizeof(addr_t), GFP_ATOMIC | GFP_DMA );
}

static inline void 
idal_free ( addr_t *idal )
{
	kfree (idal);
}

#if defined(CONFIG_ARCH_S390X)
extern unsigned long __create_idal(unsigned long address, int count);
#endif

/*
 * Function: set_normalized_cda
 * sets the address of the data in CCW
 * if necessary it allocates an IDAL and sets sthe appropriate flags
 */
static inline int
set_normalized_cda(ccw1_t * ccw, unsigned long address)
{
	int ret = 0;

#if defined (CONFIG_ARCH_S390X)
	if (((address + ccw->count) >> 31) != 0) {
		if (ccw->flags & CCW_FLAG_IDA)
			BUG();
		address = __create_idal(address, ccw->count);
		if (address)
			ccw->flags |= CCW_FLAG_IDA;
		else
			ret = -ENOMEM;
	}
#endif
	ccw->cda = (__u32) address;
	return ret;
}

/*
 * Function: clear_normalized_cda
 * releases any allocated IDAL related to the CCW
 */
static inline void
clear_normalized_cda ( ccw1_t * ccw ) 
{
#if defined(CONFIG_ARCH_S390X)
	if ( ccw -> flags & CCW_FLAG_IDA ) {
		idal_free ( (addr_t *)(unsigned long) (ccw -> cda ));
		ccw -> flags &= ~CCW_FLAG_IDA;
	}
#endif
	ccw -> cda = 0;
}

