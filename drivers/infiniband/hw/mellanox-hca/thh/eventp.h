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

#ifndef H_EVENTP_H
#define H_EVENTP_H

#include <mtl_common.h>
#include <tavor_if_defs.h>
#include <vapi_types.h>
#include <hh.h>
#include <thh.h>


typedef struct THH_eventp_res_st{
  MT_phys_addr_t  cr_base;       /* physical address of the CR-space */
  u_int8_t        intr_clr_bit;  /* Bit number to clear using the interrupt clear register */
  MOSAL_IRQ_ID_t  irq;           /* IRQ line to hook interrupt handler to */
  MT_bool         is_srq_enable; /* Is SRQ supported in this FW */
} THH_eventp_res_t;



/* Mask bits from tavor_if_eventt_mask_enum_t in tavor_if_defs.h */
typedef tavor_if_eventt_mask_t THH_eventp_mtmask_t;

#define TAVOR_IF_EV_MASK_CLR_ALL(mask)  ((mask)=0)
#define TAVOR_IF_EV_MASK_SET(mask,attr) ((mask)=((mask)|(attr)))
#define TAVOR_IF_EV_MASK_CLR(mask,attr) ((mask)=((mask)&(~(attr))))
#define TAVOR_IF_EV_IS_SET(mask,attr)   (((mask)&(attr))!=0)



/************************************************************************
 *  Function: THH_eventp_create
 *  
 
    Arguments:
    version_p - Version information 
    event_res_p - See 7.2.1 THH_eventp_res_t - Event processing resources on page 63 
    cmd_if - Command interface object to use for EQ setup commands 
    kar - KAR object to use for EQ doorbells 
    eventp_p - Returned object handle 
    
    Returns:
    HH_OK 
    HH_EINVAL -Invalid parameters 
    HH_EAGAIN -Not enough resources to create object 
    
    Description: 
    Create THH_eventp object context. No EQs are set up until an event consumer registers 
    using one of the functions below. 
    
    
 ************************************************************************/
 
extern HH_ret_t THH_eventp_create ( /*IN */ THH_hob_t hob,
                                    /*IN */ THH_eventp_res_t *event_res_p, 
                                    /*IN */ THH_uar_t kar, 
                                    /*OUT*/ THH_eventp_t *eventp_p );

/************************************************************************
 *  Function: THH_eventp_destroy
 *  
 
   Arguments:
   eventp -The THH_eventp object to destroy 
   
   Returns:
   HH_OK 
   HH_EINVAL -Invalid event object handle 
   HH_ERR - internal error
   
   Description: 
   Destroy object context. If any EQs are still set they are torn-down when this 
   function is called Those EQs should generate an HCA catastrophic error ((i.e.callbacks 
   for IB compliant, proprietary and debug events are invoked) since this call implies 
   abnormal HCA closure (in a normal HCA closure all EQs should be torn-down before a 
   call to this function). 
   
   
 ************************************************************************/

extern HH_ret_t THH_eventp_destroy( /*IN */ THH_eventp_t eventp );

/************************************************************************
 *  Function: THH_eventp_setup_comp_eq
 *  
   Arguments:
   eventp -The THH_eventp object handle 
   eventh -The callback handle for events over this EQ 
   priv_context -Private context to be used in callback invocation 
   max_outs_eqe -Maximum outstanding EQEs in EQ created 
   eqn_p -Allocated EQ index 
   
   Returns:
   HH_OK
   HH_EINVAL -Invalid handle 
   HH_EAGAIN -Not enough resources available (e.g.EQC,mem- ory,etc.).
   HH_ERR - internal error
   
   Description: 
   Set up an EQ for completion events and register given handler as a callback for such events. 
   Note that the created EQ is not mapped to any event at this stage since completion events are 
   mapped using the CQC set up on CQ creation.
   
 
 ************************************************************************/

         
extern HH_ret_t THH_eventp_setup_comp_eq(/*IN */ THH_eventp_t eventp, 
                                         /*IN */ HH_comp_eventh_t eventh, 
                                         /*IN */ void *priv_context, 
                                         /*IN */ MT_size_t  max_outs_eqe, 
                                         /*OUT*/ THH_eqn_t *eqn_p );


/************************************************************************
 *  Function: 
 *  
    Arguments:
    eventp -The THH_eventp object handle 
    event_mask -Flags combination of events to map to this EQ 
    eventh -The callback handle for events over this EQ 
    priv_context -Private context to be used in callback invocation 
    max_outs_eqe -Maximum outstanding EQEs in EQ created 
    eqn_p -Allocated EQ index 
    
    Returns:
    HH_OK 
    HH_EINVAL -Invalid handle 
    HH_EAGAIN -Not enough resources available (e.g.EQC,mem- ory,etc.). 
    HH_ERR - internal error
    
    Description: 
    Set up an EQ and map events given in mask to it. Events over new EQ are reported to 
    given handler. 
    
    
 ************************************************************************/

extern HH_ret_t THH_eventp_setup_ib_eq(/*IN */ THH_eventp_t eventp, 
                                       /*IN */ HH_async_eventh_t eventh, 
                                       /*IN */ void *priv_context, 
                                       /*IN */ MT_size_t max_outs_eqe, 
                                       /*OUT*/ THH_eqn_t *eqn_p );


/************************************************************************
 *  Function: 
 *  
    Arguments:
    eventp -The THH_eventp object handle 
    max_outs_eqe -Maximum outstanding EQEs in EQ created 
    
    
    Return::
    HH_OK 
    HH_EINVAL -Invalid handle 
    HH_EAGAIN -Not enough resources available (e.g.EQC,mem- ory,etc.). 
    HH_ERR - internal error
    
    Description: 
    This function setup an EQ and maps command interface events to it. It also takes 
    care of notifying the THH_cmd associated with it (as de  ned on the THH_eventp creation)of 
    this EQ availability using the THH_cmd_set_eq() (see page 38)function.This causes the THH_cmd 
    to set event generation to given EQ for all commands dispatched after this noti  cation. The 
    THH_eventp automatically sets noti  cation of events from this EQ to the THH_cmd_eventh() 
    (see page 39)callback of assoicated THH_cmd. The function should be invoked by the THH_hob 
    in order to cause the THH_cmd associated with this eventp to execute commands using events. 
    
    
 ************************************************************************/

extern HH_ret_t THH_eventp_setup_cmd_eq ( /*IN */ THH_eventp_t eventp,
                                          /*IN */ MT_size_t max_outs_eqe);

/************************************************************************
 *  Function: 
 *  
    Arguments:
    eventp -The THH_eventp object handle 
    event_mask -Flags combination of events to map to this EQ 
    eventh -The callback handle for events over this EQ 
    priv_context -Private context to be used in callback invocation 
    max_outs_eqe -Maximum outstanding EQEs in EQ created eqn_p -Allocated EQ index 
    
    Returns:
    HH_OK 
    HH_EINVAL -Invalid handle 
    HH_EAGAIN -Not enough resources available (e.g.EQC,mem- ory,etc.). 
    HH_ERR - internal error
    
    Description: 
    Set up an EQ for reporting events beyond IB-spec.(debug events and others). All events 
    given in the event_mask are mapped to the new EQ. 
    
    
    
 ************************************************************************/

extern HH_ret_t THH_eventp_setup_mt_eq(/*IN */ THH_eventp_t eventp, 
                                       /*IN */ THH_eventp_mtmask_t event_mask, 
                                       /*IN */ THH_mlx_eventh_t eventh, 
                                       /*IN */ void *priv_context, 
                                       /*IN */ MT_size_t max_outs_eqe, 
                                       /*OUT*/ THH_eqn_t *eqn_p);


/************************************************************************
 *  Function: 
 *  
    Arguments:
    eventp 
    eq -The EQ to replace handler for 
    eventh -The new handler 
    priv_context -Private context to be used with handler
    
    Returns:
    HH_OK 
    HH_EINVAL 
    HH_ENORSC -Given EQ is not set up 
    HH_ERR - internal error
    
    Description: 
    Replace the callback function of an EQ previously set up. This may be used 
    in order to change the handler without loosing events.It retains the EQ and 
    just replaces the callback function.All EQEs polled after a return from this 
    function will be repored to the new handler.
    
 ************************************************************************/

extern HH_ret_t THH_eventp_replace_handler(/*IN */ THH_eventp_t eventp, 
                                           /*IN */ THH_eqn_t eq, 
                                           /*IN */ THH_eventp_handler_t eventh, 
                                           /*IN */ void *priv_context);

/************************************************************************
 *  Function: 
 *  
    Arguments:
    eventp -The THH_eventp object handle 
    eqn -The EQ to teardown 
    
    Returns:
    HH_OK 
    HH_EINVAL -Invalid handles (e.g.no such EQ set up) 
    HH_ERR - internal error
    
    Description: 
    This function tear down an EQ set up by one of the previous functions. Given eqn denes 
    which EQ to tear down. This teardown includes cleaning of any context relating to 
    the callback associated with it. 
    
    
 ************************************************************************/

extern HH_ret_t THH_eventp_teardown_eq(/*IN */ THH_eventp_t eventp, 
                                       /*IN */ THH_eqn_t eqn );


extern HH_ret_t THH_eventp_notify_fatal ( /*IN */ THH_eventp_t eventp,
                                   /*IN */ THH_fatal_err_t fatal_err);

extern HH_ret_t THH_eventp_handle_fatal ( /*IN */ THH_eventp_t eventp);
#endif /* H_EVENTP_H */
