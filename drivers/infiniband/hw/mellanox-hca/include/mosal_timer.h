/*
  This software is available to you under a choice of one of two
  licenses.  You may choose to be licensed under the terms of the GNU
  General Public License (GPL) Version 2, available at
  <http://www.fsf.org/copyleft/gpl.html>, or the OpenIB.org BSD
  license, available in the LICENSE.TXT file accompanying this
  software.  These details are also available at
  <http://openib.org/license.html>.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  Copyright (c) 2004 Mellanox Technologies Ltd.  All rights reserved.
*/

#ifndef H_MOSAL_TIMER_H
#define H_MOSAL_TIMER_H

#if defined(__LINUX__) && defined(__x86_64__)
#include <asm/msr.h>
#endif

#ifdef MT_KERNEL

/*  typedef for MOSAL INTERRUPT object */
struct MOSAL_ISR;
typedef struct MOSAL_ISR MOSAL_ISR_t;

/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_ISR_set
 *
 *  Description:
 *    connect interrupt handler
 *
 *  Parameters:
 *	  isr_p(IN) MOSAL_ISR_t *
 *		  interrupt object
 *    handler(IN) intr_handler_t
 *        Pointer to the intr. handler.
 *    irq(IN)   MOSAL_IRQ_ID_t
 *        The IRQ number of the intr which invokes this handler.
 *    name(IN) char *
 *        Just informationa name. In Linux environment
 *        that name will be presented in /proc/interrupts
 *    dev_id(IN)   MT_ulong_ptr_t
 *        Unique device ID. Use on intr sharing.
 *        If NULL, the device may NOT share the IRQ
 *        with other devices (handlers).
 *        Otherwise, this pointer may be used as the "This"
 *        pointer of device data object when calling handler.
 *
 *  Returns:
 *    call_result_t
 *        0-OK -1-Error
 *
 ******************************************************************************/
call_result_t MOSAL_ISR_set(
	MOSAL_ISR_t *  		isr_p,
	MOSAL_ISR_func_t 	handler,
    MOSAL_IRQ_ID_t 		irq,
    char *				name,
    MT_ulong_ptr_t			dev_id
    );

/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_ISR_unset
 *
 *  Description:
 *    disconnect interrupt handler
 *
 *  Parameters:
 *	  isr_p(IN) MOSAL_ISR_t *
 *		  interrupt object
 *
 *  Returns:
 *    call_result_t
 *        0-OK -1-Error
 *
 ******************************************************************************/
call_result_t MOSAL_ISR_unset( MOSAL_ISR_t * isr_p );

#if !defined(__DARWIN__)
/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_set_intr_handler
 *
 *  Description:
 *    Sets the given interrupt handler to be called back on the given IRQ.
 *    If dev_id!=NULL, the handler would be linked to IRQ only if previously
 *    set handler were sharing handlers (had dev_id!=NULL), too.
 *    This means that a non-sharing handler may be the only handler which
 *    is called back on a given IRQ.
 *
 *  Parameters:
 *    handler(IN) intr_handler_t
 *        Pointer to the intr. handler.
 *    irq(IN)   MOSAL_IRQ_ID_t
 *        The IRQ number of the intr which invokes this handler.
 *    name(IN) char *
 *        Just informationa name. In Linux environment
 *        that name will be presented in /proc/interrupts
 *    dev_id(IN)   void*
 *        Unique device ID. Use on intr sharing.
 *        If NULL, the device may NOT share the IRQ
 *        with other devices (handlers).
 *        Otherwise, this pointer may be used as the "This"
 *        pointer of device data object when calling handler.
 *
 *  Returns:
 *    call_result_t
 *        0-OK -1-Error
 *
 ******************************************************************************/
call_result_t MOSAL_set_intr_handler(intr_handler_t handler,
                                     MOSAL_IRQ_ID_t irq,
                                     char *name,
                                     void* dev_id);


/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_unset_intr_handler
 *
 *  Description:
 *    Removes the given interrupt handler of the callback chain on the given IRQ.
 *
 *  Parameters:
 *    handler(IN) intr_handler_t
 *        Pointer to the intr. handler.
 *    irq(IN) MOSAL_IRQ_ID_t
 *        The IRQ number of the intr which invokes this handler.
 *    dev_id(IN) void*
 *        Unique device ID. Use on intr sharing.
 *
 *  Returns:
 *    call_result_t
 *        MT_OK if success, MT_ERROR if failed.
 *
 ******************************************************************************/
call_result_t MOSAL_unset_intr_handler(intr_handler_t handler,
                                       MOSAL_IRQ_ID_t irq,
                                       void* dev_id);

#endif /* ! defined__DARWIN__ */

#if ((defined(__LINUX__) && (LINUX_KERNEL_2_4 || LINUX_KERNEL_2_6))) || defined(__WIN__) || defined(VXWORKS_OS) || defined(__DARWIN__)
/* This code is only for kernel 2.4 or kernel 2.6 */



/* ////////////////////////////////////////////////////////////////////////////// */

/*  DPC (=tasklet) functions  */

/* ////////////////////////////////////////////////////////////////////////////// */

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Component: 
 *		MOSAL tasklet/DPC implementation [tasklets and DPC are synonyms]
 *
 * Data object: 
 * 		MOSAL DPC object	- struct MOSAL_DPC,	wrapping tasklet in Linux and DPC in Windows
 *
 * Functions:
 *		void MOSAL_DPC_init(MOSAL_DPC_t  *d ...)			- initialize a MOSAL DPC object;
 *		void MOSAL_DPC_schedule(MOSAL_DPC_t *d)				- schedule a MOSAL DPC object;
 *		void MOSAL_DPC_schedule_ctx(MOSAL_DPC_t *d)			- schedule a MOSAL DPC object with parameters;
 *		call_result_t MOSAL_DPC_add_ctx(MOSAL_DPC_t *d,...) -  add DPC request context to a MOSAL DPC object;
 *		
 *
 * Macros:
 *		MOSAL_DPC_enable(MOSAL_DPC_t *d)					- enable DPC (only Linux)
 *		MOSAL_DPC_disable(MOSAL_DPC_t *d)					- disable DPC (only Linux)
 *
 * Example of usage (taken from CM\msgdspch.c and ported to MOSAL API):
 *
 *		**  declare a MOSAL DPC object 'cm_machine_tasklet', calling function 'cm_machine' 
 *		MOSAL_DPC_t cm_machine_tasklet;
 *		MOSAL_DPC_init (&cm_machine_tasklet, cm_machine, 0, 0);
 *
 *		**  schedule it **
 *		MOSAL_DPC_schedule( &cm_machine_tasklet );
 * 
 * Notes:
 *		1. There are no static initialization like Linux's DECLARE_TASKLET() !
 *		2. DPC function has Linux's prototype: void a_DPC_function(unsigned long ), 
 *		   but the meaning of the parameters depends on the type of DPC.
 *		3. There are 2 types of DPC: MOSAL_SINGLE_CTX and MOSAL_MULTIPLE_CTX.
 *      4. DPC of MOSAL_SINGLE_CTX type:
 *			- allows only one interrupt per one DPC invocation;
 *			- doesn't relay dynamic parameters from ISR to DPC;
 *			- The parameter of a_DPC_function() is in fact a user callback context;
 *		5. DPC of MULITPLE_CTX type:
 *			- allows several interrupts before and during a DPC invocation;
 *			- on every interrupt ISR fills a DPC context with static and dynamic parameters and
 *			  enqueues it to the DPC context chain. The DPC handles all the chain while it works.
 *			- The parameter of a_DPC_function() is in fact a pointer to DPC_CONTEXT_t (see below);
 *		   
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
 

struct DPC_CONTEXT;
typedef struct DPC_CONTEXT DPC_CONTEXT_t;

/*  typedef for MOSAL DPC object */
struct MOSAL_DPC;
typedef struct MOSAL_DPC MOSAL_DPC_t;


/*  Functions */


 /******************************************************************************
 *  Function (kernel-mode only):
 *		MOSAL_DPC_init
 *
 *  Description:
 *		init a MOSAL DPC object
 *
 *  Parameters:
 *		d			- MOSAL DPC object
 *		func		- user DPC;
 *		data		- its data;
 *		type 		- type of DPC
 *
 *  Returns:
 *
 *  Notes:
 *		(Windows) Callers of this routine must be running at IRQL PASSIVE_LEVEL
 *
 ******************************************************************************/
void MOSAL_DPC_init(MOSAL_DPC_t *d, MOSAL_DPC_func_t func, MT_ulong_ptr_t func_ctx, MOSAL_DPC_type_t type );

 /******************************************************************************
 *  Function (kernel-mode only):
 *		MOSAL_DPC_schedule
 *
 *  Description:
 *		schedule user DPC 
 *
 *  Parameters:
 *		d			- MOSAL DPC object
 *
 *  Returns:
 *
 *  Notes:
 *		(Windows) Callers of this routine must be running at IRQL PASSIVE_LEVEL
 *
 ******************************************************************************/
MT_bool  MOSAL_DPC_schedule(MOSAL_DPC_t *d);


#if !defined(__DARWIN__)
 /******************************************************************************
 *  Function (kernel-mode only):
 *		MOSAL_DPC_schedule_ctx
 *
 *  Description:
 *		schedule user DPC with relaying a context
 *
 *  Parameters:
 *		d				- MOSAL DPC object
 *		isr_ctx1		- context, relayed by ISR, inserting DPC;
 *		isr_ctx2		- context, relayed by ISR, inserting DPC; 
 *
 *  Returns:
 *
 *  Notes:
 *		(Windows) Callers of this routine must be running at IRQL PASSIVE_LEVEL
 *
 ******************************************************************************/
MT_bool  MOSAL_DPC_schedule_ctx(MOSAL_DPC_t *d, void * isr_ctx1, void * isr_ctx2);

#ifdef SUPPORT_MULTIPLE_CTX	
 /******************************************************************************
 *  Function (kernel-mode only):
 *		MOSAL_DPC_add_ctx
 *
 *  Description:
 *		add DPC request context to a MOSAL DPC object
 *
 *  Parameters:
 *		d			- MOSAL DPC object
 *		ctx1		- context, relayed by ISR, inserting DPC;
 *		ctx2		- context, relayed by ISR, inserting DPC; 
 *
 *  Returns:
 *		MT_ENORSC	- if no ctx structures
 *		MT_OK		- otherwise
 *
 *  Notes:
 *		A helper routine for ISR, inserting DPC 
 *
 ******************************************************************************/
call_result_t MOSAL_DPC_add_ctx(MOSAL_DPC_t *d, PVOID ctx1, PVOID ctx2);
#  endif /* SUPPORT_MULTIPLE_CTX */
#endif /* ! defined __DARWIN__ */

/* ////////////////////////////////////////////////////////////////////////////// */

/*  Timer functions  */

/* ////////////////////////////////////////////////////////////////////////////// */

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Component: 
 *		MOSAL timer functions implementation 
 *
 * Data object: 
 * 		MOSAL timer object	- struct MOSAL_timer, wrapping OS-dependent structures
 *
 * Functions:
 *		void MOSAL_timer_init(MOSAL_timer_t *t)		- initialize a MOSAL timer object;
 *		void MOSAL_timer_add(MOSAL_timer_t *t ...)	- start timer;
 *		void MOSAL_timer_del(MOSAL_timer_t *t ...)	- cancel timer;
 *		void MOSAL_timer_mod(MOSAL_timer_t *t ...)	- restart timer;
 *
 * Example of usage (taken from CMkernel\cm_sm.c and ported to MOSAL API):
 *
 *		**  declare a MOSAL timer object 'cm_machine_tasklet', calling function 'cm_machine' **
 *		MOSAL_timer_t try_timer;
 *		MOSAL_timer_init (&try_timer);
 *
 *		**  start timer **
 *		MOSAL_timer_add( &try_timer, cmk_try_timeout, connection->local_com_id, usec );
 * 
 * Portability notes:
 *		1. Functions MOSAL_timer_del() and MOSAL_timer_mod() return 'void' (and not 'int' as in Linux);
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*  timer  object */
struct MOSAL_timer;
typedef struct MOSAL_timer MOSAL_timer_t;

#if  !defined(__DARWIN__)
 /******************************************************************************
 *  Function (kernel-mode only):
 *		MOSAL_timer_init
 *
 *  Description:
 *		init a MOSAL DPC object
 *
 *  Parameters:
 *		d			- MOSAL timer object
 *		func		- user DPC;
 *		data		- its data;
 *
 *  Returns:
 *
 *  Notes:
 *		1. Callers of this routine must be running at IRQL PASSIVE_LEVEL;
 *		2. Every timer must use its own MOSAL timer object ! 
 *		3. Different MOSAL timer objects may use the same DPC function;
 *
 ******************************************************************************/
__INLINE__ void MOSAL_timer_init(MOSAL_timer_t *t);

 /******************************************************************************
 *  Function (kernel-mode only):
 *		MOSAL_timer_add
 *
 *  Description:
 *		start timer
 *
 *  Parameters:
 *		d			- MOSAL timer object
 *		func		- user DPC;
 *		data		- its data;
 *		usecs		- interval; 'func' will be called in 'usecs' microseconds
 *
 *  Returns:
 *
 *  Notes:
 *		Callers of this routine must be running at IRQL <= DISPATCH_LEVEL
 *
 ******************************************************************************/
__INLINE__ void MOSAL_timer_add(MOSAL_timer_t *t, MOSAL_DPC_func_t func, MT_ulong_ptr_t data, long usecs);

 /******************************************************************************
 *  Function (kernel-mode only):
 *		MOSAL_timer_del
 *
 *  Description:
 *		delete timer
 *
 *  Parameters:
 *		d			- MOSAL timer object
 *
 *  Returns:
 *
 *  Notes:
 *		Callers of this routine must be running at IRQL <= DISPATCH_LEVEL 
 *    NOTE for Linux - *** do not use the callback function to call add_timer to
 *                     add this timer to the list as it may cause a race condition ***
 *
 ******************************************************************************/
__INLINE__ void MOSAL_timer_del(MOSAL_timer_t *t);

 /******************************************************************************
 *  Function (kernel-mode only):
 *		MOSAL_timer_mod
 *
 *  Description:
 *		stop the running timer and restart it in 'usecs' microseconds
 *
 *  Parameters:
 *		d			- MOSAL timer object
 *		usecs		- interval; 'func' will be called in 'usecs' microseconds
 *
 *  Returns:
 *
 *  Notes:
 *		Callers of this routine must be running at IRQL <= DISPATCH_LEVEL
 *
 ******************************************************************************/
__INLINE__ void MOSAL_timer_mod(MOSAL_timer_t *t, long usecs);

#endif /* ! defined __DARWIN__ */


#endif  /* ((defined(__LINUX__) && LINUX_KERNEL_2_4)) || defined(__WIN__) */
#endif  /* MT_KERNEL */

/* ////////////////////////////////////////////////////////////////////////////// */

/*  Time functions  */

/* ////////////////////////////////////////////////////////////////////////////// */

#if defined(__WIN__) || defined(VXWORKS_OS)

/*  Time */


/* taken from Linux/time.h */
struct MOSAL_timespec {
	long	tv_sec;		/* seconds */
	long	tv_nsec;	/* nanoseconds */
};


typedef struct MOSAL_timespec MOSAL_timespec_t;

#endif /* __WIN__ || VXWORKS_OS */

#ifdef __DARWIN__
typedef struct mach_timespec  MOSAL_timespec_t;
#endif

#if defined(__LINUX__)
typedef struct timespec MOSAL_timespec_t;
#endif


#ifdef MT_KERNEL
 /******************************************************************************
 *  Function (kernel-mode only):
 *		MOSAL_time_get_clock
 *
 *  Description:
 *		get current system clock (in microseconds)
 *
 *  Parameters:
 *		ts(OUT) - pointer to a structure, describing the time
 *
 *  Returns:
 *
 *  Notes:
 *		Callers of this routine must be running at IRQL <= DISPATCH_LEVEL
 *
 ******************************************************************************/
 void MOSAL_time_get_clock(MOSAL_timespec_t *ts);

 /******************************************************************************
 *  Function (kernel-mode only):
 *		MOSAL_time_compare
 *
 *  Description:
 *		compare 2 absolute times
 *
 *  Parameters:
 *		ts1(IN) - pointer to a structure, describing the time
 *		ts2(IN) - pointer to a structure, describing the time
 *
 *  Returns:
 *		positive - when ts1 > ts2
 *		negative - when ts1 < ts2
 *		zero	 - when ts1 = ts2
 *
 *
 ******************************************************************************/
static __INLINE__ int MOSAL_time_compare(MOSAL_timespec_t *ts1, MOSAL_timespec_t *ts2 )
{
	if (ts1->tv_sec > ts2->tv_sec)
		return 1;
	if (ts1->tv_sec < ts2->tv_sec)
		return -1;
	if (ts1->tv_nsec < ts2->tv_nsec)
		return -1;
	return ts1->tv_nsec > ts2->tv_nsec;
}

 /******************************************************************************
 *  Function (kernel-mode only):
 *		MOSAL_time_add_usec
 *
 *  Description:
 *		enlarge an absolute time 'ts' by 'usec' of microseconds
 *
 *  Parameters:
 *		ts(IN) 		- pointer to a structure, describing the time
 *		usecs(IN) 	- a POSITIVE number of microseconds to add
 *
 *  Returns:
 *		updated 'ts'
 *
 ******************************************************************************/
static __INLINE__ MOSAL_timespec_t * MOSAL_time_add_usec(MOSAL_timespec_t *ts, long usecs )
{
	ts->tv_sec += usecs / 1000000L;
	ts->tv_nsec += (usecs % 1000000L) * 1000;
	if (ts->tv_nsec > 1000000000L) {
		ts->tv_sec++;
		ts->tv_nsec -= 1000000000L;
	}
	return ts;
}

 /******************************************************************************
 *  Function (kernel-mode only):
 *		MOSAL_time_init
 *
 *  Description:
 *		Zero 'ts' structure
 *
 *  Parameters:
 *		ts(IN) 		- pointer to a structure, describing the time
 *		usecs(IN) 	- a POSITIVE number of microseconds to add
 *
 *  Returns:
 *		updated 'ts'
 *
 ******************************************************************************/
static __INLINE__ void MOSAL_time_init(MOSAL_timespec_t *ts)
{
	ts->tv_sec = ts->tv_nsec = 0;
}
#endif /* MT_KERNEL */

 /******************************************************************************
 *  Function (inline): 
 *		MOSAL_get_time_counter
 *
 *  Description:
 *		get te current value of a counter that progresses monotonically with time
 *
 *  Parameters:
 *
 *  Returns:
 *		value of the counter or 0 if not supported by the architecture
 *
 ******************************************************************************/
static __INLINE__ u_int64_t MOSAL_get_time_counter(void)
{
#if (defined(__i386__) || defined(i386)) && (defined(__LINUX__) || defined(VXWORKS_OS))
     unsigned long long int x;
     __asm__ volatile (".byte 0x0f, 0x31" : "=A" (x));
     return x;
        
#elif defined(__WIN__)
  	return win_get_time_counter(); /* defined in mtl_sys_defs.h */
  	      
#elif defined(__x86_64__) && defined(__LINUX__)
  u_int32_t low, high;
  rdtsc(low, high);
  return (((u_int64_t)high)<<32) | (u_int64_t)low;
#else
  return 0;
#endif
}

/******************************************************************************
 *  Function: MOSAL_get_counts_per_sec
 *
 *  Description: get number of counts in 1 sec (refer to MOSAL_get_time_counter)
 *
 *  Parameters:
 *
 *  Returns:
 *    Number of counts in 1 sec or 0 if not supported
 *
 ******************************************************************************/
u_int64_t MOSAL_get_counts_per_sec(void);

#endif /* H_MOSAL_TIMER_H */

