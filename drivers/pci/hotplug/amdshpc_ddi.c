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
#include <linux/smp_lock.h>
#include "amdshpc_ddi.h"
#include "amdshpc.h"

static unsigned long async_callback (void* driver_context,
				     u8 slot_number,
				     enum shpc_async_request async_request,
				     struct slot_status_info slot_status,
				     void* request_context );

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
		    struct shpc_context* shpc_context,
		    void* driver_context,
		    SHPC_ASYNC_CALLBACK Callback,
		    unsigned long shpc_instance
		    )

{
	struct slot_context* slot_context;
	u8 i;
	DECLARE_TASKLET(mrl_sensor_dpc0, hp_mrl_sensor_dpc, (unsigned long) &shpc_context->slot_context[0] );
	DECLARE_TASKLET(attn_button_dpc0, hp_attn_button_dpc, (unsigned long) &shpc_context->slot_context[0]);
	DECLARE_TASKLET(card_presence_dpc0, hp_card_presence_dpc, (unsigned long) &shpc_context->slot_context[0]);
	DECLARE_TASKLET(isolated_power_fault_dpc0, hp_isolated_power_fault_dpc, (unsigned long) &shpc_context->slot_context[0]);
	DECLARE_TASKLET(connected_power_fault_dpc0, hp_connected_power_fault_dpc, (unsigned long) &shpc_context->slot_context[0]);

	DECLARE_TASKLET(mrl_sensor_dpc1, hp_mrl_sensor_dpc, (unsigned long) &shpc_context->slot_context[1] );
	DECLARE_TASKLET(attn_button_dpc1, hp_attn_button_dpc, (unsigned long) &shpc_context->slot_context[1]);
	DECLARE_TASKLET(card_presence_dpc1, hp_card_presence_dpc, (unsigned long) &shpc_context->slot_context[1]);
	DECLARE_TASKLET(isolated_power_fault_dpc1, hp_isolated_power_fault_dpc, (unsigned long) &shpc_context->slot_context[1]);
	DECLARE_TASKLET(connected_power_fault_dpc1, hp_connected_power_fault_dpc, (unsigned long) &shpc_context->slot_context[1]);

	DECLARE_TASKLET(mrl_sensor_dpc2, hp_mrl_sensor_dpc, (unsigned long) &shpc_context->slot_context[2] );
	DECLARE_TASKLET(attn_button_dpc2, hp_attn_button_dpc, (unsigned long) &shpc_context->slot_context[2]);
	DECLARE_TASKLET(card_presence_dpc2, hp_card_presence_dpc, (unsigned long) &shpc_context->slot_context[2]);
	DECLARE_TASKLET(isolated_power_fault_dpc2, hp_isolated_power_fault_dpc, (unsigned long) &shpc_context->slot_context[2]);
	DECLARE_TASKLET(connected_power_fault_dpc2, hp_connected_power_fault_dpc, (unsigned long) &shpc_context->slot_context[2]);

	DECLARE_TASKLET(mrl_sensor_dpc3, hp_mrl_sensor_dpc, (unsigned long) &shpc_context->slot_context[3] );
	DECLARE_TASKLET(attn_button_dpc3, hp_attn_button_dpc, (unsigned long) &shpc_context->slot_context[3]);
	DECLARE_TASKLET(card_presence_dpc3, hp_card_presence_dpc, (unsigned long) &shpc_context->slot_context[3]);
	DECLARE_TASKLET(isolated_power_fault_dpc3, hp_isolated_power_fault_dpc, (unsigned long) &shpc_context->slot_context[3]);
	DECLARE_TASKLET(connected_power_fault_dpc3, hp_connected_power_fault_dpc, (unsigned long) &shpc_context->slot_context[3]);


	DECLARE_TASKLET(cmd_completion_dpc, hp_cmd_completion_dpc, (unsigned long) shpc_context );

	//
	// Init common resources
	//
	shpc_context->cmd_completion_dpc = cmd_completion_dpc;
	shpc_context->driver_context = driver_context;
	shpc_context->async_callback = (SHPC_ASYNC_CALLBACK)async_callback;
	shpc_context->shpc_instance = shpc_instance;
	shpc_context->slots_enabled = 0;
	shpc_context->number_of_slots = 0;
	shpc_context->at_power_device_d0 = FALSE;
	shpc_context->bus_released = FALSE;
	shpc_context->user_event_pointer = NULL;
	spin_lock_init( &shpc_context->shpc_spinlock );
	sema_init( &shpc_context->cmd_available_mutex, 1);
	sema_init( &shpc_context->bus_available_mutex, 1);
	sema_init( &shpc_context->shpc_event_bits_semaphore, 1);

	shpc_context->shpc_event_bits=0;	// all shpc events cleared

	dbg("%s -->HwInstance[ %d ]", __FUNCTION__ ,shpc_context->shpc_instance );

	//
	// Init slot resources
	//
	for ( i=0; i< SHPC_MAX_NUM_SLOTS; ++i ) {
		slot_context = &shpc_context->slot_context[ i ];
		slot_context->shpc_context = ( void* )shpc_context;
		slot_context->slot_number = ( u8 )i+1;
		slot_context->slot_enabled = FALSE;
		slot_context->in_bus_speed_mode_contention = FALSE;
		slot_context->problem_detected = FALSE;
		slot_context->slot_quiesced = FALSE;
		slot_context->slot_thread = NULL;
		slot_context->slot_function = NULL;
		slot_context->attn_led_thread = NULL;
		slot_context->attn_led_function = NULL;

		//
		// Slot SpinLocks and semaphores
		//
		spin_lock_init( &slot_context->slot_spinlock);
		sema_init(&slot_context->slot_event_bits_semaphore, 1);
		sema_init(&slot_context->cmd_acquire_mutex, 1);
		sema_init(&slot_context->bus_acquire_mutex, 1);

		//
		// Slot timers
		//
		init_timer(&slot_context->slot_timer1);
		init_timer(&slot_context->slot_timer2);
		init_timer(&slot_context->slot_timer3);
		init_timer(&slot_context->slot_timer4);
		init_timer(&slot_context->slot_timer5);
		init_timer(&slot_context->slot_timer6);
		init_timer(&slot_context->slot_timer7);
		init_timer(&slot_context->slot_timer8);
		init_timer(&slot_context->slot_timer9);
		init_timer(&slot_context->slot_timer10);
		init_timer(&slot_context->led_timer1);
		init_timer(&slot_context->led_timer2);
		init_timer(&slot_context->led_timer3);
		init_timer(&slot_context->led_timer4);

		//
		// Interrupt Service
		//
		switch (i) {
		case 0:
			slot_context->attn_button_dpc           = attn_button_dpc0;
			slot_context->mrl_sensor_dpc            = mrl_sensor_dpc0;
			slot_context->card_presence_dpc         = card_presence_dpc0;
			slot_context->isolated_power_fault_dpc  = isolated_power_fault_dpc0;
			slot_context->connected_power_fault_dpc = connected_power_fault_dpc0;
			break;
		case 1:
			slot_context->attn_button_dpc           = attn_button_dpc1;
			slot_context->mrl_sensor_dpc            = mrl_sensor_dpc1;
			slot_context->card_presence_dpc         = card_presence_dpc1;
			slot_context->isolated_power_fault_dpc  = isolated_power_fault_dpc1;
			slot_context->connected_power_fault_dpc = connected_power_fault_dpc1;
			break;
		case 2:
			slot_context->attn_button_dpc           = attn_button_dpc2;
			slot_context->mrl_sensor_dpc            = mrl_sensor_dpc2;
			slot_context->card_presence_dpc         = card_presence_dpc2;
			slot_context->isolated_power_fault_dpc  = isolated_power_fault_dpc2;
			slot_context->connected_power_fault_dpc = connected_power_fault_dpc2;
			break;
		case 3:
			slot_context->attn_button_dpc           = attn_button_dpc3;
			slot_context->mrl_sensor_dpc            = mrl_sensor_dpc3;
			slot_context->card_presence_dpc         = card_presence_dpc3;
			slot_context->isolated_power_fault_dpc  = isolated_power_fault_dpc3;
			slot_context->connected_power_fault_dpc = connected_power_fault_dpc3;
			break;
		}


		//
		// Slot Events
		//
		slot_context->slot_event_bits=0;	// all slot events cleared

		dbg("%s -->Init slot wait queues",__FUNCTION__ );

		init_waitqueue_head(&slot_context->slot_event);
		init_waitqueue_head(&slot_context->led_cmd_acquire_event);
		init_waitqueue_head(&slot_context->led_cmd_release_event);
		init_waitqueue_head(&slot_context->cmd_acquire_event);
		init_waitqueue_head(&slot_context->cmd_release_event);
		init_waitqueue_head(&slot_context->bus_acquire_event);
		init_waitqueue_head(&slot_context->bus_release_event);
	}
	return STATUS_SUCCESS;
}


// ****************************************************************************
//
// hp_StartDevice()
//
// Parameters
//	shpc_context - Caller provided storage for SHPC context data.
//
// Return Value
//  Status returned by any system calls made within hp_StartDevice().
//
//
// ****************************************************************************
long hp_StartDevice(
		      struct shpc_context* shpc_context
		      )
{
	struct slot_context* slot_context;
	long status = STATUS_SUCCESS;
	u32 *logical_slot_addr;
	u8 i;
	int pid;

	dbg("%s -->From hp_StartDevice:  MmioBase[ %p ]",__FUNCTION__ , (unsigned long*)shpc_context->mmio_base_addr);

	//
	// Disable Global Interrupts
	//
	dbg("%s -->hp_disable_global_interrupts( shpc_context=%p );",__FUNCTION__ , shpc_context);
	hp_disable_global_interrupts( shpc_context );

	//
	// Reset common resources
	//
	shpc_context->at_power_device_d0 = TRUE;
	shpc_context->bus_released = FALSE;

	//
	// Reset slot resources
	//
	logical_slot_addr = shpc_context->mmio_base_addr + SHPC_LOGICAL_SLOT_REG_OFFSET;
	for ( i=0; i< SHPC_MAX_NUM_SLOTS; ++i ) {
		slot_context = &shpc_context->slot_context[ i ];

		//
		// Assign Logical Slot Register Address
		//
		slot_context->logical_slot_addr = logical_slot_addr++;

		//
		// Disable Slot Interrupts
		//
		dbg("%s -->hp_disable_slot_interrupts(slot_context)=%p",__FUNCTION__ , slot_context);
		hp_disable_slot_interrupts(slot_context);

		//
		// Reset slot flags and pointers
		//
		slot_context->slot_enabled = FALSE;
		slot_context->in_bus_speed_mode_contention = FALSE;
		slot_context->problem_detected = FALSE;
		slot_context->slot_quiesced = FALSE;
		slot_context->slot_thread = NULL;
		slot_context->slot_function = NULL;
		slot_context->attn_led_thread = NULL;
		slot_context->attn_led_function = NULL;
		slot_context->slot_occupied = 0;
	}

	//
	// Get initial slot configuration: number_of_slots, slots_enabled, SlotStateFunction
	//
	shpc_context->slots_enabled = 0;
	shpc_context->number_of_slots = 0;
	hp_get_slot_configuration( shpc_context );
	dbg("%s -->from hp_StartDevice() number_of_slots = %d", __FUNCTION__ ,shpc_context->number_of_slots);
	if ( shpc_context->number_of_slots == 0 ) {
		status = STATUS_UNSUCCESSFUL;
	}

	//
	//  Hook Interrupt
	//
	dbg("%s -->HPC interrupt = %d \n", __FUNCTION__ ,shpc_context->interrupt);

	if (request_irq(shpc_context->interrupt, hp_interrupt_service, SA_SHIRQ, MY_NAME, shpc_context)) {
		err("Can't get irq %d for the PCI hotplug controller\n", shpc_context->interrupt);
		status = STATUS_UNSUCCESSFUL;
		return(status);
	}

	//
	// Set slot operation in motion
	//
	for ( i=0; i<shpc_context->number_of_slots && status; ++i ) {

		slot_context = &shpc_context->slot_context[ i ];

		//
		// Launch slot command and bus completion mutex threads
		//
		// get led cmd available thread
		pid = kernel_thread(hp_get_led_cmd_available_mutex_thread, slot_context, CLONE_SIGHAND);
		if (pid < 0) {
			err ("Can't start up our get_led_cmd_available_mutex thread\n");
			status = STATUS_UNSUCCESSFUL;
		}
		dbg("%s -->Our hp_get_led_cmd_available_mutex thread pid = %d",__FUNCTION__ , pid);

		// get cmd available thread
		pid = kernel_thread(hp_get_cmd_available_mutex_thread, slot_context, CLONE_SIGHAND);
		if (pid < 0) {
			err ("Can't start up our get_cmd_available_mutex thread\n");
			status = STATUS_UNSUCCESSFUL;
		}
		dbg("%s -->Our hp_get_cmd_available_mutex thread pid = %d",__FUNCTION__ , pid);

		// get bus available thread
		pid = kernel_thread(hp_get_bus_available_mutex_thread, slot_context, CLONE_SIGHAND);
		if (pid < 0) {
			err ("Can't start up our get_bus_available_mutex thread\n");
			status = STATUS_UNSUCCESSFUL;
		}
		dbg("%s \n\n\n-->Our get_bus_available_mutex thread pid = %d",__FUNCTION__ , pid);

		//
		// Launch slot thread
		//
		pid = kernel_thread(hp_slot_thread, slot_context, CLONE_SIGHAND);
		if (pid < 0) {
			err ("Can't start up our event thread\n");
			status = STATUS_UNSUCCESSFUL;
		}
		dbg("%s -->Our slot event thread pid = %d\n",__FUNCTION__ , pid);

		//
		// Launch Attention LED Thread
		//
		pid = kernel_thread(hp_attn_led_thread, slot_context, CLONE_SIGHAND);
		if (pid < 0) {
			err ("Can't start up our event thread\n");
			status = STATUS_UNSUCCESSFUL;
		}
		dbg("%s -->Our LED event thread pid = %d\n",__FUNCTION__ , pid);

		//
		// Enable Slot Interrupts: Attn Button, MRL Sensor, Card Presence, Power-Fault
		//
		if (status) {
			dbg("%s -->hpStartDevice() Enabling slot interrupts...",__FUNCTION__ );
			hp_enable_slot_interrupts( slot_context );
		}
	}

	//
	// Enable Global Interrupts: Command Completion
	//
	if (status) {
		dbg("%s -->hpStartDevice() Enabling global interrupts...",__FUNCTION__ );
		hp_enable_global_interrupts( shpc_context );
	} else {
		//
		// Bail out, we're hosed!
		//
		hp_StopDevice( shpc_context );
		status = STATUS_UNSUCCESSFUL;
	}
	dbg("%s -->status = %d\n",__FUNCTION__ , (u32)status);

	return status;
}


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
long
	hp_StopDevice(
		     struct shpc_context* shpc_context
		     )
{
	struct slot_context* slot_context;
	long status = STATUS_SUCCESS;
	unsigned long   old_irq_flags;
	u8 i;

	//
	// Already stopped or never started ?
	//
	if ( shpc_context->mmio_base_addr == 0 ) {
		return STATUS_UNSUCCESSFUL;
	}
	//
	// Disable Global Interrupts
	//
	hp_disable_global_interrupts( shpc_context );

	//
	// Signal EXIT request to slot threads
	//
	spin_lock_irqsave(&shpc_context->shpc_spinlock, old_irq_flags);
	hp_clear_shpc_event_bit(shpc_context, SUSPEND_EVENT);
	hp_send_event_to_all_slots(shpc_context,RESUME_EVENT | REMOVE_EVENT | EXIT_REQUEST_EVENT);
	spin_unlock_irqrestore( &shpc_context->shpc_spinlock, old_irq_flags );

	for ( i=0; i<SHPC_MAX_NUM_SLOTS; ++i ) {
		slot_context = &shpc_context->slot_context[ i ];

		//
		// Disable Slot Interrupts
		//
		hp_disable_slot_interrupts( slot_context );

		//
		// Remove scheduled slot DPCs
		//
		tasklet_kill( &slot_context->attn_button_dpc );
		tasklet_kill( &slot_context->card_presence_dpc );
		tasklet_kill( &slot_context->isolated_power_fault_dpc );
		tasklet_kill( &slot_context->connected_power_fault_dpc );

		//
		// Send events to kill all threads
		//
		//
		// Set event bits to send to running threads
		//
		spin_lock_irqsave(&shpc_context->shpc_spinlock, old_irq_flags);
		hp_set_shpc_event_bit(shpc_context,(RESUME_EVENT | REMOVE_EVENT | EXIT_REQUEST_EVENT));
		spin_unlock_irqrestore( &shpc_context->shpc_spinlock, old_irq_flags );

		wake_up_interruptible(&slot_context->led_cmd_acquire_event);
		wake_up_interruptible(&slot_context->cmd_acquire_event);
		wake_up_interruptible(&slot_context->bus_acquire_event);
		wake_up_interruptible(&slot_context->led_cmd_release_event);
		wake_up_interruptible(&slot_context->cmd_release_event);
		wake_up_interruptible(&slot_context->bus_release_event);
		//
		// Reset slot pointers and flags
		//
		slot_context->slot_enabled = FALSE;
		slot_context->slot_thread = NULL;
		slot_context->slot_function = NULL;
		slot_context->attn_led_thread = NULL;
		slot_context->attn_led_function = NULL;
	}

	//
	// Remove scheduled common DPC
	//
	tasklet_kill(&shpc_context->cmd_completion_dpc );

	//
	// Reset common resources
	//
	shpc_context->number_of_slots = 0;
	shpc_context->slots_enabled = 0;
	shpc_context->at_power_device_d0 = FALSE;

	return status;
}


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
long
	hp_SuspendDevice(
			struct shpc_context* shpc_context
			)
{
	long status = STATUS_SUCCESS;
	unsigned long   old_irq_flags;

	dbg("%s -->HwInstance[ %d ]", __FUNCTION__ ,shpc_context->shpc_instance );

	spin_lock_irqsave( &shpc_context->shpc_spinlock, old_irq_flags );

	if (shpc_context->mmio_base_addr &&
	    (!shpc_context->shpc_event_bits & SUSPEND_EVENT) &&
	    (!shpc_context->shpc_event_bits & REMOVE_EVENT)) {
		hp_clear_shpc_event_bit(shpc_context, RESUME_EVENT);

		hp_send_event_to_all_slots(shpc_context, SUSPEND_EVENT);
		hp_send_event_to_all_slots(shpc_context, EXIT_REQUEST_EVENT);

		shpc_context->at_power_device_d0 = FALSE;
	}
	spin_unlock_irqrestore( &shpc_context->shpc_spinlock, old_irq_flags );

	return status;
}


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
long
	hp_ResumeDevice(
		       struct shpc_context* shpc_context
		       )
{
	long status = STATUS_SUCCESS;
	unsigned long           old_irq_flags;

	dbg("%s -->HwInstance[ %d ]", __FUNCTION__ ,shpc_context->shpc_instance );

	spin_lock_irqsave( &shpc_context->shpc_spinlock, old_irq_flags );
	if (shpc_context->mmio_base_addr &&
	    (shpc_context->shpc_event_bits & SUSPEND_EVENT) &&
	    (!shpc_context->shpc_event_bits & REMOVE_EVENT)) {
		hp_clear_shpc_event_bit(shpc_context, SUSPEND_EVENT);
		hp_clear_shpc_event_bit(shpc_context, EXIT_REQUEST_EVENT);
		hp_send_event_to_all_slots(shpc_context, RESUME_EVENT);
		shpc_context->at_power_device_d0 = TRUE;
	}

	spin_unlock_irqrestore( &shpc_context->shpc_spinlock, old_irq_flags );

	return status;
}


// ****************************************************************************
//
// hp_QuerySlots() 
//
// Parameters
//	shpc_context - Caller provided storage for SHPC context data.
//	slot_config - Caller provided storage for slots configuration info.
//
// Return Value
//  Status returned by any system calls made within hp_QuerySlots().
//
// ****************************************************************************
long
	hp_QuerySlots(
		     struct shpc_context* shpc_context,
		     struct slot_config_info* slot_config
		     )
{
	long status = STATUS_SUCCESS;
	u32 slot_config_reg;

	dbg("%s -->HwInstance[ %d ]  Slots[ %d ]",__FUNCTION__ ,
	    shpc_context->shpc_instance, shpc_context->number_of_slots );

	//
	// Get slot configuration
	//
	slot_config_reg = readl(shpc_context->mmio_base_addr + SHPC_SLOT_CONFIG_REG_OFFSET);

	memset(slot_config, 0, sizeof(struct slot_config_info));
	slot_config->lu_slots_implemented = ((slot_config_reg & NSI_MASK) >> NSI_OFFSET);
	slot_config->lu_base_PSN = ((slot_config_reg & PSN_MASK) >> PSN_OFFSET);
	slot_config->lu_PSN_up = ((slot_config_reg & PSN_UP_MASK) >> PSN_UP_OFFSET);
	slot_config->lu_base_FDN = ((slot_config_reg & FDN_MASK) >> FDN_OFFSET);

	return status;
}


// ****************************************************************************
//
// hp_QuerySlotStatus()
//
// Parameters
//	shpc_context - Caller provided storage for SHPC context data.
//	slot_id - Zero-based slot number (0..n-1). = slot_number -1
//	Query - Pointer to Slot Status Structure
//
// Return Value
//  Status returned by any system calls made within hp_QuerySlotStatus().
//
// ****************************************************************************
long
	hp_QuerySlotStatus(
			  struct shpc_context* shpc_context,
			  u8 slot_id,
			  struct slot_status_info* Query
			  )
{
	struct slot_context* slot_context;
	long status = STATUS_SUCCESS;
	u32 logical_slot_reg;

	dbg("%s -->slot_id[ %d:%d ]",__FUNCTION__ , shpc_context->shpc_instance, slot_id );

	//
	// Valid slot_id?
	//
	if ( slot_id >= shpc_context->number_of_slots ) {
		status = STATUS_UNSUCCESSFUL;
	} else {
		//
		// Which slot?
		//
		slot_context = &shpc_context->slot_context[ slot_id ];

		//
		// Get Max Speed/Mode from common context
		//
		Query->lu_max_bus_mode_freq = hp_translate_speed_mode( shpc_context->max_speed_mode );

		//
		// Get Bus Speed/Mode from HW
		//
		Query->lu_bus_mode_freq = hp_translate_speed_mode( hp_get_bus_speed_mode( shpc_context ));

		//
		// Get Card Speed/Mode from HW
		//
		Query->lu_card_mode_freq_cap = hp_translate_speed_mode( hp_get_card_speed_mode_cap( slot_context ));

		//
		// Get current slot info from HW
		//
		logical_slot_reg = readl( slot_context->logical_slot_addr );

		//
		// Card Present?
		//
		Query->lu_card_present = ( ((logical_slot_reg & PRSNT1_2_MASK) >> PRSNT1_2_OFFSET) != SHPC_SLOT_EMPTY ) ?
					 HP_TRUE : HP_FALSE;

		//
		// Get Card PCI-66 capability
		//
		Query->lu_card_pci66_capable = ( ((logical_slot_reg & PRSNT1_2_MASK) >> PRSNT1_2_OFFSET) != SHPC_SLOT_EMPTY &&
						 ( ((logical_slot_reg & S_STATE_MASK) >> S_STATE_OFFSET) == SHPC_POWER_ONLY || 
						   ((logical_slot_reg & S_STATE_MASK) >> S_STATE_OFFSET) == SHPC_ENABLE_SLOT ) &&
						 ((logical_slot_reg & M66_CAP_MASK) >> M66_CAP_OFFSET) == SHPC_STATUS_SET ) ?
					       HP_TRUE : HP_FALSE;

		//
		// Power-Fault?
		//
		Query->lu_power_fault = ( ((logical_slot_reg & PF_MASK) >> PF_OFFSET) == SHPC_STATUS_SET ) ?
					HP_TRUE : HP_FALSE;

		//
		//  Card Power Requirements
		//
		Query->lu_card_power = hp_translate_card_power( ((logical_slot_reg & PRSNT1_2_MASK) >> PRSNT1_2_OFFSET) );

		//
		//  Attention Indicator
		//
		Query->lu_ai_state = hp_translate_indicator( ((logical_slot_reg & AIS_MASK) >> AIS_OFFSET) );

		//
		//  Power Indicator
		//
		Query->lu_pi_state = hp_translate_indicator( ((logical_slot_reg & PIS_MASK) >> PIS_OFFSET) );

		//
		// MRL Implemented?
		//
		Query->lu_mrl_implemented = ( ((logical_slot_reg & MRLS_IM_MASK) >> MRLS_IM_OFFSET) == SHPC_UNMASKED ) ?
					    HP_TRUE : HP_FALSE;

		//
		// MRL Opened?
		//
		Query->lu_mrl_opened = (( ((logical_slot_reg & MRLS_MASK) >> MRLS_OFFSET) == SHPC_MRL_OPEN ) &&
					( ((logical_slot_reg & MRLS_IM_MASK) >> MRLS_IM_OFFSET) == SHPC_UNMASKED )) ? HP_TRUE : HP_FALSE;

		//
		// Slot State: Card Present, MRL closed, No Power-Fault, Enabled?
		//
		if ( ((logical_slot_reg & PRSNT1_2_MASK) >> PRSNT1_2_OFFSET) != SHPC_SLOT_EMPTY &&
		     (((logical_slot_reg & MRLS_IM_MASK) >> MRLS_IM_OFFSET) == SHPC_MASKED ||
		      ((logical_slot_reg & MRLS_MASK) >> MRLS_OFFSET) == SHPC_MRL_CLOSED) &&
		     ((logical_slot_reg & PF_MASK) >> PF_OFFSET) == SHPC_STATUS_CLEARED &&
		     ((logical_slot_reg & S_STATE_MASK) >> S_STATE_OFFSET) == SHPC_ENABLE_SLOT ) {
			Query->lu_slot_state = SLOT_ENABLE;                                                                     
		} else {
			Query->lu_slot_state = SLOT_DISABLE;
		}

		//
		// OK, it's all there!
		//
//		Query->lu_reserved1 = 0;
//		Query->lu_reserved2 = 0;
		Query->lu_request_failed = HP_FALSE;
	}

	return status;
}


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
long
	hp_StartAsyncRequest(
			    struct shpc_context* shpc_context,
			    u8 slot_id,
			    enum shpc_async_request request,
			    u32 timeout,
			    void* request_context
			    )
{
	unsigned long           old_irq_flags;
	struct slot_context* slot_context;
	long status = STATUS_SUCCESS;

	dbg("%s -->slot_id[ %d:%d ]  Request[ %d ]",__FUNCTION__ ,
	    shpc_context->shpc_instance, slot_id, request );

	//
	// Valid slot_id?
	//
	if ( slot_id >= shpc_context->number_of_slots ) {
		status = STATUS_UNSUCCESSFUL;
	} else {
		slot_context = &shpc_context->slot_context[ slot_id ];

		switch ( request ) {
		case SHPC_ASYNC_ENABLE_SLOT:
			dbg("%s SHPC_ASYNC_ENABLE_SLOT",__FUNCTION__);
		case SHPC_ASYNC_DISABLE_SLOT:
			dbg("%s SHPC_ASYNC_DISABLE_SLOT",__FUNCTION__);
			//
			// Slot Request Pending?
			//
			spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
			down_interruptible(&slot_context->slot_event_bits_semaphore);
			down_interruptible(&shpc_context->shpc_event_bits_semaphore);
			if ((slot_context->slot_event_bits & SLOT_REQUEST_EVENT) ||
			    (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)) {
				status = STATUS_UNSUCCESSFUL;
				up(&slot_context->slot_event_bits_semaphore);
				up(&shpc_context->shpc_event_bits_semaphore);
			} else {
				up(&slot_context->slot_event_bits_semaphore);
				up(&shpc_context->shpc_event_bits_semaphore);
				slot_context->slot_request.type = request;
				slot_context->slot_request.request_context = request_context;
				hp_send_slot_event(slot_context, SLOT_REQUEST_EVENT);
			}
			spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );
			break;

		case SHPC_ASYNC_LED_LOCATE:
			dbg("%s SHPC_ASYNC_LED_LOCATE",__FUNCTION__);
		case SHPC_ASYNC_LED_NORMAL:
			dbg("%s SHPC_ASYNC_LED_NORMAL",__FUNCTION__);
			//
			// AttnLED Request Pending?
			//
			spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
			down_interruptible(&slot_context->slot_event_bits_semaphore);
			down_interruptible(&shpc_context->shpc_event_bits_semaphore);
			if ((slot_context->slot_event_bits & ATTN_LED_REQUEST_EVENT) ||
			    (shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)) {
				dbg("%s  LED--STATUS_UNSUCCESSFUL  slot_event_bits = %08X", __FUNCTION__ ,slot_context->slot_event_bits);
				status = STATUS_UNSUCCESSFUL;
				up(&slot_context->slot_event_bits_semaphore);
				up(&shpc_context->shpc_event_bits_semaphore);
			} else {
				up(&slot_context->slot_event_bits_semaphore);
				up(&shpc_context->shpc_event_bits_semaphore);
				slot_context->attn_led_request.type = request;
				slot_context->attn_led_request.timeout = timeout;
				slot_context->attn_led_request.request_context = request_context;
				hp_send_slot_event(slot_context, ATTN_LED_REQUEST_EVENT);
			}
			spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );
			break;

		case SHPC_ASYNC_QUIESCE_DEVNODE_NOTIFY:
			dbg("%s SHPC_ASYNC_QUIESCE_DEVNODE_NOTIFY",__FUNCTION__);
			//
			// HP library notification: DevNode is quiesced
			//
			spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
			++slot_context->quiesce_replies;
			if ( slot_context->quiesce_requests &&
			     slot_context->quiesce_replies >= slot_context->quiesce_requests ) {
				slot_context->slot_quiesced = TRUE;
				hp_send_slot_event(slot_context, QUIESCE_EVENT);
			}
			spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );
			break;

		case SHPC_ASYNC_CANCEL_QUIESCE_DEVNODE:
			dbg("%s SHPC_ASYNC_CANCEL_QUIESCE_DEVNODE",__FUNCTION__);
			//
			// HP library notification: could not quiesce DevNode
			//
			spin_lock_irqsave( &slot_context->slot_spinlock, old_irq_flags );
			slot_context->slot_quiesced = FALSE;
			hp_send_slot_event(slot_context, QUIESCE_EVENT);
			spin_unlock_irqrestore( &slot_context->slot_spinlock, old_irq_flags );

			//
			// Abort bus-rebalancing
			//
			spin_lock_irqsave( &shpc_context->shpc_spinlock, old_irq_flags );
			shpc_context->bus_released = FALSE;
			hp_send_event_to_all_slots(shpc_context, BUS_COMPLETE_EVENT);
			spin_unlock_irqrestore( &shpc_context->shpc_spinlock, old_irq_flags );
			break;

		default:
			status = STATUS_UNSUCCESSFUL;
			break;
		}
	}

	return status;
}

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
long hp_Queryslot_psn(struct shpc_context *shpc_context, unsigned char slot_ID, unsigned long *slot_psn)
{
	struct slot_context *slot_context;
	long status = STATUS_SUCCESS;
	dbg("%s slot_ID[ %d:%d ]",__FUNCTION__ , shpc_context->shpc_instance, slot_ID);
	//
	// Valid SlotID?
	//
	if ( slot_ID >= shpc_context->number_of_slots || slot_psn == NULL ) {
		status = STATUS_UNSUCCESSFUL;
	} else {
		//
		// Which slot?
		//
		slot_context = &shpc_context->slot_context[ slot_ID ];
		//
		// Get slot PSN
		//
		*slot_psn = slot_context->slot_psn;
	}
	return status;
}

// ****************************************************************************
//
// hp_slot_timers1-10func(): Function passed to timer to send event
//
// Parameters
//	slot_context - Caller provided storage for SHPC context data.
//
// Return Value
//	void
//
// ****************************************************************************
void hp_slot_timer1_func(unsigned long data){

	struct slot_context *slot_context;
	slot_context = (struct slot_context*) data;

	dbg("%s", __FUNCTION__);
	hp_set_slot_event_bit(slot_context, SLOT_TIMER1_EVENT);

	wake_up_interruptible(&slot_context->slot_event);
}

void hp_slot_timer2_func(unsigned long data){

	struct slot_context *slot_context;
	slot_context = (struct slot_context*) data;

	dbg("%s", __FUNCTION__);
	hp_set_slot_event_bit(slot_context, SLOT_TIMER2_EVENT);

	wake_up_interruptible(&slot_context->slot_event);
}

void hp_slot_timer3_func(unsigned long data){

	struct slot_context *slot_context;
	slot_context = (struct slot_context*) data;

	dbg("%s", __FUNCTION__);
	hp_set_slot_event_bit(slot_context, SLOT_TIMER3_EVENT);

	wake_up_interruptible(&slot_context->slot_event);
}

void hp_slot_timer4_func(unsigned long data){

	struct slot_context *slot_context;
	slot_context = (struct slot_context*) data;

	dbg("%s", __FUNCTION__);
	hp_set_slot_event_bit(slot_context, SLOT_TIMER4_EVENT);

	wake_up_interruptible(&slot_context->slot_event);
}

void hp_slot_timer5_func(unsigned long data){

	struct slot_context *slot_context;
	slot_context = (struct slot_context*) data;

	dbg("%s", __FUNCTION__);
	hp_set_slot_event_bit(slot_context, SLOT_TIMER5_EVENT);

	wake_up_interruptible(&slot_context->slot_event);
}

void hp_slot_timer6_func(unsigned long data){

	struct slot_context *slot_context;
	slot_context = (struct slot_context*) data;

	dbg("%s", __FUNCTION__);
	hp_set_slot_event_bit(slot_context, SLOT_TIMER6_EVENT);

	wake_up_interruptible(&slot_context->slot_event);
}

void hp_slot_timer7_func(unsigned long data){

	struct slot_context *slot_context;
	slot_context = (struct slot_context*) data;

	dbg("%s", __FUNCTION__);
	hp_set_slot_event_bit(slot_context, SLOT_TIMER7_EVENT);

	wake_up_interruptible(&slot_context->slot_event);
}

void hp_slot_timer8_func(unsigned long data){

	struct slot_context *slot_context;
	slot_context = (struct slot_context*) data;

	dbg("%s", __FUNCTION__);
	hp_set_slot_event_bit(slot_context, SLOT_TIMER8_EVENT);

	wake_up_interruptible(&slot_context->slot_event);
}

void hp_slot_timer9_func(unsigned long data){

	struct slot_context *slot_context;
	slot_context = (struct slot_context*) data;

	dbg("%s", __FUNCTION__);
	hp_set_slot_event_bit(slot_context, SLOT_TIMER9_EVENT);

	wake_up_interruptible(&slot_context->slot_event);
}

void hp_slot_timer10_func(unsigned long data){

	struct slot_context *slot_context;
	slot_context = (struct slot_context*) data;

	dbg("%s", __FUNCTION__);
	hp_set_slot_event_bit(slot_context, SLOT_TIMER10_EVENT);

	wake_up_interruptible(&slot_context->slot_event);
}

// ****************************************************************************
//
// hp_led_timers1-4_func(): Function passed to timer to send event
//
// Parameters
//	slot_context - Caller provided storage for SHPC context data.
//
// Return Value
//	void
//
// ****************************************************************************
void hp_led_timer1_func(unsigned long data){

	struct slot_context *slot_context;
	slot_context = (struct slot_context*) data;

	dbg("%s", __FUNCTION__);
	hp_set_slot_event_bit(slot_context, LED_TIMER1_EVENT);

	wake_up_interruptible(&slot_context->slot_event);
}

void hp_led_timer2_func(unsigned long data){

	struct slot_context *slot_context;
	slot_context = (struct slot_context*) data;

	dbg("%s", __FUNCTION__);
	hp_set_slot_event_bit(slot_context, LED_TIMER2_EVENT);

	wake_up_interruptible(&slot_context->slot_event);
}

void hp_led_timer3_func(unsigned long data){

	struct slot_context *slot_context;
	slot_context = (struct slot_context*) data;

	dbg("%s", __FUNCTION__);
	hp_set_slot_event_bit(slot_context, LED_TIMER3_EVENT);

	wake_up_interruptible(&slot_context->slot_event);
}

void hp_led_timer4_func(unsigned long data){

	struct slot_context *slot_context;
	slot_context = (struct slot_context*) data;

	dbg("%s", __FUNCTION__);
	hp_set_slot_event_bit(slot_context, LED_TIMER4_EVENT);

	wake_up_interruptible(&slot_context->slot_event);
}

// ****************************************************************************
//
// hp_clear_slot_event_bit():
//
// Parameters
//	slot_context - Caller provided storage for SHPC context data.
//
// Return Value
//	void
//
// ****************************************************************************
void hp_clear_slot_event_bit(struct slot_context * slot_context, u32 mask)
{
//	dbg("%s -->slot bits %08X   MASK=%08X",__FUNCTION__ ,slot_context->slot_event_bits, mask);

	down_interruptible(&slot_context->slot_event_bits_semaphore);
	// cleareventbit
	slot_context->slot_event_bits &= ~mask;
	up(&slot_context->slot_event_bits_semaphore);
}

// ****************************************************************************
//
// hp_set_slot_event_bit():
//
// Parameters
//	slot_context - Caller provided storage for SHPC context data.
//
// Return Value
//	void
//
// ****************************************************************************
void hp_set_slot_event_bit(struct slot_context * slot_context, u32 mask)
{
//	dbg("%s -->slot bits %08X   MASK=%08X",__FUNCTION__ ,slot_context->slot_event_bits, mask);

	down_interruptible(&slot_context->slot_event_bits_semaphore);
	// cleareventbit
	slot_context->slot_event_bits |= mask;
	up(&slot_context->slot_event_bits_semaphore);
}

// ****************************************************************************
//
// hp_clear_shpc_event_bit():
//
// Parameters
//	slot_context - Caller provided storage for SHPC context data.
//
// Return Value
//	void
//
// ****************************************************************************
void hp_clear_shpc_event_bit(struct shpc_context * shpc_context, u32 mask)
{
	down_interruptible(&shpc_context->shpc_event_bits_semaphore);
	// cleareventbit
	shpc_context->shpc_event_bits &= ~mask;
	up(&shpc_context->shpc_event_bits_semaphore);
}

// ****************************************************************************
//
// hp_set_shpc_event_bit():
//
// Parameters
//	slot_context - Caller provided storage for SHPC context data.
//
// Return Value
//	void
//
// ****************************************************************************
void hp_set_shpc_event_bit(struct shpc_context * shpc_context, u32 mask)
{
	down_interruptible(&shpc_context->shpc_event_bits_semaphore);
	// set event bit
	shpc_context->shpc_event_bits |= mask;
	up(&shpc_context->shpc_event_bits_semaphore);
}

// ****************************************************************************
//
// hp_send_event_to_all_slots():
//
// Parameters
//	slot_context - Caller provided storage for SHPC context data.
//
// Return Value
//	void
//
// ****************************************************************************
void hp_send_event_to_all_slots(struct shpc_context *shpc_context, u32 mask)
{
	u8 i;
	struct slot_context * slot_context;

	down_interruptible(&shpc_context->shpc_event_bits_semaphore);
	// set event bit
	shpc_context->shpc_event_bits |= mask;
	// send event to each slot thread
	for ( i=0; i<shpc_context->number_of_slots; ++i ) {
		slot_context = &shpc_context->slot_context[ i ];
		wake_up_interruptible(&slot_context->slot_event);
	}
	up(&shpc_context->shpc_event_bits_semaphore);
}

// ****************************************************************************
//
// hp_send_slot_event():
//
// Parameters
//	slot_context - Caller provided storage for SHPC context data.
//
// Return Value
//	void
//
// ****************************************************************************
void hp_send_slot_event(struct slot_context * slot_context, u32 mask)
{
	// set event bit
	hp_set_slot_event_bit(slot_context, mask);
	wake_up_interruptible( &slot_context->slot_event);
}


// ****************************************************************************
//
// hp_get_led_cmd_available_mutex_thread():  run as a thread per each slot
//
// Parameters
//	slot_context - Caller provided storage for slot context data.
//
// Return Value
//	void
//
// ****************************************************************************
int hp_get_led_cmd_available_mutex_thread(void *ptr)
{
	long status = STATUS_SUCCESS;
	struct shpc_context* shpc_context;
	struct slot_context* slot_context;
	int pid;

	lock_kernel ();
	daemonize ("amdshpc_getledcmd_av_mutex");
	
	unlock_kernel ();

	slot_context = (struct slot_context* ) ptr;
	shpc_context = (struct shpc_context* ) slot_context->shpc_context;
	do {
		interruptible_sleep_on(&slot_context->led_cmd_acquire_event);
		if (slot_context->slot_event_bits & LED_CMD_ACQUIRE_EVENT) {
			hp_clear_slot_event_bit(slot_context, LED_CMD_ACQUIRE_EVENT);
			pid = kernel_thread(hp_led_cmd_available_mutex_thread, slot_context, CLONE_SIGHAND);
			if (pid < 0) {
				err ("Can't start up our hp_led_cmd_available_mutex_thread\n");
				status = STATUS_UNSUCCESSFUL;
				break;
			}
		} else {
			dbg("%s terminating return 0  slot_id[ %d:%d ]",__FUNCTION__,
			    shpc_context->shpc_instance, slot_context->slot_number-1);
			return 0;
		}
	} while (1);
	return(status);
}

// ****************************************************************************
//
// hp_led_cmd_available_mutex_thread():  run as a thread per each request for cmd
//
// Parameters
//	slot_context - Caller provided storage for SHPC context data.
//
// Return Value
//	void
//
// ****************************************************************************
int hp_led_cmd_available_mutex_thread(void *ptr)
{
	struct shpc_context* shpc_context;
	struct slot_context* slot_context;
	unsigned long   old_irq_flags;

	lock_kernel ();
	daemonize ("amdshpc_ledcmd_av_mutex");
	
	unlock_kernel ();

	slot_context = (struct slot_context* ) ptr;
	shpc_context = (struct shpc_context* ) slot_context->shpc_context;

	//
	// acquire the main mutex for all slots exclusion
	//
	dbg("%s ATTEMPTING TO ACQUIRE cmd_available_mutex  slot_id[ %d:%d ]",__FUNCTION__,
	    shpc_context->shpc_instance, slot_context->slot_number-1);
	down_interruptible(&shpc_context->cmd_available_mutex);
	if ((shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)||
	    (shpc_context->shpc_event_bits & RESUME_EVENT)||
	    (shpc_context->shpc_event_bits & REMOVE_EVENT)) {
		up(&shpc_context->cmd_available_mutex);
		return 0;
	}

	//
	// now tell our slot thread that it has the mutex
	//
	dbg("%s cmd_available_mutex ACQUIRED  slot_id[ %d:%d ]",__FUNCTION__,
	    shpc_context->shpc_instance, slot_context->slot_number-1);
	hp_set_shpc_event_bit(shpc_context, LED_CMD_AVAILABLE_MUTEX_EVENT);
	wake_up_interruptible(&slot_context->slot_event);

	//
	// wait for our slot thread to release the mutex
	//
	interruptible_sleep_on(&slot_context->led_cmd_release_event);
	if ((shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)||
	    (shpc_context->shpc_event_bits & RESUME_EVENT)||
	    (shpc_context->shpc_event_bits & REMOVE_EVENT)) {
		up(&shpc_context->cmd_available_mutex);
		return 0;
	}
	hp_clear_slot_event_bit(slot_context, LED_CMD_RELEASE_EVENT);

	spin_lock_irqsave( &shpc_context->shpc_spinlock, old_irq_flags );
	hp_clear_shpc_event_bit(shpc_context, LED_CMD_AVAILABLE_MUTEX_EVENT);
	spin_unlock_irqrestore( &shpc_context->shpc_spinlock, old_irq_flags );

	up(&shpc_context->cmd_available_mutex);
	dbg("%s cmd_available_mutex RELEASED  slot_id[ %d:%d ]",__FUNCTION__,
	    shpc_context->shpc_instance, slot_context->slot_number-1);
	return(0);
}

// ****************************************************************************
//
// hp_get_cmd_available_mutex_thread():  run as a thread per each slot
//
// Parameters
//	slot_context - Caller provided storage for slot context data.
//
// Return Value
//	void
//
// ****************************************************************************
int hp_get_cmd_available_mutex_thread(void *ptr)
{
	long status = STATUS_SUCCESS;
	struct shpc_context* shpc_context;
	struct slot_context* slot_context;
	int pid;

	lock_kernel ();
	daemonize ("amdshpc_getcmd_av_mutex");
	
	unlock_kernel ();

	slot_context = (struct slot_context* ) ptr;
	shpc_context = (struct shpc_context* ) slot_context->shpc_context;

	do {
		interruptible_sleep_on(&slot_context->cmd_acquire_event);
		if ((slot_context->slot_event_bits & CMD_ACQUIRE_EVENT) || 
		    (slot_context->slot_event_bits & CMD_RELEASE_EVENT)) {
			hp_clear_slot_event_bit(slot_context,CMD_ACQUIRE_EVENT);
			pid = kernel_thread(hp_cmd_available_mutex_thread, slot_context, CLONE_SIGHAND);
			if (pid < 0) {
				err ("Can't start up our hp_get_cmd_available_mutex_thread\n");
				status = STATUS_UNSUCCESSFUL;
				break;
			}
		} else {
			dbg("%s terminating return 0  slot_id[ %d:%d ]",__FUNCTION__,
			    shpc_context->shpc_instance, slot_context->slot_number-1);
			return 0;
		}
	} while (1);
	return(status);
}

// ****************************************************************************
//
// hp_cmd_available_mutex_thread():  run as a thread per each request for cmd
//
// Parameters
//	slot_context - Caller provided storage for SHPC context data.
//
// Return Value
//	void
//
// ****************************************************************************
int hp_cmd_available_mutex_thread(void *ptr)
{
	struct shpc_context* shpc_context;
	struct slot_context* slot_context;
	unsigned long   old_irq_flags;

	lock_kernel ();
	daemonize ("amdshpc_cmd_av_mutex");
	
	unlock_kernel ();

	slot_context = (struct slot_context* ) ptr;
	shpc_context = (struct shpc_context* ) slot_context->shpc_context;

	//
	// acquire the main mutex for all slots exclusion
	//
	dbg("%s ATTEMPTING TO ACQUIRE cmd_available_mutex  slot_id[ %d:%d ]",__FUNCTION__,
	    shpc_context->shpc_instance, slot_context->slot_number-1);
	down_interruptible(&shpc_context->cmd_available_mutex);
	if ((shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)||
	    (shpc_context->shpc_event_bits & RESUME_EVENT)||
	    (shpc_context->shpc_event_bits & REMOVE_EVENT)) {
		up(&shpc_context->cmd_available_mutex);
		return 0;
	}

	//
	// now tell our slot thread that it has the mutex
	//
	dbg("%s cmd_available_mutex ACQUIRED slot_id[ %d:%d ]",__FUNCTION__,
	    shpc_context->shpc_instance, slot_context->slot_number-1);
	hp_set_shpc_event_bit(shpc_context, CMD_AVAILABLE_MUTEX_EVENT);
	wake_up_interruptible(&slot_context->slot_event);

	//
	// wait for our slot thread to release the mutex
	//
	interruptible_sleep_on(&slot_context->cmd_release_event);
	if ((shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)||
	    (shpc_context->shpc_event_bits & RESUME_EVENT)||
	    (shpc_context->shpc_event_bits & REMOVE_EVENT)) {
		up(&shpc_context->cmd_available_mutex);
		return 0;
	}
	hp_clear_slot_event_bit(slot_context,CMD_RELEASE_EVENT);

	spin_lock_irqsave( &shpc_context->shpc_spinlock, old_irq_flags );
	hp_clear_shpc_event_bit(shpc_context, CMD_AVAILABLE_MUTEX_EVENT);
	spin_unlock_irqrestore( &shpc_context->shpc_spinlock, old_irq_flags );

	up(&shpc_context->cmd_available_mutex);
	dbg("%s cmd_available_mutex RELEASED  slot_id[ %d:%d ]",__FUNCTION__,
	    shpc_context->shpc_instance, slot_context->slot_number-1);
	return(0);
}

// ****************************************************************************
//
// hp_get_bus_available_mutex_thread():  run as a thread per each slot
//
// Parameters
//	slot_context - Caller provided storage for slot context data.
//
// Return Value
//	void
//
// ****************************************************************************
int hp_get_bus_available_mutex_thread(void *ptr)
{
	long status = STATUS_SUCCESS;
	struct shpc_context* shpc_context;
	struct slot_context* slot_context;
	int pid;

	lock_kernel ();
	daemonize ("amdshpc_getbus_av_mutex");
	
	unlock_kernel ();

	slot_context = (struct slot_context* ) ptr;
	shpc_context = (struct shpc_context* ) slot_context->shpc_context;

	do {
		interruptible_sleep_on(&slot_context->bus_acquire_event);
		if (slot_context->slot_event_bits & BUS_ACQUIRE_EVENT) {
			hp_clear_slot_event_bit(slot_context, BUS_ACQUIRE_EVENT);
			pid = kernel_thread(hp_bus_available_mutex_thread, slot_context, CLONE_SIGHAND);
			if (pid < 0) {
				err ("Can't start up our hp_get_bus_available_mutex_thread\n");
				status = STATUS_UNSUCCESSFUL;
				break;
			}
		} else {
			dbg("%s terminating return 0  slot_id[ %d:%d ]",__FUNCTION__,
			    shpc_context->shpc_instance, slot_context->slot_number-1);
			return 0;
		}
	} while (1);
	return(status);
}

// ****************************************************************************
//
// hp_bus_available_mutex_thread():
//
// Parameters
//	slot_context - Caller provided storage for SHPC context data.
//
// Return Value
//	void
//
// ****************************************************************************
int hp_bus_available_mutex_thread(void *ptr)
{
	struct shpc_context* shpc_context;
	struct slot_context* slot_context;
	unsigned long   old_irq_flags;

	lock_kernel ();
	daemonize ("amdshpc_bus_av_mutex");
	
	unlock_kernel ();

	slot_context = (struct slot_context* ) ptr;
	shpc_context = (struct shpc_context* ) slot_context->shpc_context;

	//
	// acquire the main mutex for all slots exclusion
	//
	dbg("%s ATTEMPTING TO ACQUIRE bus_available_mutex  slot_id[ %d:%d ]",__FUNCTION__,
	    shpc_context->shpc_instance, slot_context->slot_number-1);
	down_interruptible(&shpc_context->bus_available_mutex);
	if ((shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)||
	    (shpc_context->shpc_event_bits & RESUME_EVENT)||
	    (shpc_context->shpc_event_bits & REMOVE_EVENT)) {
		up(&shpc_context->bus_available_mutex);
		return 0;
	}

	//
	// now tell our slot thread that it has the mutex
	//
	dbg("%s bus_available_mutex ACQUIRED  slot_id[ %d:%d ]",__FUNCTION__,
	    shpc_context->shpc_instance, slot_context->slot_number-1);
	hp_set_shpc_event_bit(shpc_context, BUS_AVAILABLE_MUTEX_EVENT);
	wake_up_interruptible(&slot_context->slot_event);

	//
	// wait for our slot thread to release the mutex
	//
	interruptible_sleep_on(&slot_context->bus_release_event);
	if ((shpc_context->shpc_event_bits & EXIT_REQUEST_EVENT)||
	    (shpc_context->shpc_event_bits & RESUME_EVENT)||
	    (shpc_context->shpc_event_bits & REMOVE_EVENT)) {
		up(&shpc_context->bus_available_mutex);
		return 0;
	}
	hp_clear_slot_event_bit(slot_context, BUS_RELEASE_EVENT);

	spin_lock_irqsave( &shpc_context->shpc_spinlock, old_irq_flags );
	hp_clear_shpc_event_bit(shpc_context, BUS_AVAILABLE_MUTEX_EVENT);
	spin_unlock_irqrestore( &shpc_context->shpc_spinlock, old_irq_flags );

	up(&shpc_context->bus_available_mutex);
	dbg("%s bus_available_mutex RELEASED  slot_id[ %d:%d ]",__FUNCTION__,
	    shpc_context->shpc_instance, slot_context->slot_number-1);
	return(0);
}

// ****************************************************************************
//
// call_back_routine():
//
// Parameters
//	slot_context - Caller provided storage for SHPC context data.
//
// Return Value
//	void
//
// ****************************************************************************
static unsigned long async_callback (void* driver_context,
				     u8 slot_id,
				     enum shpc_async_request async_request,
				     struct slot_status_info slot_status,
				     void* request_context )
{
	u8 phys_slot_num;
	long rc=0;
	struct pci_func *slot_func;
	struct controller *ctrl;
	struct shpc_context *shpc_context;
	u8 bus=0;
	u8 device=0;
	u8 function=0;
	unsigned long devices_still_quiescing = 0;

	dbg("%s slot_id = %d",__FUNCTION__, slot_id);

	ctrl = ((struct controller*) driver_context);
	if (ctrl == NULL) {
		return -ENODEV;
	}

	shpc_context = (struct shpc_context* ) ctrl->shpc_context;
	phys_slot_num = shpc_context->slot_context[slot_id].slot_psn;

	bus             = ctrl->bus;
	device  = slot_id + 1;

	dbg("%s - physical_slot = %d  instance = %d",__FUNCTION__, phys_slot_num, shpc_context->shpc_instance);

	switch ( async_request ) {
	case SHPC_ASYNC_ENABLE_SLOT:
		dbg("%s SHPC_ASYNC_ENABLE_SLOT",__FUNCTION__);
		dbg("%s slot occupied = %d",__FUNCTION__,shpc_context->slot_context[slot_id].slot_occupied);
		if (shpc_context->slot_context[slot_id].slot_occupied == 1) {
			return 0;
		}
		//
		// Force pci-bus re-enumeration (probe), to load drivers on behalf on enabled device(s) on this slot.
		//
		dbg("%s   In callback routine processing enable slot",__FUNCTION__ );

		dbg("%s   CALLING amdshpc_slot_find  bus, dev, fn = %d, %d, %d\n",__FUNCTION__ ,
		    bus, device, function);
		slot_func = amdshpc_slot_find(bus, device, function);
		dbg("%s  slot_func = %p ",__FUNCTION__ , slot_func);
		if (!slot_func) {
			dbg("%s --> slot_func not found",__FUNCTION__ );
			return -ENODEV;
		}
		// Take care of the POGO and GOLAM erratas
		adjust_pcix_capabilities(ctrl,
					ctrl->pcix_slots,
					ctrl->max_split_trans_perslot[slot_id],
					ctrl->pcix_max_read_byte_count,
					ctrl->bus_info.x.SecondaryBus,
					slot_func->device );

		slot_func->bus = bus;
		slot_func->device = device;
		slot_func->function = function;
		slot_func->configured = 0;
		dbg("%s   CALLING amdshpc_process_SI(ctrl=%p slot_func=%p)\n",__FUNCTION__ , ctrl, slot_func);
		rc = amdshpc_process_SI(ctrl, slot_func);
		if (!rc ) {
			shpc_context->slot_context[slot_id].slot_occupied = 1;
		}
		dbg("%s   amdshpc_process_SI returned  rc=%d",__FUNCTION__ , (int)rc);
		break;

	case SHPC_ASYNC_SURPRISE_REMOVE:
		dbg("%s SHPC_ASYNC_SURPRISE_REMOVE",__FUNCTION__);
		//
		// Something went wrong with the slot (eg, power-fault), and loaded drivers must be removed.
		//
	case SHPC_ASYNC_QUIESCE_DEVNODE:
		dbg("%s SHPC_ASYNC_QUIESCE_DEVNODE",__FUNCTION__);
		//
		// Friendly opportunity to quiesce (remove) drivers, prior to disabling the slot.
		// After device drivers are removed, it's OK to show messages to that effect.
		//
		// If device quiecing will complete at a later time (from a separate thread),
		// then set "devices_still_quiescing" accordingly, and upon quiecing-completion,
		// call hp_StartAsyncRequest() with a "SHPC_ASYNC_QUIESCE_DEVNODE_NOTIFY" request.
		//
	case SHPC_ASYNC_QUIESCE_DEVNODE_QUIET:
		dbg("%s SHPC_ASYNC_QUIESCE_DEVNODE_QUIET",__FUNCTION__);
		//
		// Friendly opportunity to quiesce (remove) drivers, prior to disabling the slot.
		// After device drivers are removed, don't show messages to that effect.
		//
		// If device quiecing will complete at a later time (from a separate thread),
		// then set "devices_still_quiescing" accordingly, and upon quiecing-completion,
		// call hp_StartAsyncRequest() with a "SHPC_ASYNC_QUIESCE_DEVNODE_NOTIFY" request.
		//
		dbg("%s   Processing disable slot",__FUNCTION__ );

		dbg("%s   CALLING amdshpc_slot_find  bus, dev, fn = %d, %d, %d\n",__FUNCTION__ ,
		    bus, device, function);

		slot_func = amdshpc_slot_find(bus, device, function);
		dbg("%s  slot_func = %p ",__FUNCTION__ , slot_func);
		if (!slot_func) {
			dbg("%s --> slot_func not found",__FUNCTION__ );
			return -ENODEV;
		}

		dbg("%s   CALLING amdshpc_process_SS(ctrl=%p slot_func=%p)\n",__FUNCTION__ , ctrl, slot_func);
		rc = amdshpc_process_SS(ctrl, slot_func);
		if (!rc ) {
			shpc_context->slot_context[slot_id].slot_occupied = 0;
		}
		dbg("%s   amdshpc_process_SS returned  rc=%d",__FUNCTION__ , (int)rc);

		break;

	case SHPC_ASYNC_DISABLE_SLOT:
		dbg("%s SHPC_ASYNC_DISABLE_SLOT",__FUNCTION__);
		//
		// Just a notification, may be used to update some interested GUI application.
		//
		break;

	default:
		break;
	}
	return devices_still_quiescing;
}

