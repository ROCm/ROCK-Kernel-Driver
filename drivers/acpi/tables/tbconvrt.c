/******************************************************************************
 *
 * Module Name: tbconvrt - ACPI Table conversion utilities
 *              $Revision: 36 $
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000 - 2002, R. Byron Moore
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
#include "actables.h"


#define _COMPONENT          ACPI_TABLES
	 ACPI_MODULE_NAME    ("tbconvrt")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_get_table_count
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION: Calculate the number of tables
 *
 ******************************************************************************/

u32
acpi_tb_get_table_count (
	RSDP_DESCRIPTOR         *RSDP,
	acpi_table_header       *RSDT)
{
	u32                     pointer_size;


	ACPI_FUNCTION_ENTRY ();


#ifndef _IA64

	if (RSDP->revision < 2) {
		pointer_size = sizeof (u32);
	}
	else
#endif
	{
		pointer_size = sizeof (u64);
	}

	/*
	 * Determine the number of tables pointed to by the RSDT/XSDT.
	 * This is defined by the ACPI Specification to be the number of
	 * pointers contained within the RSDT/XSDT.  The size of the pointers
	 * is architecture-dependent.
	 */
	return ((RSDT->length - sizeof (acpi_table_header)) / pointer_size);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_convert_to_xsdt
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION: Convert an RSDT to an XSDT (internal common format)
 *
 ******************************************************************************/

acpi_status
acpi_tb_convert_to_xsdt (
	acpi_table_desc         *table_info,
	u32                     *number_of_tables)
{
	u32                     table_size;
	u32                     i;
	xsdt_descriptor         *new_table;


	ACPI_FUNCTION_ENTRY ();


	/* Get the number of tables defined in the RSDT or XSDT */

	*number_of_tables = acpi_tb_get_table_count (acpi_gbl_RSDP, table_info->pointer);

	/* Compute size of the converted XSDT */

	table_size = (*number_of_tables * sizeof (u64)) + sizeof (acpi_table_header);

	/* Allocate an XSDT */

	new_table = ACPI_MEM_CALLOCATE (table_size);
	if (!new_table) {
		return (AE_NO_MEMORY);
	}

	/* Copy the header and set the length */

	ACPI_MEMCPY (new_table, table_info->pointer, sizeof (acpi_table_header));
	new_table->header.length = table_size;

	/* Copy the table pointers */

	for (i = 0; i < *number_of_tables; i++) {
		if (acpi_gbl_RSDP->revision < 2) {
			ACPI_STORE_ADDRESS (new_table->table_offset_entry[i],
				((RSDT_DESCRIPTOR_REV1 *) table_info->pointer)->table_offset_entry[i]);
		}
		else {
			new_table->table_offset_entry[i] =
				((xsdt_descriptor *) table_info->pointer)->table_offset_entry[i];
		}
	}

	/* Delete the original table (either mapped or in a buffer) */

	acpi_tb_delete_single_table (table_info);

	/* Point the table descriptor to the new table */

	table_info->pointer     = (acpi_table_header *) new_table;
	table_info->base_pointer = (acpi_table_header *) new_table;
	table_info->length      = table_size;
	table_info->allocation  = ACPI_MEM_ALLOCATED;

	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_convert_table_fadt
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION:
 *    Converts a BIOS supplied ACPI 1.0 FADT to an intermediate
 *    ACPI 2.0 FADT. If the BIOS supplied a 2.0 FADT then it is simply
 *    copied to the intermediate FADT.  The ACPI CA software uses this
 *    intermediate FADT. Thus a significant amount of special #ifdef
 *    type codeing is saved. This intermediate FADT will need to be
 *    freed at some point.
 *
 ******************************************************************************/

acpi_status
acpi_tb_convert_table_fadt (void)
{
	fadt_descriptor_rev1   *FADT1;
	fadt_descriptor_rev2   *FADT2;
	acpi_table_desc        *table_desc;


	ACPI_FUNCTION_TRACE ("Tb_convert_table_fadt");


	/*
	 * Acpi_gbl_FADT is valid
	 * Allocate and zero the 2.0 FADT buffer
	 */
	FADT2 = ACPI_MEM_CALLOCATE (sizeof (fadt_descriptor_rev2));
	if (FADT2 == NULL) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	/*
	 * The ACPI FADT revision number is FADT2_REVISION_ID=3
	 * So, if the current table revision is less than 3 it is type 1.0
	 */
	if (acpi_gbl_FADT->header.revision >= FADT2_REVISION_ID) {
		/* We have an ACPI 2.0 FADT but we must copy it to our local buffer */

		*FADT2 = *((fadt_descriptor_rev2*) acpi_gbl_FADT);
	}
	else {
		/* ACPI 1.0 FACS */

		/* The BIOS stored FADT should agree with Revision 1.0 */

		FADT1 = (fadt_descriptor_rev1*) acpi_gbl_FADT;

		/*
		 * Copy the table header and the common part of the tables.
		 *
		 * The 2.0 table is an extension of the 1.0 table, so the entire 1.0
		 * table can be copied first, then expand some fields to 64 bits.
		 */
		ACPI_MEMCPY (FADT2, FADT1, sizeof (fadt_descriptor_rev1));

		/* Convert table pointers to 64-bit fields */

		ACPI_STORE_ADDRESS (FADT2->Xfirmware_ctrl, FADT1->firmware_ctrl);
		ACPI_STORE_ADDRESS (FADT2->Xdsdt, FADT1->dsdt);

		/*
		 * System Interrupt Model isn't used in ACPI 2.0 (FADT2->Reserved1 = 0;)
		 */

		/*
		 * This field is set by the OEM to convey the preferred power management
		 * profile to OSPM. It doesn't have any 1.0 equivalence.  Since we don't
		 * know what kind of 32-bit system this is, we will use "unspecified".
		 */
		FADT2->prefer_PM_profile = PM_UNSPECIFIED;

		/*
		 * Processor Performance State Control. This is the value OSPM writes to
		 * the SMI_CMD register to assume processor performance state control
		 * responsibility. There isn't any equivalence in 1.0, leave it zeroed.
		 */
		FADT2->pstate_cnt = 0;

		/*
		 * Support for the _CST object and C States change notification.
		 * This data item hasn't any 1.0 equivalence so leave it zero.
		 */
		FADT2->cst_cnt = 0;

		/*
		 * Since there isn't any equivalence in 1.0 and since it highly likely
		 * that a 1.0 system has legacy support.
		 */
		FADT2->iapc_boot_arch = BAF_LEGACY_DEVICES;

		/*
		 * Convert the V1.0 block addresses to V2.0 GAS structures
		 * in this order:
		 *
		 * PM 1_a Events
		 * PM 1_b Events
		 * PM 1_a Control
		 * PM 1_b Control
		 * PM 2 Control
		 * PM Timer Control
		 * GPE Block 0
		 * GPE Block 1
		 */
		ASL_BUILD_GAS_FROM_V1_ENTRY (FADT2->Xpm1a_evt_blk, FADT1->pm1_evt_len, FADT1->pm1a_evt_blk);
		ASL_BUILD_GAS_FROM_V1_ENTRY (FADT2->Xpm1b_evt_blk, FADT1->pm1_evt_len, FADT1->pm1b_evt_blk);
		ASL_BUILD_GAS_FROM_V1_ENTRY (FADT2->Xpm1a_cnt_blk, FADT1->pm1_cnt_len, FADT1->pm1a_cnt_blk);
		ASL_BUILD_GAS_FROM_V1_ENTRY (FADT2->Xpm1b_cnt_blk, FADT1->pm1_cnt_len, FADT1->pm1b_cnt_blk);
		ASL_BUILD_GAS_FROM_V1_ENTRY (FADT2->Xpm2_cnt_blk, FADT1->pm2_cnt_len, FADT1->pm2_cnt_blk);
		ASL_BUILD_GAS_FROM_V1_ENTRY (FADT2->Xpm_tmr_blk, FADT1->pm_tm_len,  FADT1->pm_tmr_blk);
		ASL_BUILD_GAS_FROM_V1_ENTRY (FADT2->Xgpe0_blk,   FADT1->gpe0_blk_len, FADT1->gpe0_blk);
		ASL_BUILD_GAS_FROM_V1_ENTRY (FADT2->Xgpe1_blk,   FADT1->gpe1_blk_len, FADT1->gpe1_blk);
	}

	/*
	 * Global FADT pointer will point to the common V2.0 FADT
	 */
	acpi_gbl_FADT = FADT2;
	acpi_gbl_FADT->header.length = sizeof (FADT_DESCRIPTOR);

	/* Free the original table */

	table_desc = &acpi_gbl_acpi_tables[ACPI_TABLE_FADT];
	acpi_tb_delete_single_table (table_desc);

	/* Install the new table */

	table_desc->pointer = (acpi_table_header *) acpi_gbl_FADT;
	table_desc->base_pointer = acpi_gbl_FADT;
	table_desc->allocation = ACPI_MEM_ALLOCATED;
	table_desc->length = sizeof (fadt_descriptor_rev2);

	/* Dump the entire FADT */

	ACPI_DEBUG_PRINT ((ACPI_DB_TABLES,
		"Hex dump of common internal FADT, size %d (%X)\n",
		acpi_gbl_FADT->header.length, acpi_gbl_FADT->header.length));
	ACPI_DUMP_BUFFER ((u8 *) (acpi_gbl_FADT), acpi_gbl_FADT->header.length);

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_convert_table_facs
 *
 * PARAMETERS:  Table_info      - Info for currently installad FACS
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert ACPI 1.0 and ACPI 2.0 FACS to a common internal
 *              table format.
 *
 ******************************************************************************/

acpi_status
acpi_tb_build_common_facs (
	acpi_table_desc         *table_info)
{
	facs_descriptor_rev1    *FACS1;
	facs_descriptor_rev2    *FACS2;


	ACPI_FUNCTION_TRACE ("Tb_build_common_facs");


	/* Copy fields to the new FACS */

	if (acpi_gbl_RSDP->revision < 2) {
		/* ACPI 1.0 FACS */

		FACS1 = (facs_descriptor_rev1 *) acpi_gbl_FACS;

		acpi_gbl_common_fACS.global_lock = &(FACS1->global_lock);
		acpi_gbl_common_fACS.firmware_waking_vector = (u64 *) &FACS1->firmware_waking_vector;
		acpi_gbl_common_fACS.vector_width = 32;
	}
	else {
		/* ACPI 2.0 FACS */

		FACS2 = (facs_descriptor_rev2 *) acpi_gbl_FACS;

		acpi_gbl_common_fACS.global_lock = &(FACS2->global_lock);
		acpi_gbl_common_fACS.firmware_waking_vector = &FACS2->Xfirmware_waking_vector;
		acpi_gbl_common_fACS.vector_width = 64;
	}

	return_ACPI_STATUS  (AE_OK);
}


