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
#ifndef H_IB_MGT_H
#define H_IB_MGT_H

#include <mtl_common.h>
#include <ib_defs.h>
#include <vip_array.h>

#define IB_MGT_FEATURE_ECLASS_RET  /* added ECLASS return code */
#define IB_MGT_FEATURE_ESYSCALL_RET    /* added ESYSCALL return code, and allow device file open only */
                                       /* from procs get_handle, reattach_hca, and local_mad */

typedef enum {
         IB_MGT_OK = 0,              /* success */
         IB_MGT_EHNDL = -255,           /* Invalid handle */
         IB_MGT_EGEN,            /* general error */
         IB_MGT_EBUSY,           /* class already taken */
         IB_MGT_EDEV,            /* no such device */
         IB_MGT_EPORT,           /* no such port */
         IB_MGT_EAGAIN,          /* no more handles available */
         IB_MGT_ESEND,           /* could not send packet */
         IB_MGT_EINVALID_PACKET,  /* packet failed semantic checks */
         IB_MGT_TIMEOUT,         /* timeout on send completion */
         IB_MGT_EMASK_EMPTY,      /* no callbacks specified for registration */
         IB_MGT_WOULD_BLOCK,     /* Not an error.  No data to return on synchr rcv */
         IB_MGT_INTERRUPT,       /* synchronous receive was interrupted */
         IB_MGT_ECB,             /* a rcv_notify callback is registered for this mad handle:*/
                                 /*  -- Synchronous receive is not allowed */
         IB_MGT_EMALLOC,           /* MALLOC error */
         IB_MGT_ENOSYS,           /* unsupported feature */
         IB_MGT_UL_RETRY,          /* user level should retry operation immediately */
         IB_MGT_ECLASS,            /* tried to bind or unbind an illegal GMP class */
         IB_MGT_EPERM,              /* permissions error */
         IB_MGT_ESYSCALL,           /* error in an underlying O/S call */
         IB_MGT_EFATAL              /* fatal error on this mgt handle */
} IB_MGT_ret_t;

#define IB_MGT_DEFAULT_SEND_TIME/*OUT*/          0xFFFFFFFF

/* constants used to define which callbacks are being registered */
#define IB_MGT_SEND_CB_MASK  1
#define IB_MGT_RCV_CB_MASK   2

typedef  enum {
      IB_MGT_SMI,
      IB_MGT_GSI
} IB_MGT_mad_type_t;

typedef u_int32_t   IB_MGT_mad_hndl_t;

typedef enum {
    IB_MGT_OWNERSHIP_UNALLOC,
    IB_MGT_OWNERSHIP_APPLIC,
    IB_MGT_OWNERSHIP_INTERNAL
} IB_MGT_ownership_t;



typedef struct {
      u_int64_t       wrid;
      IB_lid_t        remote_lid;
      IB_sl_t         sl;
      u_int32_t       qp;
      u_int8_t        local_path_bits;
      MT_bool         grh_flag;
      IB_grh_t        grh;            /* valid if grh_flag = TRUE */
      u_int32_t       pkey_ix;        /* for GSI */
      IB_comp_status_t compl_status;
} IB_MGT_mad_rcv_desc_t;


/* IB_MGT_local_mad options*/

/* enumeration of options (effectively, bits in a bitmask) */
typedef enum {
  IB_MGT_MAD_IGNORE_MKEY       = 1  /* IB_MGT_local_mad will not validate the MKEY */
} IB_MGT_local_mad_opt_enum_t;

/* Associated "bitmask" type for packing  IB_MGT_local_mad_opt_enum_t flags */
typedef u_int32_t IB_MGT_local_mad_opt_t;


/*********************************************************************************
 *  name:    IB_MGT_mad_rcv_cb_t
 *  args:
 *       mad_hndl - handle for receiving the packet
 *       private_ctx_p - pointer to a private context, specified at callback
 *                       registration time
 *       payload_p - pointer to the received data buffer (of length 256 bytes)
 *       rcv_remote_info_p - pointer to structure containing packet meta-data 
 *                   (global routing information, addressing information for the 
 *                   remote node, and other miscellaneous information).
 *
 *  returns: 
 *      none
 *
 *  description:
 *      Is invoked when a MAD packet destined for this mad handle is received (and
 *      this rcv_notify callback was registered for the given mad handle). The
 *      payload and rcv_remote_info_p buffers must be copied in the callback, since
 *      they may be deallocated or re-used when the callback returns.
 *********************************************************************************/
typedef void (*IB_MGT_mad_rcv_cb_t)(
              /* IN*/    IB_MGT_mad_hndl_t        mad_hndl,
              /* IN*/    void*                    private_ctx_p,
              /* IN*/    void*                    payload_p,
              /* IN*/    IB_MGT_mad_rcv_desc_t    *rcv_remote_info_p
             );

/*********************************************************************************
 *  name:    IB_MGT_mad_send_cb_t
 *  args:
 *       mad_hndl - handle for receiving the packet
 *       wrid - work request id of the sent packet, provided in IB_MGT_send_mad call.
 *       status - status of the sent packet.
 *       private_ctx_p - pointer to a private context, specified at callback
 *                       registration time
 *
 *  returns: 
 *      none
 *
 *  description:
 *      Invoked if the application has registered a send callback for this mad
 *      handle, when  a send completion event has occurred for a packet sent via
 *      IB_MGT_send_mad().  The application can match 'mad_send_cb()'s to
 *      'send_mad()'s via the wrid parameter included in the mad_send_cb.
 *********************************************************************************/
typedef void (*IB_MGT_mad_send_cb_t)(
              /* IN*/    IB_MGT_mad_hndl_t    mad_hndl,
              /* IN*/    u_int64_t            wrid,
              /* IN*/    IB_comp_status_t     status,
              /* IN*/    void*                private_ctx_p
             );

/********************** IB_MGT Management API *****************************/

/*********************************************************************************
 *  name:    IB_MGT_get_handle
 *  args:
 *   dev_name - character string giving the device name
 *   port_num - port number that this mad handle will operate on, starting at 1
 *   mad_type - Type of mad handle desired (SMI or GSI)
 *   mad_hndl_p - returned handle
 *
 *  returns: 
 *      IB_MGT_OK   - success
 *      IB_MGT_EDEV - no such device
 *      IB_MGT_EPORT - invalid port number
 *      IB_MGT_EAGAIN - no more handles available
 *      IB_MGT_ESYSCALL - failure in underlying O/S call (open device file or ioctl)
 *      IB_MGT_EFATAL - a fatal error has occurred
 *
 *  description:   
 *      The application invokes this procedure to request a handle for processing
 *      MAD SMI  or GSI packets for the given device and port.  Once a handle is
 *      obtained, it may be used send packets of the given type to the fabric.  If
 *      the application desires to receive packets as well, it must bind the handle
 *      to one or more management classes (SMA and/or SM for SMI, one or more
 *      classes for GSI) using the bind functions given below.
 *********************************************************************************/
IB_MGT_ret_t IB_MGT_get_handle(
              /* IN*/    const char *             dev_name,
              /* IN*/    IB_port_t                port_num,
              /* IN*/    IB_MGT_mad_type_t        mad_type,
              /*OUT*/    IB_MGT_mad_hndl_t *      mad_hndl_p
      );


/*********************************************************************************
 *  name:    IB_MGT_bind_sm
 *  args:
 *       mad_hndl - handle of the MAD object
 *
 *  returns: 
 *      IB_MGT_OK    - success
 *      IB_MGT_EHNDL - Invalid mad handle
 *      IB_MGT_EBUSY - SM packets are already being
 *                      forwarded to another mad handle on this device and
 *                      port.
 *      IB_MGT_ESYSCALL - failure in underlying O/S call (ioctl)
 *      IB_MGT_EFATAL - a fatal error has occurred
 *
 *  description:   
 *      The application invokes this procedure to request that SM packets arriving
 *      at the given mad handle's port the be made available for processing via the
 *      mad handle.  The mad handle must be of type IB_MGT_SMI.
 *********************************************************************************/
IB_MGT_ret_t IB_MGT_bind_sm(
              /* IN*/    IB_MGT_mad_hndl_t        mad_hndl
      );


/*********************************************************************************
 *  name:    IB_MGT_unbind_sm
 *  args:
 *       mad_hndl - handle of the MAD object
 *
 *  returns: 
 *      IB_MGT_OK    - success
 *      IB_MGT_EHNDL - Invalid mad handle
 *      IB_MGT_EFATAL - a fatal error has occurred
 *      IB_MGT_ESYSCALL - failure in underlying O/S call (ioctl)
 *
 *  description:
 *      The application invokes this procedure to request that SM packets arriving
 *      at the given mad handle's port no longer be made available for processing
 *      via the mad handle.  The mad handle must be of type IB_MGT_SMI.
 *********************************************************************************/
IB_MGT_ret_t IB_MGT_unbind_sm(
              /* IN*/    IB_MGT_mad_hndl_t        mad_hndl
      );

/*********************************************************************************
 *  name:    IB_MGT_bind_sma
 *  args:
 *       mad_hndl - handle of the MAD object
 *
 *  returns: 
 *      IB_MGT_OK    - success
 *      IB_MGT_EHNDL - Invalid mad handle
 *      IB_MGT_EBUSY - SMA packets are already being
 *                      forwarded to another mad handle on this device and
 *                      port.
 *      IB_MGT_ESYSCALL - failure in underlying O/S call (ioctl)
 *      IB_MGT_EFATAL - a fatal error has occurred
 *
 *  description:   
 *      The application invokes this procedure to request that SMA packets arriving
 *      at the given mad handle's port the be made available for processing via the
 *      mad handle.  The mad handle must be of type IB_MGT_SMI.
 *********************************************************************************/
IB_MGT_ret_t IB_MGT_bind_sma(
              /* IN*/    IB_MGT_mad_hndl_t        mad_hndl
      );


/*********************************************************************************
 *  name:    IB_MGT_unbind_sma
 *  args:
 *       mad_hndl - handle of the MAD object
 *
 *  returns: 
 *      IB_MGT_OK    - success
 *      IB_MGT_EHNDL - Invalid mad handle
 *      IB_MGT_ESYSCALL - failure in underlying O/S call (ioctl)
 *      IB_MGT_EFATAL - a fatal error has occurred
 *
 *  description:
 *      The application invokes this procedure to request that SMA packets arriving
 *      at the given mad handle's port no longer be made available for processing
 *      via the mad handle.  The mad handle must be of type IB_MGT_SMI.
 *********************************************************************************/
IB_MGT_ret_t IB_MGT_unbind_sma(
              /* IN*/    IB_MGT_mad_hndl_t        mad_hndl
      );

/*********************************************************************************
 *  name:    IB_MGT_bind_gsi_class
 *  args:
 *       mad_hndl - handle of the MAD object
 *       mgt_class -management class to add to this mad handle
 *
 *  returns: 
 *      IB_MGT_OK    - success
 *      IB_MGT_EHNDL - Invalid mad handle
 *      IB_MGT_EBUSY - GSI packets of this class are already being
 *                      forwarded to another mad handle on this device and
 *                      port.
 *      IB_MGT_ECLASS  - illegal GMP class specified.
 *      IB_MGT_ESYSCALL - failure in underlying O/S call (ioctl)
 *      IB_MGT_EFATAL - a fatal error has occurred
 *
 *  description:   
 *      The application invokes this procedure to request that the given management
 *      class be added to the given mad handle for processing. GSI packets of the
 *      given class arriving at the mad handle's registered port will be made
 *      available for processing via the mad handle.  The provided mad handle must
 *      be of type IB_MGT_GSI. Subnet Management classes (0x1, 0x81) are not allowed.
 *********************************************************************************/
IB_MGT_ret_t IB_MGT_bind_gsi_class(
              /* IN*/    IB_MGT_mad_hndl_t        mad_hndl,
              /* IN*/    u_int8_t                 mgt_class
      );


/*********************************************************************************
 *  name:    IB_MGT_unbind_gsi_class
 *  args:
 *       mad_hndl - handle of the MAD object
 *       mgt_class -management class to add to this mad handle
 *
 *  returns: 
 *      IB_MGT_OK    - success
 *      IB_MGT_EHNDL - Invalid mad handle
 *      IB_MGT_ECLASS  - illegal GMP class specified.
 *      IB_MGT_ESYSCALL - failure in underlying O/S call (ioctl)
 *      IB_MGT_EFATAL - a fatal error has occurred
 *
 *  description:
 *      The application invokes this procedure to request that GSI packets of the
 *      given class no longer be made available for processing via the mad handle.
 *      The mad handle must be of type IB_MGT_GSI.  The class must be a valid GMP class.
 *********************************************************************************/
IB_MGT_ret_t IB_MGT_unbind_gsi_class(
              /* IN*/    IB_MGT_mad_hndl_t        mad_hndl,
              /* IN*/    u_int8_t                 mgt_class
      );

/*********************************************************************************
 *  name:    IB_MGT_send_mad
 *  args:
 *       mad_hndl - handle of the MAD object
 *       MAD_payload_p - pointer to the SMP/GMP payload buffer
 *       udav_p        - pointer to the UD address vector to use for sending
 *       wrid          - caller-specified work request ID.  The caller can use
 *                       this ID to match sends and send completions when using a 
 *                       send callback.
 *       timeout       - time to wait for send completion (milliseconds)
 *
 *  returns: 
 *      IB_MGT_OK    - success
 *      IB_MGT_EHNDL - Invalid mad handle
 *      IB_MGT_ESEND - could not send packet
 *      IB_MGT_INVALID_PACKET - packet failed semantic checks
 *                 associated with mad handle type (SMI or GSI).
 *      IB_MGT_TIMEOUT - timeout on send completion.
 *      IB_MGT_ESYSCALL - failure in underlying O/S call (ioctl)
 *      IB_MGT_EFATAL - a fatal error has occurred
 *
 *  description:
 *      Sends a MAD packet to the destination specified in udav_ p, using the given
 *      mad handle.  If a send callback has been registered for this mad handle,
 *      the procedure returns immediately (asynchronous operation).  If not, it
 *      sends the packet and waits up to timeout milliseconds for a completion
 *      before returning to the caller.  If timeout is zero, the procedure returns
 *      immediately, without waiting for a send completion (which is handled internally).
 *      If timeout is IB_MGT_DEFAULT_SEND_TIME, it uses a default timeout value. Note that 
 *      if a timeout is received, there is no guarantee that the packet was sent to the 
 *      fabric. 
 *      The MAD_payload_t buffer may be de-allocated when this call returns in all cases, 
 *      since the payload is copied internally for transport.
 *********************************************************************************/
IB_MGT_ret_t IB_MGT_send_mad(
              /* IN*/    IB_MGT_mad_hndl_t   mad_hndl,
              /* IN*/    void*               MAD_payload_p,
              /* IN*/    const IB_ud_av_t    *udav_p,
              /* IN*/    u_int64_t           wrid,
              /* IN*/    u_int32_t           timeout

      );

/*********************************************************************************
 *  name:    IB_MGT_send_mad_to_qp
 *  args:
 *       mad_hndl - handle of the MAD object
 *       MAD_payload_p - pointer to the SMP/GMP payload buffer
 *       udav_p        - pointer to the UD address vector to use for sending
 *       wrid          - caller-specified work request ID.  The caller can use
 *                       this ID to match sends and send completions when using a 
 *                       send callback.
 *       timeout       - time to wait for send completion (milliseconds)
 *       remote_qp     - the QP number on the remote host to which to send the payload.
 *
 *  returns: 
 *      IB_MGT_OK    - success
 *      IB_MGT_EHNDL - Invalid mad handle
 *      IB_MGT_ESEND - could not send packet
 *      IB_MGT_INVALID_PACKET - packet failed semantic checks
 *                 associated with mad handle type (SMI or GSI).
 *      IB_MGT_TIMEOUT - timeout on send completion.
 *      IB_MGT_ESYSCALL - failure in underlying O/S call (ioctl)
 *      IB_MGT_EFATAL - a fatal error has occurred
 *
 *  description:
 *      Same as for IB_MGT_send_mad, except that can specify (for GSI packets), the
 *      remote QP to which to send the packet.  When used for GSI, specifying
 *      remote QP = 0 is not legal.  Should not be used for SMI, but SMI is 
 *      supported: the remote_qp parameter is ignored in the SMI case, and the 
 *      function therefore acts identically to IB_MGT_send_mad for SMI.
 *
 *      This function should be used only for redirection in GSI.
 *********************************************************************************/
IB_MGT_ret_t IB_MGT_send_mad_to_qp(
              /* IN*/    IB_MGT_mad_hndl_t   mad_hndl,
              /* IN*/    void*               MAD_payload_p,
              /* IN*/    const IB_ud_av_t    *udav_p,
              /* IN*/    u_int64_t           wrid,
              /* IN*/    u_int32_t           timeout,
              /* IN*/    VAPI_qp_num_t       remote_qp

      );
/*********************************************************************************
 *  name:    IB_MGT_reg_cb
 *  args:
 *       mad_hndl - handle for sending the MAD packet
 *       rcv_notify_cb - function to call when a packet is received
 *       rcv_private_ctx_p - pointer to a private context, passed to rcv_notify_cb
 *       send_notify_cb - function to call when a packet is received
 *       send_private_ctx_p - pointer to a private context, passed to send_notify_cb
 *       cb_mask  - indicates which callback is valid in this call.  Must be non-zero.
 *
 *  returns: 
 *      IB_MGT_OK    - success
 *      IB_MGT_EHNDL - Invalid mad handle
 *      IB_MGT_EMASK_EMPTY
 *      IB_MGT_ESYSCALL - failure in underlying O/S call (ioctl)
 *      IB_MGT_EFATAL - a fatal error has occurred
 *
 *  description:
 *      Registers or un-registers callbacks for this mad handle.  The cb_mask
 *      indicates which callbacks are being registered or unregistered.
 *      
 *      To register, say, a send callback only, set the cb_mask to
 *      IB_MGT_SEND_CB_MASK, and provide a send_notify callback pointer and
 *      optionally a context pointer.  To un-register the send callback, set the
 *      cb_mask to IB_MGT_SEND_CB_MASK  and set the send_notify callback pointer to
 *      NULL.  New callbacks are silently re-registered (i.e., the registration is
 *      performed, replacing the previous value, and the function returns OK).
 *
 *      NOTE:  Once a receive callback is registered for a mad handle, the function
 *             IB_MGT_rcv_mad(), described below, is disabled for that mad handle.
 *********************************************************************************/
IB_MGT_ret_t IB_MGT_reg_cb(
              /* IN*/    IB_MGT_mad_hndl_t        mad_hndl,
              /* IN*/    IB_MGT_mad_rcv_cb_t      rcv_notify_cb,
              /* IN*/    void *                   rcv_private_ctx_p,
              /* IN*/    IB_MGT_mad_send_cb_t     send_notify_cb,
              /* IN*/    void *                   send_private_ctx_p,
              /* IN*/    u_int32_t                cb_mask
      );

/*********************************************************************************
 *  name:    IB_MGT_rcv_mad
 *  args:
 *       mad_hndl - handle for receiving the packet
 *       block - TRUE if caller wants the call to block until data is available.
 *       timeout - maximum time (in milliseconds) to wait before
 *                   returning to caller if no packets are received.  
 *                   If  timeout = 0, wait forever.
 *       payload_p - pointer to an empty payload buffer for
 *                   receiving the packet. The buffer must have space 
 *                   for the MAD payload (256 bytes)
 *       rcv_remote_info_p - pointer to structure for returning packet meta-data 
 *                   (global routing information, addressing information for the 
 *                   remote node, and other miscellaneous information).
 *
 *  returns: 
 *      IB_MGT_OK    - success
 *      IB_MGT_EHNDL - Invalid mad handle
 *      IB_MGT_WOULD_BLOCK - Not an error.  No data to return (block=FALSE only).
 *      IB_MGT_INTERRUPT - if call terminated because process was signaled (block = TRUE only).
 *      IB_MGT_TIMEOUT - call timed out (block = TRUE only).
 *      IB_MGT_ECB - a rcv_notify callback is registered for this mad handle
 *      IB_MGT_ESYSCALL - failure in underlying O/S call (ioctl)
 *      IB_MGT_EFATAL - a fatal error has occurred
 *
 *  description:
 *      Polls for a received MAD packet, returning immediately if the block flag is
 *      false.  If there is nothing to return, and block is FALSE, the function
 *      returns IB_MGT_WOULD_BLOCK.
 *      
 *      If block is set to TRUE, the function will block until there is data to
 *      return or the timeout value is reached.  
 *
 *      NOTE: This function is only valid if there is no receive callback 
 *            registered for this mad handle.
 *********************************************************************************/
IB_MGT_ret_t IB_MGT_rcv_mad(
              /* IN*/    IB_MGT_mad_hndl_t      mad_hndl,
              /* IN*/    MT_bool                block,
              /* IN*/    u_int32_t              timeout,
              /*OUT*/   void*                  payload_p,
              /*OUT*/   IB_MGT_mad_rcv_desc_t  *rcv_remote_info_p
      );

/*********************************************************************************
 *  name:    IB_MGT_local_mad
 *  args:
 *   dev_name - character string giving the device name
 *   port_num - port number that this mad handle will operate on, starting at 1
 *   slid - Source LID of incoming MAD. Required Mkey violation trap genenration.
 *         (this parameter is ignored if IB_MGT_MAD_IGNORE_MKEY flag is set)
 *   proc_mad_opts: Modifiers to MAD processing.
 *         currently, only modifier is : IB_MGT_MAD_IGNORE_MKEY
 *   mad_in_p - pointer to a buffer containing a MAD packet, for local processing.
 *   mad_out_p - pointer to a buffer to contain the MAD response (if any) which 
 *               is generated by the local HCA. The buffer must be at least 256 bytes.
 *
 *  returns: 
 *      IB_MGT_OK   - success
 *      IB_MGT_EDEV - no such device
 *      IB_MGT_EPORT - invalid port number
 *      IB_MGT_EGEN - general error
 *      IB_MGT_ESYSCALL - failure in underlying O/S call (ioctl)
 *      IB_MGT_EFATAL - a fatal error has occurred
 *
 *  description:  
 *      Submits a MAD packet to the HCA for processing locally.  Any response MAD
 *      is returned in the mad_out_p buffer (and not sent over the fabric).
 *********************************************************************************/
IB_MGT_ret_t IB_MGT_local_mad(
              /* IN*/    const char*     dev_name,
              /* IN*/    IB_port_t       port_num,
              /* IN */   IB_lid_t        slid, /* ignored on IB_MGT_MAD_IGNORE_MKEY option */
              /* IN */   IB_MGT_local_mad_opt_t  proc_mad_opts,
              /* IN*/    void*           mad_in_p,
              /*OUT*/    void*           mad_out_p
             );

/*********************************************************************************
 *  name:    IB_MGT_release_handle
 *  args:
 *       mad_hndl - handle of the MAD object
 *
 *  returns: 
 *      IB_MGT_OK    - success
 *      IB_MGT_EHNDL - Invalid mad handle
 *      IB_MGT_EPERM - User-space -- permissions violation
 *      IB_MGT_EGEN - an error occurred
 *      IB_MGT_ESYSCALL - failure in underlying O/S call (ioctl)
 *
 *  description:
 *      Cancels forwarding to the given mad handle, releases its resources, and
 *      returns the mad handle to the driver handle pool.  
 *
 *      Note, though that this function does NOT unbind the handle from the classes 
 *      it was registered for.  You MUST unbind a handle before releasing it.
 *********************************************************************************/
IB_MGT_ret_t IB_MGT_release_handle(
              /* IN*/    IB_MGT_mad_hndl_t        mad_hndl
      );

/*********************************************************************************
 *  name:    IB_MGT_reattach_hca
 *  args:
 *   dev_name - character string giving the device name
 *
 *  returns: 
 *      IB_MGT_OK    - success
 *      IB_MGT_EBUSY - This HCA is still active in IB_MGT
 *      IB_MGT_EDEV  - This HCA is not available on the host
 *      IB_MGT_EGEN - an error occurred
 *      IB_MGT_ESYSCALL - failure in underlying O/S call (open device file or ioctl)
 *
 *  description:
 *      Is used to re-attach an HCA to the management module following a 
 *      catastrophic failure cleanup of that HCA.  
 *********************************************************************************/
IB_MGT_ret_t IB_MGT_reattach_hca( /* IN*/ const char * dev_name);

#endif

