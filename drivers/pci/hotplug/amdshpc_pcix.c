#include <linux/module.h>
#include "amdshpc.h"
// +
// PCI-X Max outstanding Split Transactions (1..32)
// -
u32 PCIX_SplitTransactionTable[] = {
	1,		// 000b
	2,		// 001b
	3,		// 010b
	4,		// 011b
	8,		// 100b
	12,		// 101b
	16,		// 110b
	32		// 111b
};

// +
// PCI-X Max read byte count (512..4096)
// -
u32 PCIX_ReadByteCountTable[] = {
	512,	// 00b
	1024,	// 01b
	2048,	// 10b
	4096,	// 11b
};

struct pcix_context_struct {
	u32 functions;
	u32 max_split_transactions;
	u32 max_read_byte_count;
};

void adjust_pcix_capabilities(
			struct controller * ctrl,
			u32 pcix_slots,
			u32 max_split_transactions,
			u32 max_read_byte_count,
			u32 secondary_bus,
			u32 device
			);

typedef void( *pcix_callback_function )(
				struct controller * ctrl,
				u32 bus,
				u32 device,
				u32 function,
				u32 cap_ptr,
				void *context
				);

void pcix_exclude_devices(
			 struct controller * ctrl,
			 u32 enabled_slot_PSN,
			 u8 pcix_exclude_device[]
			 );


u8 pcix_enumerate_device(
			struct controller * ctrl,
			u32 bus,
			u32 device,
			pcix_callback_function pcix_callback,
			void *context
			);

void pcix_enumerate_bridge(
			struct controller *ctrl,
			u32 secondary_bus,
			u32 subordinate_bus,
			pcix_callback_function pcix_callback,
			void *context
			);

union pci_cmd_status is_bus_master(
				struct controller * ctrl,
				u32 bus,
				u32 device,
				u32 function
				);

void pcix_functions_callback(
			struct controller * ctrl,
			u32 bus,
			u32 device,
			u32 function,
			u32 cap_ptr,
			void *context
			);

void pcix_adjust_callback(
			struct controller * ctrl,
			u32 bus,
			u32 device,
			u32 function,
			u32 cap_ptr,
			void *context
			);

// ---------------------------------------------------------------------
//
// adjust_pcix_capabilities()
//
// ---------------------------------------------------------------------
void adjust_pcix_capabilities(
			struct controller *ctrl,
			u32 pcix_slots,
			u32 max_split_transactions,
			u32 max_read_byte_count,
			u32 secondary_bus,
			u32 device
			)
{
	struct pcix_context_struct pcix_context;

	dbg("%s ",__FUNCTION__);

	// sanity check
	if ( pcix_slots > SHPC_MAX_NUM_SLOTS ) {
		pcix_slots = SHPC_MAX_NUM_SLOTS;
	}

	// configure one device per slot on seconday bus
	// initialize device context
	pcix_context.functions = 0;
	pcix_context.max_split_transactions = max_split_transactions;
	pcix_context.max_read_byte_count = max_read_byte_count;


	if ( pcix_enumerate_device( ctrl, secondary_bus, device,
				    pcix_functions_callback, ( void *)&pcix_context )) {
		dbg("%s  PCI-X: Functions[ %d ]", __FUNCTION__, pcix_context.functions );

		// adjust detected PCI-X functions
		pcix_enumerate_device( ctrl, secondary_bus, device,
				       pcix_adjust_callback, ( void *)&pcix_context );
	}
}


// ---------------------------------------------------------------------
//
// pcix_enumerate_device()
//
// ---------------------------------------------------------------------
u8 pcix_enumerate_device(
			struct controller *ctrl,
			u32 bus,
			u32 device,
			pcix_callback_function pcix_callback,
			void *context
			)
{
	u32		function = 0;
	u32 		multi_function;
	u32		bus_dev_fun;
	u32		search, cap_ptr;
	u8		f_decode;
	union pci_header_info	header_info;
	union pci_cmd_status	cmd_status;
	union pci_cap_info	pci_cap;
	union pci_bus_info	bus_info;

	dbg("%s ",__FUNCTION__);

	// enumerate (PCI-X, non-bridge) functions for this device
	//	search: Bus[15..8], Dev[7..3], Fun[2..0]
	

	// attempt reading device's header
	pci_bus_read_config_dword(ctrl->pci_bus, PCI_DEVFN(device, function), PCI_HEADER_INFO_OFFSET, &header_info.AsDWord);
	if ( header_info.AsDWord == 0 || header_info.AsDWord == 0xFFFFFFFF ) {
		f_decode = FALSE;
	} else {
		// multiple-function device? 
		f_decode = TRUE;
		multi_function = header_info.x.MultiFunction ? 8 : 1;
		dbg( "%s  multi_function = 0x%X",  __FUNCTION__, multi_function );
		// enumerate functions for this device
		for ( function = 0; function < multi_function; ++function ) {
			bus_dev_fun = ( bus << 8 ) | (device << 3 ) | function;
			cmd_status = is_bus_master(ctrl, bus, device, function );
			if ( function > 0 ) {
				pci_bus_read_config_dword(ctrl->pci_bus, PCI_DEVFN(device, function), PCI_HEADER_INFO_OFFSET, &header_info.AsDWord);
			}

			// PCI-X to PCI-X bridge?
			if ( header_info.x.HeaderType == 1 &&
			     cmd_status.x.CapabilitiesList ) {
				// enumerate (PCI-X, non-bridge) functions behind this bridge
				pci_bus_read_config_dword(ctrl->pci_bus, PCI_DEVFN(device, function), PCI_BUS_INFO_OFFSET, &bus_info.AsDWord);

				pcix_enumerate_bridge(ctrl, bus_info.x.SecondaryBus,
						       bus_info.x.SubordinateBus, pcix_callback, context );
			}
			// non-bridge, bus-master?
			else if ( header_info.x.HeaderType == 0 &&
				  cmd_status.x.CapabilitiesList &&
				  cmd_status.x.BusMaster ) {

				dbg ( "%s  BDF[ 0x%X ]  CmdStatus[ 0x%X ]", __FUNCTION__,
				      bus_dev_fun, cmd_status.AsDWord );

				// search for PCI-X capability (with 8-bit cap pointer)
				search = 256;	// limit the loop search
				pci_bus_read_config_dword(ctrl->pci_bus, PCI_DEVFN(device, function), PCI_CAP_POINTER_OFFSET, &cap_ptr);
				cap_ptr &= 0xFF;
				while ( search && cap_ptr && cap_ptr != 0xFF ) {
					// get capability
					pci_bus_read_config_dword(ctrl->pci_bus, PCI_DEVFN(device, function), cap_ptr, &pci_cap.AsDWord);
					dbg ( "%s  Cap_Pointer[ 0x%X ]  Cap_Info[ 0x%X ]", __FUNCTION__,
					      cap_ptr, pci_cap.AsDWord );

					// found PCI-X capability?
					if ( pci_cap.x.CapabilityID == PCIX_CAPABILITY_ID ) {
						// we're done with this function
						pcix_callback(ctrl, bus, device, function, cap_ptr, context );
						cap_ptr = 0;
					} else {
						// check next capability
						--search;
						cap_ptr = pci_cap.x.PtrToNextCapability;
					}
				} 
			}
		}
	}

	return( f_decode );
}


// ---------------------------------------------------------------------
//
// pcix_enumerate_bridge()
//
// ---------------------------------------------------------------------
void pcix_enumerate_bridge(
			struct controller *ctrl,
			u32 secondary_bus,
			u32 subordinate_bus,
			pcix_callback_function pcix_callback,
			void *context
			)
{
	u32 bus, device;

	dbg("%s ",__FUNCTION__);

	// sanity check
	if ( subordinate_bus < secondary_bus ) {
		subordinate_bus = secondary_bus;
	}

	// enumerate (PCI-X, non-bridge) functions behind this bridge
	for ( bus = secondary_bus; bus <= subordinate_bus; ++bus ) {
		for ( device = 0; device < 32; ++device ) {
			pcix_enumerate_device( ctrl, bus, device, pcix_callback, context );
		}
	}

}


// ---------------------------------------------------------------------
//
// is_bus_master()
//
// ---------------------------------------------------------------------
union pci_cmd_status is_bus_master(
			struct controller * ctrl,
			u32 bus,
			u32 device,
			u32 function
			)
{
	union pci_cmd_status  cmd_status, bus_master;

	// save CMD_STATUS bit
	pci_bus_read_config_dword(ctrl->pci_bus, PCI_DEVFN(device, function), PCI_CMD_STATUS_OFFSET, &cmd_status.AsDWord);

	// set "bus master" bit to see if it sticks
	bus_master.AsDWord = cmd_status.AsDWord;
	bus_master.x.BusMaster = 1;
	pci_bus_write_config_dword(ctrl->pci_bus, PCI_DEVFN(device, function), PCI_CMD_STATUS_OFFSET, bus_master.AsDWord);
	pci_bus_read_config_dword(ctrl->pci_bus, PCI_DEVFN(device, function), PCI_CMD_STATUS_OFFSET, &bus_master.AsDWord);

	// restore CMD_STATUS bits
	pci_bus_read_config_dword(ctrl->pci_bus, PCI_DEVFN(device, function), PCI_CMD_STATUS_OFFSET, &cmd_status.AsDWord);

	return( bus_master );
}


// ---------------------------------------------------------------------
//
// pcix_functions_callback()
//
// ---------------------------------------------------------------------
void pcix_functions_callback(
				struct controller * ctrl,
				u32 bus,
				u32 device,
				u32 function,
				u32 cap_ptr,
				void *context
				)
{
	struct pcix_context_struct  * pcix_context = (struct pcix_context_struct * )context;

	dbg("%s ",__FUNCTION__);

	++pcix_context->functions;

}


// ---------------------------------------------------------------------
//
// pcix_adjust_callback()
//
// ---------------------------------------------------------------------
void pcix_adjust_callback(
			struct controller * ctrl,
			u32 bus,
			u32 device,
			u32 function,
			u32 cap_ptr,
			void *context
			)
{
	struct pcix_context_struct	*pcix_context = (struct pcix_context_struct * )context;
	u32			max_split_transactions;
	u32			max_code;
	union pci_cap_info	pci_cap;
	union pci_cmd_info	pcix_cmd;
	union pcix_status_info	pcix_status;

	dbg("%s ",__FUNCTION__);

	// any detected functions left?
	if ( pcix_context->functions ) {
		// read PCI-X capability
		pci_bus_read_config_dword(ctrl->pci_bus, PCI_DEVFN(device, function), cap_ptr, &pci_cap.AsDWord);
		pcix_cmd.AsWord = ( u16 )pci_cap.x.FeatureSpecific;
		pci_bus_read_config_dword(ctrl->pci_bus, PCI_DEVFN(device, function), cap_ptr+4, &pcix_status.AsDWord);

		dbg ( "%s  PCI-X: Command[ 0x%X ]  Status[ 0x%X ]", __FUNCTION__,
		      pcix_cmd.AsWord, pcix_status.AsDWord );

		// assign at least 1 split transaction
		max_split_transactions = pcix_context->max_split_transactions / pcix_context->functions;
		if ( max_split_transactions == 0 ) {
			max_split_transactions = 1;
		}

		// adjust PCI-X requester's split transactions 
		max_code = sizeof( PCIX_SplitTransactionTable )/sizeof( u32 ) - 1;
		while ( max_code &&
			PCIX_SplitTransactionTable[ max_code ] > max_split_transactions ) {
			--max_code;
		}
		
		dbg("%s   DesignedMaxOutstandingSplitTransactions = %d", 
		    __FUNCTION__, pcix_status.x.DesignedMaxOutstandingSplitTransactions);

		dbg( "%s  MaxOutstandingSplitTransactions = %d", 
		     __FUNCTION__, pcix_cmd.x.MaxOutstandingSplitTransactions );

		dbg( "%s  max_code before = %d", __FUNCTION__, max_code );
		
		if ( max_code > pcix_status.x.DesignedMaxOutstandingSplitTransactions ) {
			max_code = pcix_status.x.DesignedMaxOutstandingSplitTransactions;
		}
		if ( max_code > pcix_cmd.x.MaxOutstandingSplitTransactions ) {
			max_code = pcix_cmd.x.MaxOutstandingSplitTransactions;
		}
		pcix_cmd.x.MaxOutstandingSplitTransactions = ( u16 )max_code;

		dbg( "%s  max_code after = %d", __FUNCTION__, max_code );
		
		// update "split transaction" credits
		--pcix_context->functions;
		if ( pcix_context->max_split_transactions > PCIX_SplitTransactionTable[ max_code ] ) {
			pcix_context->max_split_transactions -= PCIX_SplitTransactionTable[ max_code ];
		} else {
			pcix_context->max_split_transactions = 0;
		}
		
		// adjust PCI-X requester's read byte count 
		max_code = sizeof( PCIX_ReadByteCountTable )/sizeof( u32 ) - 1;
		while ( max_code &&
			PCIX_ReadByteCountTable[ max_code ] > pcix_context->max_read_byte_count ) {
			--max_code;
		}
		dbg("%s   DesignedMaxMemoryReadByteCount = %d", 
		    __FUNCTION__, pcix_status.x.DesignedMaxMemoryReadByteCount);

		dbg( "%s  MaxMemoryReadByteCount = %d", 
		     __FUNCTION__, pcix_cmd.x.MaxMemoryReadByteCount );

		dbg( "%s  max_code before = %d", __FUNCTION__, max_code );
		
		if ( max_code > pcix_status.x.DesignedMaxMemoryReadByteCount ) {
			max_code = pcix_status.x.DesignedMaxMemoryReadByteCount;
		}
		if ( max_code > pcix_cmd.x.MaxMemoryReadByteCount ) {
			max_code = pcix_cmd.x.MaxMemoryReadByteCount;
		}
		pcix_cmd.x.MaxMemoryReadByteCount = ( u16 )max_code;

		dbg( "%s  max_code after = %d", __FUNCTION__, max_code );

		// update PCI-X capability
		pci_cap.x.FeatureSpecific = ( u32 )pcix_cmd.AsWord;
		dbg ( "%s  PCI-X: PRE-Write of pci_cap[ 0x%X ]", __FUNCTION__,
		      pci_cap.AsDWord );

		pci_bus_write_config_dword(ctrl->pci_bus, PCI_DEVFN(device, function), cap_ptr, pci_cap.AsDWord);
		pci_bus_read_config_dword(ctrl->pci_bus, PCI_DEVFN(device, function), cap_ptr, &pci_cap.AsDWord);

		dbg ( "%s  PCI-X: ReadBack of pci_cap[ 0x%X ]", __FUNCTION__,
		      pci_cap.AsDWord );
		dbg ( "%s  PCI-X: Adjusted Command[ 0x%X ]", __FUNCTION__,
		      pcix_cmd.AsWord );
	}
}

