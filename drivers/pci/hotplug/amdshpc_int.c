/*
 * Copyright (C) 2003 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2003 IBM Corp.
 * Copyright (C) 2002-2003 Advanced Micro Devices
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
// hp_interrupt_service()
//
// ****************************************************************************
irqreturn_t hp_interrupt_service(int IRQ, void *v, struct pt_regs *regs)
{
	struct shpc_context *shpc_context = v;
	struct slot_context *slot_context;
	union SHPC_SERR_INT_DWREG	SerrIntReg;
	union SHPC_INT_LOCATOR_DWREG	IntLocatorReg, SlotIndex;
	union SHPC_LOGICAL_SLOT_DWREG logical_slot_reg;
	u8 IsShpcInterrupt = FALSE;
	u8 i;

	//
	// Device at PowerDeviceD0?
	//
	if( !shpc_context->at_power_device_d0 ) {
		return IRQ_HANDLED;
	}

	//
	// Read Interrupt Locator Register ( Pending Interrupts )
	//
	IntLocatorReg.AsDWord = readl(shpc_context->mmio_base_addr + SHPC_INT_LOCATOR_REG_OFFSET);

	//
	// Read SERR-INT Register ( Global Mask, Command Completion )
	//
	SerrIntReg.AsDWord = readl(shpc_context->mmio_base_addr + SHPC_SERR_INT_REG_OFFSET);

	//
	// Global Interrupts Disabled?
	//
//	if( SerrIntReg.x.GIM == SHPC_MASKED ) {
//		return FALSE;
//	}

	//
	// Command Completion?
	//
	if( IntLocatorReg.x.CC_IP ) {
		if(( SerrIntReg.x.CC_STS == SHPC_STATUS_SET ) &&
			( SerrIntReg.x.CC_IM == SHPC_UNMASKED )) {
			//
			// Schedule Dpc
			//
			IsShpcInterrupt = TRUE;
			tasklet_schedule(&shpc_context->cmd_completion_dpc);

			//
			// Clear Interrput (Write-back 1 to STS bits)
			//
			writel(SerrIntReg.AsDWord, shpc_context->mmio_base_addr + SHPC_SERR_INT_REG_OFFSET);
		}
	}

	//
	// Slot Interrupts?
	//
	if( IntLocatorReg.x.SLOT_IP ) {
		//
		// Walk a "1" thru each bit position (one bit per slot)
		//
		for( i=0, SlotIndex.x.SLOT_IP = 1; i< SHPC_MAX_NUM_SLOTS; ++i, SlotIndex.x.SLOT_IP <<= 1 ) {
			slot_context = &shpc_context->slot_context[ i ];

			//
			// Interrupt from this slot?
			//
			if( IntLocatorReg.x.SLOT_IP & SlotIndex.x.SLOT_IP ) {
				//
				//  Read Logical Slot Register
				//
				logical_slot_reg.AsDWord = readl( slot_context->logical_slot_addr );

				//
				// Attention Button?
				//
				if(( logical_slot_reg.x.ABP_STS == SHPC_STATUS_SET ) &&
					( logical_slot_reg.x.AB_IM == SHPC_UNMASKED )) {
					//
					// Schedule Dpc
					//
					IsShpcInterrupt = TRUE;
					tasklet_schedule(&slot_context->attn_button_dpc);
				}

				//
				// MRL Sensor?
				//
				if(( logical_slot_reg.x.MRLSC_STS == SHPC_STATUS_SET ) &&
					( logical_slot_reg.x.MRLS_IM == SHPC_UNMASKED )) {
					//
					// Schedule Dpc
					//
					IsShpcInterrupt = TRUE;
					tasklet_schedule(&slot_context->mrl_sensor_dpc);
				}

				//
				// Card Presence Change?
				//
				if(( logical_slot_reg.x.CPC_STS == SHPC_STATUS_SET ) &&
					( logical_slot_reg.x.CP_IM == SHPC_UNMASKED )) {
					//
					// Schedule Dpc
					//
					IsShpcInterrupt = TRUE;
					tasklet_schedule(&slot_context->card_presence_dpc);
				}

				//
				// Isolated Power Fault?
				//
				if(( logical_slot_reg.x.IPF_STS == SHPC_STATUS_SET ) &&
					( logical_slot_reg.x.IPF_IM == SHPC_UNMASKED )) {
					//
					// Schedule Dpc
					//
					IsShpcInterrupt = TRUE;
					tasklet_schedule(&slot_context->isolated_power_fault_dpc);
				}

				//
				// Connected Power Fault?
				//
				if(( logical_slot_reg.x.CPF_STS == SHPC_STATUS_SET ) &&
					( logical_slot_reg.x.CPF_IM == SHPC_UNMASKED )) {
					//
					// Schedule Dpc
					//
					IsShpcInterrupt = TRUE;
					tasklet_schedule(&slot_context->connected_power_fault_dpc);
				}

				//
				// Clear Interrputs for this slot (Write-back 1 to STS bits)
				//
				writel(logical_slot_reg.AsDWord, slot_context->logical_slot_addr);
			}
		}
	}
	return IRQ_HANDLED;
}


// ****************************************************************************
//
// hp_attn_button_dpc() @ DISPATCH_LEVEL
//
// ****************************************************************************
void
hp_attn_button_dpc(
	unsigned long deferred_context	// struct slot_context*
	)
{
	struct slot_context* slot_context = ( struct slot_context* )deferred_context;
	struct shpc_context* shpc_context = slot_context->shpc_context;

	dbg("%s ->slot_id[ %d:%d ]", __FUNCTION__, shpc_context->shpc_instance, (slot_context->slot_number-1) );
	//
	// Notification Event: Attention Button pressed
	//
	spin_lock( &slot_context->slot_spinlock );
	hp_send_slot_event(slot_context, ATTN_BUTTON_EVENT);
	spin_unlock( &slot_context->slot_spinlock );
}


// ****************************************************************************
//
// hp_card_presence_dpc() @ DISPATCH_LEVEL
//
// ****************************************************************************
void
hp_card_presence_dpc(
	unsigned long deferred_context	// struct slot_context*
	)
{
	struct slot_context* slot_context = ( struct slot_context* )deferred_context;
	struct shpc_context* shpc_context = slot_context->shpc_context;
	union SHPC_LOGICAL_SLOT_DWREG logical_slot_reg;

	dbg("%s -->slot_id[ %d:%d ]", __FUNCTION__, shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Signal registered user EVENT
	//
	hp_signal_user_event_at_dpc_level( shpc_context );

	//
	// Card Removed?
	//
	logical_slot_reg.AsDWord = readl( slot_context->logical_slot_addr );
	if( logical_slot_reg.x.PRSNT1_2 == SHPC_SLOT_EMPTY ) {
		//
		// Signal Alert EVENT
		//
		spin_lock( &slot_context->slot_spinlock );
		hp_send_slot_event(slot_context, ALERT_EVENT);
		spin_unlock( &slot_context->slot_spinlock );
	}
}


// ****************************************************************************
//
// hp_mrl_sensor_dpc() @ DISPATCH_LEVEL
//
// ****************************************************************************
void
hp_mrl_sensor_dpc(
	unsigned long deferred_context	// struct slot_context*
	)
{
	struct slot_context* slot_context = ( struct slot_context* )deferred_context;
	struct shpc_context* shpc_context = slot_context->shpc_context;
	union SHPC_LOGICAL_SLOT_DWREG logical_slot_reg;

	dbg("%s -->slot_id[ %d:%d ]", __FUNCTION__, shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Signal registered user EVENT
	//
	hp_signal_user_event_at_dpc_level( shpc_context );

	//
	// MRL Sensor opened?
	//
	logical_slot_reg.AsDWord = readl( slot_context->logical_slot_addr );
	if( logical_slot_reg.x.MRLS == SHPC_MRL_OPEN ) {
		//
		// Card Present?
		//
		if( logical_slot_reg.x.PRSNT1_2 != SHPC_SLOT_EMPTY ) {
			//
			// Signal Alert EVENT
			//
			spin_lock( &slot_context->slot_spinlock );
			hp_send_slot_event(slot_context, ALERT_EVENT);
			spin_unlock( &slot_context->slot_spinlock );
		}
	}
	else {
		//
		// Power Fault detected whith MRL closed?
		// Note: Golem A0 may not generate power-fault interrupt
		if( logical_slot_reg.x.PF == SHPC_STATUS_SET ) {
			//
			// Signal Alert EVENT
			//
			spin_lock( &slot_context->slot_spinlock );
			hp_send_slot_event(slot_context, ALERT_EVENT);
			spin_unlock( &slot_context->slot_spinlock );
		}
	}
}

// ****************************************************************************
//
// isolated_power_fault_dpc() @ DISPATCH_LEVEL
//
// ****************************************************************************
void
hp_isolated_power_fault_dpc(
	unsigned long deferred_context	// struct slot_context*
	)
{
	struct slot_context* slot_context = ( struct slot_context* )deferred_context;
	struct shpc_context* shpc_context = slot_context->shpc_context;
	union SHPC_LOGICAL_SLOT_DWREG logical_slot_reg;

	dbg("%s -->slot_id[ %d:%d ]", __FUNCTION__, shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Signal registered user EVENT
	//
	hp_signal_user_event_at_dpc_level( shpc_context );

	//
	// Power Fault detected?
	//
	logical_slot_reg.AsDWord = readl( slot_context->logical_slot_addr );
	if( logical_slot_reg.x.PF == SHPC_STATUS_SET ) {
		//
		// Signal Alert EVENT
		//
		spin_lock( &slot_context->slot_spinlock );
		hp_send_slot_event(slot_context, ALERT_EVENT);
		spin_unlock( &slot_context->slot_spinlock );
	}
}


// ****************************************************************************
//
// connected_power_fault_dpc() @ DISPATCH_LEVEL
//
// ****************************************************************************
void
hp_connected_power_fault_dpc(
	unsigned long deferred_context	// struct slot_context*
	)
{
	struct slot_context* slot_context = ( struct slot_context* )deferred_context;
	struct shpc_context* shpc_context = slot_context->shpc_context;
	union SHPC_LOGICAL_SLOT_DWREG logical_slot_reg;

	dbg("%s -->slot_id[ %d:%d ]", __FUNCTION__, shpc_context->shpc_instance, slot_context->slot_number-1 );

	//
	// Signal registered user EVENT
	//
	hp_signal_user_event_at_dpc_level( shpc_context );

	//
	// Power Fault detected?
	//
	logical_slot_reg.AsDWord = readl( slot_context->logical_slot_addr );
	if( logical_slot_reg.x.PF == SHPC_STATUS_SET ) {
		//
		// Signal Alert EVENT
		//
		spin_lock( &slot_context->slot_spinlock );
		hp_send_slot_event(slot_context, ALERT_EVENT);
		spin_unlock( &slot_context->slot_spinlock );
	}
}


// ****************************************************************************
//
// hp_cmd_completion_dpc() @ DISPATCH_LEVEL
//
// ****************************************************************************
void
hp_cmd_completion_dpc(
	unsigned long deferred_context	// struct shpc_context*
	)
{
	struct shpc_context* shpc_context = ( struct shpc_context* )deferred_context;

	dbg("%s -->HwInstance[ %d ]", __FUNCTION__, shpc_context->shpc_instance );

	//
	// Notification Event: Command Completion
	//
	spin_lock( &shpc_context->shpc_spinlock );
	hp_send_event_to_all_slots(shpc_context, CMD_COMPLETION_EVENT);
	spin_unlock( &shpc_context->shpc_spinlock );
}
