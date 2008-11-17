/*
 * QLogic iSCSI HBA Driver
 * Copyright (c)  2003-2006 QLogic Corporation
 *
 * See LICENSE.qla4xxx for copyright and licensing details.
 */

/*
 * Driver debug definitions.
 */
/* #define QL_DEBUG  */			/* DEBUG messages */
/* #define QL_DEBUG_LEVEL_3  */		/* Output function tracing */
/* #define QL_DEBUG_LEVEL_4  */
/* #define QL_DEBUG_LEVEL_5  */
/* #define QL_DEBUG_LEVEL_6  */
/* #define QL_DEBUG_LEVEL_9  */
#ifndef _QL4_DBG_
#define _QL4_DBG_

#define QL_DEBUG_LEVEL_2	/* ALways enable error messagess */
#if defined(QL_DEBUG)
#define DEBUG(x)      do {if(extended_error_logging & 0x01) x;} while (0);
#else
#define DEBUG(x)
#endif

#if defined(QL_DEBUG_LEVEL_2)
#define DEBUG2(x)      do {if(extended_error_logging & 0x02) x;} while (0);
#else
#define DEBUG2(x)
#endif

#if defined(QL_DEBUG_LEVEL_3)
#define DEBUG3(x)      do {if(extended_error_logging & 0x04) x;} while (0);
#else
#define DEBUG3(x)
#endif

#if defined(QL_DEBUG_LEVEL_4)
#define DEBUG4(x)      do {if(extended_error_logging & 0x08) x;} while (0);
#else
#define DEBUG4(x)
#endif

#if defined(QL_DEBUG_LEVEL_5)
#define DEBUG5(x)      do {if(extended_error_logging & 0x10) x;} while (0);
#else
#define DEBUG5(x)
#endif

#if defined(QL_DEBUG_LEVEL_6)
#define DEBUG6(x)      do {if(extended_error_logging & 0x20) x;} while (0);
#else
#define DEBUG6(x)
#endif

#endif /*_QL4_DBG_*/
