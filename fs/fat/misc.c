/*
 *  linux/fs/fat/misc.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *  22/11/2000 - Fixed fat_date_unix2dos for dates earlier than 01/01/1980
 *		 and date_dos2unix for date==0 by Igor Zhbanov(bsg@uniyar.ac.ru)
 */

#include <linux/fs.h>
#include <linux/msdos_fs.h>
#include <linux/buffer_head.h>

/*
 * fat_fs_panic reports a severe file system problem and sets the file system
 * read-only. The file system can be made writable again by remounting it.
 */

static char panic_msg[512];

void fat_fs_panic(struct super_block *s, const char *fmt, ...)
{
	int not_ro;
	va_list args;

	va_start (args, fmt);
	vsnprintf (panic_msg, sizeof(panic_msg), fmt, args);
	va_end (args);

	not_ro = !(s->s_flags & MS_RDONLY);
	if (not_ro)
		s->s_flags |= MS_RDONLY;

	printk(KERN_ERR "FAT: Filesystem panic (dev %s)\n"
	       "    %s\n", s->s_id, panic_msg);
	if (not_ro)
		printk(KERN_ERR "    File system has been set read-only\n");
}

void lock_fat(struct super_block *sb)
{
	down(&(MSDOS_SB(sb)->fat_lock));
}

void unlock_fat(struct super_block *sb)
{
	up(&(MSDOS_SB(sb)->fat_lock));
}

/* Flushes the number of free clusters on FAT32 */
/* XXX: Need to write one per FSINFO block.  Currently only writes 1 */
void fat_clusters_flush(struct super_block *sb)
{
	struct buffer_head *bh;
	struct fat_boot_fsinfo *fsinfo;

	if (MSDOS_SB(sb)->fat_bits != 32)
		return;
	if (MSDOS_SB(sb)->free_clusters == -1)
		return;

	bh = sb_bread(sb, MSDOS_SB(sb)->fsinfo_sector);
	if (bh == NULL) {
		printk(KERN_ERR "FAT bread failed in fat_clusters_flush\n");
		return;
	}

	fsinfo = (struct fat_boot_fsinfo *)bh->b_data;
	/* Sanity check */
	if (!IS_FSINFO(fsinfo)) {
		printk(KERN_ERR "FAT: Did not find valid FSINFO signature.\n"
		       "     Found signature1 0x%08x signature2 0x%08x"
		       " (sector = %lu)\n",
		       CF_LE_L(fsinfo->signature1), CF_LE_L(fsinfo->signature2),
		       MSDOS_SB(sb)->fsinfo_sector);
	} else {
		fsinfo->free_clusters = CF_LE_L(MSDOS_SB(sb)->free_clusters);
		fsinfo->next_cluster = CF_LE_L(MSDOS_SB(sb)->prev_free);
		mark_buffer_dirty(bh);
	}
	brelse(bh);
}

/*
 * fat_add_cluster tries to allocate a new cluster and adds it to the
 * file represented by inode.
 */
int fat_add_cluster(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	int count, nr, limit, last, curr, file_cluster;
	int cluster_bits = MSDOS_SB(sb)->cluster_bits;
	
	/* 
	 * We must locate the last cluster of the file to add this new
	 * one (nr) to the end of the link list (the FAT).
	 *
	 * In order to confirm that the cluster chain is valid, we
	 * find out EOF first.
	 */
	nr = -EIO;
	last = file_cluster = 0;
	if ((curr = MSDOS_I(inode)->i_start) != 0) {
		int max_cluster = MSDOS_I(inode)->mmu_private >> cluster_bits;

		fat_cache_lookup(inode, INT_MAX, &last, &curr);
		file_cluster = last;
		while (curr && curr != FAT_ENT_EOF) {
			file_cluster++;
			curr = fat_access(sb, last = curr, -1);
			if (curr < 0)
				return curr;
			else if (curr == FAT_ENT_FREE) {
				fat_fs_panic(sb, "%s: invalid cluster chain"
					     " (ino %lu)",
					     __FUNCTION__, inode->i_ino);
				goto out;
			}
			if (file_cluster > max_cluster) {
				fat_fs_panic(sb, "%s: bad cluster counts"
					     " (ino %lu)",
					     __FUNCTION__, inode->i_ino);
				goto out;
			}
		}
	}

	/* find free FAT entry */
	lock_fat(sb);
	
	if (MSDOS_SB(sb)->free_clusters == 0) {
		unlock_fat(sb);
		return -ENOSPC;
	}

	limit = MSDOS_SB(sb)->clusters + 2;
	nr = MSDOS_SB(sb)->prev_free + 1;
	for (count = 0; count < MSDOS_SB(sb)->clusters; count++, nr++) {
		nr = nr % limit;
		if (nr < 2)
			nr = 2;
		if (fat_access(sb, nr, -1) == FAT_ENT_FREE)
			break;
	}
	if (count >= MSDOS_SB(sb)->clusters) {
		MSDOS_SB(sb)->free_clusters = 0;
		unlock_fat(sb);
		return -ENOSPC;
	}
	MSDOS_SB(sb)->prev_free = nr;

	fat_access(sb, nr, FAT_ENT_EOF);
	if (MSDOS_SB(sb)->free_clusters != -1)
		MSDOS_SB(sb)->free_clusters--;
	fat_clusters_flush(sb);
	
	unlock_fat(sb);

	/* add new one to the last of the cluster chain */
	if (last) {
		fat_access(sb, last, nr);
		fat_cache_add(inode, file_cluster, nr);
	} else {
		MSDOS_I(inode)->i_start = nr;
		MSDOS_I(inode)->i_logstart = nr;
		mark_inode_dirty(inode);
	}
	if (file_cluster != (inode->i_blocks >> (cluster_bits - 9))) {
		printk (KERN_ERR "file_cluster badly computed!!! %d <> %ld\n",
			file_cluster, inode->i_blocks >> (cluster_bits - 9));
		fat_cache_inval_inode(inode);
	}
	inode->i_blocks += (1 << cluster_bits) >> 9;

out:
	return nr;
}

struct buffer_head *fat_extend_dir(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct buffer_head *bh, *res = NULL;
	int nr, cluster_size = MSDOS_SB(sb)->cluster_size;
	sector_t sector, last_sector;

	if (MSDOS_SB(sb)->fat_bits != 32) {
		if (inode->i_ino == MSDOS_ROOT_INO)
			return ERR_PTR(-ENOSPC);
	}

	nr = fat_add_cluster(inode);
	if (nr < 0)
		return ERR_PTR(nr);
	
	sector = ((sector_t)nr - 2) * cluster_size + MSDOS_SB(sb)->data_start;
	last_sector = sector + cluster_size;
	for ( ; sector < last_sector; sector++) {
		if ((bh = sb_getblk(sb, sector))) {
			memset(bh->b_data, 0, sb->s_blocksize);
			set_buffer_uptodate(bh);
			mark_buffer_dirty(bh);
			if (!res)
				res = bh;
			else
				brelse(bh);
		}
	}
	if (res == NULL)
		res = ERR_PTR(-EIO);
	if (inode->i_size & (sb->s_blocksize - 1)) {
		fat_fs_panic(sb, "Odd directory size");
		inode->i_size = (inode->i_size + sb->s_blocksize)
			& ~(sb->s_blocksize - 1);
	}
	inode->i_size += 1 << MSDOS_SB(sb)->cluster_bits;
	MSDOS_I(inode)->mmu_private += 1 << MSDOS_SB(sb)->cluster_bits;

	return res;
}

/* Linear day numbers of the respective 1sts in non-leap years. */

static int day_n[] = { 0,31,59,90,120,151,181,212,243,273,304,334,0,0,0,0 };
		  /* JanFebMarApr May Jun Jul Aug Sep Oct Nov Dec */


extern struct timezone sys_tz;


/* Convert a MS-DOS time/date pair to a UNIX date (seconds since 1 1 70). */

int date_dos2unix(unsigned short time,unsigned short date)
{
	int month,year,secs;

	/* first subtract and mask after that... Otherwise, if
	   date == 0, bad things happen */
	month = ((date >> 5) - 1) & 15;
	year = date >> 9;
	secs = (time & 31)*2+60*((time >> 5) & 63)+(time >> 11)*3600+86400*
	    ((date & 31)-1+day_n[month]+(year/4)+year*365-((year & 3) == 0 &&
	    month < 2 ? 1 : 0)+3653);
			/* days since 1.1.70 plus 80's leap day */
	secs += sys_tz.tz_minuteswest*60;
	return secs;
}


/* Convert linear UNIX date to a MS-DOS time/date pair. */

void fat_date_unix2dos(int unix_date,unsigned short *time,
    unsigned short *date)
{
	int day,year,nl_day,month;

	unix_date -= sys_tz.tz_minuteswest*60;

	/* Jan 1 GMT 00:00:00 1980. But what about another time zone? */
	if (unix_date < 315532800)
		unix_date = 315532800;

	*time = (unix_date % 60)/2+(((unix_date/60) % 60) << 5)+
	    (((unix_date/3600) % 24) << 11);
	day = unix_date/86400-3652;
	year = day/365;
	if ((year+3)/4+365*year > day) year--;
	day -= (year+3)/4+365*year;
	if (day == 59 && !(year & 3)) {
		nl_day = day;
		month = 2;
	}
	else {
		nl_day = (year & 3) || day <= 59 ? day : day-1;
		for (month = 0; month < 12; month++)
			if (day_n[month] > nl_day) break;
	}
	*date = nl_day-day_n[month-1]+1+(month << 5)+(year << 9);
}


/* Returns the inode number of the directory entry at offset pos. If bh is
   non-NULL, it is brelse'd before. Pos is incremented. The buffer header is
   returned in bh.
   AV. Most often we do it item-by-item. Makes sense to optimize.
   AV. OK, there we go: if both bh and de are non-NULL we assume that we just
   AV. want the next entry (took one explicit de=NULL in vfat/namei.c).
   AV. It's done in fat_get_entry() (inlined), here the slow case lives.
   AV. Additionally, when we return -1 (i.e. reached the end of directory)
   AV. we make bh NULL. 
 */

int fat__get_entry(struct inode *dir, loff_t *pos,struct buffer_head **bh,
		   struct msdos_dir_entry **de, loff_t *i_pos)
{
	struct super_block *sb = dir->i_sb;
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	sector_t phys, iblock;
	loff_t offset;
	int err;

next:
	offset = *pos;
	if (*bh)
		brelse(*bh);

	*bh = NULL;
	iblock = *pos >> sb->s_blocksize_bits;
	err = fat_bmap(dir, iblock, &phys);
	if (err || !phys)
		return -1;	/* beyond EOF or error */

	*bh = sb_bread(sb, phys);
	if (*bh == NULL) {
		printk(KERN_ERR "FAT: Directory bread(block %llu) failed\n",
		       (unsigned long long)phys);
		/* skip this block */
		*pos = (iblock + 1) << sb->s_blocksize_bits;
		goto next;
	}

	offset &= sb->s_blocksize - 1;
	*pos += sizeof(struct msdos_dir_entry);
	*de = (struct msdos_dir_entry *)((*bh)->b_data + offset);
	*i_pos = ((loff_t)phys << sbi->dir_per_block_bits) + (offset >> MSDOS_DIR_BITS);

	return 0;
}


/*
 * Now an ugly part: this set of directory scan routines works on clusters
 * rather than on inodes and sectors. They are necessary to locate the '..'
 * directory "inode". raw_scan_sector operates in four modes:
 *
 * name     number   ino      action
 * -------- -------- -------- -------------------------------------------------
 * non-NULL -        X        Find an entry with that name
 * NULL     non-NULL non-NULL Find an entry whose data starts at *number
 * NULL     non-NULL NULL     Count subdirectories in *number. (*)
 * NULL     NULL     non-NULL Find an empty entry
 *
 * (*) The return code should be ignored. It DOES NOT indicate success or
 *     failure. *number has to be initialized to zero.
 *
 * - = not used, X = a value is returned unless NULL
 *
 * If res_bh is non-NULL, the buffer is not deallocated but returned to the
 * caller on success. res_de is set accordingly.
 *
 * If cont is non-zero, raw_found continues with the entry after the one
 * res_bh/res_de point to.
 */


#define RSS_NAME /* search for name */ \
    done = !strncmp(data[entry].name,name,MSDOS_NAME) && \
     !(data[entry].attr & ATTR_VOLUME);

#define RSS_START /* search for start cluster */ \
    done = !IS_FREE(data[entry].name) \
      && ( \
           ( \
             (sbi->fat_bits != 32) ? 0 : (CF_LE_W(data[entry].starthi) << 16) \
           ) \
           | CF_LE_W(data[entry].start) \
         ) == *number;

#define RSS_FREE /* search for free entry */ \
    { \
	done = IS_FREE(data[entry].name); \
    }

#define RSS_COUNT /* count subdirectories */ \
    { \
	done = 0; \
	if (!IS_FREE(data[entry].name) && (data[entry].attr & ATTR_DIR)) \
	    (*number)++; \
    }

static int raw_scan_sector(struct super_block *sb, sector_t sector,
			   const char *name, int *number, loff_t *i_pos,
			   struct buffer_head **res_bh,
			   struct msdos_dir_entry **res_de)
{
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	struct buffer_head *bh;
	struct msdos_dir_entry *data;
	int entry,start,done;

	if (!(bh = sb_bread(sb, sector)))
		return -EIO;
	data = (struct msdos_dir_entry *) bh->b_data;
	for (entry = 0; entry < sbi->dir_per_block; entry++) {
/* RSS_COUNT:  if (data[entry].name == name) done=true else done=false. */
		if (name) {
			RSS_NAME
		} else {
			if (!i_pos) RSS_COUNT
			else {
				if (number) RSS_START
				else RSS_FREE
			}
		}
		if (done) {
			if (i_pos) {
				*i_pos = ((loff_t)sector << sbi->dir_per_block_bits) + entry;
			}
			start = CF_LE_W(data[entry].start);
			if (sbi->fat_bits == 32)
				start |= (CF_LE_W(data[entry].starthi) << 16);

			if (!res_bh)
				brelse(bh);
			else {
				*res_bh = bh;
				*res_de = &data[entry];
			}
			return start;
		}
	}
	brelse(bh);
	return -ENOENT;
}


/*
 * raw_scan_root performs raw_scan_sector on the root directory until the
 * requested entry is found or the end of the directory is reached.
 */

static int raw_scan_root(struct super_block *sb, const char *name,
			 int *number, loff_t *i_pos,
			 struct buffer_head **res_bh,
			 struct msdos_dir_entry **res_de)
{
	int count,cluster;

	for (count = 0;
	     count < MSDOS_SB(sb)->dir_entries / MSDOS_SB(sb)->dir_per_block;
	     count++) {
		cluster = raw_scan_sector(sb, MSDOS_SB(sb)->dir_start + count,
					  name, number, i_pos, res_bh, res_de);
		if (cluster >= 0)
			return cluster;
	}
	return -ENOENT;
}


/*
 * raw_scan_nonroot performs raw_scan_sector on a non-root directory until the
 * requested entry is found or the end of the directory is reached.
 */

static int raw_scan_nonroot(struct super_block *sb, int start, const char *name,
			    int *number, loff_t *i_pos,
			    struct buffer_head **res_bh,
			    struct msdos_dir_entry **res_de)
{
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	int count, cluster;
	unsigned long dir_size = 0;
	sector_t sector;

#ifdef DEBUG
	printk("raw_scan_nonroot: start=%d\n",start);
#endif
	do {
		for (count = 0; count < sbi->cluster_size; count++) {
			sector = ((sector_t)start - 2) * sbi->cluster_size
				+ count + sbi->data_start;
			cluster = raw_scan_sector(sb, sector, name, number,
						  i_pos, res_bh, res_de);
			if (cluster >= 0)
				return cluster;
		}
		dir_size += 1 << sbi->cluster_bits;
		if (dir_size > FAT_MAX_DIR_SIZE) {
			fat_fs_panic(sb, "Directory %d: "
				     "exceeded the maximum size of directory",
				     start);
			break;
		}
		start = fat_access(sb, start, -1);
		if (start < 0)
			return start;
		else if (start == FAT_ENT_FREE) {
			fat_fs_panic(sb, "%s: invalid cluster chain",
				     __FUNCTION__);
			break;
		}
#ifdef DEBUG
	printk("next start: %d\n",start);
#endif
	} while (start != FAT_ENT_EOF);

	return -ENOENT;
}


/*
 * raw_scan performs raw_scan_sector on any sector.
 *
 * NOTE: raw_scan must not be used on a directory that is is the process of
 *       being created.
 */

static int raw_scan(struct super_block *sb, int start, const char *name,
		    loff_t *i_pos, struct buffer_head **res_bh,
		    struct msdos_dir_entry **res_de)
{
	if (start)
		return raw_scan_nonroot(sb,start,name,NULL,i_pos,res_bh,res_de);
	else
		return raw_scan_root(sb,name,NULL,i_pos,res_bh,res_de);
}

/*
 * fat_subdirs counts the number of sub-directories of dir. It can be run
 * on directories being created.
 */
int fat_subdirs(struct inode *dir)
{
	struct msdos_sb_info *sbi = MSDOS_SB(dir->i_sb);
	int number;

	number = 0;
	if ((dir->i_ino == MSDOS_ROOT_INO) && (sbi->fat_bits != 32))
		raw_scan_root(dir->i_sb, NULL, &number, NULL, NULL, NULL);
	else {
		if ((dir->i_ino != MSDOS_ROOT_INO) && !MSDOS_I(dir)->i_start)
			return 0; /* in mkdir */
		else {
			raw_scan_nonroot(dir->i_sb, MSDOS_I(dir)->i_start,
					 NULL, &number, NULL, NULL, NULL);
		}
	}
	return number;
}


/*
 * Scans a directory for a given file (name points to its formatted name) or
 * for an empty directory slot (name is NULL). Returns an error code or zero.
 */

int fat_scan(struct inode *dir, const char *name, struct buffer_head **res_bh,
	     struct msdos_dir_entry **res_de, loff_t *i_pos)
{
	int res;

	res = raw_scan(dir->i_sb, MSDOS_I(dir)->i_start, name, i_pos,
		       res_bh, res_de);
	return (res < 0) ? res : 0;
}
