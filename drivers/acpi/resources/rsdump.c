/*******************************************************************************
 *
 * Module Name: rsdump - Functions to display the resource structures.
 *              $Revision: 23 $
 *
 ******************************************************************************/

/*
 *  Copyright (C) 2000, 2001 R. Byron Moore
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#include "acpi.h"
#include "acresrc.h"

#define _COMPONENT          ACPI_RESOURCES
	 MODULE_NAME         ("rsdump")


#ifdef ACPI_DEBUG

/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_dump_irq
 *
 * PARAMETERS:  Data            - pointer to the resource structure to dump.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Prints out the various members of the Data structure type.
 *
 ******************************************************************************/

void
acpi_rs_dump_irq (
	acpi_resource_data      *data)
{
	acpi_resource_irq       *irq_data = (acpi_resource_irq *) data;
	u8                      index = 0;


	FUNCTION_ENTRY ();


	acpi_os_printf ("IRQ Resource\n");

	acpi_os_printf ("  %s Triggered\n",
			 LEVEL_SENSITIVE == irq_data->edge_level ? "Level" : "Edge");

	acpi_os_printf ("  Active %s\n",
			 ACTIVE_LOW == irq_data->active_high_low ? "Low" : "High");

	acpi_os_printf ("  %s\n",
			 SHARED == irq_data->shared_exclusive ? "Shared" : "Exclusive");

	acpi_os_printf ("  %X Interrupts ( ", irq_data->number_of_interrupts);

	for (index = 0; index < irq_data->number_of_interrupts; index++) {
		acpi_os_printf ("%X ", irq_data->interrupts[index]);
	}

	acpi_os_printf (")\n");
	return;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_dump_dma
 *
 * PARAMETERS:  Data            - pointer to the resource structure to dump.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Prints out the various members of the Data structure type.
 *
 ******************************************************************************/

void
acpi_rs_dump_dma (
	acpi_resource_data      *data)
{
	acpi_resource_dma       *dma_data = (acpi_resource_dma *) data;
	u8                      index = 0;


	FUNCTION_ENTRY ();


	acpi_os_printf ("DMA Resource\n");

	switch (dma_data->type) {
	case COMPATIBILITY:
		acpi_os_printf ("  Compatibility mode\n");
		break;

	case TYPE_A:
		acpi_os_printf ("  Type A\n");
		break;

	case TYPE_B:
		acpi_os_printf ("  Type B\n");
		break;

	case TYPE_F:
		acpi_os_printf ("  Type F\n");
		break;

	default:
		acpi_os_printf ("  Invalid DMA type\n");
		break;
	}

	acpi_os_printf ("  %sBus Master\n",
			 BUS_MASTER == dma_data->bus_master ? "" : "Not a ");


	switch (dma_data->transfer) {
	case TRANSFER_8:
		acpi_os_printf ("  8-bit only transfer\n");
		break;

	case TRANSFER_8_16:
		acpi_os_printf ("  8 and 16-bit transfer\n");
		break;

	case TRANSFER_16:
		acpi_os_printf ("  16 bit only transfer\n");
		break;

	default:
		acpi_os_printf ("  Invalid transfer preference\n");
		break;
	}

	acpi_os_printf ("  Number of Channels: %X ( ", dma_data->number_of_channels);

	for (index = 0; index < dma_data->number_of_channels; index++) {
		acpi_os_printf ("%X ", dma_data->channels[index]);
	}

	acpi_os_printf (")\n");
	return;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_dump_start_dependent_functions
 *
 * PARAMETERS:  Data            - pointer to the resource structure to dump.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Prints out the various members of the Data structure type.
 *
 ******************************************************************************/

void
acpi_rs_dump_start_dependent_functions (
	acpi_resource_data          *data)
{
	acpi_resource_start_dpf     *sdf_data = (acpi_resource_start_dpf *) data;


	FUNCTION_ENTRY ();


	acpi_os_printf ("Start Dependent Functions Resource\n");

	switch (sdf_data->compatibility_priority) {
	case GOOD_CONFIGURATION:
		acpi_os_printf ("  Good configuration\n");
		break;

	case ACCEPTABLE_CONFIGURATION:
		acpi_os_printf ("  Acceptable configuration\n");
		break;

	case SUB_OPTIMAL_CONFIGURATION:
		acpi_os_printf ("  Sub-optimal configuration\n");
		break;

	default:
		acpi_os_printf ("  Invalid compatibility priority\n");
		break;
	}

	switch(sdf_data->performance_robustness) {
	case GOOD_CONFIGURATION:
		acpi_os_printf ("  Good configuration\n");
		break;

	case ACCEPTABLE_CONFIGURATION:
		acpi_os_printf ("  Acceptable configuration\n");
		break;

	case SUB_OPTIMAL_CONFIGURATION:
		acpi_os_printf ("  Sub-optimal configuration\n");
		break;

	default:
		acpi_os_printf ("  Invalid performance "
				  "robustness preference\n");
		break;
	}

	return;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_dump_io
 *
 * PARAMETERS:  Data            - pointer to the resource structure to dump.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Prints out the various members of the Data structure type.
 *
 ******************************************************************************/

void
acpi_rs_dump_io (
	acpi_resource_data      *data)
{
	acpi_resource_io        *io_data = (acpi_resource_io *) data;


	FUNCTION_ENTRY ();


	acpi_os_printf ("Io Resource\n");

	acpi_os_printf ("  %d bit decode\n",
			 DECODE_16 == io_data->io_decode ? 16 : 10);

	acpi_os_printf ("  Range minimum base: %08X\n",
			 io_data->min_base_address);

	acpi_os_printf ("  Range maximum base: %08X\n",
			 io_data->max_base_address);

	acpi_os_printf ("  Alignment: %08X\n",
			 io_data->alignment);

	acpi_os_printf ("  Range Length: %08X\n",
			 io_data->range_length);

	return;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_dump_fixed_io
 *
 * PARAMETERS:  Data            - pointer to the resource structure to dump.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Prints out the various members of the Data structure type.
 *
 ******************************************************************************/

void
acpi_rs_dump_fixed_io (
	acpi_resource_data      *data)
{
	acpi_resource_fixed_io  *fixed_io_data = (acpi_resource_fixed_io *) data;


	FUNCTION_ENTRY ();


	acpi_os_printf ("Fixed Io Resource\n");
	acpi_os_printf ("  Range base address: %08X",
			 fixed_io_data->base_address);

	acpi_os_printf ("  Range length: %08X",
			 fixed_io_data->range_length);

	return;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_dump_vendor_specific
 *
 * PARAMETERS:  Data            - pointer to the resource structure to dump.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Prints out the various members of the Data structure type.
 *
 ******************************************************************************/

void
acpi_rs_dump_vendor_specific (
	acpi_resource_data      *data)
{
	acpi_resource_vendor    *vendor_data = (acpi_resource_vendor *) data;
	u16                     index = 0;


	FUNCTION_ENTRY ();


	acpi_os_printf ("Vendor Specific Resource\n");

	acpi_os_printf ("  Length: %08X\n", vendor_data->length);

	for (index = 0; index < vendor_data->length; index++) {
		acpi_os_printf ("  Byte %X: %08X\n",
				 index, vendor_data->reserved[index]);
	}

	return;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_dump_memory24
 *
 * PARAMETERS:  Data            - pointer to the resource structure to dump.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Prints out the various members of the Data structure type.
 *
 ******************************************************************************/

void
acpi_rs_dump_memory24 (
	acpi_resource_data      *data)
{
	acpi_resource_mem24     *memory24_data = (acpi_resource_mem24 *) data;


	FUNCTION_ENTRY ();


	acpi_os_printf ("24-Bit Memory Range Resource\n");

	acpi_os_printf ("  Read%s\n",
			 READ_WRITE_MEMORY ==
			 memory24_data->read_write_attribute ?
			 "/Write" : " only");

	acpi_os_printf ("  Range minimum base: %08X\n",
			 memory24_data->min_base_address);

	acpi_os_printf ("  Range maximum base: %08X\n",
			 memory24_data->max_base_address);

	acpi_os_printf ("  Alignment: %08X\n",
			 memory24_data->alignment);

	acpi_os_printf ("  Range length: %08X\n",
			 memory24_data->range_length);

	return;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_dump_memory32
 *
 * PARAMETERS:  Data            - pointer to the resource structure to dump.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Prints out the various members of the Data structure type.
 *
 ******************************************************************************/

void
acpi_rs_dump_memory32 (
	acpi_resource_data      *data)
{
	acpi_resource_mem32     *memory32_data = (acpi_resource_mem32 *) data;


	FUNCTION_ENTRY ();


	acpi_os_printf ("32-Bit Memory Range Resource\n");

	acpi_os_printf ("  Read%s\n",
			 READ_WRITE_MEMORY ==
			 memory32_data->read_write_attribute ?
			 "/Write" : " only");

	acpi_os_printf ("  Range minimum base: %08X\n",
			 memory32_data->min_base_address);

	acpi_os_printf ("  Range maximum base: %08X\n",
			 memory32_data->max_base_address);

	acpi_os_printf ("  Alignment: %08X\n",
			 memory32_data->alignment);

	acpi_os_printf ("  Range length: %08X\n",
			 memory32_data->range_length);

	return;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_dump_fixed_memory32
 *
 * PARAMETERS:  Data            - pointer to the resource structure to dump.
 *
 * RETURN:
 *
 * DESCRIPTION: Prints out the various members of the Data structure type.
 *
 ******************************************************************************/

void
acpi_rs_dump_fixed_memory32 (
	acpi_resource_data          *data)
{
	acpi_resource_fixed_mem32   *fixed_memory32_data = (acpi_resource_fixed_mem32 *) data;


	FUNCTION_ENTRY ();


	acpi_os_printf ("32-Bit Fixed Location Memory Range Resource\n");

	acpi_os_printf ("  Read%s\n",
			 READ_WRITE_MEMORY ==
			 fixed_memory32_data->read_write_attribute ?
			 "/Write" : " Only");

	acpi_os_printf ("  Range base address: %08X\n",
			 fixed_memory32_data->range_base_address);

	acpi_os_printf ("  Range length: %08X\n",
			 fixed_memory32_data->range_length);

	return;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_dump_address16
 *
 * PARAMETERS:  Data            - pointer to the resource structure to dump.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Prints out the various members of the Data structure type.
 *
 ******************************************************************************/

void
acpi_rs_dump_address16 (
	acpi_resource_data      *data)
{
	acpi_resource_address16 *address16_data = (acpi_resource_address16 *) data;


	FUNCTION_ENTRY ();


	acpi_os_printf ("16-Bit Address Space Resource\n");
	acpi_os_printf ("  Resource Type: ");

	switch (address16_data->resource_type) {
	case MEMORY_RANGE:

		acpi_os_printf ("Memory Range\n");

		switch (address16_data->attribute.memory.cache_attribute) {
		case NON_CACHEABLE_MEMORY:
			acpi_os_printf ("  Type Specific: "
					  "Noncacheable memory\n");
			break;

		case CACHABLE_MEMORY:
			acpi_os_printf ("  Type Specific: "
					  "Cacheable memory\n");
			break;

		case WRITE_COMBINING_MEMORY:
			acpi_os_printf ("  Type Specific: "
					  "Write-combining memory\n");
			break;

		case PREFETCHABLE_MEMORY:
			acpi_os_printf ("  Type Specific: "
					  "Prefetchable memory\n");
			break;

		default:
			acpi_os_printf ("  Type Specific: "
					  "Invalid cache attribute\n");
			break;
		}

		acpi_os_printf ("  Type Specific: Read%s\n",
			READ_WRITE_MEMORY ==
			address16_data->attribute.memory.read_write_attribute ?
			"/Write" : " Only");
		break;

	case IO_RANGE:

		acpi_os_printf ("I/O Range\n");

		switch (address16_data->attribute.io.range_attribute) {
		case NON_ISA_ONLY_RANGES:
			acpi_os_printf ("  Type Specific: "
					  "Non-ISA Io Addresses\n");
			break;

		case ISA_ONLY_RANGES:
			acpi_os_printf ("  Type Specific: "
					  "ISA Io Addresses\n");
			break;

		case ENTIRE_RANGE:
			acpi_os_printf ("  Type Specific: "
					  "ISA and non-ISA Io Addresses\n");
			break;

		default:
			acpi_os_printf ("  Type Specific: "
					  "Invalid range attribute\n");
			break;
		}
		break;

	case BUS_NUMBER_RANGE:

		acpi_os_printf ("Bus Number Range\n");
		break;

	default:

		acpi_os_printf ("Invalid resource type. Exiting.\n");
		return;
	}

	acpi_os_printf ("  Resource %s\n",
			CONSUMER == address16_data->producer_consumer ?
			"Consumer" : "Producer");

	acpi_os_printf ("  %s decode\n",
			 SUB_DECODE == address16_data->decode ?
			 "Subtractive" : "Positive");

	acpi_os_printf ("  Min address is %s fixed\n",
			 ADDRESS_FIXED == address16_data->min_address_fixed ?
			 "" : "not");

	acpi_os_printf ("  Max address is %s fixed\n",
			 ADDRESS_FIXED == address16_data->max_address_fixed ?
			 "" : "not");

	acpi_os_printf ("  Granularity: %08X\n",
			 address16_data->granularity);

	acpi_os_printf ("  Address range min: %08X\n",
			 address16_data->min_address_range);

	acpi_os_printf ("  Address range max: %08X\n",
			 address16_data->max_address_range);

	acpi_os_printf ("  Address translation offset: %08X\n",
			 address16_data->address_translation_offset);

	acpi_os_printf ("  Address Length: %08X\n",
			 address16_data->address_length);

	if (0xFF != address16_data->resource_source.index) {
		acpi_os_printf ("  Resource Source Index: %X\n",
				 address16_data->resource_source.index);
		acpi_os_printf ("  Resource Source: %s\n",
				 address16_data->resource_source.string_ptr);
	}

	return;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_dump_address32
 *
 * PARAMETERS:  Data            - pointer to the resource structure to dump.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Prints out the various members of the Data structure type.
 *
 ******************************************************************************/

void
acpi_rs_dump_address32 (
	acpi_resource_data      *data)
{
	acpi_resource_address32 *address32_data = (acpi_resource_address32 *) data;


	FUNCTION_ENTRY ();


	acpi_os_printf ("32-Bit Address Space Resource\n");

	switch (address32_data->resource_type) {
	case MEMORY_RANGE:

		acpi_os_printf ("  Resource Type: Memory Range\n");

		switch (address32_data->attribute.memory.cache_attribute) {
		case NON_CACHEABLE_MEMORY:
			acpi_os_printf ("  Type Specific: "
					  "Noncacheable memory\n");
			break;

		case CACHABLE_MEMORY:
			acpi_os_printf ("  Type Specific: "
					  "Cacheable memory\n");
			break;

		case WRITE_COMBINING_MEMORY:
			acpi_os_printf ("  Type Specific: "
					  "Write-combining memory\n");
			break;

		case PREFETCHABLE_MEMORY:
			acpi_os_printf ("  Type Specific: "
					  "Prefetchable memory\n");
			break;

		default:
			acpi_os_printf ("  Type Specific: "
					  "Invalid cache attribute\n");
			break;
		}

		acpi_os_printf ("  Type Specific: Read%s\n",
			READ_WRITE_MEMORY ==
			address32_data->attribute.memory.read_write_attribute ?
			"/Write" : " Only");
		break;

	case IO_RANGE:

		acpi_os_printf ("  Resource Type: Io Range\n");

		switch (address32_data->attribute.io.range_attribute) {
			case NON_ISA_ONLY_RANGES:
				acpi_os_printf ("  Type Specific: "
						  "Non-ISA Io Addresses\n");
				break;

			case ISA_ONLY_RANGES:
				acpi_os_printf ("  Type Specific: "
						  "ISA Io Addresses\n");
				break;

			case ENTIRE_RANGE:
				acpi_os_printf ("  Type Specific: "
						  "ISA and non-ISA Io Addresses\n");
				break;

			default:
				acpi_os_printf ("  Type Specific: "
						  "Invalid Range attribute");
				break;
			}
		break;

	case BUS_NUMBER_RANGE:

		acpi_os_printf ("  Resource Type: Bus Number Range\n");
		break;

	default:

		acpi_os_printf ("  Invalid Resource Type..exiting.\n");
		return;
	}

	acpi_os_printf ("  Resource %s\n",
			 CONSUMER == address32_data->producer_consumer ?
			 "Consumer" : "Producer");

	acpi_os_printf ("  %s decode\n",
			 SUB_DECODE == address32_data->decode ?
			 "Subtractive" : "Positive");

	acpi_os_printf ("  Min address is %s fixed\n",
			 ADDRESS_FIXED == address32_data->min_address_fixed ?
			 "" : "not ");

	acpi_os_printf ("  Max address is %s fixed\n",
			 ADDRESS_FIXED == address32_data->max_address_fixed ?
			 "" : "not ");

	acpi_os_printf ("  Granularity: %08X\n",
			 address32_data->granularity);

	acpi_os_printf ("  Address range min: %08X\n",
			 address32_data->min_address_range);

	acpi_os_printf ("  Address range max: %08X\n",
			 address32_data->max_address_range);

	acpi_os_printf ("  Address translation offset: %08X\n",
			 address32_data->address_translation_offset);

	acpi_os_printf ("  Address Length: %08X\n",
			 address32_data->address_length);

	if(0xFF != address32_data->resource_source.index) {
		acpi_os_printf ("  Resource Source Index: %X\n",
				 address32_data->resource_source.index);
		acpi_os_printf ("  Resource Source: %s\n",
				 address32_data->resource_source.string_ptr);
	}

	return;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_dump_address64
 *
 * PARAMETERS:  Data            - pointer to the resource structure to dump.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Prints out the various members of the Data structure type.
 *
 ******************************************************************************/

void
acpi_rs_dump_address64 (
	acpi_resource_data      *data)
{
	acpi_resource_address64 *address64_data = (acpi_resource_address64 *) data;


	FUNCTION_ENTRY ();


	acpi_os_printf ("64-Bit Address Space Resource\n");

	switch (address64_data->resource_type) {
	case MEMORY_RANGE:

		acpi_os_printf ("  Resource Type: Memory Range\n");

		switch (address64_data->attribute.memory.cache_attribute) {
		case NON_CACHEABLE_MEMORY:
			acpi_os_printf ("  Type Specific: "
					  "Noncacheable memory\n");
			break;

		case CACHABLE_MEMORY:
			acpi_os_printf ("  Type Specific: "
					  "Cacheable memory\n");
			break;

		case WRITE_COMBINING_MEMORY:
			acpi_os_printf ("  Type Specific: "
					  "Write-combining memory\n");
			break;

		case PREFETCHABLE_MEMORY:
			acpi_os_printf ("  Type Specific: "
					  "Prefetchable memory\n");
			break;

		default:
			acpi_os_printf ("  Type Specific: "
					  "Invalid cache attribute\n");
			break;
		}

		acpi_os_printf ("  Type Specific: Read%s\n",
			READ_WRITE_MEMORY ==
			address64_data->attribute.memory.read_write_attribute ?
			"/Write" : " Only");
		break;

	case IO_RANGE:

		acpi_os_printf ("  Resource Type: Io Range\n");

		switch (address64_data->attribute.io.range_attribute) {
			case NON_ISA_ONLY_RANGES:
				acpi_os_printf ("  Type Specific: "
						  "Non-ISA Io Addresses\n");
				break;

			case ISA_ONLY_RANGES:
				acpi_os_printf ("  Type Specific: "
						  "ISA Io Addresses\n");
				break;

			case ENTIRE_RANGE:
				acpi_os_printf ("  Type Specific: "
						  "ISA and non-ISA Io Addresses\n");
				break;

			default:
				acpi_os_printf ("  Type Specific: "
						  "Invalid Range attribute");
				break;
			}
		break;

	case BUS_NUMBER_RANGE:

		acpi_os_printf ("  Resource Type: Bus Number Range\n");
		break;

	default:

		acpi_os_printf ("  Invalid Resource Type..exiting.\n");
		return;
	}

	acpi_os_printf ("  Resource %s\n",
			 CONSUMER == address64_data->producer_consumer ?
			 "Consumer" : "Producer");

	acpi_os_printf ("  %s decode\n",
			 SUB_DECODE == address64_data->decode ?
			 "Subtractive" : "Positive");

	acpi_os_printf ("  Min address is %s fixed\n",
			 ADDRESS_FIXED == address64_data->min_address_fixed ?
			 "" : "not ");

	acpi_os_printf ("  Max address is %s fixed\n",
			 ADDRESS_FIXED == address64_data->max_address_fixed ?
			 "" : "not ");

	acpi_os_printf ("  Granularity: %16X\n",
			 address64_data->granularity);

	acpi_os_printf ("  Address range min: %16X\n",
			 address64_data->min_address_range);

	acpi_os_printf ("  Address range max: %16X\n",
			 address64_data->max_address_range);

	acpi_os_printf ("  Address translation offset: %16X\n",
			 address64_data->address_translation_offset);

	acpi_os_printf ("  Address Length: %16X\n",
			 address64_data->address_length);

	if(0xFF != address64_data->resource_source.index) {
		acpi_os_printf ("  Resource Source Index: %X\n",
				 address64_data->resource_source.index);
		acpi_os_printf ("  Resource Source: %s\n",
				 address64_data->resource_source.string_ptr);
	}

	return;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_dump_extended_irq
 *
 * PARAMETERS:  Data            - pointer to the resource structure to dump.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Prints out the various members of the Data structure type.
 *
 ******************************************************************************/

void
acpi_rs_dump_extended_irq (
	acpi_resource_data      *data)
{
	acpi_resource_ext_irq   *ext_irq_data = (acpi_resource_ext_irq *) data;
	u8                      index = 0;


	FUNCTION_ENTRY ();


	acpi_os_printf ("Extended IRQ Resource\n");

	acpi_os_printf ("  Resource %s\n",
			 CONSUMER == ext_irq_data->producer_consumer ?
			 "Consumer" : "Producer");

	acpi_os_printf ("  %s\n",
			 LEVEL_SENSITIVE == ext_irq_data->edge_level ?
			 "Level" : "Edge");

	acpi_os_printf ("  Active %s\n",
			 ACTIVE_LOW == ext_irq_data->active_high_low ?
			 "low" : "high");

	acpi_os_printf ("  %s\n",
			 SHARED == ext_irq_data->shared_exclusive ?
			 "Shared" : "Exclusive");

	acpi_os_printf ("  Interrupts : %X ( ",
			 ext_irq_data->number_of_interrupts);

	for (index = 0; index < ext_irq_data->number_of_interrupts; index++) {
		acpi_os_printf ("%X ", ext_irq_data->interrupts[index]);
	}

	acpi_os_printf (")\n");

	if(0xFF != ext_irq_data->resource_source.index) {
		acpi_os_printf ("  Resource Source Index: %X",
				 ext_irq_data->resource_source.index);
		acpi_os_printf ("  Resource Source: %s",
				 ext_irq_data->resource_source.string_ptr);
	}

	return;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_dump_resource_list
 *
 * PARAMETERS:  Data            - pointer to the resource structure to dump.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dispatches the structure to the correct dump routine.
 *
 ******************************************************************************/

void
acpi_rs_dump_resource_list (
	acpi_resource       *resource)
{
	u8                  count = 0;
	u8                  done = FALSE;


	FUNCTION_ENTRY ();


	if (acpi_dbg_level & ACPI_LV_RESOURCES && _COMPONENT & acpi_dbg_layer) {
		while (!done) {
			acpi_os_printf ("Resource structure %x.\n", count++);

			switch (resource->id) {
			case ACPI_RSTYPE_IRQ:
				acpi_rs_dump_irq (&resource->data);
				break;

			case ACPI_RSTYPE_DMA:
				acpi_rs_dump_dma (&resource->data);
				break;

			case ACPI_RSTYPE_START_DPF:
				acpi_rs_dump_start_dependent_functions (&resource->data);
				break;

			case ACPI_RSTYPE_END_DPF:
				acpi_os_printf ("End_dependent_functions Resource\n");
				/* Acpi_rs_dump_end_dependent_functions (Resource->Data);*/
				break;

			case ACPI_RSTYPE_IO:
				acpi_rs_dump_io (&resource->data);
				break;

			case ACPI_RSTYPE_FIXED_IO:
				acpi_rs_dump_fixed_io (&resource->data);
				break;

			case ACPI_RSTYPE_VENDOR:
				acpi_rs_dump_vendor_specific (&resource->data);
				break;

			case ACPI_RSTYPE_END_TAG:
				/*Rs_dump_end_tag (Resource->Data);*/
				acpi_os_printf ("End_tag Resource\n");
				done = TRUE;
				break;

			case ACPI_RSTYPE_MEM24:
				acpi_rs_dump_memory24 (&resource->data);
				break;

			case ACPI_RSTYPE_MEM32:
				acpi_rs_dump_memory32 (&resource->data);
				break;

			case ACPI_RSTYPE_FIXED_MEM32:
				acpi_rs_dump_fixed_memory32 (&resource->data);
				break;

			case ACPI_RSTYPE_ADDRESS16:
				acpi_rs_dump_address16 (&resource->data);
				break;

			case ACPI_RSTYPE_ADDRESS32:
				acpi_rs_dump_address32 (&resource->data);
				break;

			case ACPI_RSTYPE_ADDRESS64:
				acpi_rs_dump_address64 (&resource->data);
				break;

			case ACPI_RSTYPE_EXT_IRQ:
				acpi_rs_dump_extended_irq (&resource->data);
				break;

			default:
				acpi_os_printf ("Invalid resource type\n");
				break;

			}

			resource = POINTER_ADD (acpi_resource, resource, resource->length);
		}
	}

	return;
}

/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_dump_irq_list
 *
 * PARAMETERS:  Data            - pointer to the routing table to dump.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dispatches the structures to the correct dump routine.
 *
 ******************************************************************************/

void
acpi_rs_dump_irq_list (
	u8                  *route_table)
{
	u8                  *buffer = route_table;
	u8                  count = 0;
	u8                  done = FALSE;
	pci_routing_table   *prt_element;


	FUNCTION_ENTRY ();


	if (acpi_dbg_level & ACPI_LV_RESOURCES && _COMPONENT & acpi_dbg_layer) {
		prt_element = (pci_routing_table *) buffer;

		while (!done) {
			acpi_os_printf ("PCI IRQ Routing Table structure %X.\n", count++);

			acpi_os_printf ("  Address: %X\n",
					 prt_element->address);

			acpi_os_printf ("  Pin: %X\n", prt_element->pin);

			acpi_os_printf ("  Source: %s\n", prt_element->source);

			acpi_os_printf ("  Source_index: %X\n",
					 prt_element->source_index);

			buffer += prt_element->length;

			prt_element = (pci_routing_table *) buffer;

			if(0 == prt_element->length) {
				done = TRUE;
			}
		}
	}

	return;
}

#endif

