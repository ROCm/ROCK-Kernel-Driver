/* 
 * AMD Standard Hot Plug Controller Driver
 *
 * Copyright (C) 2001,2003 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001,2003 IBM Corp.
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

#include <linux/module.h>
#include <linux/sched.h>
#include "amdshpc_ddi.h"
#include "amdshpc.h"


// ****************************************************************************
//
// hp_wait_for_attn_led_request() @ PASSIVE_LEVEL
//
// ****************************************************************************
long
	hp_wait_for_attn_led_request(
				    struct shpc_context* shpc_context,
				    struct slot_context* slot_context
				    )
{
	unsigned long old_irq_flags;
	long status = STATUS_SUCCESS;
	struct slot_status_info slot_status;
	u32 logical_slot_reg;

	dbg("%s -->slot_id[ %d:%d ]", __FUNCTION__, shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// LED "Normal": complete pending request
	//
	if ( slot_context->attn_led_completion.done ) {
		//
		// Call Completion Callback()
		//
		hp_QuerySlotStatus(     shpc_context, slot_context->slot_number - 1, &slot_status );
		slot_status.lu_request_failed = slot_context->slot_completion.failed;
		shpc_context->async_callback(
					    shpc_context->driver_context,
					    slot_context->slot_number - 1,
					    slot_context->attn_led_completion.type,
					    slot_status,
					    slot_context->attn_led_completion.request_context );

		//
		// Signal registered user EVENT
		//
		hp_signal_user_event( shpc_context );

		//
		// Clear completion flag
		//
		slot_context->attn_led_completion.done = FALSE;
	}

	//
	// Wait for slot request
	//
	wait_event_interruptible(slot_context->slot_event,
				 ((slot_context->slot_event_bits & ATTN_LED_REQUEST_EVENT) ||
				  (slot_context->slot_event_bits & ATTN_LED_PROBLEM_EVENT) ||
				  (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)));

	// AttnLEDRequestEvent
	if (slot_context->slot_event_bits & ATTN_LED_REQUEST_EVENT) {
		//
		// Set completion info for SW-initiated request
		//
		slot_context->attn_led_completion.hw_initiated = FALSE;
		slot_context->attn_led_completion.type = slot_context->attn_led_request.type;
		slot_context->attn_led_completion.timeout = slot_context->attn_led_request.timeout;
		slot_context->attn_led_completion.request_context = slot_context->attn_led_request.request_context;

		//
		// Request to locate slot?
		//
		if ( slot_context->attn_led_request.type == SHPC_ASYNC_LED_LOCATE ) {
			dbg("%s -->LED_LOCATE_REQ: slot_id[ %d:%d ]", __FUNCTION__,
			    shpc_context->shpc_instance, slot_context->slot_number-1 );

			//
			// Grab Command MUTEX to blink Attn LED
			//
			slot_context->attn_led_function = (SLOT_STATE_FUNCTION)hp_wait_for_attn_led_blink_cmd_available;
		} else {
			dbg("%s -->LED_NORMAL_REQ: slot_id[ %d:%d ]", __FUNCTION__,
			    shpc_context->shpc_instance, slot_context->slot_number-1 );

			logical_slot_reg = readl( slot_context->logical_slot_addr );
			if ( ((logical_slot_reg & AIS_MASK) >> AIS_OFFSET) == SHPC_LED_ON || 
			     ((logical_slot_reg & AIS_MASK) >> AIS_OFFSET) == SHPC_LED_OFF ) {
				//
				// Already "Normal", just complete the request
				//
				slot_context->attn_led_completion.failed = HP_FALSE;
				slot_context->attn_led_completion.done = TRUE;
			}
			//
			// While waitimg on a request here, the Attn LED should already be On/Off, but...
			//
			else {
				//
				// Grab Command MUTEX to set Attn LED to "Normal" (On/Off) state
				//
				slot_context->attn_led_function = (SLOT_STATE_FUNCTION)hp_wait_for_attn_led_normal_cmd_available;
			}
		}

		//
		// Allow next SW-initiated request while processing this one
		//
		spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
		hp_clear_slot_event_bit(slot_context, ATTN_LED_REQUEST_EVENT);
		spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );
	}
	// attn_led_problem_event: Detected, Resolved
	else if (slot_context->slot_event_bits & ATTN_LED_PROBLEM_EVENT) {
		//
		// Set completion info for HW-initiated request
		//
		slot_context->attn_led_completion.hw_initiated = TRUE;
		slot_context->attn_led_completion.type = SHPC_ASYNC_LED_NORMAL;
		slot_context->attn_led_completion.timeout = 0;
		slot_context->attn_led_completion.request_context = NULL;

		//
		// Grab Command MUTEX to update Attention LED (On/Off)
		//
		slot_context->attn_led_function = (SLOT_STATE_FUNCTION)hp_wait_for_attn_led_normal_cmd_available;
	} else {  // exit_request_event
		status = STATUS_UNSUCCESSFUL;
		dbg("%s -->EXIT_REQUEST: slot_id[ %d:%d ]", __FUNCTION__,
		    shpc_context->shpc_instance, slot_context->slot_number-1 );
	}
	return status;
}


// ****************************************************************************
//
// hp_wait_for_attn_led_blink_cmd_available() @ PASSIVE_LEVEL
//
// ****************************************************************************
long
	hp_wait_for_attn_led_blink_cmd_available(
						struct shpc_context* shpc_context,
						struct slot_context* slot_context
						)
{
	unsigned long old_irq_flags;
	long status = STATUS_SUCCESS;
	u16 command_reg = 0;

	dbg("%s -->slot_id[ %d:%d ]", __FUNCTION__, shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Wait for Command Available MUTEX
	//
	hp_set_slot_event_bit(slot_context, LED_CMD_ACQUIRE_EVENT);
	wake_up_interruptible(&slot_context->led_cmd_acquire_event);

	wait_event_interruptible(slot_context->slot_event,
				 ((shpc_context->shpc_event_bits & LED_CMD_AVAILABLE_MUTEX_EVENT) ||
				  (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)));

	// cmd_available_mutex
	if (shpc_context->shpc_event_bits & LED_CMD_AVAILABLE_MUTEX_EVENT) {
		//
		// Clear Completion EVENT before issuing next command
		//
		spin_lock_irqsave( &shpc_context->shpc_spinlock, old_irq_flags );
		hp_clear_shpc_event_bit(shpc_context, LED_CMD_AVAILABLE_MUTEX_EVENT);
		hp_clear_shpc_event_bit(shpc_context, CMD_COMPLETION_EVENT);
		spin_unlock_irqrestore( &shpc_context->shpc_spinlock, old_irq_flags );

		//
		// Blink Attention LED
		//
		command_reg |= SHPC_SLOT_OPERATION;
		command_reg |= SHPC_PWR_LED_NO_CHANGE;
		command_reg |= SHPC_ATTN_LED_BLINK;
		command_reg |= SHPC_SLOT_NO_CHANGE;
		command_reg |= (slot_context->slot_number << SLOT_TGT_OFFSET);
		writew(command_reg, shpc_context->mmio_base_addr + SHPC_COMMAND_REG_OFFSET);

		//
		// Wait for command to complete (while holding MUTEX)
		//
		slot_context->attn_led_function = (SLOT_STATE_FUNCTION)hp_wait_for_attn_led_blink_cmd_completion;
	}
	// exit_request_event
	else {
		//
		// Release Command MUTEX
		//
		hp_set_slot_event_bit(slot_context, LED_CMD_RELEASE_EVENT);
		wake_up_interruptible(&slot_context->led_cmd_release_event);
		status = STATUS_UNSUCCESSFUL;
	}
	return status;
}


// ****************************************************************************
//
// hp_wait_for_attn_led_blink_cmd_completion() @ PASSIVE_LEVEL
//
// ****************************************************************************
long
	hp_wait_for_attn_led_blink_cmd_completion(
						 struct shpc_context* shpc_context,
						 struct slot_context* slot_context
						 )
{
	long status = STATUS_SUCCESS;
	struct slot_status_info slot_status;
	u16 status_reg;

	dbg("%s -->slot_id[ %d:%d ]", __FUNCTION__, shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Setup our timer
	//
	hp_clear_slot_event_bit(slot_context, LED_TIMER1_EVENT);
	slot_context->led_timer1.data = (unsigned long)slot_context;
	slot_context->led_timer1.function = hp_led_timer1_func;
	slot_context->led_timer1.expires = jiffies + ONE_SEC_TIMEOUT;
	add_timer(&slot_context->led_timer1);

	//
	// Wait for Command Completion EVENT while holding MUTEX
	//
	wait_event_interruptible(slot_context->slot_event,
				 ((shpc_context->shpc_event_bits & CMD_COMPLETION_EVENT) ||
				  (slot_context->slot_event_bits & LED_TIMER1_EVENT) ||
				  (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)));

	if (!(slot_context->slot_event_bits & LED_TIMER1_EVENT)) {
		//
		// delete the timer because we got an event other than the timer
		//
		del_timer_sync(&slot_context->led_timer1);
	}

	if (shpc_context->shpc_event_bits & CMD_COMPLETION_EVENT) {
		// cmd_completion_event, timeout
		if ((shpc_context->shpc_event_bits & CMD_COMPLETION_EVENT) || (slot_context->slot_event_bits & LED_TIMER1_EVENT)) {
			//
			// Command completed OK?
			//
			status_reg = readw(shpc_context->mmio_base_addr + SHPC_STATUS_REG_OFFSET);

			if ( ((status_reg & STS_BSY_MASK) >> STS_BSY_OFFSET) == SHPC_STATUS_CLEARED  &&
			     ((status_reg & INVCMD_ERR_MASK) >> INVCMD_ERR_OFFSET) == SHPC_STATUS_CLEARED ) {
				//
				// Call Completion Callback()
				//
				hp_QuerySlotStatus(     shpc_context, slot_context->slot_number - 1, &slot_status );
				slot_status.lu_request_failed = HP_FALSE;
				shpc_context->async_callback(
							    shpc_context->driver_context,
							    slot_context->slot_number - 1,
							    SHPC_ASYNC_LED_LOCATE,
							    slot_status,
							    slot_context->attn_led_completion.request_context );

				//
				// Signal registered user EVENT
				//
				hp_signal_user_event( shpc_context );

				//
				// Wait for specified timeout (in seconds)
				//
				slot_context->attn_led_function = (SLOT_STATE_FUNCTION)hp_wait_for_attn_led_blink_timeout;
			} else {
				//
				// Fail on-going request
				//
				slot_context->attn_led_completion.failed = HP_TRUE;
				slot_context->attn_led_completion.done = TRUE;

				//
				// Grab Command MUTEX to make sure Attn LED gets back to "Normal" (On/Off)
				//
				dbg("%s -->CMD_ERROR: slot_id[ %d:%d ]  Cmd[ %X ]", __FUNCTION__,
				    shpc_context->shpc_instance, slot_context->slot_number-1, status_reg );
				slot_context->attn_led_function = (SLOT_STATE_FUNCTION)hp_wait_for_attn_led_back_to_normal_cmd_available;
			}

			// exit_request_event
		} else {
			status = STATUS_UNSUCCESSFUL;
		}
	}
	//
	// Release Command MUTEX
	//
	hp_set_slot_event_bit(slot_context, LED_CMD_RELEASE_EVENT);
	wake_up_interruptible(&slot_context->led_cmd_release_event);

	return status;
}

// ****************************************************************************
//
// hp_wait_for_attn_led_blink_timeout() @ PASSIVE_LEVEL
//
// ****************************************************************************
long
	hp_wait_for_attn_led_blink_timeout(
					  struct shpc_context* shpc_context,
					  struct slot_context* slot_context
					  )
{
	unsigned long old_irq_flags;
	long status = STATUS_SUCCESS;
	struct slot_status_info slot_status;

	dbg("%s -->slot_id[ %d:%d ]", __FUNCTION__, shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Setup our timer
	//
	hp_clear_slot_event_bit(slot_context, LED_TIMER2_EVENT);
	slot_context->led_timer2.data = (unsigned long)slot_context;
	slot_context->led_timer2.function = hp_led_timer2_func;
	slot_context->led_timer2.expires = jiffies + (ONE_SEC_INCREMENT * slot_context->attn_led_completion.timeout);
	add_timer(&slot_context->led_timer2);

	//
	// Wait for specified timeout ( in seconds )
	//
	wait_event_interruptible(slot_context->slot_event,
				 ((slot_context->slot_event_bits & ATTN_LED_REQUEST_EVENT) ||
				  (slot_context->slot_event_bits & LED_TIMER2_EVENT) ||
				  (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)));

	if (!(slot_context->slot_event_bits & LED_TIMER2_EVENT)) {
		//
		// delete the timer because we got an event other than the timer
		//
		hp_clear_slot_event_bit(slot_context, LED_TIMER2_EVENT);
		del_timer_sync(&slot_context->led_timer2);
	}

	// AttnLEDRequestEvent
	if (slot_context->slot_event_bits & ATTN_LED_REQUEST_EVENT) {
		//
		// Set completion info for SW-initiated request
		//
		slot_context->attn_led_completion.hw_initiated = FALSE;
		slot_context->attn_led_completion.type = slot_context->attn_led_request.type;
		slot_context->attn_led_completion.timeout = slot_context->attn_led_request.timeout;
		slot_context->attn_led_completion.request_context = slot_context->attn_led_request.request_context;

		//
		// Request to cancel locate?
		//
		if ( slot_context->attn_led_request.type == SHPC_ASYNC_LED_NORMAL ) {
			dbg("%s -->LED_NORMAL_REQ: slot_id[ %d:%d ]", __FUNCTION__,
			    shpc_context->shpc_instance, slot_context->slot_number-1 );

			//
			// Grab Command MUTEX to set Attn LED at "Normal" (On/Off) state
			//
			slot_context->attn_led_function = (SLOT_STATE_FUNCTION)&hp_wait_for_attn_led_normal_cmd_available;

			//
			// Allow next SW-initiated request while processing this one
			//
			spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
			hp_clear_slot_event_bit(slot_context, ATTN_LED_REQUEST_EVENT);
			spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );
		}
		//
		// Already located (Attn LED blinking), just re-start timeout
		//
		else {
			dbg("%s -->LED_LOCATE_REQ: slot_id[ %d:%d ]", __FUNCTION__,
			    shpc_context->shpc_instance, slot_context->slot_number-1 );

			//
			// Allow next SW-initiated request before invoking callback, since next
			// request may be sent in the context of this thread.
			//
			spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
			hp_clear_slot_event_bit(slot_context, ATTN_LED_REQUEST_EVENT);
			spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );

			//
			// Call Completion Callback()
			//
			hp_QuerySlotStatus(     shpc_context, slot_context->slot_number - 1, &slot_status );
			slot_status.lu_request_failed = HP_FALSE;
			shpc_context->async_callback(
						    shpc_context->driver_context,
						    slot_context->slot_number - 1,
						    SHPC_ASYNC_LED_LOCATE,
						    slot_status,
						    slot_context->attn_led_completion.request_context );

			//
			// Signal registered user EVENT
			//
			hp_signal_user_event( shpc_context );
		}
	}
	// timeout
	else if (slot_context->slot_event_bits & LED_TIMER2_EVENT) {
		//
		// Set completion info for HW-initiated request
		//
		slot_context->attn_led_completion.hw_initiated = TRUE;
		slot_context->attn_led_completion.type = SHPC_ASYNC_LED_NORMAL;
		slot_context->attn_led_completion.timeout = 0;
		slot_context->attn_led_completion.request_context = NULL;

		//
		// Grab Command MUTEX to set Attn LED at "Normal" (On/Off) state
		//
		slot_context->attn_led_function = (SLOT_STATE_FUNCTION)hp_wait_for_attn_led_normal_cmd_available;

	}
	// exit_request_event
	else {
		status = STATUS_UNSUCCESSFUL;
	}
	return status;
}


// ****************************************************************************
//
// hp_wait_for_attn_led_normal_cmd_available() @ PASSIVE_LEVEL
//
// ****************************************************************************
long
	hp_wait_for_attn_led_normal_cmd_available(
						 struct shpc_context* shpc_context,
						 struct slot_context* slot_context
						 )
{
	unsigned long old_irq_flags;
	long status = STATUS_SUCCESS;
	u16 command_reg = 0;

	dbg("%s -->slot_id[ %d:%d ]", __FUNCTION__, shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Wait for Command Available MUTEX
	//
	//down_interruptible(&slot_context->cmd_acquire_mutex);
	hp_set_slot_event_bit(slot_context, LED_CMD_ACQUIRE_EVENT);
	wake_up_interruptible(&slot_context->led_cmd_acquire_event);

	wait_event_interruptible(slot_context->slot_event,
				 ((shpc_context->shpc_event_bits & LED_CMD_AVAILABLE_MUTEX_EVENT) ||
				  (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)));

	//
	// cmd_available_mutex
	//
	if (shpc_context->shpc_event_bits & LED_CMD_AVAILABLE_MUTEX_EVENT) {
		//
		// Clear Completion EVENT before issuing next command
		//
		spin_lock_irqsave( &shpc_context->shpc_spinlock, old_irq_flags );
		hp_clear_shpc_event_bit(shpc_context, LED_CMD_AVAILABLE_MUTEX_EVENT);
		hp_clear_shpc_event_bit(shpc_context, CMD_COMPLETION_EVENT);
		spin_unlock_irqrestore( &shpc_context->shpc_spinlock, old_irq_flags );

		//
		// Update Attention LED
		//
		spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
		command_reg |= (slot_context->problem_detected << ATTENTION_LED_OFFSET) ?
			       SHPC_ATTN_LED_ON : SHPC_ATTN_LED_OFF;
		hp_clear_slot_event_bit(slot_context, ATTN_LED_PROBLEM_EVENT);
		spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );
		command_reg |= SHPC_SLOT_OPERATION;
		command_reg |= SHPC_PWR_LED_NO_CHANGE;
		command_reg |= SHPC_SLOT_NO_CHANGE;
		command_reg |= (slot_context->slot_number << SLOT_TGT_OFFSET);
		writew(command_reg, shpc_context->mmio_base_addr + SHPC_COMMAND_REG_OFFSET);

		//
		// Wait for command to complete (while holding MUTEX)
		//
		slot_context->attn_led_function = (SLOT_STATE_FUNCTION)hp_wait_for_attn_led_normal_cmd_completion;
	}
	//
	// exit_request_event
	//
	else {
		//
		// Release Command MUTEX
		//
		hp_set_slot_event_bit(slot_context, LED_CMD_RELEASE_EVENT);
		wake_up_interruptible(&slot_context->led_cmd_release_event);
		status = STATUS_UNSUCCESSFUL;
	}
	return status;
}


// ****************************************************************************
//
// hp_wait_for_attn_led_normal_cmd_completion() @ PASSIVE_LEVEL
//
// ****************************************************************************
long
	hp_wait_for_attn_led_normal_cmd_completion(
						  struct shpc_context* shpc_context,
						  struct slot_context* slot_context
						  )
{
	long status = STATUS_SUCCESS;
	u16 status_reg;

	dbg("%s -->slot_id[ %d:%d ]", __FUNCTION__, shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Setup our timer
	//
	hp_clear_slot_event_bit(slot_context, LED_TIMER3_EVENT);
	slot_context->led_timer3.data = (unsigned long)slot_context;
	slot_context->led_timer3.function = hp_led_timer3_func;
	slot_context->led_timer3.expires = jiffies + ONE_SEC_TIMEOUT;
	add_timer(&slot_context->led_timer3);

	//
	// Wait for Command Completion EVENT while holding MUTEX
	//
	wait_event_interruptible(slot_context->slot_event,
				 ((shpc_context->shpc_event_bits & CMD_COMPLETION_EVENT) ||
				  (slot_context->slot_event_bits & LED_TIMER3_EVENT) ||
				  (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)));

	if (!(slot_context->slot_event_bits & LED_TIMER3_EVENT)) {
		//
		// delete the timer because we got an event other than the timer
		//
		del_timer_sync(&slot_context->led_timer3);
	}

	//
	// cmd_completion_event, timeout
	//
	if ((shpc_context->shpc_event_bits & CMD_COMPLETION_EVENT) ||
	    (slot_context->slot_event_bits & LED_TIMER3_EVENT)) {
		//
		// Command completed OK?
		//
		status_reg = readw(shpc_context->mmio_base_addr + SHPC_STATUS_REG_OFFSET);

		if ( ((status_reg & STS_BSY_MASK) >> STS_BSY_OFFSET) == SHPC_STATUS_CLEARED &&
		     ((status_reg & INVCMD_ERR_MASK) >> INVCMD_ERR_OFFSET) == SHPC_STATUS_CLEARED ) {
			//
			// Complete succesful ENABLE request
			//
			slot_context->attn_led_completion.failed = HP_FALSE;
			slot_context->attn_led_completion.done = TRUE;
			slot_context->attn_led_function = (SLOT_STATE_FUNCTION)hp_wait_for_attn_led_request;
		} else {
			//
			// Fail on-going request
			//
			slot_context->attn_led_completion.failed = HP_TRUE;
			slot_context->attn_led_completion.done = TRUE;

			//
			// Grab Command MUTEX to make sure Attn LED gets back to "Normal" (On/Off)
			//
			dbg("%s -->CMD_ERROR: slot_id[ %d:%d ]  Cmd[ %X ]", __FUNCTION__,
			    shpc_context->shpc_instance, slot_context->slot_number-1, status_reg );
			slot_context->attn_led_function = (SLOT_STATE_FUNCTION)hp_wait_for_attn_led_back_to_normal_cmd_available;
		}
	}
	//
	// exit_request_event
	//
	else {
		status = STATUS_UNSUCCESSFUL;
	}
	//
	// Release Command MUTEX
	//
	hp_set_slot_event_bit(slot_context, LED_CMD_RELEASE_EVENT);
	wake_up_interruptible(&slot_context->led_cmd_release_event);

	return status;
}


// ****************************************************************************
//
// hp_wait_for_attn_led_back_to_normal_cmd_available() @ PASSIVE_LEVEL
//
// ****************************************************************************
long
	hp_wait_for_attn_led_back_to_normal_cmd_available(
							 struct shpc_context* shpc_context,
							 struct slot_context* slot_context
							 )
{
	unsigned long old_irq_flags;
	long status = STATUS_SUCCESS;
	u16 command_reg = 0;

	dbg("%s -->slot_id[ %d:%d ]", __FUNCTION__, shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Wait for Command Available MUTEX
	//
	//down_interruptible(&slot_context->cmd_acquire_mutex);
	hp_set_slot_event_bit(slot_context, LED_CMD_ACQUIRE_EVENT);
	wake_up_interruptible(&slot_context->led_cmd_acquire_event);

	wait_event_interruptible(slot_context->slot_event,
				 ((shpc_context->shpc_event_bits & LED_CMD_AVAILABLE_MUTEX_EVENT) ||
				  (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)));

	//
	// cmd_available_mutex
	//
	if (shpc_context->shpc_event_bits & LED_CMD_AVAILABLE_MUTEX_EVENT) {
		//
		// Clear Completion EVENT before issuing next command
		//
		spin_lock_irqsave( &shpc_context->shpc_spinlock, old_irq_flags );
		hp_clear_shpc_event_bit(shpc_context, LED_CMD_AVAILABLE_MUTEX_EVENT);
		hp_clear_shpc_event_bit(shpc_context, CMD_COMPLETION_EVENT);
		spin_unlock_irqrestore( &shpc_context->shpc_spinlock, old_irq_flags );

		//
		// Update Attention LED
		//
		spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
		command_reg |= (slot_context->problem_detected << ATTENTION_LED_OFFSET) ?
			       SHPC_ATTN_LED_ON : SHPC_ATTN_LED_OFF;
		hp_clear_slot_event_bit(slot_context, ATTN_LED_PROBLEM_EVENT);
		spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );
		command_reg |= SHPC_SLOT_OPERATION;
		command_reg |= SHPC_PWR_LED_NO_CHANGE;
		command_reg |= SHPC_SLOT_NO_CHANGE;
		command_reg |= (slot_context->slot_number << SLOT_TGT_OFFSET);
		writew(command_reg, shpc_context->mmio_base_addr + SHPC_COMMAND_REG_OFFSET);

		//
		// Wait for command to complete (while holding MUTEX)
		//
		slot_context->attn_led_function = (SLOT_STATE_FUNCTION)hp_wait_for_attn_led_back_to_normal_cmd_completion;
	}
	//
	// exit_request_event
	//
	else {
		//
		// Release Command MUTEX
		//
		hp_set_slot_event_bit(slot_context, LED_CMD_RELEASE_EVENT);
		wake_up_interruptible(&slot_context->led_cmd_release_event);
		status = STATUS_UNSUCCESSFUL;
	}
	return status;
}


// ****************************************************************************
//
// hp_wait_for_attn_led_back_to_normal_cmd_completion() @ PASSIVE_LEVEL
//
// ****************************************************************************
long
	hp_wait_for_attn_led_back_to_normal_cmd_completion(
							  struct shpc_context* shpc_context,
							  struct slot_context* slot_context
							  )
{
	long status = STATUS_SUCCESS;

	dbg("%s -->slot_id[ %d:%d ]", __FUNCTION__, shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Setup our timer
	//
	hp_clear_slot_event_bit(slot_context, LED_TIMER4_EVENT);
	slot_context->led_timer4.data = (unsigned long)slot_context;
	slot_context->led_timer4.function = hp_led_timer4_func;
	slot_context->led_timer4.expires = jiffies + ONE_SEC_TIMEOUT;
	add_timer(&slot_context->led_timer4);

	//
	// Wait for Command Completion EVENT while holding MUTEX
	//
	wait_event_interruptible(slot_context->slot_event,
				 ((shpc_context->shpc_event_bits & CMD_COMPLETION_EVENT) ||
				  (slot_context->slot_event_bits & LED_TIMER4_EVENT) ||
				  (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)));

	if (!(slot_context->slot_event_bits & LED_TIMER4_EVENT)) {
		//
		// delete the timer because we got an event other than the timer
		//
		del_timer_sync(&slot_context->led_timer4);
	}

	//
	// cmd_completion_event, timeout
	//
	if ((shpc_context->shpc_event_bits & CMD_COMPLETION_EVENT) ||
	    (slot_context->slot_event_bits & LED_TIMER4_EVENT)) {
		slot_context->attn_led_function = (SLOT_STATE_FUNCTION)hp_wait_for_attn_led_request;
	}
	//
	// exit_request_event
	//
	else {
		status = STATUS_UNSUCCESSFUL;
	}
	//
	// Release Command MUTEX
	//
	hp_set_slot_event_bit(slot_context, LED_CMD_RELEASE_EVENT);
	wake_up_interruptible(&slot_context->led_cmd_release_event);

	return status;
}
