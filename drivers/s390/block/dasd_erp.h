/* 
 * File...........: linux/drivers/s390/block/dasd_erp.h
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 2000
 */

#include "dasd_types.h"

typedef enum {
  dasd_era_fatal = -1,
  dasd_era_none = 0
} dasd_era_t;

dasd_era_t dasd_erp_examine ( cqr_t * );
