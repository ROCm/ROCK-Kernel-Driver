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
// hp_at_slot_disabled_wait_for_slot_request()
//
// ****************************************************************************
long
	hp_at_slot_disabled_wait_for_slot_request(
						 struct shpc_context* shpc_context,
						 struct slot_context* slot_context
						 )
{
	unsigned long   old_irq_flags;
	long status = STATUS_SUCCESS;
	struct slot_status_info slot_status;
	u32 logical_slot_reg;

	dbg("%s -->slot_id[ %d:%d ]", __FUNCTION__ ,shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Slot Disabled: complete pending slot request
	//
	if ( slot_context->slot_completion.done ) {
		dbg("%s -->DISABLE_DONE: slot_id[ %d:%d ]",__FUNCTION__ ,
		    shpc_context->shpc_instance, slot_context->slot_number-1 );
		//
		// Call Completion Callback()
		//
		hp_QuerySlotStatus(     shpc_context, slot_context->slot_number - 1, &slot_status );
		slot_status.lu_request_failed = slot_context->slot_completion.failed;
		shpc_context->async_callback(
					    shpc_context->driver_context,
					    slot_context->slot_number - 1,
					    slot_context->slot_completion.type,
					    slot_status,
					    slot_context->slot_completion.request_context );

		//
		// Signal registered user EVENT
		//
		hp_signal_user_event( shpc_context );

		//
		// Clear completion flag
		//
		slot_context->slot_completion.done = FALSE;
	}

	//
	// Clear Button EVENT before waiting
	//
	spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
	hp_clear_slot_event_bit(slot_context, ATTN_BUTTON_EVENT);
	spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );

	//
	// Wait for slot request
	//
	wait_event_interruptible(slot_context->slot_event,
				 ((slot_context->slot_event_bits & ATTN_BUTTON_EVENT) ||
				  (slot_context->slot_event_bits & SLOT_REQUEST_EVENT) ||
				  (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)));

	//
	// attn_button_event
	//
	if (slot_context->slot_event_bits & ATTN_BUTTON_EVENT) {
		//
		// Set completion info for HW-initiated request
		//
		slot_context->slot_completion.hw_initiated = TRUE;
		slot_context->slot_completion.type = SHPC_ASYNC_ENABLE_SLOT;
		slot_context->slot_completion.request_context = NULL;

		//
		// Get current HW disposition
		//
		logical_slot_reg = readl( slot_context->logical_slot_addr );
		//
		// Card present, MRL closed, and no Power-Fault?
		//
		if ( ((logical_slot_reg & PRSNT1_2_MASK) >> PRSNT1_2_OFFSET) != SHPC_SLOT_EMPTY &&
		     (((logical_slot_reg & MRLS_IM_MASK) >> MRLS_IM_OFFSET) == SHPC_MASKED ||
		      ((logical_slot_reg & MRLS_MASK) >> MRLS_OFFSET) == SHPC_MRL_CLOSED) &&
		     ((logical_slot_reg & PF_MASK) >> PF_OFFSET) == SHPC_STATUS_CLEARED ) {

			//
			// Clear Alert EVENT and Attention LED
			//
			spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
			hp_clear_slot_event_bit(slot_context, ALERT_EVENT);
			slot_context->problem_detected = FALSE;
			hp_send_slot_event(slot_context, ATTN_LED_PROBLEM_EVENT);
			spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );

			//
			// Grab Command MUTEX to blink Power LED
			//
			dbg("%s -->ENABLE_REQ: slot_id[ %d:%d ]",__FUNCTION__ ,
			    shpc_context->shpc_instance, slot_context->slot_number-1 );
			slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_disabled_wait_for_led_cmd_available;
		}
		//
		// Alert: MRL Opened, Power-Fault?
		//
		else if ( ((logical_slot_reg & PRSNT1_2_MASK) >> PRSNT1_2_OFFSET) != SHPC_SLOT_EMPTY ) {
			//
			// Update Attention LED
			//
			spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
			slot_context->problem_detected = TRUE;
			hp_send_slot_event(slot_context, ATTN_LED_PROBLEM_EVENT);
			spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );

			//
			// Fail on-going request
			//
			slot_context->slot_completion.failed = HP_TRUE;
			slot_context->slot_completion.done = TRUE;
			dbg("%s -->ALERT: slot_id[ %d:%d ]  LSR_13:0[ %X ]",__FUNCTION__ ,
			    shpc_context->shpc_instance, slot_context->slot_number-1,
			    logical_slot_reg & 0x3F );
		}
	}
	//
	// SlotRequestEvent
	//
	else if (slot_context->slot_event_bits & SLOT_REQUEST_EVENT) {
		//
		// Set completion info for SW-initiated request
		//
		slot_context->slot_completion.hw_initiated = FALSE;
		slot_context->slot_completion.type = slot_context->slot_request.type;
		slot_context->slot_completion.request_context = slot_context->slot_request.request_context;

		//
		// Request to enable slot?
		//
		if ( slot_context->slot_request.type == SHPC_ASYNC_ENABLE_SLOT ) {
			//
			// Update alert events based on current HW disposition
			//
			logical_slot_reg = readl( slot_context->logical_slot_addr );
			//
			// Card present, MRL closed, and no Power-Fault?
			//
			if ( ((logical_slot_reg & PRSNT1_2_MASK) >> PRSNT1_2_OFFSET) != SHPC_SLOT_EMPTY &&
			     (((logical_slot_reg & MRLS_IM_MASK) >> MRLS_IM_OFFSET) == SHPC_MASKED ||
			      ((logical_slot_reg & MRLS_MASK) >> MRLS_OFFSET) == SHPC_MRL_CLOSED) &&
			     ((logical_slot_reg & PF_MASK) >> PF_OFFSET) == SHPC_STATUS_CLEARED ) {
				//
				// Clear Alert EVENT and Attention LED
				//
				spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
				slot_context->problem_detected = FALSE;
				hp_clear_slot_event_bit(slot_context, ALERT_EVENT);
				hp_send_slot_event(slot_context, ATTN_LED_PROBLEM_EVENT);
				spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );

				//
				// Grab Command MUTEX to blink Power LED
				//
				dbg("%s -->ENABLE_REQ: slot_id[ %d:%d ]",__FUNCTION__ ,
				    shpc_context->shpc_instance, slot_context->slot_number-1 );
				slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_disabled_wait_for_led_cmd_available;
			}
			//
			// Alert: MRL Opened, Power-Fault?
			//
			else if ( ((logical_slot_reg & PRSNT1_2_MASK) >> PRSNT1_2_OFFSET) != SHPC_SLOT_EMPTY ) {
				//
				// Update Attention LED
				//
				spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
				slot_context->problem_detected = TRUE;
				hp_send_slot_event(slot_context, ATTN_LED_PROBLEM_EVENT);
				spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );

				//
				// Fail on-going request
				//
				slot_context->slot_completion.failed = HP_TRUE;
				slot_context->slot_completion.done = TRUE;
				dbg("%s -->ALERT: slot_id[ %d:%d ]  LSR_13:0[ %X ]",__FUNCTION__ ,
				    shpc_context->shpc_instance, slot_context->slot_number-1,
				    logical_slot_reg & 0x3F );
			}
		} else {
			//
			// Slot already disabled, just complete the request
			//
			dbg("%s -->DISABLE_REQ: slot_id[ %d:%d ]",__FUNCTION__ ,
			    shpc_context->shpc_instance, slot_context->slot_number-1 );
			slot_context->slot_completion.failed = HP_FALSE;
			slot_context->slot_completion.done = TRUE;
		}

		//
		// Allow next SW-initiated slot request while processing this one
		//
		spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
		hp_clear_slot_event_bit(slot_context, SLOT_REQUEST_EVENT);
		spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );
	}
	//
	// exit_request_event
	//
	else {
		status = STATUS_UNSUCCESSFUL;
	}
	return status;
}


// ****************************************************************************
//
// hp_at_slot_disabled_wait_for_led_cmd_available()
//
// ****************************************************************************
long
	hp_at_slot_disabled_wait_for_led_cmd_available(
						      struct shpc_context* shpc_context,
						      struct slot_context* slot_context
						      )
{
	unsigned long           old_irq_flags;
	long status = STATUS_SUCCESS;
	u16 command_reg = 0;

	dbg("%s -->slot_id[ %d:%d ]",__FUNCTION__ , shpc_context->shpc_instance, slot_context->slot_number-1 );
	//
	// Wait for Command Available MUTEX
	//
	hp_set_slot_event_bit(slot_context, CMD_ACQUIRE_EVENT);
	wake_up_interruptible(&slot_context->cmd_acquire_event);

	wait_event_interruptible(slot_context->slot_event,
				 ((shpc_context->shpc_event_bits & CMD_AVAILABLE_MUTEX_EVENT) ||
				  (slot_context->slot_event_bits & ALERT_EVENT) ||
				  (slot_context->slot_event_bits & SLOT_REQUEST_EVENT) ||
				  (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)));

	//
	// Alert: MRL Opened, Card Removed, Power-Fault?
	//
	if (slot_context->slot_event_bits & ALERT_EVENT) {

		//
		// Update attn_led_problem_event LED
		//
		spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
		slot_context->problem_detected = TRUE;
		hp_send_slot_event(slot_context, ATTN_LED_PROBLEM_EVENT);
		spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );

		//
		// Release Command MUTEX
		//
		hp_set_slot_event_bit(slot_context, CMD_RELEASE_EVENT);
		wake_up_interruptible(&slot_context->cmd_release_event);

		//
		// Fail on-going request
		//
		slot_context->slot_completion.failed = HP_TRUE;
		slot_context->slot_completion.done = TRUE;

		//
		// Wait for next request
		//
		dbg("%s -->ALERT: slot_id[ %d:%d ]  LSR_13:0[ %X ]",__FUNCTION__ ,
		    shpc_context->shpc_instance, slot_context->slot_number-1,
		    (readl( slot_context->logical_slot_addr ) & 0x3F ));
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_disabled_wait_for_slot_request;
	}
	//
	// cmd_available_mutex
	//
	else if (shpc_context->shpc_event_bits & CMD_AVAILABLE_MUTEX_EVENT) {
		//
		// Clear Completion EVENT before issuing next command
		//
		spin_lock_irqsave( &shpc_context->shpc_spinlock, old_irq_flags );
		hp_clear_shpc_event_bit(shpc_context, CMD_AVAILABLE_MUTEX_EVENT);
		hp_clear_shpc_event_bit(shpc_context, CMD_COMPLETION_EVENT);
		spin_unlock_irqrestore( &shpc_context->shpc_spinlock, old_irq_flags );

		//
		// Blink Power LED
		//
		command_reg |= SHPC_SLOT_OPERATION;
		command_reg |= SHPC_PWR_LED_BLINK;
		command_reg |= SHPC_ATTN_LED_NO_CHANGE;
		command_reg |= SHPC_SLOT_NO_CHANGE;
		command_reg |= (slot_context->slot_number << SLOT_TGT_OFFSET);
		writew(command_reg ,shpc_context->mmio_base_addr + SHPC_COMMAND_REG_OFFSET);

		//
		// Wait for command to complete (while holding MUTEX)
		//
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_disabled_wait_for_led_cmd_completion;
	}
	//
	// exit_request_event
	//
	else {
		//
		// Release Command MUTEX
		//
		hp_set_slot_event_bit(slot_context, CMD_RELEASE_EVENT);
		wake_up_interruptible(&slot_context->cmd_release_event);
		status = STATUS_UNSUCCESSFUL;
	}
	return status;
}


// ****************************************************************************
//
// hp_at_slot_disabled_wait_for_led_cmd_completion()
//
// ****************************************************************************
long
	hp_at_slot_disabled_wait_for_led_cmd_completion(
						       struct shpc_context* shpc_context,
						       struct slot_context* slot_context
						       )
{
	unsigned long   old_irq_flags;
	long status = STATUS_SUCCESS;
	u16 status_reg;

	dbg( "%s -->slot_id[ %d:%d ]",__FUNCTION__ , shpc_context->shpc_instance, slot_context->slot_number-1 );
	//
	// Setup our timer
	//
	hp_clear_slot_event_bit(slot_context, SLOT_TIMER1_EVENT);
	slot_context->slot_timer1.data = (unsigned long)slot_context;
	slot_context->slot_timer1.function = hp_slot_timer1_func;
	slot_context->slot_timer1.expires = jiffies + ONE_SEC_TIMEOUT;
	add_timer(&slot_context->slot_timer1);

	//
	// Wait for Command Completion EVENT while holding MUTEX
	//
	wait_event_interruptible(slot_context->slot_event,
				 ((shpc_context->shpc_event_bits & CMD_COMPLETION_EVENT) ||
				  (slot_context->slot_event_bits & ALERT_EVENT) ||
				  (slot_context->slot_event_bits & SLOT_TIMER1_EVENT) ||
				  (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)));

	if (!(slot_context->slot_event_bits & SLOT_TIMER1_EVENT)) {
		//
		// delete the timer because we got an event other than the timer
		//
		del_timer_sync(&slot_context->slot_timer1);
	}
	//
	// Alert: MRL Opened, Card Removed, Power-Fault?
	//
	if (slot_context->slot_event_bits & ALERT_EVENT) {

		//
		// Update attn_led_problem_event LED
		//
		spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
		slot_context->problem_detected = TRUE;
		hp_send_slot_event(slot_context, ATTN_LED_PROBLEM_EVENT);
		spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );

		//
		// Fail on-going request
		//
		slot_context->slot_completion.failed = HP_TRUE;
		slot_context->slot_completion.done = TRUE;

		//
		// Grab Command MUTEX to make sure Power LED is OFF
		//
		dbg("%s -->ALERT: slot_id[ %d:%d ]  LSR_13:0[ %X ]",__FUNCTION__ ,
		    shpc_context->shpc_instance, slot_context->slot_number-1,
		    readl( slot_context->logical_slot_addr ) & 0x3F );
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_to_slot_disabled_wait_for_led_cmd_available;
	}
	//
	// cmd_completion_event, timeout
	//
	else if ((shpc_context->shpc_event_bits & CMD_COMPLETION_EVENT) ||
		 (slot_context->slot_event_bits & SLOT_TIMER1_EVENT)) {
		//
		// Command completed OK?
		//
		status_reg = readw(shpc_context->mmio_base_addr + SHPC_STATUS_REG_OFFSET);

		if ( ((status_reg & STS_BSY_MASK) >> STS_BSY_OFFSET) == SHPC_STATUS_CLEARED &&
		     ((status_reg & INVCMD_ERR_MASK) >> INVCMD_ERR_OFFSET) == SHPC_STATUS_CLEARED ) {
			//
			// Allow cancellation of operation?
			//
			if ( slot_context->slot_completion.hw_initiated ) {
				//
				// Wait for 5 sec timeout
				//
				slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_disabled_wait_for_timeout;
			} else {
				//
				// Grab Command MUTEX to power-on the slot
				//
				slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_disabled_wait_for_power_cmd_available;
			}
		} else {
			//
			// Fail on-going request
			//
			slot_context->slot_completion.failed = HP_TRUE;
			slot_context->slot_completion.done = TRUE;

			//
			// Grab Command MUTEX to make sure Power LED is OFF
			//
			dbg("%s -->CMD_ERROR: slot_id[ %d:%d ]  Cmd[ %X ]",__FUNCTION__ ,
			    shpc_context->shpc_instance, slot_context->slot_number-1, status_reg );
			slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_to_slot_disabled_wait_for_led_cmd_available;
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
	hp_set_slot_event_bit(slot_context, CMD_RELEASE_EVENT);
	wake_up_interruptible(&slot_context->cmd_release_event);

	return status;
}


// ****************************************************************************
//
// hp_at_slot_disabled_wait_for_timeout()
//
// ****************************************************************************
long
	hp_at_slot_disabled_wait_for_timeout(
					    struct shpc_context* shpc_context,
					    struct slot_context* slot_context
					    )
{
	unsigned long           old_irq_flags;
	long status = STATUS_SUCCESS;

	dbg("%s -->slot_id[ %d:%d ]", __FUNCTION__ ,shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Clear Button EVENT before waiting
	//
	spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
	hp_clear_slot_event_bit(slot_context, ATTN_BUTTON_EVENT);
	spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );
	//
	// Setup our timer
	//
	hp_clear_slot_event_bit(slot_context, SLOT_TIMER2_EVENT);
	slot_context->slot_timer2.data = (unsigned long)slot_context;
	slot_context->slot_timer2.function = hp_slot_timer2_func;
	slot_context->slot_timer2.expires = jiffies + FIVE_SEC_TIMEOUT;
	add_timer(&slot_context->slot_timer2);

	//
	// Wait for 5 sec timeout
	//
	wait_event_interruptible(slot_context->slot_event,
				 ((slot_context->slot_event_bits & ATTN_BUTTON_EVENT) ||
				  (slot_context->slot_event_bits & ALERT_EVENT) ||
				  (slot_context->slot_event_bits & SLOT_TIMER2_EVENT) ||
				  (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)));

	if (!(slot_context->slot_event_bits & SLOT_TIMER2_EVENT)) {
		//
		// delete the timer because we got an event other than the timer
		//
		del_timer_sync(&slot_context->slot_timer2);
	}

	//
	// Alert: MRL Opened, Card Removed, Power-Fault?
	//
	if (slot_context->slot_event_bits & ALERT_EVENT) {

		//
		// Update attn_led_problem_event LED
		//
		spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
		slot_context->problem_detected = TRUE;
		hp_send_slot_event(slot_context, ATTN_LED_PROBLEM_EVENT);
		spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );

		//
		// Fail on-going request
		//
		slot_context->slot_completion.failed = HP_TRUE;
		slot_context->slot_completion.done = TRUE;

		//
		// Grab Command MUTEX to turn OFF Power LED
		//
		dbg("%s -->ALERT: slot_id[ %d:%d ]  LSR_13:0[ %X ]",__FUNCTION__ ,
		    shpc_context->shpc_instance, slot_context->slot_number-1,
		    readl( slot_context->logical_slot_addr ) & 0x3F );
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_to_slot_disabled_wait_for_led_cmd_available;
	}
	//
	// attn_button_event
	//
	else if (slot_context->slot_event_bits & ATTN_BUTTON_EVENT) {
		//
		// Cancel request, grab Command MUTEX to turn OFF Power LED
		//
		dbg("%s -->ENABLE_REQ: slot_id[ %d:%d ] Cancelled: Attn Button",__FUNCTION__ ,
		    shpc_context->shpc_instance, slot_context->slot_number-1 );
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_to_slot_disabled_wait_for_led_cmd_available;
	}
	//
	// timeout
	//
	else if (slot_context->slot_event_bits & SLOT_TIMER2_EVENT) {
		//
		// Grab Command MUTEX to set slot at Power-Only state
		//
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_disabled_wait_for_power_cmd_available;
	}
	//
	// exit_request_event
	//
	else {
		status = STATUS_UNSUCCESSFUL;
	}
	return status;
}


// ****************************************************************************
//
// hp_at_slot_disabled_wait_for_power_cmd_available()
//
// ****************************************************************************
long
	hp_at_slot_disabled_wait_for_power_cmd_available(
							struct shpc_context* shpc_context,
							struct slot_context* slot_context
							)
{
	unsigned long           old_irq_flags;
	long status = STATUS_SUCCESS;
	u16 command_reg = 0;

	dbg("%s -->slot_id[ %d:%d ]",__FUNCTION__ , shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Wait for Command Available MUTEX
	//
	hp_set_slot_event_bit(slot_context, CMD_ACQUIRE_EVENT);
	wake_up_interruptible(&slot_context->cmd_acquire_event);

	wait_event_interruptible(slot_context->slot_event,
				 ((shpc_context->shpc_event_bits & CMD_AVAILABLE_MUTEX_EVENT) ||
				  (slot_context->slot_event_bits & ALERT_EVENT) ||
				  (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)));

	//
	// Alert: MRL Opened, Card Removed, Power-Fault?
	//
	if (slot_context->slot_event_bits & ALERT_EVENT) {

		//
		// Update attn_led_problem_event LED
		//
		spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
		slot_context->problem_detected = TRUE;
		hp_send_slot_event(slot_context, ATTN_LED_PROBLEM_EVENT);
		spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );

		//
		// Release Command MUTEX
		//
		hp_set_slot_event_bit(slot_context, CMD_RELEASE_EVENT);
		wake_up_interruptible(&slot_context->cmd_release_event);

		//
		// Fail on-going request
		//
		slot_context->slot_completion.failed = HP_TRUE;
		slot_context->slot_completion.done = TRUE;

		//
		// Grab Command MUTEX to turn OFF Power LED
		//
		dbg("%s -->ALERT: slot_id[ %d:%d ]  LSR_13:0[ %X ]",__FUNCTION__ ,
		    shpc_context->shpc_instance, slot_context->slot_number-1,
		    readl( slot_context->logical_slot_addr ) & 0x3F );
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_to_slot_disabled_wait_for_led_cmd_available;
	}
	//
	// cmd_available_mutex
	//
	else if (shpc_context->shpc_event_bits & CMD_AVAILABLE_MUTEX_EVENT) {
		//
		// Clear Completion EVENT before issuing next command
		//
		spin_lock_irqsave( &shpc_context->shpc_spinlock, old_irq_flags );
		hp_clear_shpc_event_bit(shpc_context, CMD_AVAILABLE_MUTEX_EVENT);
		hp_clear_shpc_event_bit(shpc_context, CMD_COMPLETION_EVENT);
		spin_unlock_irqrestore( &shpc_context->shpc_spinlock, old_irq_flags );

		//
		// Power-on the slot
		//
		command_reg |= SHPC_SLOT_OPERATION;
		command_reg |= SHPC_PWR_LED_NO_CHANGE;
		command_reg |= SHPC_ATTN_LED_NO_CHANGE;
		command_reg |= SHPC_POWER_ONLY;
		command_reg |= (slot_context->slot_number << SLOT_TGT_OFFSET);
		dbg("%s -->slot_id[ %d:%d ]  -->command_reg = %08x ",__FUNCTION__ , 
		    shpc_context->shpc_instance, slot_context->slot_number-1, command_reg );
		writew(command_reg, shpc_context->mmio_base_addr + SHPC_COMMAND_REG_OFFSET);

		//
		// Wait for 100ms completion pre-amble on RevB-Errata (while holding MUTEX)
		//
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_disabled_wait_for_power_cmd_timeout;
	}
	//
	// exit_request_event
	//
	else {
		//
		// Release Command MUTEX
		//
		hp_set_slot_event_bit(slot_context, CMD_RELEASE_EVENT);
		wake_up_interruptible(&slot_context->cmd_release_event);
		status = STATUS_UNSUCCESSFUL;
	}
	return status;
}


// ****************************************************************************
//
// hp_at_slot_disabled_wait_for_power_cmd_timeout()
//
// ****************************************************************************
long
	hp_at_slot_disabled_wait_for_power_cmd_timeout(
						      struct shpc_context* shpc_context,
						      struct slot_context* slot_context
						      )
{
	unsigned long           old_irq_flags;
	long status = STATUS_SUCCESS;

	dbg("%s -->slot_id[ %d:%d ]",__FUNCTION__ , shpc_context->shpc_instance, slot_context->slot_number-1 );
	//
	// Setup our timer
	//
	hp_clear_slot_event_bit(slot_context, SLOT_TIMER3_EVENT);
	slot_context->slot_timer3.data = (unsigned long)slot_context;
	slot_context->slot_timer3.function = hp_slot_timer3_func;
	slot_context->slot_timer3.expires = jiffies + ONE_TENTH_SEC_TIMEOUT;
	add_timer(&slot_context->slot_timer3);

	//
	// Wait for 100ms completion pre-amble on RevB-Errata (while holding MUTEX)
	//
	wait_event_interruptible(slot_context->slot_event,
				 ((slot_context->slot_event_bits & ALERT_EVENT) ||
				  (slot_context->slot_event_bits & SLOT_TIMER3_EVENT) ||
				  (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)));

	if (!(slot_context->slot_event_bits & SLOT_TIMER3_EVENT)) {
		//
		// delete the timer because we got an event other than the timer
		//
		del_timer_sync(&slot_context->slot_timer3);
	}

	//
	// Alert: MRL Opened, Card Removed, Power-Fault?
	//
	if (slot_context->slot_event_bits & ALERT_EVENT) {

		//
		// Update attn_led_problem_event LED
		//
		spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
		slot_context->problem_detected = TRUE;
		hp_send_slot_event(slot_context, ATTN_LED_PROBLEM_EVENT);
		spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );

		//
		// Release Command MUTEX
		//
		hp_set_slot_event_bit(slot_context, CMD_RELEASE_EVENT);
		wake_up_interruptible(&slot_context->cmd_release_event);

		//
		// Fail on-going request
		//
		slot_context->slot_completion.failed = HP_TRUE;
		slot_context->slot_completion.done = TRUE;

		//
		// Grab Command MUTEX to disable slot
		//
		dbg("%s -->ALERT: slot_id[ %d:%d ]  LSR_13:0[ %X ]",__FUNCTION__ ,
		    shpc_context->shpc_instance, slot_context->slot_number-1,
		    readl( slot_context->logical_slot_addr ) & 0x3F );
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_to_slot_disabled_wait_for_disable_cmd_available;
	}
	//
	// timeout
	//
	else if (slot_context->slot_event_bits & SLOT_TIMER3_EVENT) {
		//
		// Wait for command to complete (while holding MUTEX)
		//
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_disabled_wait_for_power_cmd_completion;
	}
	//
	// exit_request_event
	//
	else {
		//
		// Release Command MUTEX
		//
		hp_set_slot_event_bit(slot_context, CMD_RELEASE_EVENT);
		wake_up_interruptible(&slot_context->cmd_release_event);
		status = STATUS_UNSUCCESSFUL;
	}
	return status;
}


// ****************************************************************************
//
// hp_at_slot_disabled_wait_for_power_cmd_completion()
//
// ****************************************************************************
long
	hp_at_slot_disabled_wait_for_power_cmd_completion(
							 struct shpc_context* shpc_context,
							 struct slot_context* slot_context
							 )
{
	unsigned long           old_irq_flags;
	long status = STATUS_SUCCESS;
	u16 status_reg;

	dbg("%s -->slot_id[ %d:%d ]",__FUNCTION__ , shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Setup our timer
	//
	hp_clear_slot_event_bit(slot_context, SLOT_TIMER4_EVENT);
	slot_context->slot_timer4.data = (unsigned long)slot_context;
	slot_context->slot_timer4.function = hp_slot_timer4_func;
	slot_context->slot_timer4.expires = jiffies + FIFTEEN_SEC_TIMEOUT;
	add_timer(&slot_context->slot_timer4);

	//
	// Wait for Command Completion EVENT while holding MUTEX
	//
	wait_event_interruptible(slot_context->slot_event,
				 ((shpc_context->shpc_event_bits & CMD_COMPLETION_EVENT) ||
				  (slot_context->slot_event_bits & ALERT_EVENT) ||
				  (slot_context->slot_event_bits & SLOT_TIMER4_EVENT) ||
				  (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)));

	if (!(slot_context->slot_event_bits & SLOT_TIMER4_EVENT)) {
		//
		// delete the timer because we got an event other than the timer
		//
		del_timer_sync(&slot_context->slot_timer4);
	}

	//
	// Alert: MRL Opened, Card Removed, Power-Fault?
	//
	if (slot_context->slot_event_bits & ALERT_EVENT) {

		//
		// Update attn_led_problem_event LED
		//
		spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
		slot_context->problem_detected = TRUE;
		hp_send_slot_event(slot_context, ATTN_LED_PROBLEM_EVENT);
		spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );

		//
		// Fail on-going request
		//
		slot_context->slot_completion.failed = HP_TRUE;
		slot_context->slot_completion.done = TRUE;

		//
		// Grab Command MUTEX to disable slot
		//
		dbg("%s -->ALERT: slot_id[ %d:%d ]  LSR_13:0[ %X ]",__FUNCTION__ ,
		    shpc_context->shpc_instance, slot_context->slot_number-1,
		    readl( slot_context->logical_slot_addr ) & 0x3F );
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_to_slot_disabled_wait_for_disable_cmd_available;
	}
	//
	// cmd_completion_event, timeout
	//
	else if ((shpc_context->shpc_event_bits & CMD_COMPLETION_EVENT) ||
		 (slot_context->slot_event_bits & SLOT_TIMER4_EVENT)) {
		//
		// Command completed OK?
		//
		status_reg = readw(shpc_context->mmio_base_addr + SHPC_STATUS_REG_OFFSET);

		if ( ((status_reg & STS_BSY_MASK) >> STS_BSY_OFFSET) == SHPC_STATUS_CLEARED &&
		     ((status_reg & MRLO_ERR_MASK) >> MRLO_ERR_OFFSET) == SHPC_STATUS_CLEARED &&
		     ((status_reg & INVCMD_ERR_MASK) >> INVCMD_ERR_OFFSET) == SHPC_STATUS_CLEARED) {
			//
			// Grab Bus MUTEX to validate speed/mode
			//
			slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_disabled_wait_for_bus_available;
		} else {
			//
			// Fail on-going request
			//
			slot_context->slot_completion.failed = HP_TRUE;
			slot_context->slot_completion.done = TRUE;

			//
			// Grab Command MUTEX to disable slot
			//
			dbg("%s -->CMD_ERROR: slot_id[ %d:%d ]  Cmd[ %X ]",__FUNCTION__ ,
			    shpc_context->shpc_instance, slot_context->slot_number-1, status_reg );
			slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_to_slot_disabled_wait_for_disable_cmd_available;
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
	hp_set_slot_event_bit(slot_context, CMD_RELEASE_EVENT);
	wake_up_interruptible(&slot_context->cmd_release_event);

	return status;
}


// ****************************************************************************
//
// hp_at_slot_disabled_wait_for_bus_available()
//
// ****************************************************************************
long
	hp_at_slot_disabled_wait_for_bus_available(
						  struct shpc_context* shpc_context,
						  struct slot_context* slot_context
						  )
{
	unsigned long   old_irq_flags;
	long status = STATUS_SUCCESS;
	enum shpc_speed_mode max_speed_mode, bus_speed_mode;

	dbg("%s -->slot_id[ %d:%d ]",__FUNCTION__ , shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Wait for Bus Available MUTEX
	//
	hp_set_slot_event_bit(slot_context, BUS_ACQUIRE_EVENT);
	wake_up_interruptible(&slot_context->bus_acquire_event);

	wait_event_interruptible(slot_context->slot_event,
				 ((shpc_context->shpc_event_bits & BUS_AVAILABLE_MUTEX_EVENT) ||
				  (slot_context->slot_event_bits & ALERT_EVENT) ||
				  (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)));

	//
	// Alert: MRL Opened, Card Removed, Power-Fault?
	//
	if (slot_context->slot_event_bits & ALERT_EVENT) {

		dbg("%s -->ALERT: slot_id[ %d:%d ]  LSR_13:0[ %X ]",__FUNCTION__ ,
		    shpc_context->shpc_instance, slot_context->slot_number-1,
		    readl( slot_context->logical_slot_addr ) & 0x3F );
		//
		// Update attn_led_problem_event LED
		//
		spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
		slot_context->problem_detected = TRUE;
		hp_send_slot_event(slot_context, ATTN_LED_PROBLEM_EVENT);
		spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );

		//
		// Release Bus MUTEX
		//
		hp_set_slot_event_bit(slot_context, BUS_RELEASE_EVENT);
		wake_up_interruptible(&slot_context->bus_release_event);

		//
		// Fail on-going request
		//
		slot_context->slot_completion.failed = HP_TRUE;
		slot_context->slot_completion.done = TRUE;

		//
		// Grab Command MUTEX to disable slot
		//
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_to_slot_disabled_wait_for_disable_cmd_available;
	}
	//
	// bus_available_mutex
	//
	else if (shpc_context->shpc_event_bits & BUS_AVAILABLE_MUTEX_EVENT) {
		//
		// Clear bus availabe mutex event
		//
		spin_lock_irqsave( &shpc_context->shpc_spinlock, old_irq_flags );
		hp_clear_shpc_event_bit(shpc_context, BUS_AVAILABLE_MUTEX_EVENT);
		spin_unlock_irqrestore( &shpc_context->shpc_spinlock, old_irq_flags );
		//
		// Get current HW speed/mode
		//
		bus_speed_mode = hp_get_bus_speed_mode( shpc_context );
		max_speed_mode = hp_get_card_speed_mode_cap( slot_context );
		dbg("%s -->slot_id[ %d:%d ], bus_speed %d   max_speed %d",__FUNCTION__ , 
		    shpc_context->shpc_instance, slot_context->slot_number-1, bus_speed_mode, max_speed_mode );
		if ( max_speed_mode > shpc_context->max_speed_mode ) {
			//
			// Can only go as fast as the controller allows
			//
			max_speed_mode = shpc_context->max_speed_mode;
		}
		dbg("%s -->slot_id[ %d:%d ], bus_speed %d   max_speed %d after compare",__FUNCTION__ , 
		    shpc_context->shpc_instance, slot_context->slot_number-1, bus_speed_mode, max_speed_mode );

		//
		// Grab global spinlock to check current speed/mode settings
		//
		spin_lock_irqsave( &shpc_context->shpc_spinlock, old_irq_flags );

		//
		// Other slots in contetion for bus speed/mode changes?
		//
		slot_context->in_bus_speed_mode_contention = FALSE;
		max_speed_mode = hp_GetMaxSpeedModeForEnabledSlots( shpc_context, max_speed_mode );
		dbg("%s -->slot_id[ %d:%d ], bus_speed %d   max_speed %d after GetMaxSpeedModeForEnabledSlots",__FUNCTION__ , 
		    shpc_context->shpc_instance, slot_context->slot_number-1, bus_speed_mode, max_speed_mode );

		//
		// Make this card can handle PCI-66 speed/mode
		//
		if ( max_speed_mode == SHPC_BUS_CONV_66 && !slot_context->card_pci66_capable ) {
			//
			// Fall back to slower common denominator
			//
			max_speed_mode = SHPC_BUS_CONV_33;
		}

		//
		// Bus running at incompatible speed/mode?
		//
		if ( bus_speed_mode != max_speed_mode ) {
			//
			// Other slots already enabled?
			//
			if ( hp_signal_enabled_slots_to_rebalance_bus( shpc_context )) {
				//
				// Wait for enabled slots to release the bus, then change bus speed/mode
				//
				shpc_context->bus_speed_mode = max_speed_mode;
				shpc_context->bus_released = FALSE;
				hp_clear_shpc_event_bit(shpc_context, BUS_COMPLETE_EVENT);
				hp_clear_slot_event_bit(slot_context, BUS_RELEASE_EVENT);
				slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_disabled_wait_for_bus_released;
			} else {
				//
				// Change bus speed/mode to enable this slot
				//
				shpc_context->bus_speed_mode = max_speed_mode;
				slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_disabled_wait_for_speed_mode_cmd_available;
			}
		} else {
			//
			// Enable slot at current bus speed/mode
			//
			shpc_context->bus_speed_mode = bus_speed_mode;
			slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_disabled_wait_for_enable_cmd_available;
		}

		//
		// Flag this slot in contention for bus speed/mode validation
		//
		slot_context->in_bus_speed_mode_contention = TRUE;

		//
		// Release global spinlock since we're done checking speed/mode
		//
		spin_unlock_irqrestore( &shpc_context->shpc_spinlock, old_irq_flags );

		dbg("%s -->ENABLE_IN_PROGRESS: slot_id[ %d:%d ]  card_speed_mode_cap[ %d+%d ]  bus_speed_mode[ %d=>%d ]",__FUNCTION__ ,
		    shpc_context->shpc_instance, slot_context->slot_number-1,
		    slot_context->card_speed_mode_cap, slot_context->card_pci66_capable,
		    bus_speed_mode, shpc_context->bus_speed_mode );
	}
	//
	// exit_request_event
	//
	else {
		//
		// Release Bus MUTEX
		//
		hp_set_slot_event_bit(slot_context, BUS_RELEASE_EVENT);
		wake_up_interruptible(&slot_context->bus_release_event);
		status = STATUS_UNSUCCESSFUL;
	}
	return status;
}


// ****************************************************************************
//
// hp_at_slot_disabled_wait_for_bus_released() 
//
// ****************************************************************************
long
	hp_at_slot_disabled_wait_for_bus_released(
						 struct shpc_context* shpc_context,
						 struct slot_context* slot_context
						 )
{
	unsigned long           old_irq_flags;
	long status = STATUS_SUCCESS;

	dbg("%s -->slot_id[ %d:%d ]",__FUNCTION__ , shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Wait for Bus Release EVENT while holding MUTEX
	//
	spin_lock_irqsave( &shpc_context->shpc_spinlock, old_irq_flags );
	shpc_context->shpc_event_bits = 0;
//	slot_context->slot_event_bits = 0;
	spin_unlock_irqrestore( &shpc_context->shpc_spinlock, old_irq_flags );

	wait_event_interruptible(slot_context->slot_event,
				 ((slot_context->slot_event_bits & BUS_RELEASE_EVENT) ||
				  (shpc_context->shpc_event_bits & BUS_COMPLETE_EVENT) ||
				  (slot_context->slot_event_bits & ALERT_EVENT) ||
				  (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)));

	//
	// Alert: MRL Opened, Card Removed, Power-Fault?
	//
	if (slot_context->slot_event_bits & ALERT_EVENT) {

		//
		// Update attn_led_problem_event LED
		//
		spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
		slot_context->problem_detected = TRUE;
		hp_send_slot_event(slot_context, ATTN_LED_PROBLEM_EVENT);
		spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );

		//
		// Release Bus MUTEX
		//
		hp_set_slot_event_bit(slot_context, BUS_RELEASE_EVENT);
		wake_up_interruptible(&slot_context->bus_release_event);

		//
		// Fail on-going request
		//
		slot_context->slot_completion.failed = HP_TRUE;
		slot_context->slot_completion.done = TRUE;

		//
		// Grab Command MUTEX to disable slot
		//
		dbg("%s -->ALERT: slot_id[ %d:%d ]  LSR_13:0[ %X ]",__FUNCTION__ ,
		    shpc_context->shpc_instance, slot_context->slot_number-1,
		    readl( slot_context->logical_slot_addr ) & 0x3F );
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_to_slot_disabled_wait_for_disable_cmd_available;
	}
	//
	// bus_release_event
	//
	else if ( (slot_context->slot_event_bits & BUS_RELEASE_EVENT ) || (shpc_context->shpc_event_bits & BUS_COMPLETE_EVENT) ) {
		spin_lock_irqsave( &shpc_context->shpc_spinlock, old_irq_flags );
		hp_clear_shpc_event_bit(shpc_context, BUS_COMPLETE_EVENT);
		if ( shpc_context->bus_released ) {
			spin_unlock_irqrestore( &shpc_context->shpc_spinlock, old_irq_flags );
			//
			// Grab Command MUTEX to set Bus speed/mode
			//
			slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_disabled_wait_for_speed_mode_cmd_available;
		} else {
			spin_unlock_irqrestore( &shpc_context->shpc_spinlock, old_irq_flags );
			//
			// Release Bus MUTEX
			//
			hp_set_slot_event_bit(slot_context, BUS_RELEASE_EVENT);
			wake_up_interruptible(&slot_context->bus_release_event);

			//
			// Fail on-going request
			//
			slot_context->slot_completion.failed = HP_TRUE;
			slot_context->slot_completion.done = TRUE;

			//
			// Grab Command MUTEX to disable slot
			//
			dbg("%s -->ENABLE_REQ: slot_id[ %d:%d ] Cancelled: BUSY DevNode",__FUNCTION__ ,
			    shpc_context->shpc_instance, slot_context->slot_number-1 );
			slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_to_slot_disabled_wait_for_disable_cmd_available;
		}
	}
	//
	// exit_request_event
	//
	else {
		//
		// Release Bus MUTEX
		//
		hp_set_slot_event_bit(slot_context, BUS_RELEASE_EVENT);
		wake_up_interruptible(&slot_context->bus_release_event);
		status =STATUS_UNSUCCESSFUL;
	}
	return status;
}


// ****************************************************************************
//
// hp_at_slot_disabled_wait_for_speed_mode_cmd_available()
//
// ****************************************************************************
long
	hp_at_slot_disabled_wait_for_speed_mode_cmd_available(
							     struct shpc_context* shpc_context,
							     struct slot_context* slot_context
							     )
{
	unsigned long   old_irq_flags;
	long status = STATUS_SUCCESS;
	u16 command_reg = 0;

	dbg("%s -->slot_id[ %d:%d ]",__FUNCTION__ , shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Wait for Command Available MUTEX
	//
	hp_set_slot_event_bit(slot_context, CMD_ACQUIRE_EVENT);
	wake_up_interruptible(&slot_context->cmd_acquire_event);

	wait_event_interruptible(slot_context->slot_event,
				 ((shpc_context->shpc_event_bits & CMD_AVAILABLE_MUTEX_EVENT) ||
				  (slot_context->slot_event_bits & ALERT_EVENT) ||
				  (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)));

	//
	// Alert: MRL Opened, Card Removed, Power-Fault?
	//
	if (slot_context->slot_event_bits & ALERT_EVENT) {

		//
		// Update attn_led_problem_event LED
		//
		spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
		slot_context->problem_detected = TRUE;
		hp_send_slot_event(slot_context, ATTN_LED_PROBLEM_EVENT);
		spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );

		//
		// Release Bus, Command MUTEX
		//
		hp_set_slot_event_bit(slot_context, CMD_RELEASE_EVENT);
		wake_up_interruptible(&slot_context->cmd_release_event);
		hp_set_slot_event_bit(slot_context, BUS_RELEASE_EVENT);
		wake_up_interruptible(&slot_context->bus_release_event);

		//
		// Fail on-going request
		//
		slot_context->slot_completion.failed = HP_TRUE;
		slot_context->slot_completion.done = TRUE;

		//
		// Grab Command MUTEX to disable slot
		//
		dbg("%s -->ALERT: slot_id[ %d:%d ]  LSR_13:0[ %X ]",__FUNCTION__ ,
		    shpc_context->shpc_instance, slot_context->slot_number-1,
		    readl( slot_context->logical_slot_addr ) & 0x3F );
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_to_slot_disabled_wait_for_disable_cmd_available;
	}
	//
	// cmd_available_mutex
	//
	else if (shpc_context->shpc_event_bits & CMD_AVAILABLE_MUTEX_EVENT) {
		//
		// Clear Completion EVENT before issuing next command
		//
		spin_lock_irqsave( &shpc_context->shpc_spinlock, old_irq_flags );
		hp_clear_shpc_event_bit(shpc_context, CMD_AVAILABLE_MUTEX_EVENT);
		hp_clear_shpc_event_bit(shpc_context, CMD_COMPLETION_EVENT);
		spin_unlock_irqrestore( &shpc_context->shpc_spinlock, old_irq_flags );

		//
		// Set Bus speed/mode
		//
		if ( shpc_context->shpc_interface == SHPC_MODE2_INTERFACE ) {
			//
			// Mode2 Interface
			//
			command_reg |= SHPC_SET_BUS_SPEED_MODE2;
			command_reg |= (shpc_context->bus_speed_mode << MODE2_SPEED_MODE_OFFSET);
		} else {
			//
			// Mode1 Interface
			//
			command_reg |= SHPC_SET_BUS_SPEED_MODE1;
			command_reg |= (shpc_context->bus_speed_mode << MODE1_SPEED_MODE_OFFSET);
		}
		dbg("%s -->slot_id[ %d:%d ]  -->command_reg = %08x ",__FUNCTION__ , 
		    shpc_context->shpc_instance, slot_context->slot_number-1, command_reg );
		writew(command_reg, shpc_context->mmio_base_addr + SHPC_COMMAND_REG_OFFSET);

		//
		// Wait for command to complete (while holding MUTEX)
		//
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_disabled_wait_for_speed_mode_cmd_completion;
	}
	//
	// exit_request_event
	//
	else {
		//
		// Release Bus, Command MUTEX
		//
		hp_set_slot_event_bit(slot_context, CMD_RELEASE_EVENT);
		wake_up_interruptible(&slot_context->cmd_release_event);
		hp_set_slot_event_bit(slot_context, BUS_RELEASE_EVENT);
		wake_up_interruptible(&slot_context->bus_release_event);
		status =STATUS_UNSUCCESSFUL;
	}
	return status;
}


// ****************************************************************************
//
// hp_at_slot_disabled_wait_for_speed_mode_cmd_completion()
//
// ****************************************************************************
long
	hp_at_slot_disabled_wait_for_speed_mode_cmd_completion(
							      struct shpc_context* shpc_context,
							      struct slot_context* slot_context
							      )
{
	unsigned long           old_irq_flags;
	long status = STATUS_SUCCESS;
	u16 status_reg;

	dbg("%s -->slot_id[ %d:%d ]",__FUNCTION__ , shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Setup our timer
	//
	hp_clear_slot_event_bit(slot_context, SLOT_TIMER5_EVENT);
	slot_context->slot_timer5.data = (unsigned long)slot_context;
	slot_context->slot_timer5.function = hp_slot_timer5_func;
	slot_context->slot_timer5.expires = jiffies + FIFTEEN_SEC_TIMEOUT;
	add_timer(&slot_context->slot_timer5);

	//
	// Wait for Command Completion EVENT while holding MUTEX
	//
	wait_event_interruptible(slot_context->slot_event,
				 ((shpc_context->shpc_event_bits & CMD_COMPLETION_EVENT) ||
				  (slot_context->slot_event_bits & ALERT_EVENT) ||
				  (slot_context->slot_event_bits & SLOT_TIMER5_EVENT) ||
				  (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)));

	if (!(slot_context->slot_event_bits & SLOT_TIMER5_EVENT)) {
		//
		// delete the timer because we got an event other than the timer
		//
		del_timer_sync(&slot_context->slot_timer5);
	}

	//
	// Alert: MRL Opened, Card Removed, Power-Fault?
	//
	if (slot_context->slot_event_bits & ALERT_EVENT) {

		//
		// Update attn_led_problem_event LED
		//
		spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
		slot_context->problem_detected = TRUE;
		hp_send_slot_event(slot_context, ATTN_LED_PROBLEM_EVENT);
		spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );

		//
		// Release Bus MUTEX
		//
		hp_set_slot_event_bit(slot_context, BUS_RELEASE_EVENT);
		wake_up_interruptible(&slot_context->bus_release_event);

		//
		// Fail on-going request
		//
		slot_context->slot_completion.failed = HP_TRUE;
		slot_context->slot_completion.done = TRUE;

		//
		// Grab Command MUTEX to disable slot
		//
		dbg("%s -->ALERT: slot_id[ %d:%d ]  LSR_13:0[ %X ]",__FUNCTION__ ,
		    shpc_context->shpc_instance, slot_context->slot_number-1,
		    readl( slot_context->logical_slot_addr ) & 0x3F );
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_to_slot_disabled_wait_for_disable_cmd_available;
	}
	//
	// cmd_completion_event, timeout
	//
	else if ((shpc_context->shpc_event_bits & CMD_COMPLETION_EVENT) ||
		 (slot_context->slot_event_bits & SLOT_TIMER5_EVENT)) {

		//
		// Command completed OK?
		//
		status_reg = readw(shpc_context->mmio_base_addr + SHPC_STATUS_REG_OFFSET);

		if ( ((status_reg & STS_BSY_MASK) >> STS_BSY_OFFSET) == SHPC_STATUS_CLEARED &&
		     ((status_reg & MRLO_ERR_MASK) >> MRLO_ERR_OFFSET) == SHPC_STATUS_CLEARED &&
		     ((status_reg & INVCMD_ERR_MASK) >> INVCMD_ERR_OFFSET) == SHPC_STATUS_CLEARED &&
		     ((status_reg & INVSM_ERR_MASK) >> INVSM_ERR_OFFSET) == SHPC_STATUS_CLEARED) {
			//
			// Grab Command MUTEX to enable slot
			//
			slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_disabled_wait_for_enable_cmd_available;
		} else {
			//
			// Release Bus MUTEX
			//
			hp_set_slot_event_bit(slot_context, BUS_RELEASE_EVENT);
			wake_up_interruptible(&slot_context->bus_release_event);

			//
			// Fail on-going request
			//
			slot_context->slot_completion.failed = HP_TRUE;
			slot_context->slot_completion.done = TRUE;

			//
			// Grab Command MUTEX to disable slot
			//
			dbg("%s -->CMD_ERROR: slot_id[ %d:%d ]  Cmd[ %X ]",__FUNCTION__ ,
			    shpc_context->shpc_instance, slot_context->slot_number-1, status_reg );
			slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_to_slot_disabled_wait_for_disable_cmd_available;
		}
	}
	//
	// exit_request_event
	//
	else {
		//
		// Release Bus MUTEX
		//
		hp_set_slot_event_bit(slot_context, BUS_RELEASE_EVENT);
		wake_up_interruptible(&slot_context->bus_release_event);
		status =STATUS_UNSUCCESSFUL;
	}
	//
	// Release Command MUTEX
	//
	hp_set_slot_event_bit(slot_context, CMD_RELEASE_EVENT);
	wake_up_interruptible(&slot_context->cmd_release_event);

	return status;
}


// ****************************************************************************
//
// hp_at_slot_disabled_wait_for_enable_cmd_available() 
//
// ****************************************************************************
long
	hp_at_slot_disabled_wait_for_enable_cmd_available(
							 struct shpc_context* shpc_context,
							 struct slot_context* slot_context
							 )
{
	unsigned long           old_irq_flags;
	long status = STATUS_SUCCESS;
	u16 command_reg = 0;

	dbg("%s -->slot_id[ %d:%d ]",__FUNCTION__ , shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Wait for Command Available MUTEX
	//
	hp_set_slot_event_bit(slot_context, CMD_ACQUIRE_EVENT);
	wake_up_interruptible(&slot_context->cmd_acquire_event);

	wait_event_interruptible(slot_context->slot_event,
				 ((shpc_context->shpc_event_bits & CMD_AVAILABLE_MUTEX_EVENT) ||
				  (slot_context->slot_event_bits & ALERT_EVENT) ||
				  (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)));

	//
	// Alert: MRL Opened, Card Removed, Power-Fault?
	//
	if (slot_context->slot_event_bits & ALERT_EVENT) {

		//
		// Update attn_led_problem_event LED
		//
		spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
		slot_context->problem_detected = TRUE;
		hp_send_slot_event(slot_context, ATTN_LED_PROBLEM_EVENT);
		spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );

		//
		// Release Bus, Command MUTEX
		//
		hp_set_slot_event_bit(slot_context, BUS_RELEASE_EVENT);
		wake_up_interruptible(&slot_context->bus_release_event);
		hp_set_slot_event_bit(slot_context, CMD_RELEASE_EVENT);
		wake_up_interruptible(&slot_context->cmd_release_event);

		//
		// Fail on-going request
		//
		slot_context->slot_completion.failed = HP_TRUE;
		slot_context->slot_completion.done = TRUE;

		//
		// Grab Command MUTEX to disable slot
		//
		dbg("%s -->ALERT: slot_id[ %d:%d ]  LSR_13:0[ %X ]",__FUNCTION__ ,
		    shpc_context->shpc_instance, slot_context->slot_number-1,
		    readl( slot_context->logical_slot_addr ) & 0x3F );
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_to_slot_disabled_wait_for_disable_cmd_available;
	}
	//
	// cmd_available_mutex
	//
	else if (shpc_context->shpc_event_bits & CMD_AVAILABLE_MUTEX_EVENT) {
		dbg("%s  ENABLING SLOT..-->slot_id[ %d:%d ]",__FUNCTION__,shpc_context->shpc_instance, slot_context->slot_number-1  );
		//
		// Clear Completion EVENT before issuing next command
		//
		spin_lock_irqsave( &shpc_context->shpc_spinlock, old_irq_flags );
		hp_clear_shpc_event_bit(shpc_context, CMD_AVAILABLE_MUTEX_EVENT);
		hp_clear_shpc_event_bit(shpc_context, CMD_COMPLETION_EVENT);
		spin_unlock_irqrestore( &shpc_context->shpc_spinlock, old_irq_flags );

		//
		// Enable the slot
		//
		command_reg |= SHPC_SLOT_OPERATION;
		command_reg |= SHPC_PWR_LED_ON;
		command_reg |= SHPC_ATTN_LED_NO_CHANGE;
		command_reg |= SHPC_ENABLE_SLOT;
		command_reg |= (slot_context->slot_number << SLOT_TGT_OFFSET);
		dbg("%s -->slot_id[ %d:%d ]  -->command_reg = %08x ",__FUNCTION__ , 
		    shpc_context->shpc_instance, slot_context->slot_number-1, command_reg );
		writew(command_reg, shpc_context->mmio_base_addr + SHPC_COMMAND_REG_OFFSET);

		//
		// Wait for command to complete (while holding Bus,Command MUTEX)
		//
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_disabled_wait_for_enable_cmd_completion;
	}
	//
	// exit_request_event
	//
	else {
		//
		// Release Bus, Command MUTEX
		//
		hp_set_slot_event_bit(slot_context, BUS_RELEASE_EVENT);
		wake_up_interruptible(&slot_context->bus_release_event);
		hp_set_slot_event_bit(slot_context, CMD_RELEASE_EVENT);
		wake_up_interruptible(&slot_context->cmd_release_event);
		status = STATUS_UNSUCCESSFUL;
	}
	return status;
}


// ****************************************************************************
//
// hp_at_slot_disabled_wait_for_enable_cmd_completion()
//
// ****************************************************************************
long
	hp_at_slot_disabled_wait_for_enable_cmd_completion(
							  struct shpc_context* shpc_context,
							  struct slot_context* slot_context
							  )
{
	unsigned long           old_irq_flags;
	long status = STATUS_SUCCESS;
	u16 status_reg;

	dbg("%s -->slot_id[ %d:%d ]",__FUNCTION__ , shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Setup our timer
	//
	hp_clear_slot_event_bit(slot_context, SLOT_TIMER6_EVENT);
	slot_context->slot_timer6.data = (unsigned long)slot_context;
	slot_context->slot_timer6.function = hp_slot_timer6_func;
	slot_context->slot_timer6.expires = jiffies + FIFTEEN_SEC_TIMEOUT;
	add_timer(&slot_context->slot_timer6);

	//
	// Wait for Command Completion EVENT while holding MUTEX
	//
	wait_event_interruptible(slot_context->slot_event,
				 ((shpc_context->shpc_event_bits & CMD_COMPLETION_EVENT) ||
				  (slot_context->slot_event_bits & ALERT_EVENT) ||
				  (slot_context->slot_event_bits & SLOT_TIMER6_EVENT) ||
				  (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)));

	if (!(slot_context->slot_event_bits & SLOT_TIMER6_EVENT)) {
		//
		// delete the timer because we got an event other than the timer
		//
		del_timer_sync(&slot_context->slot_timer6);
	}

	//
	// Alert: MRL Opened, Card Removed, Power-Fault?
	//
	if (slot_context->slot_event_bits & ALERT_EVENT) {

		//
		// Update attn_led_problem_event LED
		//
		spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
		slot_context->problem_detected = TRUE;
		hp_send_slot_event(slot_context, ATTN_LED_PROBLEM_EVENT);
		spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );

		//
		// Release Bus MUTEX
		//
		hp_set_slot_event_bit(slot_context, BUS_RELEASE_EVENT);
		wake_up_interruptible(&slot_context->bus_release_event);

		//
		// Fail on-going request
		//
		slot_context->slot_completion.failed = HP_TRUE;
		slot_context->slot_completion.done = TRUE;

		//
		// Grab Command MUTEX to disable slot
		//
		dbg("%s -->ALERT: slot_id[ %d:%d ]  LSR_13:0[ %X ]", __FUNCTION__,
		    shpc_context->shpc_instance, slot_context->slot_number-1,
		    readl( slot_context->logical_slot_addr ) & 0x3F );
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_to_slot_disabled_wait_for_disable_cmd_available;
	}
	//
	// cmd_completion_event, timeout
	//
	else if ((shpc_context->shpc_event_bits & CMD_COMPLETION_EVENT) ||
		 (slot_context->slot_event_bits & SLOT_TIMER6_EVENT)) {
		//
		// Command completed OK?
		//
		status_reg = readw(shpc_context->mmio_base_addr + SHPC_STATUS_REG_OFFSET);

		if ( ((status_reg & STS_BSY_MASK) >> STS_BSY_OFFSET) == SHPC_STATUS_CLEARED &&
		     ((status_reg & MRLO_ERR_MASK) >> MRLO_ERR_OFFSET) == SHPC_STATUS_CLEARED &&
		     ((status_reg & INVCMD_ERR_MASK) >> INVCMD_ERR_OFFSET) == SHPC_STATUS_CLEARED &&
		     ((status_reg & INVSM_ERR_MASK) >> INVSM_ERR_OFFSET) == SHPC_STATUS_CLEARED) {
			//
			// Wait for settling time
			//
			slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_disabled_wait_for_enable_timeout;
		} else {
			//
			// Release Bus MUTEX
			//
			hp_set_slot_event_bit(slot_context, BUS_RELEASE_EVENT);
			wake_up_interruptible(&slot_context->bus_release_event);

			//
			// Fail on-going request
			//
			slot_context->slot_completion.failed = HP_TRUE;
			slot_context->slot_completion.done = TRUE;

			//
			// Grab Command MUTEX to disable slot
			//
			dbg("%s -->CMD_ERROR: slot_id[ %d:%d ]  Cmd[ %X ]", __FUNCTION__,
			    shpc_context->shpc_instance, slot_context->slot_number-1, status_reg );
			slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_to_slot_disabled_wait_for_disable_cmd_available;
		}
	}
	//
	// exit_request_event
	//
	else {
		//
		// Release Bus MUTEX
		//
		hp_set_slot_event_bit(slot_context, BUS_RELEASE_EVENT);
		wake_up_interruptible(&slot_context->bus_release_event);
		status = STATUS_UNSUCCESSFUL;
	}
	//
	// Release Command MUTEX
	//
	hp_set_slot_event_bit(slot_context, CMD_RELEASE_EVENT);
	wake_up_interruptible(&slot_context->cmd_release_event);

	return status;
}


// ****************************************************************************
//
// hp_at_slot_disabled_wait_for_enable_timeout()
//
// ****************************************************************************
long
	hp_at_slot_disabled_wait_for_enable_timeout(
						   struct shpc_context* shpc_context,
						   struct slot_context* slot_context
						   )
{
	unsigned long           old_irq_flags;
	long status = STATUS_SUCCESS;

	dbg("%s -->slot_id[ %d:%d ]", __FUNCTION__, shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Setup our timer
	//
	hp_clear_slot_event_bit(slot_context, SLOT_TIMER7_EVENT);
	slot_context->slot_timer7.data = (unsigned long)slot_context;
	slot_context->slot_timer7.function = hp_slot_timer7_func;
	slot_context->slot_timer7.expires = jiffies + ONE_SEC_TIMEOUT;
	add_timer(&slot_context->slot_timer7);

	//
	// Wait for timeout
	//
	wait_event_interruptible(slot_context->slot_event,
				 ((slot_context->slot_event_bits & ALERT_EVENT) ||
				  (slot_context->slot_event_bits & SLOT_TIMER7_EVENT) ||
				  (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)));

	if (!(slot_context->slot_event_bits & SLOT_TIMER7_EVENT)) {
		//
		// delete the timer because we got an event other than the timer
		//
		del_timer_sync(&slot_context->slot_timer7);
	}
	//
	// Alert: MRL Opened, Card Removed, Power-Fault?
	//
	if (slot_context->slot_event_bits & ALERT_EVENT) {

		//
		// Update attn_led_problem_event LED
		//
		spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
		slot_context->problem_detected = TRUE;
		hp_send_slot_event(slot_context, ATTN_LED_PROBLEM_EVENT);
		spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );

		//
		// Fail on-going request
		//
		slot_context->slot_completion.failed = HP_TRUE;
		slot_context->slot_completion.done = TRUE;

		//
		// Grab Command MUTEX to disable slot
		//
		dbg("%s -->ALERT: slot_id[ %d:%d ]  LSR_13:0[ %X ]", __FUNCTION__,
		    shpc_context->shpc_instance, slot_context->slot_number-1,
		    readl( slot_context->logical_slot_addr ) & 0x3F );
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_to_slot_disabled_wait_for_disable_cmd_available;
	}
	//
	// timeout
	//
	else if (slot_context->slot_event_bits & SLOT_TIMER7_EVENT) {
		//
		// Flag this slot as ENABLED
		//
		hp_flag_slot_as_enabled( shpc_context, slot_context );

		//
		// Complete succesful ENABLE request
		//
		slot_context->slot_completion.failed = HP_FALSE;
		slot_context->slot_completion.done = TRUE;
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_enabled_wait_for_slot_request;
	}
	//
	// exit_request_event
	//
	else {
		status =STATUS_UNSUCCESSFUL;
	}
	//
	// Release Bus MUTEX
	//
	hp_set_slot_event_bit(slot_context, BUS_RELEASE_EVENT);
	wake_up_interruptible(&slot_context->bus_release_event);

	return status;
}


// ****************************************************************************
//
// hp_to_slot_disabled_wait_for_led_cmd_available()
//
// ****************************************************************************
long
	hp_to_slot_disabled_wait_for_led_cmd_available(
						      struct shpc_context* shpc_context,
						      struct slot_context* slot_context
						      )
{
	unsigned long           old_irq_flags;
	long status = STATUS_SUCCESS;
	u16 command_reg = 0;

	dbg("%s -->slot_id[ %d:%d ]", __FUNCTION__, shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Wait for Command Available MUTEX
	//
	hp_set_slot_event_bit(slot_context, CMD_ACQUIRE_EVENT);
	wake_up_interruptible(&slot_context->cmd_acquire_event);

	wait_event_interruptible(slot_context->slot_event,
				 ((shpc_context->shpc_event_bits & CMD_AVAILABLE_MUTEX_EVENT) ||
				  (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)));

	if (shpc_context->shpc_event_bits & CMD_AVAILABLE_MUTEX_EVENT) {
		//
		// Clear Completion EVENT before issuing next command
		//
		spin_lock_irqsave( &shpc_context->shpc_spinlock, old_irq_flags );
		hp_clear_shpc_event_bit(shpc_context, CMD_AVAILABLE_MUTEX_EVENT);
		hp_clear_shpc_event_bit(shpc_context, CMD_COMPLETION_EVENT);
		spin_unlock_irqrestore( &shpc_context->shpc_spinlock, old_irq_flags );

		//
		// Turn OFF Power LED
		//
		command_reg |= SHPC_SLOT_OPERATION;
		command_reg |= SHPC_PWR_LED_OFF;
		command_reg |= SHPC_ATTN_LED_NO_CHANGE;
		command_reg |= SHPC_SLOT_NO_CHANGE;
		command_reg |= (slot_context->slot_number << SLOT_TGT_OFFSET);
		writew(command_reg, shpc_context->mmio_base_addr + SHPC_COMMAND_REG_OFFSET);

		//
		// Wait for Power LED command to complete
		//
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_to_slot_disabled_wait_for_led_cmd_completion;
	} else {			  // exit_request_event
		//
		// Release Command MUTEX
		//
		hp_set_slot_event_bit(slot_context, CMD_RELEASE_EVENT);
		wake_up_interruptible(&slot_context->cmd_release_event);
		status = STATUS_UNSUCCESSFUL;
	}
	return status;
}

// ****************************************************************************
//
// hp_to_slot_disabled_wait_for_led_cmd_completion()
//
// ****************************************************************************
long
	hp_to_slot_disabled_wait_for_led_cmd_completion(
						       struct shpc_context* shpc_context,
						       struct slot_context* slot_context
						       )
{
	long status = STATUS_SUCCESS;

	dbg("%s -->slot_id[ %d:%d ]", __FUNCTION__, shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Setup our timer
	//
	hp_clear_slot_event_bit(slot_context, SLOT_TIMER8_EVENT);
	slot_context->slot_timer8.data = (unsigned long)slot_context;
	slot_context->slot_timer8.function = hp_slot_timer8_func;
	slot_context->slot_timer8.expires = jiffies + ONE_SEC_TIMEOUT;
	add_timer(&slot_context->slot_timer8);

	//
	// Wait for Command Completion EVENT while holding MUTEX
	//
	wait_event_interruptible(slot_context->slot_event,
				 ((shpc_context->shpc_event_bits & CMD_COMPLETION_EVENT) ||
				  (slot_context->slot_event_bits & SLOT_TIMER8_EVENT) ||
				  (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)));

	if (!(slot_context->slot_event_bits & SLOT_TIMER8_EVENT)) {
		//
		// delete the timer because we got an event other than the timer
		//
		del_timer_sync(&slot_context->slot_timer8);
	}

	if ((shpc_context->shpc_event_bits & CMD_COMPLETION_EVENT) ||
	    (slot_context->slot_event_bits & SLOT_TIMER8_EVENT)) {
		//
		// Wait for next request
		//
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_disabled_wait_for_slot_request;

	} else {			  // exit_request_event
		status = STATUS_UNSUCCESSFUL;
	}

	//
	// Release command MUTEX
	//
	hp_set_slot_event_bit(slot_context, CMD_RELEASE_EVENT);
	wake_up_interruptible(&slot_context->cmd_release_event);

	return status;
}


// ****************************************************************************
//
// hp_to_slot_disabled_wait_for_disable_cmd_available()
//
// ****************************************************************************
long
	hp_to_slot_disabled_wait_for_disable_cmd_available(
							  struct shpc_context* shpc_context,
							  struct slot_context* slot_context
							  )
{
	unsigned long           old_irq_flags;
	long status = STATUS_SUCCESS;
	u16 command_reg = 0;

	dbg("%s -->slot_id[ %d:%d ]", __FUNCTION__, shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Wait for Command Available MUTEX
	//
	hp_set_slot_event_bit(slot_context, CMD_ACQUIRE_EVENT);
	wake_up_interruptible(&slot_context->cmd_acquire_event);

	wait_event_interruptible(slot_context->slot_event,
				 ((shpc_context->shpc_event_bits & CMD_AVAILABLE_MUTEX_EVENT) ||
				  (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)));

	if (shpc_context->shpc_event_bits & CMD_AVAILABLE_MUTEX_EVENT) {
		//
		// Clear Completion EVENT before issuing next command
		//
		spin_lock_irqsave( &shpc_context->shpc_spinlock, old_irq_flags );
		hp_clear_shpc_event_bit(shpc_context, CMD_AVAILABLE_MUTEX_EVENT);
		hp_clear_shpc_event_bit(shpc_context, CMD_COMPLETION_EVENT);
		spin_unlock_irqrestore( &shpc_context->shpc_spinlock, old_irq_flags );

		//
		// Disable slot and turn OFF Power LED
		//
		command_reg |= SHPC_SLOT_OPERATION;
		command_reg |= SHPC_PWR_LED_OFF;
		command_reg |= SHPC_ATTN_LED_NO_CHANGE;
		command_reg |= SHPC_DISABLE_SLOT;
		command_reg |= (slot_context->slot_number << SLOT_TGT_OFFSET);
		writew(command_reg, shpc_context->mmio_base_addr + SHPC_COMMAND_REG_OFFSET);

		//
		// Wait for command to complete
		//
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_to_slot_disabled_wait_for_disable_cmd_completion;
	} else {
		//
		// Release Command MUTEX
		//
		hp_set_slot_event_bit(slot_context, CMD_RELEASE_EVENT);
		wake_up_interruptible(&slot_context->cmd_release_event);
		status = STATUS_UNSUCCESSFUL;
	}
	return status;
}


// ****************************************************************************
//
// hp_to_slot_disabled_wait_for_disable_cmd_completion()
//
// ****************************************************************************
long
	hp_to_slot_disabled_wait_for_disable_cmd_completion(
							   struct shpc_context* shpc_context,
							   struct slot_context* slot_context
							   )
{
	long status = STATUS_SUCCESS;

	dbg("%s -->slot_id[ %d:%d ]", __FUNCTION__, shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Setup our timer
	//
	hp_clear_slot_event_bit(slot_context, SLOT_TIMER9_EVENT);
	slot_context->slot_timer9.data = (unsigned long)slot_context;
	slot_context->slot_timer9.function = hp_slot_timer9_func;
	slot_context->slot_timer9.expires = jiffies + FIFTEEN_SEC_TIMEOUT;
	add_timer(&slot_context->slot_timer9);

	//
	// Wait for Command Completion EVENT while holding MUTEX
	//
	wait_event_interruptible(slot_context->slot_event,
				 ((shpc_context->shpc_event_bits & CMD_COMPLETION_EVENT) ||
				  (slot_context->slot_event_bits & SLOT_TIMER9_EVENT) ||
				  (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)));

	if (!(slot_context->slot_event_bits & SLOT_TIMER9_EVENT)) {
		//
		// delete the timer because we got an event other than the timer
		//
		del_timer_sync(&slot_context->slot_timer9);
	}

	if ((shpc_context->shpc_event_bits & CMD_COMPLETION_EVENT) ||
	    (slot_context->slot_event_bits & SLOT_TIMER9_EVENT)) {

		//
		// Flag this slot as DISABLED (if enabled)
		//
		if ( hp_flag_slot_as_disabled( shpc_context, slot_context )) {
			//
			// Wait for settling time
			//
			slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_to_slot_disabled_wait_for_disable_timeout;
		} else {
			//
			// Wait for next request
			//
			slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_disabled_wait_for_slot_request;
		}
	} else {			  // exit_request_event
		status = STATUS_UNSUCCESSFUL;
	}
	//
	// Release command MUTEX
	//
	hp_set_slot_event_bit(slot_context, CMD_RELEASE_EVENT);
	wake_up_interruptible(&slot_context->cmd_release_event);

	return status;
}

// ****************************************************************************
//
// hp_to_slot_disabled_wait_for_DisableTimeout()
//
// ****************************************************************************
long
	hp_to_slot_disabled_wait_for_disable_timeout(
						    struct shpc_context* shpc_context,
						    struct slot_context* slot_context
						    )
{
	long status = STATUS_SUCCESS;

	dbg("%s -->slot_id[ %d:%d ]", __FUNCTION__, shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Setup our timer
	//
	hp_clear_slot_event_bit(slot_context, SLOT_TIMER10_EVENT);
	slot_context->slot_timer10.data = (unsigned long)slot_context;
	slot_context->slot_timer10.function = hp_slot_timer10_func;
	slot_context->slot_timer10.expires = jiffies + ONE_SEC_TIMEOUT;
	add_timer(&slot_context->slot_timer10);

	//
	// Wait for timeout
	//
	wait_event_interruptible(slot_context->slot_event,
				 ((slot_context->slot_event_bits & SLOT_TIMER10_EVENT) ||
				  (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)));

	if (!(slot_context->slot_event_bits & SLOT_TIMER10_EVENT)) {
		//
		// delete the timer because we got an event other than the timer
		//
		del_timer_sync(&slot_context->slot_timer10);
	}

	//
	// timeout
	//
	if (slot_context->slot_event_bits & SLOT_TIMER10_EVENT) {
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_to_slot_disabled_wait_for_bus_available;
	}
	//
	// exit_request_event
	//
	else {
		status = STATUS_UNSUCCESSFUL;
	}
	return status;
}


// ****************************************************************************
//
// hp_to_slot_disabled_wait_for_bus_available() 
//
// ****************************************************************************
long
	hp_to_slot_disabled_wait_for_bus_available(
						  struct shpc_context* shpc_context,
						  struct slot_context* slot_context
						  )
{
	unsigned long           old_irq_flags;
	long status = STATUS_SUCCESS;
	enum shpc_speed_mode max_speed_mode;

	dbg("%s -->slot_id[ %d:%d ]",  __FUNCTION__, shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Wait for Bus Available MUTEX
	//
	hp_set_slot_event_bit(slot_context, BUS_ACQUIRE_EVENT);
	wake_up_interruptible(&slot_context->bus_acquire_event);

	wait_event_interruptible(slot_context->slot_event,
				 ((shpc_context->shpc_event_bits & BUS_AVAILABLE_MUTEX_EVENT) ||
				  (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)));

	if (shpc_context->shpc_event_bits & BUS_AVAILABLE_MUTEX_EVENT) {

		//
		// Clear bus availabe mutex event
		//
		spin_lock_irqsave( &shpc_context->shpc_spinlock, old_irq_flags );
		hp_clear_shpc_event_bit(shpc_context, BUS_AVAILABLE_MUTEX_EVENT);
		spin_unlock_irqrestore( &shpc_context->shpc_spinlock, old_irq_flags );
		//
		// Grab global spinlock to check current speed/mode settings
		//
		spin_lock_irqsave( &shpc_context->shpc_spinlock, old_irq_flags );

		//
		// Flag this slot out of contetion for bus speed/mode changes
		//
		slot_context->in_bus_speed_mode_contention = FALSE;

		//
		// Enabled slots running at maximum speed/mode?
		//
		if ( shpc_context->slots_enabled ) {
			max_speed_mode = hp_GetMaxSpeedModeForEnabledSlots( shpc_context, shpc_context->max_speed_mode );

			//
			// Signal enabled slots to release the bus, then change bus speed/mode
			//
			if ( shpc_context->bus_speed_mode != max_speed_mode ) {
				hp_signal_enabled_slots_to_rebalance_bus( shpc_context );
			}
		}

		//
		// Release global spinlock since we're done checking speed/mode
		//
		spin_unlock_irqrestore( &shpc_context->shpc_spinlock, old_irq_flags );

		//
		// Wait for next request on this slot
		//
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_disabled_wait_for_slot_request;
	} else {			  // exit_request_event
		status = STATUS_UNSUCCESSFUL;
	}

	//
	// Release Bus MUTEX
	//
	hp_set_slot_event_bit(slot_context, BUS_RELEASE_EVENT);
	wake_up_interruptible(&slot_context->bus_release_event);

	return status;
}

