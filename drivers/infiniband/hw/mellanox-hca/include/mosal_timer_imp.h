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

#ifndef H_MOSAL_TIMER_IMP_H
#define H_MOSAL_TIMER_IMP_H

typedef u_int8_t  MOSAL_IRQ_ID_t;

#ifdef __KERNEL__

#if LINUX_KERNEL_2_4 || LINUX_KERNEL_2_6
/* This code is only for kernel 2.4 or 2.6 */

#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/time.h>


/*  ISRs */


/* IRQ back compat. */
#ifndef IRQ_HANDLED
  typedef void irqreturn_t;
  #define IRQ_HANDLED
#endif



typedef struct pt_regs 	MOSAL_intr_regs_t;
typedef irqreturn_t (*intr_handler_t)(int irq, void *dev_id, MOSAL_intr_regs_t* regs_p);

/*  user ISR function prototype */
typedef irqreturn_t (*MOSAL_ISR_func_t)(MT_ulong_ptr_t func_ctx, void * isr_ctx1, void * isr_ctx2 );


struct MOSAL_ISR {
	MOSAL_ISR_func_t 	func;
    MOSAL_IRQ_ID_t 		irq;
    char *				name;
    MT_ulong_ptr_t		ctx;
    void * 				isr_ctx1;
    void * 				isr_ctx2;
};




/*  DPCs */


/*  types of DPC (if doesn't need to relay info from ISR to DPC - use MOSAL_NO_CTX value) */
typedef enum { MOSAL_NO_CTX, MOSAL_SINGLE_CTX, MOSAL_MULTIPLE_CTX } MOSAL_DPC_type_t;



/*  the structure contains the context, relayed to user DPC  */

struct DPC_CONTEXT {
  	MT_ulong_ptr_t	func_ctx;		/*  DPC ("static") context */
	void *  			isr_ctx1;		/*  "dynamic" context, relayed by ISR */
  	void *  			isr_ctx2;		/*  "dynamic" context, relayed by ISR */
};  

/*  user DPC function prototype */
typedef void (*MOSAL_DPC_func_t)(struct DPC_CONTEXT * func_ctx);

/*  MOSAL DPC object */
struct MOSAL_DPC {
	struct tasklet_struct 	tasklet;
  	MOSAL_DPC_type_t 		type;			/*  type of DPC */
	MOSAL_DPC_func_t		func;			/*  user DPC to be called */
  	struct DPC_CONTEXT		dpc_ctx;		/*  user DPC context */
  	MOSAL_spinlock_t		lock;			/*  spinlock */
};

/*  Macros  */
#define MOSAL_DPC_enable(MOSAL_DPC_p)  	tasklet_enable(&MOSAL_DPC_p->tasklet)
#define MOSAL_DPC_disable(MOSAL_DPC_p)  tasklet_disable(&MOSAL_DPC_p->tasklet)


/*  TIMERs */


/*  MOSAL timer object */
struct MOSAL_timer {
	struct timer_list  	timer;			/*  OS DPC object */
};

#endif /* LINUX_KERNEL_2_4 */

#endif /* __KERNEL__ */

#if defined(powerpc) || defined(__powerpc__) 
static __inline__ unsigned int mftbu(void)
{
        unsigned int re;
        asm volatile ("mftbu %0":"=r"(re));
        return re;
}
static __inline__ unsigned int mftb(void)
{
        unsigned int re;
        asm volatile ("mftb %0":"=r"(re));
        return re;
}

static __inline__  u_int64_t  MOSAL_ppc_cputime_get(void)
{

        u_int64_t upper, lower, temp;
        do
        {
                upper = mftbu();
                lower = mftb();
                temp  = mftbu(); /*  (?? bypass Possible bug from ~0x0 to 0x0) */
        } while (temp != upper);
        return (upper << 32)| lower;

} /*  gilboa_cputime_get */

#endif /* __powerpc__ */

#endif /* H_MOSAL_TIMER_IMP_H */

