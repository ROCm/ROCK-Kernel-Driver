/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2003 Silicon Graphics, Inc. All rights reserved.
 */
#ifndef _ASM_IA64_SN_IOERROR_HANDLING_H
#define _ASM_IA64_SN_IOERROR_HANDLING_H

#include <linux/types.h>
#include <asm/sn/sgi.h>

#ifdef __KERNEL__

/*
 * Basic types required for io error handling interfaces.
 */

/*
 * Return code from the io error handling interfaces.
 */

enum error_return_code_e {
	/* Success */
	ERROR_RETURN_CODE_SUCCESS,

	/* Unknown failure */
	ERROR_RETURN_CODE_GENERAL_FAILURE,

	/* Nth error noticed while handling the first error */
	ERROR_RETURN_CODE_NESTED_CALL,

	/* State of the vertex is invalid */
	ERROR_RETURN_CODE_INVALID_STATE,

	/* Invalid action */
	ERROR_RETURN_CODE_INVALID_ACTION,

	/* Valid action but not cannot set it */
	ERROR_RETURN_CODE_CANNOT_SET_ACTION,

	/* Valid action but not possible for the current state */
	ERROR_RETURN_CODE_CANNOT_PERFORM_ACTION,

	/* Valid state but cannot change the state of the vertex to it */
	ERROR_RETURN_CODE_CANNOT_SET_STATE,

	/* ??? */
	ERROR_RETURN_CODE_DUPLICATE,

	/* Reached the root of the system critical graph */
	ERROR_RETURN_CODE_SYS_CRITICAL_GRAPH_BEGIN,

	/* Reached the leaf of the system critical graph */
	ERROR_RETURN_CODE_SYS_CRITICAL_GRAPH_ADD,

	/* Cannot shutdown the device in hw/sw */
	ERROR_RETURN_CODE_SHUTDOWN_FAILED,

	/* Cannot restart the device in hw/sw */
	ERROR_RETURN_CODE_RESET_FAILED,

	/* Cannot failover the io subsystem */
	ERROR_RETURN_CODE_FAILOVER_FAILED,

	/* No Jump Buffer exists */
	ERROR_RETURN_CODE_NO_JUMP_BUFFER
};

typedef uint64_t  error_return_code_t;

/*
 * State of the vertex during error handling.
 */
enum error_state_e {
	/* Ignore state */
	ERROR_STATE_IGNORE,

	/* Invalid state */
	ERROR_STATE_NONE,

	/* Trying to decipher the error bits */
	ERROR_STATE_LOOKUP,

	/* Trying to carryout the action decided upon after
	 * looking at the error bits 
	 */
	ERROR_STATE_ACTION,

	/* Donot allow any other operations to this vertex from
	 * other parts of the kernel. This is also used to indicate
	 * that the device has been software shutdown.
	 */
	ERROR_STATE_SHUTDOWN,

	/* This is a transitory state when no new requests are accepted
	 * on behalf of the device. This is usually used when trying to
	 * quiesce all the outstanding operations and preparing the
	 * device for a failover / shutdown etc.
	 */
	ERROR_STATE_SHUTDOWN_IN_PROGRESS,

	/* This is the state when there is absolutely no activity going
	 * on wrt device.
	 */
	ERROR_STATE_SHUTDOWN_COMPLETE,
	
	/* This is the state when the device has issued a retry. */
	ERROR_STATE_RETRY,

	/* This is the normal state. This can also be used to indicate
	 * that the device has been software-enabled after software-
	 * shutting down previously.
	 */
	ERROR_STATE_NORMAL
	
};

typedef uint64_t  error_state_t;

/*
 * Generic error classes. This is used to classify errors after looking
 * at the error bits and helpful in deciding on the action.
 */
enum error_class_e {
	/* Unclassified error */
	ERROR_CLASS_UNKNOWN,

	/* LLP transmit error */
	ERROR_CLASS_LLP_XMIT,

	/* LLP receive error */
	ERROR_CLASS_LLP_RECV,

	/* Credit error */
	ERROR_CLASS_CREDIT,

	/* Timeout error */
	ERROR_CLASS_TIMEOUT,

	/* Access error */
	ERROR_CLASS_ACCESS,

	/* System coherency error */
	ERROR_CLASS_SYS_COHERENCY,

	/* Bad data error (ecc / parity etc) */
	ERROR_CLASS_BAD_DATA,

	/* Illegal request packet */
	ERROR_CLASS_BAD_REQ_PKT,
	
	/* Illegal response packet */
	ERROR_CLASS_BAD_RESP_PKT
};

#endif /* __KERNEL__ */
#endif /* _ASM_IA64_SN_IOERROR_HANDLING_H */
