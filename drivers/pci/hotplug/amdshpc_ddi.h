/* 
 * AMD Standard Hot Plug Controller Driver
 *
 * Copyright (C) 2002-2004 Advanced Micro Devices
 *
 * YOUR USE OF THIS CODE IS SUBJECT TO THE TERMS
 * AND CONDITIONS OF THE GNU GENERAL PUBLIC
 * LICENSE FOUND IN THE "GPL.TXT" FILE THAT IS
 * INCLUDED WITH THIS FILE AND POSTED AT
 * http://www.gnu.org/licenses/gpl.html
 *
 * Send feedback to <david.keck@amd.com>
 *
*/


#ifndef _SHPC_DDI_H_
#define _SHPC_DDI_H_

#include "amdshpc.h"


// ****************************************************************************
//
// hp_AddDevice()
//
// Parameters
//	shpc_context - Caller provided storage for SHPC context data (per hardware-instance).
//	driver_context - Caller provided pointer to be returned upon completion.
//	Callback - Caller provided function to be called upon completion of async requests.
//  shpc_instance - Zero-based hardware instance.
//
// Return Value
//  Status returned by any system calls made within hp_AddDevice().
//
// ****************************************************************************
long
	hp_AddDevice(
		    struct shpc_context *shpc_context,
		    void* driver_context,
		    SHPC_ASYNC_CALLBACK Callback,
		    unsigned long shpc_instance
		    );


// ****************************************************************************
//
// hp_StartDevice()
//
// Parameters
//	shpc_context - Caller provided storage for SHPC context data.
//  mmio_base_addr - from u.Memory member of CmResourceTypeMemory
//	IntVector - from u.Interrupt.Vector member of CmResourceTypeInterrupt
//	IntMode - from Flags member of CmResourceTypeInterrupt
//	IntShared - from ShareDisposition member of CmResourceTypeInterrupt
//	IntAffinity - from u.Interrupt.Affinity member of CmResourceTypeInterrupt
//
// Return Value
//  Status returned by any system calls made within hp_StartDevice().
//
// Comments:
//	The caller is responsible for mapping mmio_base_addr, via MmMapIoSpace(),
//	before calling hp_StartDevice().
//
// ****************************************************************************
long
	hp_StartDevice(
		      struct shpc_context* shpc_context
		      );


// ****************************************************************************
//
// hp_StopDevice() 
//
// Parameters
//	shpc_context - Caller provided storage for SHPC context data.
//
// Return Value
//  Status returned by any system calls made within hp_StopDevice().
//
// Comments:
//	The caller is responsible for unmapping mmio_base_addr, via MmUnmapIoSpace(),
//  after calling hp_StopDevice() for resource re-balancing or device removal.
//
// ****************************************************************************
long hp_StopDevice(struct shpc_context *shpc_context);

// ****************************************************************************
//
// hp_SuspendDevice()
//
// Parameters
//	shpc_context - Caller provided storage for SHPC context data.
//
// Return Value
//  Status returned by any system calls made within hp_SuspendDevice().
//
// Comments:
//	hp_SuspendDevice() must be called before transitioning away from PowerDeviceD0.
//
// ****************************************************************************
long hp_SuspendDevice(struct shpc_context *shpc_context);

// ****************************************************************************
//
// hp_ResumeDevice() 
//
// Parameters
//	shpc_context - Caller provided storage for SHPC context data.
//
// Return Value
//  Status returned by any system calls made within hp_ResumeDevice().
//
// Comments:
//	hp_SuspendDevice() must be called after transitioning back to PowerDeviceD0.
//
// ****************************************************************************
long hp_ResumeDevice(struct shpc_context *shpc_context);

// ****************************************************************************
//
// hp_QuerySlots() 
//
// Parameters
//	shpc_context - Caller provided storage for SHPC context data.
//	SlotConfig - Caller provided storage for slots configuration info.
//
// Return Value
//  Status returned by any system calls made within hp_QuerySlots().
//
// ****************************************************************************
long hp_QuerySlots(struct shpc_context *shpc_context, struct slot_config_info* SlotConfig);


// ****************************************************************************
//
// hp_QuerySlotStatus()
//
// Parameters
//	shpc_context - Caller provided storage for SHPC context data.
//	slot_id - Zero-based slot number (0..n-1).
//	Query - Pointer to Slot Status Structure
//
// Return Value
//  Status returned by any system calls made within hp_QuerySlotStatus().
//
// ****************************************************************************
long hp_QuerySlotStatus(struct shpc_context *shpc_context, u8 slot_id, struct slot_status_info* Query);

// ****************************************************************************
//
// hp_Queryslot_psn()
//
// Parameters
//          shpc_context - Caller provided storage for SHPC context data.
//          SlotID - Zero-based slot number (0..n-1).
//          slot_psn - Pointer to Physical Slot Number
//
// Return Value
//  STATUS_SUCCESS, or STATUS_UNSUCCESSFUL for invalid SlotID.
//
// ****************************************************************************
long hp_Queryslot_psn(struct shpc_context *shpc_context, unsigned char slot_ID, unsigned long *slot_psn);

// ****************************************************************************
//
// hp_StartAsyncRequest()
//
// Parameters
//	shpc_context - Caller provided storage for SHPC context data.
//	slot_id - Zero-based slot number (0..n-1).
//	Request - Async request: Slot "Enable/Disable", AttnLED "Attn/Normal").
//	timeout - For AttnLED "Attn" requests (in seconds)
//	request_context - Caller provided pointer to be returned upon completion.
//
// Return Value
//	STATUS_SUCCESS if the request is accepted.  The Callback() is later invoked with a completion status.
//  STATUS_UNSUCCESSFUL if the request is rejected (invalid parameters, or similar request in progress),
//
// Comment:
//	For AttnLED "Attn" requests, the completion Callback() function is invoked as soon as the hardware
//	completes (Blink) execution.  When the timeout period expires, the AttnLED is brought back to
//  its "Normal" (On/Off) state, and the Callback() is invoked once again.
//
// ****************************************************************************
long hp_StartAsyncRequest(
			 struct shpc_context *shpc_context,
			 u8 slot_id,     
			 enum shpc_async_request Request,
			 u32 timeout,                            
			 void* request_context                           
			 );


// ****************************************************************************
//
// hp_RegisterUserEvent()
//
// Parameters
//	shpc_context - Caller provided storage for SHPC context data.
//	user_event_pointer - Pointer to caller's provided EVENT object.
//
// Return Value
//	STATUS_SUCCESS if the request is accepted.
//  STATUS_UNSUCCESSFUL if the request is rejected (EVENT already registered).
//
// ****************************************************************************
long hp_RegisterUserEvent(
			 struct shpc_context *shpc_context,
			 wait_queue_head_t *user_event_pointer
			 );


// ****************************************************************************
//
// hp_UnRegisterUserEvent()
//
// Parameters
//	shpc_context - Caller provided storage for SHPC context data.
//
// Return Value
//	STATUS_SUCCESS if the request is accepted.
//  STATUS_UNSUCCESSFUL if the request is rejected (EVENT not previously registered).
//
// ****************************************************************************
long hp_UnRegisterUserEvent(struct shpc_context *shpc_context);

#endif	// _SHPC_DDI_H_

