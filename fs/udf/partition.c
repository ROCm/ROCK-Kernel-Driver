/*
 * partition.c
 *
 * PURPOSE
 *      Partition handling routines for the OSTA-UDF(tm) filesystem.
 *
 * CONTACTS
 *      E-mail regarding any portion of the Linux UDF file system should be
 *      directed to the development team mailing list (run by majordomo):
 *              linux_udf@hootie.lvld.hp.com
 *
 * COPYRIGHT
 *      This file is distributed under the terms of the GNU General Public
 *      License (GPL). Copies of the GPL can be obtained from:
 *              ftp://prep.ai.mit.edu/pub/gnu/GPL
 *      Each contributing author retains all rights to their own work.
 *
 *  (C) 1998-2000 Ben Fennema
 *
 * HISTORY
 *
 * 12/06/98 blf  Created file. 
 *
 */

#include "udfdecl.h"
#include "udf_sb.h"
#include "udf_i.h"

#include <linux/fs.h>
#include <linux/string.h>
#include <linux/udf_fs.h>
#include <linux/malloc.h>

inline Uint32 udf_get_pblock(struct super_block *sb, Uint32 block, Uint16 partition, Uint32 offset)
{
	if (partition >= UDF_SB_NUMPARTS(sb))
	{
		udf_debug("block=%d, partition=%d, offset=%d: invalid partition\n",
			block, partition, offset);
		return 0xFFFFFFFF;
	}
	if (UDF_SB_PARTFUNC(sb, partition))
		return UDF_SB_PARTFUNC(sb, partition)(sb, block, partition, offset);
	else
		return UDF_SB_PARTROOT(sb, partition) + block + offset;
}

Uint32 udf_get_pblock_virt15(struct super_block *sb, Uint32 block, Uint16 partition, Uint32 offset)
{
	struct buffer_head *bh = NULL;
	Uint32 newblock;
	Uint32 index;
	Uint32 loc;

	index = (sb->s_blocksize - UDF_SB_TYPEVIRT(sb,partition).s_start_offset) / sizeof(Uint32);

	if (block > UDF_SB_TYPEVIRT(sb,partition).s_num_entries)
	{
		udf_debug("Trying to access block beyond end of VAT (%d max %d)\n",
			block, UDF_SB_TYPEVIRT(sb,partition).s_num_entries);
		return 0xFFFFFFFF;
	}

	if (block >= index)
	{
		block -= index;
		newblock = 1 + (block / (sb->s_blocksize / sizeof(Uint32)));
		index = block % (sb->s_blocksize / sizeof(Uint32));
	}
	else
	{
		newblock = 0;
		index = UDF_SB_TYPEVIRT(sb,partition).s_start_offset / sizeof(Uint32) + block;
	}

	loc = udf_locked_block_map(UDF_SB_VAT(sb), newblock);

	if (!(bh = bread(sb->s_dev, loc, sb->s_blocksize)))
	{
		udf_debug("get_pblock(UDF_VIRTUAL_MAP:%p,%d,%d) VAT: %d[%d]\n",
			sb, block, partition, loc, index);
		return 0xFFFFFFFF;
	}

	loc = le32_to_cpu(((Uint32 *)bh->b_data)[index]);

	udf_release_data(bh);

	if (UDF_I_LOCATION(UDF_SB_VAT(sb)).partitionReferenceNum == partition)
	{
		udf_debug("recursive call to udf_get_pblock!\n");
		return 0xFFFFFFFF;
	}

	return udf_get_pblock(sb, loc, UDF_I_LOCATION(UDF_SB_VAT(sb)).partitionReferenceNum, offset);
}

inline Uint32 udf_get_pblock_virt20(struct super_block *sb, Uint32 block, Uint16 partition, Uint32 offset)
{
	return udf_get_pblock_virt15(sb, block, partition, offset);
}

Uint32 udf_get_pblock_spar15(struct super_block *sb, Uint32 block, Uint16 partition, Uint32 offset)
{
	Uint32 packet = (block + offset) >> UDF_SB_TYPESPAR(sb,partition).s_spar_pshift;
	Uint32 index = 0;

	if (UDF_SB_TYPESPAR(sb,partition).s_spar_indexsize == 8)
		index = UDF_SB_TYPESPAR(sb,partition).s_spar_remap.s_spar_remap8[packet];
	else if (UDF_SB_TYPESPAR(sb,partition).s_spar_indexsize == 16)
		index = UDF_SB_TYPESPAR(sb,partition).s_spar_remap.s_spar_remap16[packet];
	else if (UDF_SB_TYPESPAR(sb,partition).s_spar_indexsize == 32)
		index = UDF_SB_TYPESPAR(sb,partition).s_spar_remap.s_spar_remap32[packet];

	if (index == ((1 << UDF_SB_TYPESPAR(sb,partition).s_spar_indexsize)-1))
		return UDF_SB_PARTROOT(sb,partition) + block + offset;

	packet = UDF_SB_TYPESPAR(sb,partition).s_spar_map[index];
	return packet + ((block + offset) & ((1 << UDF_SB_TYPESPAR(sb,partition).s_spar_pshift)-1));
}

void udf_fill_spartable(struct super_block *sb, struct udf_sparing_data *sdata, int partlen)
{
	Uint16 ident;
	Uint32 spartable;
	int i;
	struct buffer_head *bh;
	struct SparingTable *st;

	for (i=0; i<4; i++)
	{
		if (!(spartable = sdata->s_spar_loc[i]))
			continue;

		bh = udf_read_tagged(sb, spartable, spartable, &ident);

		if (!bh)
		{
			sdata->s_spar_loc[i] = 0;
			continue;
		}

		if (ident == 0)
		{
			st = (struct SparingTable *)bh->b_data;
			if (!strncmp(st->sparingIdent.ident, UDF_ID_SPARING, strlen(UDF_ID_SPARING)))
			{
				SparingEntry *se;
				Uint16 rtl = le16_to_cpu(st->reallocationTableLen);
				int index;

				if (!sdata->s_spar_map)
				{
					int num = 1, mapsize;
					sdata->s_spar_indexsize = 8;
					while (rtl*sizeof(Uint32) >= (1 << sdata->s_spar_indexsize))
					{
						num ++;
						sdata->s_spar_indexsize <<= 1;
					}
					mapsize = (rtl * sizeof(Uint32)) +
						((partlen/(1 << sdata->s_spar_pshift)) * sizeof(Uint8) * num);
					sdata->s_spar_map = kmalloc(mapsize, GFP_KERNEL);
					sdata->s_spar_remap.s_spar_remap32 = &sdata->s_spar_map[rtl];
					memset(sdata->s_spar_map, 0xFF, mapsize);
				}

				index = sizeof(struct SparingTable);
				for (i=0; i<rtl; i++)
				{
					if (index > sb->s_blocksize)
					{
						udf_release_data(bh);
						bh = udf_tread(sb, ++spartable, sb->s_blocksize);
						if (!bh)
						{
							sdata->s_spar_loc[i] = 0;
							continue;
						}
						index = 0;
					}
					se = (SparingEntry *)&(bh->b_data[index]);
					index += sizeof(SparingEntry);

					if (sdata->s_spar_map[i] == 0xFFFFFFFF)
						sdata->s_spar_map[i] = le32_to_cpu(se->mappedLocation);
					else if (sdata->s_spar_map[i] != le32_to_cpu(se->mappedLocation))
					{
						udf_debug("Found conflicting Sparing Data (%d vs %d for entry %d)\n",
							sdata->s_spar_map[i], le32_to_cpu(se->mappedLocation), i);
					}

					if (le32_to_cpu(se->origLocation) < 0xFFFFFFF0)
					{
						int packet = le32_to_cpu(se->origLocation) >> sdata->s_spar_pshift;
						if (sdata->s_spar_indexsize == 8)
						{
							if (sdata->s_spar_remap.s_spar_remap8[packet] == 0xFF)
								sdata->s_spar_remap.s_spar_remap8[packet] = i;
							else if (sdata->s_spar_remap.s_spar_remap8[packet] != i)
							{
								udf_debug("Found conflicting Sparing Data (%d vs %d)\n",
									sdata->s_spar_remap.s_spar_remap8[packet], i);
							}
						}
						else if (sdata->s_spar_indexsize == 16)
						{
							if (sdata->s_spar_remap.s_spar_remap16[packet] == 0xFFFF)
								sdata->s_spar_remap.s_spar_remap16[packet] = i;
							else if (sdata->s_spar_remap.s_spar_remap16[packet] != i)
							{
								udf_debug("Found conflicting Sparing Data (%d vs %d)\n",
									sdata->s_spar_remap.s_spar_remap16[packet], i);
							}
						}
						else if (sdata->s_spar_indexsize == 32)
						{
							if (sdata->s_spar_remap.s_spar_remap32[packet] == 0xFFFFFFFF)
								sdata->s_spar_remap.s_spar_remap32[packet] = i;
							else if (sdata->s_spar_remap.s_spar_remap32[packet] != i)
							{
								udf_debug("Found conflicting Sparing Data (%d vs %d)\n",
									sdata->s_spar_remap.s_spar_remap32[packet], i);
							}
						}
					}
				}
			}
		}
		udf_release_data(bh);
	}
}
