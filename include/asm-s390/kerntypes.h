/*
 * asm-s390/kerntypes.h
 *
 * Arch-dependent header file that includes headers for all arch-specific 
 * types of interest.
 * The kernel type information is used by the lcrash utility when
 * analyzing system crash dumps or the live system. Using the type
 * information for the running system, rather than kernel header files,
 * makes for a more flexible and robust analysis tool.
 *
 * This source code is released under the GNU GPL.
 */

/* S/390 specific header files */
#ifndef _S390_KERNTYPES_H
#define _S390_KERNTYPES_H

#include <asm/lowcore.h>
#include <asm/debug.h>
#include <asm/ccwdev.h>
#include <asm/ccwgroup.h>
#include <asm/qdio.h>

/* channel subsystem driver */
#include "../../drivers/s390/cio/cio.h"
#include "../../drivers/s390/cio/chsc.h"
#include "../../drivers/s390/cio/css.h"
#include "../../drivers/s390/cio/device.h"
#include "../../drivers/s390/cio/qdio.h"

/* dasd device driver */
#include "../../drivers/s390/block/dasd_int.h"
#include "../../drivers/s390/block/dasd_diag.h"
#include "../../drivers/s390/block/dasd_eckd.h"
#include "../../drivers/s390/block/dasd_fba.h"

/* networking drivers */
#include "../../drivers/s390/net/fsm.h"
#include "../../drivers/s390/net/iucv.h"
#include "../../drivers/s390/net/lcs.h"

/* zfcp device driver */
#include "../../drivers/s390/scsi/zfcp_def.h"
#include "../../drivers/s390/scsi/zfcp_fsf.h"

#endif /* _S390_KERNTYPES_H */
