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

typedef  int (* oper_handler_func_t)( int             irq,
                                      struct _devreg *dreg);
typedef  void (* io_handler_func_t) ( int  irq,
                                      __u32 intparm );
typedef  void ( * not_oper_handler_func_t)( int irq,
                                            int status );

typedef struct _devreg {
	union {
		struct _hc {
			__u16 ctype;
			__u8  cmode;
			__u16 dtype;
			__u8  dmode;
		} hc;       /* has controller info */

		struct _hnc {
			__u16 dtype;
			__u8  dmode;
			__u16 res1;
			__u8  res2;
		} hnc;      /* has no controller info */
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


int s390_device_register    ( devreg_t *drinfo );
int s390_device_deregister  ( devreg_t *dreg );
int s390_request_irq_special( int                      irq,
                              io_handler_func_t        io_handler,
                              not_oper_handler_func_t  not_oper_handler,
                              unsigned long            irqflags,
                              const char              *devname,
                              void                    *dev_id);

#endif /* __s390dyn */
