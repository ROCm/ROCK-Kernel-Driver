/*
 *  include/asm-s390/sigp.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  sigp.h by D.J. Barrow (c) IBM 1999
 *  contains routines / structures for signalling other S/390 processors in an
 *  SMP configuration.
 */

#ifndef __SIGP__
#define __SIGP__

#include <asm/ptrace.h>
#include <asm/misc390.h>
#include <asm/atomic.h>

/* get real cpu address from logical cpu number */
extern volatile int __cpu_logical_map[];

typedef enum
{
	sigp_unassigned=0x0,
	sigp_sense,
	sigp_external_call,
	sigp_emergency_signal,
	sigp_start,
	sigp_stop,
	sigp_restart,
	sigp_unassigned1,
	sigp_unassigned2,
	sigp_stop_and_store_status,
	sigp_unassigned3,
	sigp_initial_cpu_reset,
	sigp_cpu_reset,
	sigp_set_prefix,
	sigp_store_status_at_address,
	sigp_store_extended_status_at_address
} sigp_order_code;

#if 0
/*
 * these definitions are not used at the moment, but we might need
 * them in future.
 */
typedef struct
{
        __u64   cpu_timer;
        psw_t   current_psw;
        __u32   prefix;
        __u32   access_regs[16];
        __u64   float_regs[4];
        __u32   gpr_regs[16];
        __u32   control_regs[16];
} sigp_status __attribute__((packed));

typedef struct
{
        __u8    unused1[216];
        __u64   cpu_timer;
        psw_t   current_psw;
        __u32   prefix;
        __u32   access_regs[16];
        __u64   float_regs[4];
        __u32   gpr_regs[16];
        __u32   control_regs[16];
} sigp_status_512 __attribute__((packed));

typedef struct
{
        __u32   extended_save_area_address;
        __u64   cpu_timer;
        psw_t   current_psw;
        __u32   prefix;
        __u32   access_regs[16];
        __u64   float_regs[4];
        __u32   gpr_regs[16];
        __u32   control_regs[16];
} sigp_extended_status __attribute__((packed));

typedef struct
{
        __u8    unused1[212];
        __u32   extended_save_area_address;
        __u64   cpu_timer;
        psw_t   current_psw;
        __u32   prefix;
        __u32   access_regs[16];
        __u64   float_regs[4];
        __u32   gpr_regs[16];
        __u32   control_regs[16];
} sigp_extended_status_512 __attribute__((packed));

typedef struct
{
	__u64   bfp_float_regs[16];
	__u32   bfp_float_control_reg;
	__u8    reserved[12];                     
} sigp_extended_save_area __attribute__ ((packed));

typedef struct
{
	unsigned equipment_check:1;
	unsigned unassigned1:20;
	unsigned incorrect_state:1;
	unsigned invalid_parameter:1;
	unsigned external_call_pending:1;
	unsigned stopped:1;
	unsigned operator_intervening:1;
	unsigned check_stop:1;
	unsigned unassigned2:1;
	unsigned inoperative:1;
	unsigned invalid_order:1;
	unsigned receiver_check:1;
} sigp_status_bits __attribute__((packed));
#endif

typedef __u32 sigp_status_word;

typedef enum
{
        sigp_order_code_accepted=0,
	sigp_status_stored,
	sigp_busy,
	sigp_not_operational
} sigp_ccode;


/*
 * Definitions for the external call
 */

/* 'Bit' signals, asynchronous */
typedef enum
{
	ec_schedule=0,
        ec_restart,
        ec_halt,
        ec_power_off,
	ec_bit_last
} ec_bit_sig;

/* Signals which come with a parameter area, synchronous */
typedef enum
{
	ec_set_ctl,
	ec_get_ctl,
	ec_set_ctl_masked,
        ec_cmd_last
} ec_cmd_sig;

/* state information for synchronous signals */
typedef enum
{
	ec_pending,
	ec_executing,
	ec_done
} ec_state;

/* header for the queuing of signals with a parameter area */
typedef struct ec_ext_call
{
	ec_cmd_sig cmd;
	atomic_t status;
	struct ec_ext_call *next;
	void *parms;
} ec_ext_call;

/* parameter area for the ec_set_ctl and ec_get_ctl signal */
typedef struct
{
	__u16 start_ctl;
	__u16 end_ctl;
	__u32 cregs[16];
} ec_creg_parms;

/* parameter area for the ec_set_ctl_masked signal */
typedef struct
{
	__u16 start_ctl;
	__u16 end_ctl;
	__u32 orvals[16];
	__u32 andvals[16];
} ec_creg_mask_parms;

/*
 * Signal processor
 */
extern __inline__ sigp_ccode
signal_processor(__u16 cpu_addr, sigp_order_code order_code)
{
	sigp_ccode ccode;

	__asm__ __volatile__(
		"    sr     1,1\n"        /* parameter=0 in gpr 1 */
		"    sigp   1,%1,0(%2)\n"
		"    ipm    %0\n"
		"    srl    %0,28\n"
		: "=d" (ccode)
		: "d" (__cpu_logical_map[cpu_addr]), "a" (order_code)
		: "cc" , "memory", "1" );
	return ccode;
}

/*
 * Signal processor with parameter
 */
extern __inline__ sigp_ccode
signal_processor_p(__u32 parameter,__u16 cpu_addr,sigp_order_code order_code)
{
	sigp_ccode ccode;
	
	__asm__ __volatile__(
		"    lr     1,%1\n"       /* parameter in gpr 1 */
		"    sigp   1,%2,0(%3)\n"
		"    ipm    %0\n"
		"    srl    %0,28\n"
		: "=d" (ccode)
		: "d" (parameter), "d" (__cpu_logical_map[cpu_addr]),
                  "a" (order_code)
		: "cc" , "memory", "1" );
	return ccode;
}

/*
 * Signal processor with parameter and return status
 */
extern __inline__ sigp_ccode
signal_processor_ps(__u32 *statusptr, __u32 parameter,
		    __u16 cpu_addr, sigp_order_code order_code)
{
	sigp_ccode ccode;
	
	__asm__ __volatile__(
		"    sr     2,2\n"        /* clear status so it doesn't contain rubbish if not saved. */
		"    lr     3,%2\n"       /* parameter in gpr 3 */
		"    sigp   2,%3,0(%4)\n"
		"    st     2,%1\n"
		"    ipm    %0\n"
		"    srl    %0,28\n"
		: "=d" (ccode), "=m" (*statusptr)
		: "d" (parameter), "d" (__cpu_logical_map[cpu_addr]),
                  "a" (order_code)
		: "cc" , "memory", "2" , "3"
		);
   return ccode;
}

#endif __SIGP__


