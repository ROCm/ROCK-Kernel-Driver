/* 
 * File...........: linux/drivers/s390/block/dasd_erp.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 2000
 */

#include <asm/irq.h>
#include <linux/dasd.h>
#include "dasd_erp.h"
#include "dasd_types.h"

dasd_era_t 
dasd_erp_examine ( cqr_t * cqr)
{
  devstat_t *stat = cqr->dstat ;
  if ( stat->cstat == 0x00 && 
       stat->dstat == (DEV_STAT_CHN_END|DEV_STAT_DEV_END ) )
    return dasd_era_none;
  return dasd_era_fatal;
}
