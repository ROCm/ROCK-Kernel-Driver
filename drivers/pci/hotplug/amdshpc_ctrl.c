/*
 * Copyright (C) 2001,2003 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001,2003 IBM Corp.
 * Copyright (C) 2002-2003 Advanced Micro Devices
 *
 * YOUR USE OF THIS CODE IS SUBJECT TO THE TERMS
 * AND CONDITIONS OF THE GNU GENERAL PUBLIC
 * LICENSE FOUND IN THE "GPL.TXT" FILE THAT IS
 * INCLUDED WITH THIS FILE AND POSTED AT
 * http://www.gnu.org/licenses/gpl.html
 *
 *
 * Send feedback to <david.keck@amd.com>
 *
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include "amdshpc_ddi.h"
#include "amdshpc.h"


// ****************************************************************************
//
// hp_slot_thread() @ PASSIVE_LEVEL
//
// ****************************************************************************
int hp_slot_thread(void* ptr)
{
	unsigned long		old_irq_flags;
	long status = STATUS_SUCCESS;
	struct shpc_context* shpc_context;
	struct slot_context* slot_context;
	union SLOT_STATUS_INFO slot_status;
	
	lock_kernel ();
	daemonize ("amdshpc_slot");
	unlock_kernel ();

	slot_context = (struct slot_context*) ptr;
	shpc_context = (struct shpc_context*) slot_context->shpc_context;

	//
	// Insertion/Removal State Machine (loops until requested to exit)
	//
	do {
		status = slot_context->slot_function( shpc_context, slot_context );
		//
		// Suspend?
		//
		if(!status) {
			spin_lock_irqsave(&shpc_context->shpc_spinlock, old_irq_flags);
			if(shpc_context->shpc_event_bits & SUSPEND_EVENT ) {
				status = STATUS_SUCCESS;
			}
			spin_unlock_irqrestore( &shpc_context->shpc_spinlock, old_irq_flags );

			if(status) {
				dbg( "%s-->SUSPEND: slot_id[ %d:%d ]",__FUNCTION__,
					(int)shpc_context->shpc_instance, slot_context->slot_number-1 );

				do {
					interruptible_sleep_on(&slot_context->slot_event);
				}while(!((shpc_context->shpc_event_bits & RESUME_EVENT) ||
					(shpc_context->shpc_event_bits & REMOVE_EVENT)));

				if(shpc_context->shpc_event_bits & REMOVE_EVENT ) {
					status = STATUS_UNSUCCESSFUL;
				}
				else {
					dbg("%s-->RESUME: slot_id[ %d:%d ]",__FUNCTION__,
						shpc_context->shpc_instance, slot_context->slot_number-1 );
				}
			}
		}
	} while(status);

	//
	// We're exiting, most likely due to an exit_request_event.  So, let's cleanup!
	//
	dbg("%s-->Slot Thread Termination: slot_id[ %d:%d ]",__FUNCTION__,
		shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Pending SW-initiated slot request?
	//
	if(slot_context->slot_event_bits & SLOT_REQUEST_EVENT ) {
		//
		// Complete it with failure code
		//
		hp_QuerySlotStatus(	shpc_context, slot_context->slot_number - 1, &slot_status );
		slot_status.x.lu_request_failed = HP_TRUE;
		shpc_context->async_callback(
			shpc_context->driver_context,
			slot_context->slot_number - 1,
			slot_context->slot_request.type,
			slot_status,
			slot_context->slot_request.request_context );

		//
		// Signal registered user EVENT
		//
		hp_signal_user_event( shpc_context );
	}
	return(status);
}


// ****************************************************************************
//
// hp_attn_led_thread() @ PASSIVE_LEVEL
//
// ****************************************************************************
int
hp_attn_led_thread(
	void* ptr
)
{
	unsigned long		old_irq_flags;
	long status = STATUS_SUCCESS;
	struct shpc_context* shpc_context;
	struct slot_context* slot_context;
	union SLOT_STATUS_INFO slot_status;
	
	lock_kernel ();
	daemonize ("amdshpc_led");
	unlock_kernel ();

	slot_context = (struct slot_context*) ptr;
	shpc_context = (struct shpc_context*) slot_context->shpc_context;

	//
	// Attention LED State Machine (loops until requested to exit)
	//
	do {
		status = slot_context->attn_led_function(shpc_context, slot_context);
		//
		// Suspend?
		//
		if(!status) {
			spin_lock_irqsave(&shpc_context->shpc_spinlock, old_irq_flags);
			if(shpc_context->shpc_event_bits & SUSPEND_EVENT ) {
				status = STATUS_SUCCESS;
			}
			spin_unlock_irqrestore( &shpc_context->shpc_spinlock, old_irq_flags );

			if(status) {
				dbg("%s-->SUSPEND: slot_id[ %d:%d ]",__FUNCTION__,
					shpc_context->shpc_instance, slot_context->slot_number-1 );

				do {
					interruptible_sleep_on(&slot_context->slot_event);
				}while(!((shpc_context->shpc_event_bits & RESUME_EVENT) ||
					(shpc_context->shpc_event_bits & REMOVE_EVENT)));

				if(shpc_context->shpc_event_bits & REMOVE_EVENT ) {
					status = STATUS_UNSUCCESSFUL;
				}
				else {
					dbg("%s-->RESUME: slot_id[ %d:%d ]",__FUNCTION__,
						shpc_context->shpc_instance, slot_context->slot_number-1 );
				}
			}
		}
	} while(status);

	//
	// We're exiting, most likely due to an exit_request_event.  So, let's cleanup!
	//
	dbg("%s-->LED Thread Termination: slot_id[ %d:%d ]",__FUNCTION__,
		shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Pending SW-initiated AttnLED request?
	//
	if(slot_context->slot_event_bits & ATTN_LED_REQUEST_EVENT ) {
		//
		// Complete it with failure code
		//
		hp_QuerySlotStatus(	shpc_context, slot_context->slot_number - 1, &slot_status );
		slot_status.x.lu_request_failed = HP_TRUE;
		shpc_context->async_callback(
			shpc_context->driver_context,
			slot_context->slot_number - 1,
			slot_context->attn_led_request.type,
			slot_status,
			slot_context->attn_led_request.request_context );

		//
		// Signal registered user EVENT
		//
		hp_signal_user_event( shpc_context );
	}
	return(status);
}


// ****************************************************************************
//
// hp_get_slot_configuration() @ Any IRQL
//
// ****************************************************************************
void
hp_get_slot_configuration(
	struct shpc_context* shpc_context
)
{
	struct slot_context* slot_context;
	union SHPC_SLOTS_AVAILABLE1_DWREG SlotAvail1Reg;
	union SHPC_SLOTS_AVAILABLE2_DWREG SlotAvail2Reg;
	union SHPC_SLOT_CONFIG_DWREG SlotConfigReg;
	union SHPC_LOGICAL_SLOT_DWREG logical_slot_reg;
	enum shpc_speed_mode max_speed_mode;
	u8 i;

	//
	// Get max number of slots available
	//
	SlotAvail1Reg.AsDWord = readl(shpc_context->mmio_base_addr + SHPC_SLOTS_AVAILABLE1_REG_OFFSET);
	SlotAvail2Reg.AsDWord = readl(shpc_context->mmio_base_addr + SHPC_SLOTS_AVAILABLE2_REG_OFFSET);
	//
	// Get slot configuration
	//
	SlotConfigReg.AsDWord = readl(shpc_context->mmio_base_addr + SHPC_SLOT_CONFIG_REG_OFFSET);

	//
	// Get number of available slots per speed/mode
	//
	shpc_context->slots_enabled = 0;
	shpc_context->number_of_slots = 0;
	if( SlotAvail1Reg.x.N_133PCIX ) {
		shpc_context->number_of_slots = ( u8 )SlotAvail1Reg.x.N_133PCIX;
		shpc_context->max_speed_mode = SHPC_BUS_PCIX_133;
	}
	else if( SlotAvail1Reg.x.N_100PCIX ) {
		shpc_context->number_of_slots = ( u8 )SlotAvail1Reg.x.N_100PCIX;
		shpc_context->max_speed_mode = SHPC_BUS_PCIX_100;
	}
	else if( SlotAvail1Reg.x.N_66PCIX ) {
		shpc_context->number_of_slots = ( u8 )SlotAvail1Reg.x.N_66PCIX;
		shpc_context->max_speed_mode = SHPC_BUS_PCIX_66;
	}
	else if( SlotAvail2Reg.x.N_66CONV ) {
		shpc_context->number_of_slots = ( u8 )SlotAvail2Reg.x.N_66CONV;
		shpc_context->max_speed_mode = SHPC_BUS_CONV_66;
	}
	else if( SlotAvail1Reg.x.N_33CONV ) {
		shpc_context->number_of_slots = ( u8 )SlotAvail1Reg.x.N_33CONV;
		shpc_context->max_speed_mode = SHPC_BUS_CONV_33;
	}

	if( shpc_context->number_of_slots ) {
		//
		// Be sure NSI field is not exceeded (this should not happen!)
		//
		if( shpc_context->number_of_slots > SlotConfigReg.x.NSI ) {
			shpc_context->number_of_slots = ( u8 )SlotConfigReg.x.NSI;
		}

		//
		// Limit slot count to what we're prepared to support
		//
		if( shpc_context->number_of_slots > SHPC_MAX_NUM_SLOTS ) {
			shpc_context->number_of_slots = SHPC_MAX_NUM_SLOTS;
		}

		//
		// Get current Bus speed/mode
		//
		shpc_context->bus_speed_mode = hp_get_bus_speed_mode( shpc_context );

		//
		// Initialize slot state based on HW disposition
		//
		for( i=0; i< shpc_context->number_of_slots; ++i ) {
			slot_context = &shpc_context->slot_context[ i ];

			//
			// Get Physical Slot Number (PSN-based)
			//
			if( SlotConfigReg.x.PSN_UP ) {
				slot_context->slot_psn = ( u8 )SlotConfigReg.x.PSN + i;
			}
			else {
				slot_context->slot_psn = ( u8 )SlotConfigReg.x.PSN - i;
			}

			//
			// Assign Logical Slot Number (1-based)
			//
			slot_context->slot_number = ( u8 )i+1;

			//
			// Get Card's speed/mode capabilities
			//
			hp_get_card_speed_mode( slot_context );

			//
			// Check current HW state
			//
			logical_slot_reg.AsDWord = readl( slot_context->logical_slot_addr );

			//
			// Already enabled: Card Present, MRL closed, Slot Enabled, No Power-Fault?
			//
			if( logical_slot_reg.x.PRSNT1_2 != SHPC_SLOT_EMPTY &&
				( logical_slot_reg.x.MRLS_IM == SHPC_MASKED ||
				logical_slot_reg.x.MRLS == SHPC_MRL_CLOSED ) &&
				logical_slot_reg.x.PF == SHPC_STATUS_CLEARED &&
				logical_slot_reg.x.S_STATE == SHPC_ENABLE_SLOT ) {
				//
				// Treat it as a SUCCESSFUL "Slot Enabled" HW-initiated request
				//
				slot_context->slot_completion.hw_initiated = TRUE;
				slot_context->slot_completion.type = SHPC_ASYNC_ENABLE_SLOT;
				slot_context->slot_completion.failed = HP_FALSE;
				slot_context->slot_completion.request_context = NULL;
				slot_context->slot_completion.done = TRUE;

				//
				// Flag as "Slot Enabled"
				//
				++shpc_context->slots_enabled;
				slot_context->slot_enabled = TRUE;
				slot_context->in_bus_speed_mode_contention = TRUE;
				if( logical_slot_reg.x.PIS == SHPC_LED_ON ) {
					slot_context->slot_function = (SLOT_STATE_FUNCTION) hp_at_slot_enabled_wait_for_slot_request;
				}
				else {
					slot_context->slot_function = (SLOT_STATE_FUNCTION) hp_to_slot_enabled_wait_for_led_cmd_available;
				}
			}
			else {
				//
				// Treat it as a SUCCESSFUL "Slot Disable" HW-initiated request
				//
				slot_context->slot_completion.hw_initiated = TRUE;
				slot_context->slot_completion.type = SHPC_ASYNC_DISABLE_SLOT;
				slot_context->slot_completion.failed = HP_FALSE;
				slot_context->slot_completion.request_context = NULL;
				slot_context->slot_completion.done = TRUE;

				//
				// Flag as "Slot Disabled"
				//
				slot_context->slot_enabled = FALSE;
				slot_context->in_bus_speed_mode_contention = FALSE;
				if( logical_slot_reg.x.S_STATE == SHPC_DISABLE_SLOT ) {
					slot_context->slot_function = (SLOT_STATE_FUNCTION) hp_at_slot_disabled_wait_for_slot_request;
				}
				else {
					slot_context->slot_function = (SLOT_STATE_FUNCTION) hp_to_slot_disabled_wait_for_disable_cmd_available;
				}
			}

			//
			// Set Attention LED function
			//
			if( logical_slot_reg.x.PF == SHPC_STATUS_SET ) {
				//
				// Turn it ON
				//
				slot_context->problem_detected = TRUE;
				slot_context->attn_led_function = (SLOT_STATE_FUNCTION) hp_wait_for_attn_led_back_to_normal_cmd_available;
			}
			else {
				//
				// Make sure it is turned OFF
				//
				slot_context->problem_detected = FALSE;
				if( logical_slot_reg.x.AIS == SHPC_LED_OFF ) {
					slot_context->attn_led_function = (SLOT_STATE_FUNCTION) hp_wait_for_attn_led_request;
				}
				else {
					slot_context->attn_led_function = (SLOT_STATE_FUNCTION) hp_wait_for_attn_led_back_to_normal_cmd_available;
				}
			}
		}

		//
		// Enabled slots running at maximum speed/mode?
		//
		if( shpc_context->slots_enabled ) {
			max_speed_mode = hp_get_max_speed_mode( shpc_context, shpc_context->max_speed_mode );

			//
			// Signal enabled slots to release the bus, then change bus speed/mode
			//
			if( shpc_context->bus_speed_mode != max_speed_mode ) {
				hp_signal_enabled_slots_to_rebalance_bus( shpc_context );
			}
		}
	}
}


// ****************************************************************************
//
// hp_enable_slot_interrupts() @ Any IRQL
//
// ****************************************************************************
void
hp_enable_slot_interrupts(
	struct slot_context* slot_context
)
{
	struct shpc_context* shpc_context = ( struct shpc_context* )slot_context->shpc_context;
	union SHPC_SLOT_CONFIG_DWREG SlotConfigReg;
	union SHPC_LOGICAL_SLOT_DWREG logical_slot_reg;

	//
	// Get HW implementation: Attention Button, MRL Sensor
	//
	SlotConfigReg.AsDWord = readl(shpc_context->mmio_base_addr + SHPC_SLOT_CONFIG_REG_OFFSET);

	//
	// Attention Button: Enabled only if implemented
	//
	logical_slot_reg.x.AB_IM = ( SlotConfigReg.x.ABI == SHPC_STATUS_SET ) ?
		SHPC_UNMASKED : SHPC_MASKED;
	logical_slot_reg.x.ABP_STS = SHPC_STATUS_SET;

	//
	// MRL Sensor: Enabled only if implemented (System Error Disabled)
	//
	logical_slot_reg.x.MRLS_IM = ( SlotConfigReg.x.MRLSI == SHPC_STATUS_SET ) ?
		SHPC_UNMASKED : SHPC_MASKED;
	logical_slot_reg.x.MRLS_SERRM = SHPC_MASKED;
	logical_slot_reg.x.MRLSC_STS = SHPC_STATUS_SET;

	//
	// Card Presence: Enabled
	//
	logical_slot_reg.x.CP_IM = SHPC_UNMASKED;
	logical_slot_reg.x.CPC_STS = SHPC_STATUS_SET;

	//
	// Isolated Power-Fault: Enabled
	//
	logical_slot_reg.x.IPF_IM = SHPC_UNMASKED;
	logical_slot_reg.x.IPF_STS = SHPC_STATUS_SET;

	//
	// Connected Power-Fault: Enabled (System Error Disabled)
	//
	logical_slot_reg.x.CPF_IM = SHPC_UNMASKED;
	logical_slot_reg.x.CPF_SERRM = SHPC_MASKED;
	logical_slot_reg.x.CPF_STS = SHPC_STATUS_SET;

	//
	// Update Mask and Status bits
	//
	writel(logical_slot_reg.AsDWord, slot_context->logical_slot_addr);
}


// ****************************************************************************
//
// hp_disable_slot_interrupts() @ Any IRQL
//
// ****************************************************************************
void
hp_disable_slot_interrupts(
	struct slot_context* slot_context
)
{
	union SHPC_LOGICAL_SLOT_DWREG logical_slot_reg;

	//
	// Get HW implementation: Attention Button, MRL Sensor
	//
	logical_slot_reg.AsDWord = readl(slot_context->logical_slot_addr);

	//
	// Attention Button: Disabled
	//
	logical_slot_reg.x.AB_IM = SHPC_MASKED;
	logical_slot_reg.x.ABP_STS = SHPC_STATUS_SET;

	//
	// MRL Sensor: Disabled
	//
	logical_slot_reg.x.MRLS_IM = SHPC_MASKED;
	logical_slot_reg.x.MRLS_SERRM = SHPC_MASKED;
	logical_slot_reg.x.MRLSC_STS = SHPC_STATUS_SET;

	//
	// Card Presence: Disabled
	//
	logical_slot_reg.x.CP_IM = SHPC_MASKED;
	logical_slot_reg.x.CPC_STS = SHPC_STATUS_SET;

	//
	// Isolated Power-Fault: Disabled
	//
	logical_slot_reg.x.IPF_IM = SHPC_MASKED;
	logical_slot_reg.x.IPF_STS = SHPC_STATUS_SET;

	//
	// Connected Power-Fault: Enabled (System Error Disabled)
	//
	logical_slot_reg.x.CPF_IM = SHPC_MASKED;
	logical_slot_reg.x.CPF_SERRM = SHPC_MASKED;
	logical_slot_reg.x.CPF_STS = SHPC_STATUS_SET;

	//
	// Update Mask and Status bits
	//
	writel(logical_slot_reg.AsDWord, slot_context->logical_slot_addr);
}


// ****************************************************************************
//
// hp_enable_global_interrupts() @ Any IRQL
//
// ****************************************************************************
void
hp_enable_global_interrupts(
	struct shpc_context* shpc_context
)
{
	union SHPC_SERR_INT_DWREG SerrIntReg;

	SerrIntReg.AsDWord = readl(shpc_context->mmio_base_addr + SHPC_SERR_INT_REG_OFFSET);

	//
	// Arbiter timeout: System Error Disabled
	//
	SerrIntReg.x.A_SERRM = SHPC_MASKED;
	SerrIntReg.x.ATOUT_STS = SHPC_STATUS_SET;

	//
	// Command Completion: Enabled
	//
	SerrIntReg.x.CC_IM = SHPC_UNMASKED;
	SerrIntReg.x.CC_STS = SHPC_STATUS_SET;

	//
	// Global: Interrputs Enabled, System Error Disabled
	//
	SerrIntReg.x.GIM = SHPC_UNMASKED;
	SerrIntReg.x.GSERRM = SHPC_MASKED;

	//
	// Update Mask and Status bits
	//
	writel(SerrIntReg.AsDWord, shpc_context->mmio_base_addr + SHPC_SERR_INT_REG_OFFSET);
}


// ****************************************************************************
//
// hp_disable_global_interrupts() @ Any IRQL
//
// ****************************************************************************
void
hp_disable_global_interrupts(
	struct shpc_context* shpc_context
)
{
	union SHPC_SERR_INT_DWREG SerrIntReg;

	SerrIntReg.AsDWord = readl(shpc_context->mmio_base_addr + SHPC_SERR_INT_REG_OFFSET);
	//
	// Arbiter timeout: System Error Disabled
	//
	SerrIntReg.x.A_SERRM = SHPC_MASKED;
	SerrIntReg.x.ATOUT_STS = SHPC_STATUS_SET;

	//
	// Command Completion: Disabled
	//
	SerrIntReg.x.CC_IM = SHPC_MASKED;
	SerrIntReg.x.CC_STS = SHPC_STATUS_SET;

	//
	// Global: Interrputs Disabled, System Error Disabled
	//
	SerrIntReg.x.GIM = SHPC_MASKED;
	SerrIntReg.x.GSERRM = SHPC_MASKED;

	//
	// Update Mask and Status bits
	//
	writel(SerrIntReg.AsDWord, shpc_context->mmio_base_addr + SHPC_SERR_INT_REG_OFFSET);
}


// ****************************************************************************
//
// hp_get_card_speed_mode() @ Any IRQL
//
// ****************************************************************************
enum shpc_speed_mode
hp_get_card_speed_mode(
	struct slot_context* slot_context
)
{
	union SHPC_LOGICAL_SLOT_DWREG logical_slot_reg;

	//
	// Slot powered-up?
	//
	logical_slot_reg.AsDWord = readl( slot_context->logical_slot_addr );
	if(( logical_slot_reg.x.PRSNT1_2 != SHPC_SLOT_EMPTY ) &&
		( logical_slot_reg.x.S_STATE == SHPC_POWER_ONLY ||
		logical_slot_reg.x.S_STATE == SHPC_ENABLE_SLOT )) {
		//
		// Get Card's maximum speed/mode
		//
		if( logical_slot_reg.x.PCIX_CAP == SHPC_SLOT_PCIX_133 ) {
			slot_context->card_speed_mode = SHPC_BUS_PCIX_133;
		}
		else if( logical_slot_reg.x.PCIX_CAP == SHPC_SLOT_PCIX_66 ) {
			slot_context->card_speed_mode = SHPC_BUS_PCIX_66;
		}
		else if( logical_slot_reg.x.M66_CAP == SHPC_STATUS_SET ) {
			slot_context->card_speed_mode = SHPC_BUS_CONV_66;
		}
		else {
			slot_context->card_speed_mode = SHPC_BUS_CONV_33;
		}

		//
		// Get Card's PCI-66 capability
		//
		if( logical_slot_reg.x.M66_CAP == SHPC_STATUS_SET ) {
			slot_context->card_pci66_capable = TRUE;
		}
	}
	else {
		//
		// Slot is not powered-up, use PCI-33 as default
		//
		slot_context->card_speed_mode = SHPC_BUS_CONV_33;
		slot_context->card_pci66_capable = FALSE;
	}

	return slot_context->card_speed_mode;
}

// ****************************************************************************
//
// hp_get_bus_speed_mode() @ Any IRQL
//
// ****************************************************************************
enum shpc_speed_mode
hp_get_bus_speed_mode(
	struct shpc_context* shpc_context
)
{
	union SHPC_SEC_BUS_CONFIG_DWREG bus_config_reg;
	enum shpc_speed_mode bus_speed_mode;

	bus_config_reg.AsDWord = readl(shpc_context->mmio_base_addr + SHPC_SEC_BUS_CONFIG_REG_OFFSET);

	bus_speed_mode = ( enum shpc_speed_mode )bus_config_reg.x.MODE;
	if( bus_speed_mode > SHPC_BUS_PCIX_133 ) {
		bus_speed_mode = SHPC_BUS_CONV_33;
	}

	return bus_speed_mode;
}


// ****************************************************************************
//
// hp_translate_speed_mode() @ Any IRQL
//
// ****************************************************************************
enum mode_frequency
hp_translate_speed_mode(
	enum shpc_speed_mode shpc_speed_mode
)
{
	enum mode_frequency translated_speed_mode;

	switch( shpc_speed_mode ) {
	case SHPC_BUS_PCIX_133:
		translated_speed_mode = MODE_PCIX_133;
		break;

	case SHPC_BUS_PCIX_100:
		translated_speed_mode = MODE_PCIX_100;
		break;

	case SHPC_BUS_PCIX_66:
		translated_speed_mode = MODE_PCIX_66;
		break;

	case SHPC_BUS_CONV_66:
		translated_speed_mode = MODE_PCI_66;
		break;

	case SHPC_BUS_CONV_33:
	default:
		translated_speed_mode = MODE_PCI_33;
		break;
	}

	return translated_speed_mode;
}


// ****************************************************************************
//
// hp_translate_card_power() @ Any IRQL
//
// ****************************************************************************
enum hp_power_requirements
hp_translate_card_power(
	enum shpc_card_power ShpcCardPower
)
{
	enum hp_states TranslatedCardPower;

	switch( ShpcCardPower ) {
	case SHPC_CARD_PRESENT_25W:
		TranslatedCardPower = POWER_HIGH;
		break;

	case SHPC_CARD_PRESENT_15W:
		TranslatedCardPower = POWER_MEDIUM;
		break;

	case SHPC_CARD_PRESENT_7_5W:
	default:
		TranslatedCardPower = POWER_LOW;
		break;
	}

	return TranslatedCardPower;
}


// ****************************************************************************
//
// hp_translate_indicator() @ Any IRQL
//
// ****************************************************************************
enum hp_indicators
hp_translate_indicator(
	enum shpc_slot_led ShpcIndicator
)
{
	enum hp_indicators TranslatedIndicator;

	switch( ShpcIndicator ) {
	case SHPC_LED_ON:
		TranslatedIndicator = INDICATOR_ON;
		break;

	case SHPC_LED_BLINK:
		TranslatedIndicator = INDICATOR_BLINK;
		break;

	case SHPC_LED_OFF:
	default:
		TranslatedIndicator =INDICATOR_OFF;
		break;
	}

	return TranslatedIndicator;
}


// ****************************************************************************
//
// hp_flag_slot_as_enabled() @ <= DISPATCH_LEVEL
//
// ****************************************************************************
u8
hp_flag_slot_as_enabled(
	struct shpc_context* shpc_context,
	struct slot_context* slot_context
)
{
	unsigned long		old_irq_flags;
	u8 SlotFlagged = FALSE;

	spin_lock_irqsave( &shpc_context->shpc_spinlock, old_irq_flags );
	if( !slot_context->slot_enabled ) {
		//
		// Slot just coming on-line
		//
		SlotFlagged = TRUE;
		++shpc_context->slots_enabled;
		slot_context->slot_enabled = TRUE;
		hp_clear_shpc_event_bit(shpc_context, BUS_REBALANCE_EVENT);
	}
	spin_unlock_irqrestore( &shpc_context->shpc_spinlock, old_irq_flags );

	return SlotFlagged;
}


// ****************************************************************************
//
// hp_flag_slot_as_disabled() @ <= DISPATCH_LEVEL
//
// ****************************************************************************
u8
hp_flag_slot_as_disabled(
	struct shpc_context* shpc_context,
	struct slot_context* slot_context
)
{
	unsigned long		old_irq_flags;
	u8 SlotFlagged = FALSE;

	spin_lock_irqsave( &shpc_context->shpc_spinlock, old_irq_flags );
	if( slot_context->slot_enabled ) {
		if( --shpc_context->slots_enabled == 0 ) {
			//
			// This was the last enabled slot, signal waiting thread that bus is released,
			//
			shpc_context->bus_released = TRUE;
			hp_send_event_to_all_slots(shpc_context, BUS_COMPLETE_EVENT);
		}
		SlotFlagged = TRUE;
		slot_context->slot_enabled = FALSE;
	}
	spin_unlock_irqrestore( &shpc_context->shpc_spinlock, old_irq_flags );

	return SlotFlagged;
}


// ****************************************************************************
//
// hp_signal_enabled_slots_to_rebalance_bus() @ <= DISPATCH_LEVEL
//
// Comments:
//	Assumes shpc_spinlock is already held.
//
// ****************************************************************************
u8
hp_signal_enabled_slots_to_rebalance_bus(
	struct shpc_context* shpc_context
)
{
	struct slot_context* SlotArray[ SHPC_MAX_NUM_SLOTS ];
	struct slot_context* Slot;
	u8 i, j, n;

	//
	// Initialize array of slot pointers
	//
	n = shpc_context->number_of_slots;
	for( i=0, j=0; i<n; ++i ) {
		Slot = &shpc_context->slot_context[ i ];
		if( Slot->slot_enabled ) {
			SlotArray[ j++ ] = Slot;
		}
	}
	//
	// Found slots enabled?
	//
	if( j ) {
		//
		// Bubble-sort enabled slots in order of increasing card speed/mode
		//
		n = j;
		for( i=0; i<n-1; i++ ) {
			for( j=0; j<n-1-i; j++ ) {
				if( SlotArray[ j+1 ]->card_speed_mode < SlotArray[ j ]->card_speed_mode ) {
					Slot = SlotArray[ j ];
					SlotArray[ j ] = SlotArray[ j+1 ];
					SlotArray[ j+1 ] = Slot;
				}
			}
		}
		//
		// Signal enabled slots in sorted order as an attempt to re-enable slower cards first
		//
		hp_set_shpc_event_bit(shpc_context, BUS_REBALANCE_EVENT);
		for( i=0; i<n; i++ ) {
			wake_up_interruptible( &SlotArray[ i ]->slot_event);
		}
		return TRUE;
	}

	return FALSE;
}


// ****************************************************************************
//
// hp_get_max_speed_mode() @ <= DISPATCH_LEVEL
//
// Comments:
//	Assumes shpc_spinlock is already held.
//
// ****************************************************************************
enum shpc_speed_mode
hp_get_max_speed_mode(
	struct shpc_context* shpc_context,
	enum shpc_speed_mode From_speed_mode
)
{
	struct slot_context* slot_context;
	enum shpc_speed_mode max_speed_mode;
	u8 i;

	max_speed_mode = From_speed_mode;
	for( i=0; i< shpc_context->number_of_slots; ++i ) {
		slot_context = &shpc_context->slot_context[ i ];
		if( slot_context->in_bus_speed_mode_contention &&
			slot_context->card_speed_mode < max_speed_mode ) {
			//
			// Can only go as fast as the slowest card
			//
			max_speed_mode = slot_context->card_speed_mode;
		}
	}

	//
	// Make sure all cards support conventional PCI-66 speed/mode
	//
	if( max_speed_mode == SHPC_BUS_CONV_66 ) {
		for( i=0; i< shpc_context->number_of_slots; ++i ) {
			slot_context = &shpc_context->slot_context[ i ];
			if( slot_context->in_bus_speed_mode_contention &&
				!slot_context->card_pci66_capable ) {
				//
				// Fall back to slower common denominator
				//
				max_speed_mode = SHPC_BUS_CONV_33;
			}
		}
	}

	return max_speed_mode;
}


// ****************************************************************************
//
// hp_signal_user_event() @ <= DISPATCH_LEVEL
//
// ****************************************************************************
void hp_signal_user_event(struct shpc_context* shpc_context)
{
	unsigned long		old_irq_flags;
return;
	spin_lock_irqsave( &shpc_context->shpc_spinlock, old_irq_flags );
	if( shpc_context->user_event_pointer ) {
		wake_up_interruptible( shpc_context->user_event_pointer);
	}
	spin_unlock_irqrestore( &shpc_context->shpc_spinlock, old_irq_flags );
}


// ****************************************************************************
//
// hp_signal_user_event_at_dpc_level() @ DISPATCH_LEVEL
//
// ****************************************************************************
void hp_signal_user_event_at_dpc_level(struct shpc_context* shpc_context)
{
return;
	spin_lock_bh( &shpc_context->shpc_spinlock );
	if( shpc_context->user_event_pointer ) {
		wake_up_interruptible( shpc_context->user_event_pointer);
	}
	spin_unlock_bh( &shpc_context->shpc_spinlock );
}
