/* 
   * File...........: linux/drivers/s390x/idals.c
   * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
   * Bugreports.to..: <Linux390@de.ibm.com>
   * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 2000a
   
   * History of changes
   * 07/24/00 new file
   * 12/13/00 changed IDALs to 4kByte-IDALs
 */

#include <linux/module.h>
#include <linux/config.h>
#include <linux/malloc.h>

#include <asm/irq.h>
#include <asm/idals.h>

#ifdef CONFIG_ARCH_S390X
#define IDA_SIZE_LOG 12 /* 11 for 2k , 12 for 4k */
#define IDA_BLOCK_SIZE (1L<<IDA_SIZE_LOG)
void 
set_normalized_cda ( ccw1_t * cp, unsigned long address )
{
	int nridaws;
	idaw_t *idal;
        int count = cp->count;

	if (cp->flags & CCW_FLAG_IDA)
		BUG();
	if (((address + count) >> 31) == 0) { 
		cp -> cda = address;
		return;
	}
        nridaws = ((address & (IDA_BLOCK_SIZE-1)) + count + 
		   (IDA_BLOCK_SIZE-1)) >> IDA_SIZE_LOG;
	idal = idal_alloc(nridaws);
	if ( idal == NULL ) {
		/* probably we should have a fallback here */
		panic ("Cannot allocate memory for IDAL\n");
	}
	cp->flags |= CCW_FLAG_IDA;
	cp->cda = (__u32)(unsigned long)(idaw_t *)idal;
        do {
		*idal++ = address;
		address = (address & -(IDA_BLOCK_SIZE)) + (IDA_BLOCK_SIZE);
		nridaws --;
        } while ( nridaws > 0 );
	return;
}

EXPORT_SYMBOL (set_normalized_cda);

#endif
