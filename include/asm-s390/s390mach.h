/*
 *  arch/s390/kernel/s390mach.h
 *   S/390 data definitions for machine check processing
 *
 *  S390 version
 *    Copyright (C) 2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Ingo Adlung (adlung@de.ibm.com)
 */

#ifndef __s390mach_h
#define __s390mach_h

#include <asm/types.h>

//
// machine-check-interruption code
//
typedef struct _mcic {
	__u32	to_be_defined_1 :  9;
	__u32 cp              :  1; /* channel-report pending */
	__u32	to_be_defined_2 : 22;
	__u32	to_be_defined_3;
} __attribute__ ((packed)) mcic_t;

//
// Channel Report Word
//
typedef struct _crw {
	__u32 res1    :  1;   /* reserved zero */
	__u32 slct    :  1;   /* solicited */
	__u32 oflw    :  1;   /* overflow */
	__u32 chn     :  1;   /* chained */
	__u32 rsc     :  4;   /* reporting source code */
	__u32 anc     :  1;   /* ancillary report */
	__u32 res2    :  1;   /* reserved zero */
	__u32 erc     :  6;   /* error-recovery code */
	__u32 rsid    : 16;   /* reporting-source ID */
} __attribute__ ((packed)) crw_t;

//
// CRW Entry
//
typedef struct _crwe {
	crw_t  crw;
	crw_t *crw_next;
} __attribute__ ((packed)) crwe_t;

typedef struct _mchchk_queue_element {
	spinlock_t                   lock;
	unsigned int                 status;
	mcic_t                       mcic;
	crwe_t                      *crwe;		/* CRW if applicable */
	struct mchchk_queue_element *next;
	struct mchchk_queue_element *prev;
} mchchk_queue_element_t;

#define MCHCHK_STATUS_TO_PROCESS    0x00000001
#define MCHCHK_STATUS_IN_PROGRESS   0x00000002
#define MCHCHK_STATUS_WAITING       0x00000004

void        s390_init_machine_check   ( void );
void __init s390_do_machine_check     ( void );
void __init s390_machine_check_handler( struct semaphore * );

#endif /* __s390mach */
