/*      $Id: lirc_parallel.h,v 5.1 1999/07/21 18:23:37 columbus Exp $      */

#ifndef _LIRC_PARALLEL_H
#define _LIRC_PARALLEL_H

#include <linux/lp.h>

#define LIRC_PORT_LEN 3

#define LIRC_LP_BASE    0
#define LIRC_LP_STATUS  1
#define LIRC_LP_CONTROL 2

#define LIRC_PORT_DATA           LIRC_LP_BASE    /* base */
#define LIRC_PORT_DATA_BIT               0x01    /* 1st bit */
#define LIRC_PORT_TIMER        LIRC_LP_STATUS    /* status port */
#define LIRC_PORT_TIMER_BIT          LP_PBUSY    /* busy signal */
#define LIRC_PORT_SIGNAL       LIRC_LP_STATUS    /* status port */
#define LIRC_PORT_SIGNAL_BIT          LP_PACK    /* ack signal */
#define LIRC_PORT_IRQ         LIRC_LP_CONTROL    /* control port */

#define LIRC_SFH506_DELAY 0             /* delay t_phl in usecs */

#endif
