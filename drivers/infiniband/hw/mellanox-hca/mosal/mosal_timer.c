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

#include "mosal_priv.h"



#if LINUX_KERNEL_2_4 || LINUX_KERNEL_2_6
/* This code is only for kernels 2.4 and 2.6 */

/* IRQ back compatibility */
#ifndef IRQ_HANDLED
  typedef void irqreturn_t;
  #define IRQ_HANDLED
#endif



////////////////////////////////////////////////////////////////////////////////
// Interrupt functions
////////////////////////////////////////////////////////////////////////////////


static irqreturn_t generic_isr(int irq, void *dev_id, MOSAL_intr_regs_t *regs)
{
  MOSAL_ISR_t * isr_p = (MOSAL_ISR_t *)dev_id;

  MTL_DEBUG6("generic_isr: Got IRQ=%d - will forward to  to handler %p (ctx " VIRT_ADDR_FMT ")\n",
             isr_p->irq,isr_p->func,isr_p->ctx);

  // call user's ISR
  if (isr_p->func)
    (isr_p->func)(isr_p->ctx, NULL, NULL);

  return IRQ_HANDLED;
}


/*
 * Connect interrupt handler 
 */
call_result_t MOSAL_ISR_set(
                           MOSAL_ISR_t *       isr_p,
                           MOSAL_ISR_func_t    handler,
                           MOSAL_IRQ_ID_t      irq,
                           char *              name,
                           MT_ulong_ptr_t              ctx
                           )
{
  call_result_t rc = MT_OK;
  int name_sz = strlen(name) + 1;
  int           rrc;

  MTL_TRACE1("-> MOSAL_ISR_set handler=%p, irq=%d, ctx=" VIRT_ADDR_FMT "\n",
             handler, irq, ctx);

  // allocate memory
  isr_p->name = MALLOC( name_sz );
  if (!isr_p->name) {
    rc = MT_EMALLOC;
    goto exit;
  }

  // store parameters in the object
  isr_p->func     = handler;
  isr_p->irq      = irq;
  isr_p->ctx      = ctx;
  memcpy(isr_p->name, name, name_sz);

  // connect intermediate ISR
  /* The IRQ is shared by default, since other device drivers (or apps.) may share the IRQ */
  /* The intr. handler is "slow" since we assume complicated event classification */
  if ((rrc = request_irq(irq, generic_isr, SA_SHIRQ , name, isr_p)) < 0)
    rc = (rrc == -EBUSY ? MT_EBUSY : MT_ERROR);

  exit:
  MTL_TRACE1("<- MOSAL_ISR_set rc=%d (%s)\n\n",
             rc, mtl_strerror_sym(rc));
  return rc;
}


/*
 * Disconnect interrupt handler 
 */
call_result_t MOSAL_ISR_unset( MOSAL_ISR_t * isr_p )
{
  call_result_t rc = MT_OK;;

  MTL_TRACE1("-> MOSAL_ISR_unset handler=%p, irq=%d, ctx=" VIRT_ADDR_FMT "\n",
             isr_p->func, isr_p->irq, isr_p->ctx);

  free_irq(isr_p->irq, isr_p);

  // free memory
  if ( isr_p->name ) {
    FREE( isr_p->name );
    isr_p->name = NULL;
  }

  MTL_TRACE1("<- MOSAL_ISR_unset rc=%d (%s)\n\n",
             rc, mtl_strerror_sym(rc));
  return rc;
}

/*
 * Interrupt handler registration
 */
call_result_t MOSAL_set_intr_handler(intr_handler_t handler,
                                     MOSAL_IRQ_ID_t irq,
                                     char *name,
                                     void* dev_id)
{
  call_result_t rc = MT_OK;
  int           rrc;

  MTL_TRACE1("-> MOSAL_set_intr_handler handler=%p, irq=%d, dev_id=%p\n",
             handler, irq, dev_id);

  if (dev_id == NULL)  return MT_EINVAL;  /* must provide dev_id for interrupt sharing */
  /* The IRQ is shared by default, since other device drivers (or apps.) may share the IRQ */
  /* The intr. handler is "slow" since we assume complicated event classification */
  if ((rrc = request_irq(irq, handler, SA_SHIRQ , name, dev_id)) < 0)
    rc = (rrc == -EBUSY ? MT_EBUSY : MT_ERROR);

  MTL_TRACE1("<- MOSAL_set_intr_handler rc=%d (%s)\n\n",
             rc, mtl_strerror_sym(rc));
  return rc;
}

call_result_t MOSAL_unset_intr_handler(intr_handler_t handler,
                                       MOSAL_IRQ_ID_t irq,
                                       void* dev_id)
{
  call_result_t rc = MT_OK;;

  MTL_TRACE1("-> MOSAL_unset_intr_handler handler=%p, irq=%d, dev_id=%p\n",
             handler, irq, dev_id);

  free_irq(irq, dev_id);

  MTL_TRACE1("<- MOSAL_unset_intr_handler rc=%d (%s)\n\n",
             rc, mtl_strerror_sym(rc));
  return rc;
}


////////////////////////////////////////////////////////////////////////////////
// Tasklet functions
////////////////////////////////////////////////////////////////////////////////

static void generic_dpc( unsigned long data )
{
  MOSAL_DPC_t *d = (MOSAL_DPC_t *)data;

  MTL_TRACE1("-> generic_dpc: DPC object=%p\n",d);

  // call user's DPC
  if (d->func)
    (d->func)(&d->dpc_ctx);

  MTL_TRACE1("<- generic_dpc: DPC object=%p\n",d);
}


void MOSAL_DPC_init(MOSAL_DPC_t *d, MOSAL_DPC_func_t func, MT_ulong_ptr_t data, MOSAL_DPC_type_t type)
{
  // make Linux tasklet for intermediate routine
  DECLARE_TASKLET(tasklet, (void*)generic_dpc, (unsigned long)d);

  MTL_TRACE1("-> MOSAL_DPC_init: DPC object=%p, func %p, ctx %p, type %d\n",
             d, func, (void*)data, (int)type);

  // init MOSAL DPC object
  d->func             = func;
  d->dpc_ctx.func_ctx = data;
  d->type             = type;
  MOSAL_spinlock_init( &d->lock );

  // init OS DPC object with generic DPC instead of user one
  memcpy( (char *)&d->tasklet, (char *)&tasklet, sizeof(struct tasklet_struct) ); 

  MTL_TRACE1("<- MOSAL_DPC_init\n");
}

MT_bool  MOSAL_DPC_schedule(MOSAL_DPC_t *d)
{
  MT_bool res;
  MTL_TRACE1("-> MOSAL_DPC_schedule: DPC object=%p\n", d);
  MOSAL_spinlock_irq_lock( &d->lock );
  if (test_bit(TASKLET_STATE_SCHED,&d->tasklet.state)) {
  	res = FALSE;
  }
  else {
	tasklet_schedule( &d->tasklet );
  	res = TRUE;
  }	
  MOSAL_spinlock_unlock( &d->lock );
  return res;
}

MT_bool  MOSAL_DPC_schedule_ctx(MOSAL_DPC_t *d, void * ctx1, void * ctx2)
{
  MT_bool res;
  MTL_TRACE1("-> MOSAL_DPC_schedule_ctx: DPC object=%p, ctx1 %p, ctx2 %p\n", d, ctx1, ctx2);
  MOSAL_spinlock_irq_lock( &d->lock );
  if (test_bit(TASKLET_STATE_SCHED,&d->tasklet.state)) {
  	res = FALSE;
  }
  else	{
	d->dpc_ctx.isr_ctx1 = ctx1;
  	d->dpc_ctx.isr_ctx2 = ctx2;
	tasklet_schedule( &d->tasklet );
  	res = TRUE;
  }
  MOSAL_spinlock_unlock( &d->lock );
  return res;
}

call_result_t MOSAL_DPC_add_ctx(MOSAL_DPC_t *d, void * ctx1, void * ctx2)
{
  return MT_OK;
}

////////////////////////////////////////////////////////////////////////////////
// Timer functions 
////////////////////////////////////////////////////////////////////////////////

__INLINE__ void MOSAL_timer_init(MOSAL_timer_t *t)
{
  init_timer( &t->timer );
}

__INLINE__ void MOSAL_timer_del(MOSAL_timer_t *t)
{
  del_timer_sync( &t->timer );
}

__INLINE__ void MOSAL_timer_add(MOSAL_timer_t *t, MOSAL_DPC_func_t func, MT_ulong_ptr_t data, long usecs)
{
  struct timespec ts;
  unsigned long expires;
  ts.tv_sec   = usecs / 1000000L;
  ts.tv_nsec  = (usecs % 1000000L) * 1000;
  expires     = jiffies + timespec_to_jiffies( &ts );

  // fill the LINUX timer object 
  t->timer.function   = (void*)func;
  t->timer.data       = (unsigned long)data;
  t->timer.expires    = expires;

  // start timer
  add_timer( &t->timer );
}

__INLINE__ void MOSAL_timer_mod(MOSAL_timer_t *t, long usecs)
{
  struct timespec ts;
  unsigned long expires;
  ts.tv_sec   = usecs / 1000000L;
  ts.tv_nsec  = (usecs % 1000000L) * 1000;
  expires     = jiffies + timespec_to_jiffies( &ts );

  // restart timer
  mod_timer( &t->timer, expires );
}


////////////////////////////////////////////////////////////////////////////////
// Time functions 
////////////////////////////////////////////////////////////////////////////////

void MOSAL_time_get_clock(MOSAL_timespec_t *ts)
{
  jiffies_to_timespec( jiffies, ts);
}



#endif
