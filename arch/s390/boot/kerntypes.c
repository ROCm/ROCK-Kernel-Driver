/*
 * kerntypes.c
 *
 * Dummy module that includes headers for all kernel types of interest.
 * The kernel type information is used by the lcrash utility when
 * analyzing system crash dumps or the live system. Using the type
 * information for the running system, rather than kernel header files,
 * makes for a more flexible and robust analysis tool.
 *
 * This source code is released under the GNU GPL.
 */

/* generate version for this file */
typedef char *COMPILE_VERSION;

/* General linux types */

#include <linux/autoconf.h>
#include <linux/compile.h>
#include <linux/config.h>
#include <linux/utsname.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/lowcore.h>
#include <asm/debug.h>
#include <asm/ccwdev.h>
#include <asm/ccwgroup.h>
#include <asm/qdio.h>

/* channel subsystem driver */
#include "drivers/s390/cio/cio.h"
#include "drivers/s390/cio/chsc.h"
#include "drivers/s390/cio/css.h"
#include "drivers/s390/cio/device.h"
#include "drivers/s390/cio/qdio.h"

/* dasd device driver */
#include "drivers/s390/block/dasd_int.h"
#include "drivers/s390/block/dasd_diag.h"
#include "drivers/s390/block/dasd_eckd.h"
#include "drivers/s390/block/dasd_fba.h"

/* networking drivers */
#include "drivers/s390/net/fsm.h"
#include "drivers/s390/net/iucv.h"
#include "drivers/s390/net/lcs.h"

/* zfcp device driver */
#include "drivers/s390/scsi/zfcp_def.h"
#include "drivers/s390/scsi/zfcp_fsf.h"

/* include sched.c for types: 
 *    - struct prio_array 
 *    - struct runqueue
 */
#include "kernel/sched.c"
