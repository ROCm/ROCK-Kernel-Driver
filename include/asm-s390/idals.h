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

typedef unsigned long idaw_t;

static inline idaw_t *
idal_alloc ( int nridaws )
{
	if ( nridaws > 33 )
		BUG();
	return kmalloc(nridaws * sizeof(idaw_t), GFP_ATOMIC | GFP_DMA );
}

static inline void 
idal_free ( idaw_t *idal )
{
	kfree (idal);
}

/*
 * Function: set_normalized_cda
 * sets the address of the data in CCW
 * if necessary it allocates an IDAL and sets sthe appropriate flags
 */
#if defined (CONFIG_ARCH_S390X)
extern void set_normalized_cda(ccw1_t * ccw, unsigned long address);
#else
static inline void
set_normalized_cda(ccw1_t * ccw, unsigned long address)
{
	ccw->cda = address;
}
#endif

/*
 * Function: clear_normalized_cda
 * releases any allocated IDAL related to the CCW
 */
static inline void
clear_normalized_cda ( ccw1_t * ccw ) 
{
	if ( ccw -> flags & CCW_FLAG_IDA ) {
		idal_free ( (idaw_t *) (ccw -> cda ));
		ccw -> flags &= ~CCW_FLAG_IDA;
	}
	ccw -> cda = 0;
}

