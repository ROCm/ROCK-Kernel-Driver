/*
 *   (C) Copyright IBM Corp. 2002, 2004
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * linux/drivers/md/dm-bbr.h
 *
 * Bad-block-relocation (BBR) target for device-mapper.
 *
 * The BBR target is designed to remap I/O write failures to another safe
 * location on disk. Note that most disk drives have BBR built into them,
 * this means that our software BBR will be only activated when all hardware
 * BBR replacement sectors have been used.
 */

#define BBR_TABLE_SIGNATURE		0x42627254 /* BbrT */
#define BBR_ENTRIES_PER_SECT		31
#define INITIAL_CRC			0xFFFFFFFF
#define CRC_POLYNOMIAL			0xEDB88320L

/**
 * Macros to cleanly print 64-bit numbers on both 32-bit and 64-bit machines.
 * Use these in place of %Ld, %Lu, and %Lx.
 **/
#if BITS_PER_LONG > 32
#define PFU64 "%lu"
#else
#define PFU64 "%Lu"
#endif

/**
 * struct bbr_table_entry
 * @bad_sect:		LBA of bad location.
 * @replacement_sect:	LBA of new location.
 *
 * Structure to describe one BBR remap.
 **/
struct bbr_table_entry {
	u64 bad_sect;
	u64 replacement_sect;
};

/**
 * struct bbr_table
 * @signature:		Signature on each BBR table sector.
 * @crc:		CRC for this table sector.
 * @sequence_number:	Used to resolve conflicts when primary and secondary
 *			tables do not match.
 * @in_use_cnt:		Number of in-use table entries.
 * @entries:		Actual table of remaps.
 *
 * Structure to describe each sector of the metadata table. Each sector in this
 * table can describe 31 remapped sectors.
 **/
struct bbr_table {
	u32			signature;
	u32			crc;
	u32			sequence_number;
	u32			in_use_cnt;
	struct bbr_table_entry	entries[BBR_ENTRIES_PER_SECT];
};

/**
 * struct bbr_runtime_remap
 *
 * Node in the binary tree used to keep track of remaps.
 **/
struct bbr_runtime_remap {
	struct bbr_table_entry		remap;
	struct bbr_runtime_remap	*left;
	struct bbr_runtime_remap	*right;
};

/**
 * struct bbr_private
 * @dev:			Info about underlying device.
 * @bbr_table:			Copy of metadata table.
 * @remap_root:			Binary tree containing all remaps.
 * @remap_root_lock:		Lock for the binary tree.
 * @remap_work:			For adding work items to the work-queue.
 * @remap_ios:			List of I/Os for the work-queue to handle.
 * @remap_ios_lock:		Lock for the remap_ios list.
 * @offset:			LBA of data area.
 * @lba_table1:			LBA of primary BBR table.
 * @lba_table2:			LBA of secondary BBR table.
 * @nr_sects_bbr_table:		Size of each BBR table.
 * @nr_replacement_blks:	Number of replacement blocks.
 * @start_replacement_sect:	LBA of start of replacement blocks.
 * @blksize_in_sects:		Size of each block.
 * @in_use_replacement_blks:	Current number of remapped blocks.
 *
 * Private data for each BBR target.
 **/
struct bbr_private {
	struct dm_dev			*dev;
	struct bbr_table		*bbr_table;
	struct bbr_runtime_remap	*remap_root;
	spinlock_t			remap_root_lock;

	struct work_struct		remap_work;
	struct bio_list			remap_ios;
	spinlock_t			remap_ios_lock;

	u64				offset;
	u64				lba_table1;
	u64				lba_table2;
	u64				nr_sects_bbr_table;
	u64				start_replacement_sect;
	u64				nr_replacement_blks;
	u32				blksize_in_sects;
	atomic_t			in_use_replacement_blks;
};

