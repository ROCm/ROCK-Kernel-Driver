/*
 *  arch/s390/kernel/s390dyn.h
 *   S/390 data definitions for dynamic device attachment
 *
 *  S390 version
 *    Copyright (C) 2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Ingo Adlung (adlung@de.ibm.com)
 */

#ifndef __s390dyn_h
#define __s390dyn_h

struct _devreg;

typedef  int  (* oper_handler_func_t)( int             irq,
                                       struct _devreg *dreg);

typedef struct _devreg_hc_t {
	__u16 ctype;
	__u8  cmode;
	__u16 dtype;
	__u8  dmode;
   }  __attribute__ ((packed)) devreg_hc_t;

typedef struct _devreg {
	union {
		int devno;
		devreg_hc_t hc;       /* has controller info */
	} ci;

	int                  flag;
	oper_handler_func_t  oper_func;
	struct _devreg      *prev;
	struct _devreg      *next;
} devreg_t;

#define DEVREG_EXACT_MATCH      0x00000001
#define DEVREG_MATCH_DEV_TYPE   0x00000002
#define DEVREG_MATCH_CU_TYPE    0x00000004
#define DEVREG_NO_CU_INFO       0x00000008
#define DEVREG_NO_DEV_INFO      0x00000010

#define DEVREG_TYPE_DEVNO       0x80000000
#define DEVREG_TYPE_DEVCHARS    0x40000000

int        s390_device_register  ( devreg_t *drinfo );
int        s390_device_unregister( devreg_t *dreg );
devreg_t * s390_search_devreg    ( ioinfo_t *ioinfo );

#endif /* __s390dyn */
