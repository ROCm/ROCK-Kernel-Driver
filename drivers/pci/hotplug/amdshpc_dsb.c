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
// hp_at_slot_enabled_wait_for_slot_request()
//
// ****************************************************************************
long
	hp_at_slot_enabled_wait_for_slot_request(
						struct shpc_context* shpc_context,
						struct slot_context* slot_context
						)
{
	unsigned long           old_irq_flags;
	long status = STATUS_SUCCESS;
	struct slot_status_info slot_status;
	unsigned long  DevNodes;

	dbg("%s -->slot_id[ %d:%d ]",__FUNCTION__ , shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Slot Enabled: complete pending slot request
	//
	if ( slot_context->slot_completion.done ) {
		dbg("%s -->ENABLE_DONE: slot_id[ %d:%d ]  card_speed_mode_cap[ %d+%d ]  bus_speed_mode[ %d ]",__FUNCTION__ ,
		    shpc_context->shpc_instance, slot_context->slot_number-1,
		    slot_context->card_speed_mode_cap, slot_context->card_pci66_capable,
		    shpc_context->bus_speed_mode );
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
	hp_clear_slot_event_bit(slot_context, ATTN_BUTTON_EVENT | SLOT_REQUEST_EVENT | BUS_REBALANCE_EVENT);
	spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );

	//
	// Wait for slot request
	//
	dbg("%s waiting for slot_event -->slot_id[ %d:%d ]",__FUNCTION__ , shpc_context->shpc_instance, slot_context->slot_number-1 );

	wait_event_interruptible(slot_context->slot_event,
				 ((slot_context->slot_event_bits & ATTN_BUTTON_EVENT) ||
				  (slot_context->slot_event_bits & SLOT_REQUEST_EVENT) ||
				  (slot_context->slot_event_bits & BUS_REBALANCE_EVENT) ||
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
		// Notify unrequested removal
		//
		slot_context->slot_completion.hw_initiated = TRUE;
		slot_context->slot_completion.type = SHPC_ASYNC_SURPRISE_REMOVE;
		slot_context->slot_completion.failed = HP_FALSE;
		slot_context->slot_completion.request_context = NULL;
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
	// bus_rebalance_event
	//
	else if (slot_context->slot_event_bits & BUS_REBALANCE_EVENT) {
		//
		// Clear Quiesced EVENT before invoking Callback()
		//
		dbg("%s -->RECEIVED BUS_REBALANCE_EVENT",__FUNCTION__);
		spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
		slot_context->quiesce_requests = 0;
		slot_context->quiesce_replies = 0;
		slot_context->slot_quiesced = FALSE;
		hp_clear_slot_event_bit(slot_context, QUIESCE_EVENT);
		spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );

		//
		// Call Completion Callback() to quiesce DevNode(s)
		//
		memset(&slot_status, 0, sizeof(struct slot_status_info));
		DevNodes = shpc_context->async_callback(
						       shpc_context->driver_context,
						       slot_context->slot_number - 1,
						       SHPC_ASYNC_QUIESCE_DEVNODE_QUIET,
						       slot_status,
						       ( void* )(unsigned long)slot_context->slot_psn );

		//
		// Update request count
		//
		spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
		slot_context->quiesce_requests = DevNodes;
		if ( slot_context->quiesce_requests == 0 ||
		     slot_context->quiesce_replies >= slot_context->quiesce_requests ) {
			slot_context->slot_quiesced = TRUE;
			hp_send_slot_event(slot_context, QUIESCE_EVENT);
		}
		spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );

		//
		// Wait for DevNode quiescing
		//
		dbg("%s -->BUS_REBALANCE: slot_id[ %d:%d ]",__FUNCTION__ ,
		    shpc_context->shpc_instance, slot_context->slot_number-1 );
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_enabled_wait_for_stop_on_bus_rebalance;
	}
	//
	// attn_button_event
	//
	else if (slot_context->slot_event_bits & ATTN_BUTTON_EVENT) {
		//
		// Set completion info for HW-initiated request
		//
		slot_context->slot_completion.hw_initiated = TRUE;
		slot_context->slot_completion.type = SHPC_ASYNC_DISABLE_SLOT;
		slot_context->slot_completion.request_context = NULL;

		//
		// Grab Command MUTEX to blink Power LED
		//
		dbg("%s -->DISABLE_REQ: slot_id[ %d:%d ]",__FUNCTION__ ,
		    shpc_context->shpc_instance, slot_context->slot_number-1 );
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_enabled_wait_for_led_cmd_available;
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
		// Request to disable slot?
		//
		if ( slot_context->slot_request.type == SHPC_ASYNC_DISABLE_SLOT ) {
			//
			// Grab Command MUTEX to blink Power LED
			//
			dbg("%s -->DISABLE_REQ: slot_id[ %d:%d ]",__FUNCTION__ ,
			    shpc_context->shpc_instance, slot_context->slot_number-1 );
			slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_enabled_wait_for_led_cmd_available;
		} else {
			//
			// Slot already enabled, just complete the request
			//
			dbg("%s -->ENABLE_REQ: slot_id[ %d:%d ]",__FUNCTION__ ,
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
// hp_at_slot_enabled_wait_for_stop_on_bus_rebalance()
//
// ****************************************************************************
long
	hp_at_slot_enabled_wait_for_stop_on_bus_rebalance(
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
	hp_clear_slot_event_bit(slot_context, SLOT_TIMER1_EVENT);
	slot_context->slot_timer1.data = (unsigned long)slot_context;
	slot_context->slot_timer1.function = hp_slot_timer1_func;
	slot_context->slot_timer1.expires = jiffies + QUIESCE_QUIET_TIMEOUT;
	add_timer(&slot_context->slot_timer1);

	//
	// Wait for Quiescing EVENT
	//
	wait_event_interruptible(slot_context->slot_event,
				 ((slot_context->slot_event_bits & QUIESCE_EVENT) ||
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
		// Notify unrequested removal
		//
		slot_context->slot_completion.hw_initiated = TRUE;
		slot_context->slot_completion.type = SHPC_ASYNC_SURPRISE_REMOVE;
		slot_context->slot_completion.failed = HP_FALSE;
		slot_context->slot_completion.request_context = NULL;
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
	// quiesce_event, timeout
	//
	else if ((slot_context->slot_event_bits & QUIESCE_EVENT) || (slot_context->slot_event_bits & SLOT_TIMER1_EVENT)) {
		spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
		if ((slot_context->slot_event_bits & SLOT_TIMER1_EVENT) || slot_context->slot_quiesced ) {
			spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );
			//
			// Grab Command MUTEX to set slot at power-only
			//
			if ((slot_context->slot_event_bits & SLOT_TIMER1_EVENT)) {
				dbg("%s -->BUS_REBALANCE: slot_id[ %d:%d ] Quiesce timeout",__FUNCTION__ ,
				    shpc_context->shpc_instance, slot_context->slot_number-1 );
			} else {
				dbg("%s -->BUS_REBALANCE: slot_id[ %d:%d ] Slot Quiesced",__FUNCTION__ ,
				    shpc_context->shpc_instance, slot_context->slot_number-1 );
			}
			slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_enabled_wait_for_power_cmd_available;
		} else {
			spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );
			//
			// Cancel bus re-balancing and treat it as a "Slot Enabled" request
			//
			slot_context->slot_completion.hw_initiated = TRUE;
			slot_context->slot_completion.type = SHPC_ASYNC_ENABLE_SLOT;
			slot_context->slot_completion.request_context = NULL;
			slot_context->slot_completion.failed = HP_FALSE;
			slot_context->slot_completion.done = TRUE;

			dbg("%s -->BUS_REBALANCE: slot_id[ %d:%d ] Cancelled: BUSY DevNode",__FUNCTION__ ,
			    shpc_context->shpc_instance, slot_context->slot_number-1 );
			slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_enabled_wait_for_slot_request;
		}
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
// hp_at_slot_enabled_wait_for_power_cmd_available() 
//
// ****************************************************************************
long
	hp_at_slot_enabled_wait_for_power_cmd_available(
						       struct shpc_context* shpc_context,
						       struct slot_context* slot_context
						       )
{
	struct task_struct;
	u16  command_reg = 0;
	unsigned long   old_irq_flags;
	long status = STATUS_SUCCESS;

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
		// Notify unrequested removal
		//
		slot_context->slot_completion.hw_initiated = TRUE;
		slot_context->slot_completion.type = SHPC_ASYNC_SURPRISE_REMOVE;
		slot_context->slot_completion.failed = HP_FALSE;
		slot_context->slot_completion.request_context = NULL;
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
		// Set slot to "Disable" and blink Power LED
		//
		command_reg |= SHPC_SLOT_OPERATION;
		command_reg |= SHPC_PWR_LED_BLINK;
		command_reg |= SHPC_ATTN_LED_NO_CHANGE;
		command_reg |= SHPC_DISABLE_SLOT;
		command_reg |= (slot_context->slot_number << SLOT_TGT_OFFSET);
		writew(command_reg, shpc_context->mmio_base_addr + SHPC_COMMAND_REG_OFFSET);

		//
		// Wait for command to complete (while holding MUTEX)
		//
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_enabled_wait_for_power_cmd_completion;
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
// hp_at_slot_enabled_wait_for_power_cmd_completion()
//
// ****************************************************************************
long
	hp_at_slot_enabled_wait_for_power_cmd_completion(
							struct shpc_context* shpc_context,
							struct slot_context* slot_context
							)
{
	unsigned long           old_irq_flags;
	long status = STATUS_SUCCESS;
	struct slot_status_info slot_status;
	u16 status_reg;

	dbg("%s -->slot_id[ %d:%d ]",__FUNCTION__ , shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Setup our timer
	//
	hp_clear_slot_event_bit(slot_context, SLOT_TIMER2_EVENT);
	slot_context->slot_timer2.data = (unsigned long)slot_context;
	slot_context->slot_timer2.function = hp_slot_timer2_func;
	slot_context->slot_timer2.expires = jiffies + FIFTEEN_SEC_TIMEOUT;
	add_timer(&slot_context->slot_timer2);

	//
	// Wait for Command Completion EVENT while holding MUTEX
	//
	wait_event_interruptible(slot_context->slot_event,
				 ((shpc_context->shpc_event_bits & CMD_COMPLETION_EVENT) ||
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
		// Notify unrequested removal
		//
		slot_context->slot_completion.hw_initiated = TRUE;
		slot_context->slot_completion.type = SHPC_ASYNC_SURPRISE_REMOVE;
		slot_context->slot_completion.failed = HP_FALSE;
		slot_context->slot_completion.request_context = NULL;
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
		 (slot_context->slot_event_bits & SLOT_TIMER2_EVENT)) {
		//
		// Command completed OK?
		//
		status_reg = readw(shpc_context->mmio_base_addr + SHPC_STATUS_REG_OFFSET);

		if ( ((status_reg & STS_BSY_MASK) >> STS_BSY_OFFSET) == SHPC_STATUS_CLEARED &&
		     ((status_reg & MRLO_ERR_MASK) >> MRLO_ERR_OFFSET) == SHPC_STATUS_CLEARED &&
		     ((status_reg & INVCMD_ERR_MASK) >> INVCMD_ERR_OFFSET) == SHPC_STATUS_CLEARED ) {
			//
			// Flag this slot as DISABLED
			//
			hp_flag_slot_as_disabled( shpc_context, slot_context );

			//
			// Call Completion Callback(): slot disabled
			//
			hp_QuerySlotStatus(     shpc_context, slot_context->slot_number - 1, &slot_status );
			slot_status.lu_request_failed = HP_FALSE;
			shpc_context->async_callback(
						    shpc_context->driver_context,
						    slot_context->slot_number - 1,
						    SHPC_ASYNC_DISABLE_SLOT,
						    slot_status,
						    NULL );

			//
			// Signal registered user EVENT
			//
			hp_signal_user_event( shpc_context );

			//
			// Treat it as an on-going ENABLE request
			//
			slot_context->slot_completion.hw_initiated = TRUE;
			slot_context->slot_completion.type = SHPC_ASYNC_ENABLE_SLOT;
			slot_context->slot_completion.request_context = NULL;

			//
			// Grab Command MUTEX to power-on the slot
			//
			slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_disabled_wait_for_power_cmd_available;
		} else {
			//
			// Treat it as a HW-initiated DISABLE request
			//
			slot_context->slot_completion.hw_initiated = TRUE;
			slot_context->slot_completion.type = SHPC_ASYNC_DISABLE_SLOT;
			slot_context->slot_completion.failed = HP_FALSE;
			slot_context->slot_completion.request_context = NULL;
			slot_context->slot_completion.done = TRUE;

			//
			// Grab Command MUTEX to disable slot
			//
			dbg("%s -->CMD_ERROR: slot_id[ %d:%d ]  Cmd[ %X ]",__FUNCTION__ ,
			    shpc_context->shpc_instance, slot_context->slot_number-1, status_reg);
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
// hp_at_slot_enabled_wait_for_led_cmd_available()
//
// ****************************************************************************
long
	hp_at_slot_enabled_wait_for_led_cmd_available(
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
		// Blink Power LED
		//
		command_reg |= SHPC_SLOT_OPERATION;
		command_reg |= SHPC_PWR_LED_BLINK;
		command_reg |= SHPC_ATTN_LED_NO_CHANGE;
		command_reg |= SHPC_SLOT_NO_CHANGE;
		command_reg |= (slot_context->slot_number << SLOT_TGT_OFFSET);
		writew(command_reg, shpc_context->mmio_base_addr + SHPC_COMMAND_REG_OFFSET);

		//
		// Wait for command to complete (while holding MUTEX)
		//
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_enabled_wait_for_led_cmd_completion;
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
		status =STATUS_UNSUCCESSFUL;
	}
	return status;
}


// ****************************************************************************
//
// hp_at_slot_enabled_wait_for_led_cmd_completion()
//
// ****************************************************************************
long
	hp_at_slot_enabled_wait_for_led_cmd_completion(
						      struct shpc_context* shpc_context,
						      struct slot_context* slot_context
						      )
{
	unsigned long           old_irq_flags;
	long status = STATUS_SUCCESS;
	struct slot_status_info slot_status;
	u16 status_reg;
	unsigned long  DevNodes;

	dbg("%s -->slot_id[ %d:%d ]",__FUNCTION__ , shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Setup our timer
	//
	hp_clear_slot_event_bit(slot_context, SLOT_TIMER3_EVENT);
	slot_context->slot_timer3.data = (unsigned long)slot_context;
	slot_context->slot_timer3.function = hp_slot_timer3_func;
	slot_context->slot_timer3.expires = jiffies + ONE_SEC_TIMEOUT;
	add_timer(&slot_context->slot_timer3);

	//
	// Wait for Command Completion EVENT while holding MUTEX
	//
	wait_event_interruptible(slot_context->slot_event,
				 ((shpc_context->shpc_event_bits & CMD_COMPLETION_EVENT) ||
				  (slot_context->slot_event_bits & ALERT_EVENT) ||
				  (slot_context->slot_event_bits & SLOT_TIMER3_EVENT) ||
				  (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)));

	if (!(slot_context->slot_event_bits & SLOT_TIMER3_EVENT)) {
		//
		// delete the timer because we got an event other than the timer
		//
		del_timer_sync(&slot_context->slot_timer3);
	}
	dbg("%s -->slot bits %08X   shpc bits  %08X",__FUNCTION__ ,
	    slot_context->slot_event_bits,shpc_context->shpc_event_bits);

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
		 (slot_context->slot_event_bits & SLOT_TIMER3_EVENT)) {
		//
		// Command completed OK?
		//
		status_reg = readw(shpc_context->mmio_base_addr + SHPC_STATUS_REG_OFFSET);

		if ( ((status_reg & STS_BSY_MASK) >> STS_BSY_OFFSET) == SHPC_STATUS_CLEARED &&
		     ((status_reg & MRLO_ERR_MASK) >> MRLO_ERR_OFFSET) == SHPC_STATUS_CLEARED &&
		     ((status_reg & INVCMD_ERR_MASK) >> INVCMD_ERR_OFFSET) == SHPC_STATUS_CLEARED ) {
			//
			// Allow cancellation of operation?
			//
			if ( slot_context->slot_completion.hw_initiated ) {
				//
				// Wait for 5 sec timeout
				//
				slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_enabled_wait_for_timeout;
			} else {
				//
				// Clear Quiesced EVENT before invoking Callback()
				//
				spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
				slot_context->quiesce_requests = 0;
				slot_context->quiesce_replies = 0;
				slot_context->slot_quiesced = FALSE;
				hp_clear_slot_event_bit(slot_context, QUIESCE_EVENT);
				spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );

				//
				// Call Completion Callback() to quiesce DevNode(s)
				//
				memset(&slot_status, 0, sizeof(struct slot_status_info));
				DevNodes = shpc_context->async_callback(
								       shpc_context->driver_context,
								       slot_context->slot_number - 1,
								       SHPC_ASYNC_QUIESCE_DEVNODE,
								       slot_status,
								       ( void* )(unsigned long)slot_context->slot_psn );

				//
				// Update request count
				//
				spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
				slot_context->quiesce_requests = DevNodes;
				if ( slot_context->quiesce_requests == 0 ||
				     slot_context->quiesce_replies >= slot_context->quiesce_requests ) {
					slot_context->slot_quiesced = TRUE;
					hp_send_slot_event(slot_context, QUIESCE_EVENT);
				}
				spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );

				//
				// Wait for DevNode quiescing
				//
				slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_enabled_wait_for_stop_on_slot_disable;
			}
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
// hp_at_slot_enabled_wait_for_timeout()
//
// ****************************************************************************
long
	hp_at_slot_enabled_wait_for_timeout(
					   struct shpc_context* shpc_context,
					   struct slot_context* slot_context
					   )
{
	unsigned long           old_irq_flags;
	long status = STATUS_SUCCESS;
	struct slot_status_info slot_status;
	unsigned long  DevNodes;

	dbg("%s -->slot_id[ %d:%d ]",__FUNCTION__ , shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Clear Button EVENT before waiting
	//
	spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
	down_interruptible(&slot_context->slot_event_bits_semaphore);
	slot_context->slot_event_bits &= ~ATTN_BUTTON_EVENT;
	up(&slot_context->slot_event_bits_semaphore);
	spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );

	//
	// Setup our timer
	//
	hp_clear_slot_event_bit(slot_context, SLOT_TIMER7_EVENT);
	slot_context->slot_timer7.data = (unsigned long)slot_context;
	slot_context->slot_timer7.function = hp_slot_timer7_func;
	slot_context->slot_timer7.expires = jiffies + FIVE_SEC_TIMEOUT;
	add_timer(&slot_context->slot_timer7);

	//
	// Wait for 5 sec timeout
	//
	wait_event_interruptible(slot_context->slot_event,
				 ((slot_context->slot_event_bits & ATTN_BUTTON_EVENT) ||
				  (slot_context->slot_event_bits & ALERT_EVENT) ||
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
		dbg("%s -->ALERT: slot_id[ %d:%d ]  LSR_13:0[ %X ]",__FUNCTION__ ,
		    shpc_context->shpc_instance, slot_context->slot_number-1,
		    readl( slot_context->logical_slot_addr ) & 0x3F );
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_to_slot_disabled_wait_for_disable_cmd_available;
	}
	//
	// attn_button_event
	//
	else if (slot_context->slot_event_bits & ATTN_BUTTON_EVENT) {
		//
		// Cancel request, grab Command MUTEX to Power LED back ON
		//
		dbg("%s -->DISABLE_REQ: slot_id[ %d:%d ] Cancelled: Attn Button",__FUNCTION__ ,
		    shpc_context->shpc_instance, slot_context->slot_number-1 );
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_to_slot_enabled_wait_for_led_cmd_available;
	}
	//
	// timeout
	//
	else if (slot_context->slot_event_bits & SLOT_TIMER7_EVENT) {
		//
		// Clear Quiesced EVENT before invoking Callback()
		//
		spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
		slot_context->quiesce_requests = 0;
		slot_context->quiesce_replies = 0;
		slot_context->slot_quiesced = FALSE;
		hp_clear_slot_event_bit(slot_context, QUIESCE_EVENT);
		spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );

		//
		// Call Completion Callback() to quiesce DevNode(s)
		//
		memset(&slot_status, 0, sizeof(struct slot_status_info));
		DevNodes = shpc_context->async_callback(
						       shpc_context->driver_context,
						       slot_context->slot_number - 1,
						       SHPC_ASYNC_QUIESCE_DEVNODE,
						       slot_status,
						       ( void* )(unsigned long)slot_context->slot_psn );

		//
		// Update request count
		//
		spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
		slot_context->quiesce_requests = DevNodes;
		if ( slot_context->quiesce_requests == 0 ||
		     slot_context->quiesce_replies == slot_context->quiesce_requests ) {
			slot_context->slot_quiesced = TRUE;
			hp_send_slot_event(slot_context, QUIESCE_EVENT);
		}
		spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );

		//
		// Wait for DevNode quiescing
		//
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_enabled_wait_for_stop_on_slot_disable;
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
// hp_at_slot_enabled_wait_for_stop_on_slot_disable()
//
// ****************************************************************************
long
	hp_at_slot_enabled_wait_for_stop_on_slot_disable(
							struct shpc_context* shpc_context,
							struct slot_context* slot_context
							)
{
	unsigned long           old_irq_flags;
	long status = STATUS_SUCCESS;
	struct slot_status_info slot_status;
	unsigned long  DevNodes;

	dbg("%s -->slot_id[ %d:%d ]", __FUNCTION__ ,shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Setup our timer
	//
	hp_clear_slot_event_bit(slot_context, SLOT_TIMER4_EVENT);
	slot_context->slot_timer4.data = (unsigned long)slot_context;
	slot_context->slot_timer4.function = hp_slot_timer4_func;
	slot_context->slot_timer4.expires = jiffies + QUIESCE_QUIET_TIMEOUT;
	add_timer(&slot_context->slot_timer4);

	//
	// Wait for Quiescing EVENT
	//
	wait_event_interruptible(slot_context->slot_event,
				 ((slot_context->slot_event_bits & QUIESCE_EVENT) ||
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
	// quiesce_event
	//
	else if (slot_context->slot_event_bits & QUIESCE_EVENT) {
		spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
		if ( slot_context->slot_quiesced ) {
			spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );
			//
			// Complete succesful DISABLE request
			//
			slot_context->slot_completion.failed = HP_FALSE;
			slot_context->slot_completion.done = TRUE;

			//
			// Grab Command MUTEX to disable slot
			//
			//
			dbg("%s -->DISABLE_REQ: slot_id[ %d:%d ] Slot Quiesced",__FUNCTION__ ,
			    shpc_context->shpc_instance, slot_context->slot_number-1 );
			slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_to_slot_disabled_wait_for_disable_cmd_available;
		} else {
			spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );
			//
			// Cancel request, grab Command MUTEX to turn Power LED back ON
			//
			slot_context->slot_completion.hw_initiated = TRUE;
			slot_context->slot_completion.type = SHPC_ASYNC_ENABLE_SLOT;
			slot_context->slot_completion.request_context = NULL;
			slot_context->slot_completion.failed = HP_FALSE;
			slot_context->slot_completion.done = TRUE;

			dbg("%s -->DISABLE_REQ: slot_id[ %d:%d ] Cancelled: BUSY DevNode",__FUNCTION__ ,
			    shpc_context->shpc_instance, slot_context->slot_number-1 );
			slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_to_slot_enabled_wait_for_led_cmd_available;
		}
	}
	//
	// timeout
	//
	else if (slot_context->slot_event_bits & SLOT_TIMER4_EVENT) {
		//
		// Clear Quiesced EVENT before invoking Callback()
		//
		spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
		slot_context->quiesce_requests = 0;
		slot_context->quiesce_replies = 0;
		slot_context->slot_quiesced = FALSE;
		hp_clear_slot_event_bit(slot_context, QUIESCE_EVENT);
		spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );

		//
		// Call Completion Callback() to quiesce DevNode(s)
		//
		memset(&slot_status, 0, sizeof(struct slot_status_info));
		DevNodes = shpc_context->async_callback(
						       shpc_context->driver_context,
						       slot_context->slot_number - 1,
						       SHPC_ASYNC_QUIESCE_DEVNODE_QUIET,
						       slot_status,
						       ( void* )(unsigned long)slot_context->slot_psn );

		//
		// Update request count
		//
		spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
		slot_context->quiesce_requests = DevNodes;
		if ( slot_context->quiesce_requests == 0 ||
		     slot_context->quiesce_replies == slot_context->quiesce_requests ) {
			slot_context->slot_quiesced = TRUE;
			hp_send_slot_event(slot_context, QUIESCE_EVENT);
		}
		spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );

		//
		// Wait for DevNode quiescing
		//
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_enabled_wait_for_stop_on_slot_disable_quiet;
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
// hp_at_slot_enabled_wait_for_stop_on_slot_disable_quiet()
//
// ****************************************************************************
long
	hp_at_slot_enabled_wait_for_stop_on_slot_disable_quiet(
							      struct shpc_context* shpc_context,
							      struct slot_context* slot_context
							      )
{
	unsigned long           old_irq_flags;
	long status = STATUS_SUCCESS;

	dbg("%s -->slot_id[ %d:%d ]", __FUNCTION__ ,shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Setup our timer
	//
	hp_clear_slot_event_bit(slot_context, SLOT_TIMER5_EVENT);
	slot_context->slot_timer5.data = (unsigned long)slot_context;
	slot_context->slot_timer5.function = hp_slot_timer5_func;
	slot_context->slot_timer5.expires = jiffies + QUIESCE_QUIET_TIMEOUT;
	add_timer(&slot_context->slot_timer5);

	//
	// Wait for Quiescing EVENT
	//
	wait_event_interruptible(slot_context->slot_event,
				 ((slot_context->slot_event_bits & QUIESCE_EVENT) ||
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
	// quiesce_event, timeout
	//
	else if ((slot_context->slot_event_bits & QUIESCE_EVENT) ||
		 (slot_context->slot_event_bits & SLOT_TIMER5_EVENT)) {
		spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
		if ((slot_context->slot_event_bits & SLOT_TIMER5_EVENT) || slot_context->slot_quiesced ) {
			spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );
			//
			// Complete succesful DISABLE request
			//
			slot_context->slot_completion.failed = HP_FALSE;
			slot_context->slot_completion.done = TRUE;

			//
			// Grab Command MUTEX to disable slot
			//
			//
			if (slot_context->slot_event_bits & SLOT_TIMER5_EVENT) {
				dbg("%s -->DISABLE_REQ: slot_id[ %d:%d ] Quiesce timeout",__FUNCTION__ ,
				    shpc_context->shpc_instance, slot_context->slot_number-1 );
			} else {
				dbg("%s -->DISABLE_REQ: slot_id[ %d:%d ] Slot Quiesced",__FUNCTION__ ,
				    shpc_context->shpc_instance, slot_context->slot_number-1 );
			}
			slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_to_slot_disabled_wait_for_disable_cmd_available;
		} else {
			spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );
			//
			// Cancel request, grab Command MUTEX to turn Power LED back ON
			//
			dbg("%s -->DISABLE_REQ: slot_id[ %d:%d ] Cancelled: BUSY DevNode",__FUNCTION__ ,
			    shpc_context->shpc_instance, slot_context->slot_number-1 );
			slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_to_slot_enabled_wait_for_led_cmd_available;
		}
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
// hp_to_slot_enabled_wait_for_led_cmd_available()
//
// ****************************************************************************
long
	hp_to_slot_enabled_wait_for_led_cmd_available(
						     struct shpc_context* shpc_context,
						     struct slot_context* slot_context
						     )
{
	unsigned long           old_irq_flags;
	long status = STATUS_SUCCESS;
	u16 command_reg = 0;

	dbg("%s -->slot_id[ %d:%d ]", __FUNCTION__ ,shpc_context->shpc_instance, slot_context->slot_number-1 );

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
		// Turn Power LED back ON
		//
		command_reg |= SHPC_SLOT_OPERATION;
		command_reg |= SHPC_PWR_LED_ON;
		command_reg |= SHPC_ATTN_LED_NO_CHANGE;
		command_reg |= SHPC_SLOT_NO_CHANGE;
		command_reg |= (slot_context->slot_number << SLOT_TGT_OFFSET);
		writew(command_reg, shpc_context->mmio_base_addr + SHPC_COMMAND_REG_OFFSET);

		//
		// Wait for command to complete (while holding MUTEX)
		//
		slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_to_slot_enabled_wait_for_led_cmd_completion;
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
// hp_to_slot_enabled_wait_for_led_cmd_completion()
//
// ****************************************************************************
long
	hp_to_slot_enabled_wait_for_led_cmd_completion(
						      struct shpc_context* shpc_context,
						      struct slot_context* slot_context
						      )
{
	unsigned long           old_irq_flags;
	long status = STATUS_SUCCESS;
	u16 status_reg;

	dbg("%s -->slot_id[ %d:%d ]",__FUNCTION__ , (int)shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Setup our timer
	//
	hp_clear_slot_event_bit(slot_context, SLOT_TIMER6_EVENT);
	slot_context->slot_timer6.data = (unsigned long)slot_context;
	slot_context->slot_timer6.function = hp_slot_timer6_func;
	slot_context->slot_timer6.expires = jiffies + ONE_SEC_TIMEOUT;
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
		 (slot_context->slot_event_bits & SLOT_TIMER6_EVENT)) {
		//
		// Command completed OK?
		//
		status_reg = readw(shpc_context->mmio_base_addr + SHPC_STATUS_REG_OFFSET);

		if ( ((status_reg & STS_BSY_MASK) >> STS_BSY_OFFSET) == SHPC_STATUS_CLEARED &&
		     ((status_reg & MRLO_ERR_MASK) >> MRLO_ERR_OFFSET) == SHPC_STATUS_CLEARED &&
		     ((status_reg & INVCMD_ERR_MASK) >> INVCMD_ERR_OFFSET) == SHPC_STATUS_CLEARED ) {
			//
			// Wait for next request
			//
			slot_context->slot_function = (SLOT_STATE_FUNCTION)hp_at_slot_enabled_wait_for_slot_request;
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