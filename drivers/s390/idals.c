/* 
   * File...........: linux/drivers/s390x/idals.c
   * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
   * Bugreports.to..: <Linux390@de.ibm.com>
   * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 2000a
   
   * History of changes
   * 07/24/00 new file
   * 12/13/00 changed IDALs to 4kByte-IDALs
 */

#include <linux/config.h>
#include <linux/malloc.h>

#include <asm/irq.h>
#include <asm/idals.h>

#ifdef CONFIG_ARCH_S390X
void 
set_normalized_cda ( ccw1_t * cp, unsigned long address )
{
	int nridaws;
	idaw_t *idal;
        int count = cp->count;

	if (cp->flags & CCW_FLAG_IDA)
		BUG();
	if (((address + count) >> 31) == 0) { /* do we really need  '+count'? */
		cp -> cda = address;
		return;
	}
        nridaws = ((address & 4095L) + count + 4095L) >> 12;
	idal = idal_alloc(nridaws);
	if ( idal == NULL ) {
		/* probably we should have a fallback here */
		panic ("Cannot allocate memory for IDAL\n");
	}
	cp->flags |= CCW_FLAG_IDA;
	cp->cda = (__u32)(unsigned long)(idaw_t *)idal;
        do {
		*idal++ = address;
		address = (address & -4096L) + 4096;
		nridaws --;
        } while ( nridaws > 0 );
	return;
}
#endif
