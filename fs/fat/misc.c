/*
 *  linux/fs/fat/misc.c
 *
 *  Written 1992,1993 by Werner Almesberger
 */

#include <linux/fs.h>
#include <linux/msdos_fs.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>

#include "msbuffer.h"

#if 0
#  define PRINTK(x)	printk x
#else
#  define PRINTK(x)
#endif
#define Printk(x)	printk x

/* Well-known binary file extensions - of course there are many more */

static char ascii_extensions[] =
  "TXT" "ME " "HTM" "1ST" "LOG" "   " 	/* text files */
  "C  " "H  " "CPP" "LIS" "PAS" "FOR"  /* programming languages */
  "F  " "MAK" "INC" "BAS" 		/* programming languages */
  "BAT" "SH "				/* program code :) */
  "INI"					/* config files */
  "PBM" "PGM" "DXF"			/* graphics */
  "TEX";				/* TeX */


/*
 * fat_fs_panic reports a severe file system problem and sets the file system
 * read-only. The file system can be made writable again by remounting it.
 */

void fat_fs_panic(struct super_block *s,const char *msg)
{
	int not_ro;

	not_ro = !(s->s_flags & MS_RDONLY);
	if (not_ro) s->s_flags |= MS_RDONLY;
	printk("Filesystem panic (dev %s).\n  %s\n", kdevname(s->s_dev), msg);
	if (not_ro)
		printk("  File system has been set read-only\n");
}


/*
 * fat_is_binary selects optional text conversion based on the conversion mode
 * and the extension part of the file name.
 */

int fat_is_binary(char conversion,char *extension)
{
	char *walk;

	switch (conversion) {
		case 'b':
			return 1;
		case 't':
			return 0;
		case 'a':
			for (walk = ascii_extensions; *walk; walk += 3)
				if (!strncmp(extension,walk,3)) return 0;
			return 1;	/* default binary conversion */
		default:
			printk("Invalid conversion mode - defaulting to "
			    "binary.\n");
			return 1;
	}
}


/* File creation lock. This is system-wide to avoid deadlocks in rename. */
/* (rename might deadlock before detecting cross-FS moves.) */

static DECLARE_MUTEX(creation_lock);

void fat_lock_creation(void)
{
	down(&creation_lock);
}


void fat_unlock_creation(void)
{
	up(&creation_lock);
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
	int offset;
	struct buffer_head *bh;
	struct fat_boot_fsinfo *fsinfo;

	/* The fat32 boot fs info is at offset 0x3e0 by observation */
	offset = MSDOS_SB(sb)->fsinfo_offset;
	bh = fat_bread(sb, (offset >> SECTOR_BITS));
	if (bh == NULL) {
		printk("FAT bread failed in fat_clusters_flush\n");
		return;
	}
	fsinfo = (struct fat_boot_fsinfo *)
		&bh->b_data[offset & (SECTOR_SIZE-1)];

	/* Sanity check */
	if (CF_LE_L(fsinfo->signature) != 0x61417272) {
		printk("fat_clusters_flush: Did not find valid FSINFO signature. Found 0x%x.  offset=0x%x\n", CF_LE_L(fsinfo->signature), offset);
		fat_brelse(sb, bh);
		return;
	}
	fsinfo->free_clusters = CF_LE_L(MSDOS_SB(sb)->free_clusters);
	fat_mark_buffer_dirty(sb, bh);
	fat_brelse(sb, bh);
}

/*
 * fat_add_cluster tries to allocate a new cluster and adds it to the file
 * represented by inode. The cluster is zero-initialized.
 */

/* not a directory */

int fat_add_cluster(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	int count,nr,limit,last,curr,file_cluster;
	int res = -ENOSPC;
	int cluster_size = MSDOS_SB(sb)->cluster_size;

	if (!MSDOS_SB(sb)->free_clusters) return res;
	lock_fat(sb);
	limit = MSDOS_SB(sb)->clusters;
	nr = limit; /* to keep GCC happy */
	for (count = 0; count < limit; count++) {
		nr = ((count+MSDOS_SB(sb)->prev_free) % limit)+2;
		if (fat_access(sb,nr,-1) == 0) break;
	}
	MSDOS_SB(sb)->prev_free = (count+MSDOS_SB(sb)->prev_free+1) % limit;
	if (count >= limit) {
		MSDOS_SB(sb)->free_clusters = 0;
		unlock_fat(sb);
		return res;
	}
	fat_access(sb,nr,EOF_FAT(sb));
	if (MSDOS_SB(sb)->free_clusters != -1)
		MSDOS_SB(sb)->free_clusters--;
	if (MSDOS_SB(sb)->fat_bits == 32)
		fat_clusters_flush(sb);
	unlock_fat(sb);
	last = 0;
	/* We must locate the last cluster of the file to add this
	   new one (nr) to the end of the link list (the FAT).
	   
	   Here file_cluster will be the number of the last cluster of the
	   file (before we add nr).
	   
	   last is the corresponding cluster number on the disk. We will
	   use last to plug the nr cluster. We will use file_cluster to
	   update the cache.
	*/
	file_cluster = 0;
	if ((curr = MSDOS_I(inode)->i_start) != 0) {
		fat_cache_lookup(inode,INT_MAX,&last,&curr);
		file_cluster = last;
		while (curr && curr != -1){
			file_cluster++;
			if (!(curr = fat_access(sb, last = curr,-1))) {
				fat_fs_panic(sb,"File without EOF");
				return res;
			}
		}
	}
	if (last) fat_access(sb,last,nr);
	else {
		MSDOS_I(inode)->i_start = nr;
		MSDOS_I(inode)->i_logstart = nr;
		mark_inode_dirty(inode);
	}
	fat_cache_add(inode,file_cluster,nr);
	inode->i_blocks += cluster_size;
	return 0;
}

struct buffer_head *fat_extend_dir(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	int count,nr,limit,last,curr,sector,last_sector,file_cluster;
	struct buffer_head *bh, *res=NULL;
	int cluster_size = MSDOS_SB(sb)->cluster_size;

	if (MSDOS_SB(sb)->fat_bits != 32) {
		if (inode->i_ino == MSDOS_ROOT_INO) return res;
	}
	if (!MSDOS_SB(sb)->free_clusters) return res;
	lock_fat(sb);
	limit = MSDOS_SB(sb)->clusters;
	nr = limit; /* to keep GCC happy */
	for (count = 0; count < limit; count++) {
		nr = ((count+MSDOS_SB(sb)->prev_free) % limit)+2;
		if (fat_access(sb,nr,-1) == 0) break;
	}
	PRINTK (("cnt = %d --",count));
#ifdef DEBUG
printk("free cluster: %d\n",nr);
#endif
	MSDOS_SB(sb)->prev_free = (count+MSDOS_SB(sb)->prev_free+1) % limit;
	if (count >= limit) {
		MSDOS_SB(sb)->free_clusters = 0;
		unlock_fat(sb);
		return res;
	}
	fat_access(sb,nr,EOF_FAT(sb));
	if (MSDOS_SB(sb)->free_clusters != -1)
		MSDOS_SB(sb)->free_clusters--;
	if (MSDOS_SB(sb)->fat_bits == 32)
		fat_clusters_flush(sb);
	unlock_fat(sb);
#ifdef DEBUG
printk("set to %x\n",fat_access(sb,nr,-1));
#endif
	last = 0;
	/* We must locate the last cluster of the file to add this
	   new one (nr) to the end of the link list (the FAT).
	   
	   Here file_cluster will be the number of the last cluster of the
	   file (before we add nr).
	   
	   last is the corresponding cluster number on the disk. We will
	   use last to plug the nr cluster. We will use file_cluster to
	   update the cache.
	*/
	file_cluster = 0;
	if ((curr = MSDOS_I(inode)->i_start) != 0) {
		fat_cache_lookup(inode,INT_MAX,&last,&curr);
		file_cluster = last;
		while (curr && curr != -1){
			PRINTK (("."));
			file_cluster++;
			if (!(curr = fat_access(sb,
			    last = curr,-1))) {
				fat_fs_panic(sb,"File without EOF");
				return res;
			}
		}
		PRINTK ((" --  "));
	}
#ifdef DEBUG
printk("last = %d\n",last);
#endif
	if (last) fat_access(sb,last,nr);
	else {
		MSDOS_I(inode)->i_start = nr;
		MSDOS_I(inode)->i_logstart = nr;
		mark_inode_dirty(inode);
	}
#ifdef DEBUG
if (last) printk("next set to %d\n",fat_access(sb,last,-1));
#endif
	sector = MSDOS_SB(sb)->data_start+(nr-2)*cluster_size;
	last_sector = sector + cluster_size;
	if (MSDOS_SB(sb)->cvf_format &&
	    MSDOS_SB(sb)->cvf_format->zero_out_cluster)
		MSDOS_SB(sb)->cvf_format->zero_out_cluster(inode,nr);
	else
	for ( ; sector < last_sector; sector++) {
		#ifdef DEBUG
			printk("zeroing sector %d\n",sector);
		#endif
		if (!(bh = fat_getblk(sb, sector)))
			printk("getblk failed\n");
		else {
			memset(bh->b_data,0,SECTOR_SIZE);
			fat_set_uptodate(sb, bh, 1);
			fat_mark_buffer_dirty(sb, bh);
			if (!res)
				res=bh;
			else
				fat_brelse(sb, bh);
		}
	}
	if (file_cluster != inode->i_blocks/cluster_size){
		printk ("file_cluster badly computed!!! %d <> %ld\n"
			,file_cluster,inode->i_blocks/cluster_size);
	}else{
		fat_cache_add(inode,file_cluster,nr);
	}
	inode->i_blocks += cluster_size;
	if (inode->i_size & (SECTOR_SIZE-1)) {
		fat_fs_panic(sb,"Odd directory size");
		inode->i_size = (inode->i_size+SECTOR_SIZE) &
		    ~(SECTOR_SIZE-1);
	}
	inode->i_size += SECTOR_SIZE*cluster_size;
	MSDOS_I(inode)->mmu_private += SECTOR_SIZE*cluster_size;
	mark_inode_dirty(inode);
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

	month = ((date >> 5) & 15)-1;
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
	if (sys_tz.tz_dsttime) unix_date += 3600;

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
    struct msdos_dir_entry **de, int *ino)
{
	struct super_block *sb = dir->i_sb;
	int sector, offset;

	while (1) {
		offset = *pos;
		PRINTK (("get_entry offset %d\n",offset));
		if (*bh)
			fat_brelse(sb, *bh);
		*bh = NULL;
		if ((sector = fat_bmap(dir,offset >> SECTOR_BITS)) == -1)
			return -1;
		PRINTK (("get_entry sector %d %p\n",sector,*bh));
		PRINTK (("get_entry sector apres brelse\n"));
		if (!sector)
			return -1; /* beyond EOF */
		*pos += sizeof(struct msdos_dir_entry);
		if (!(*bh = fat_bread(sb, sector))) {
			printk("Directory sread (sector 0x%x) failed\n",sector);
			continue;
		}
		PRINTK (("get_entry apres sread\n"));
		*de = (struct msdos_dir_entry *) ((*bh)->b_data+(offset &
		    (SECTOR_SIZE-1)));
		*ino = (sector << MSDOS_DPS_BITS)+((offset & (SECTOR_SIZE-1)) >>
		    MSDOS_DIR_BITS);
		return 0;
	}
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
             (MSDOS_SB(sb)->fat_bits != 32) ? 0 : (CF_LE_W(data[entry].starthi) << 16) \
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

static int raw_scan_sector(struct super_block *sb,int sector,const char *name,
    int *number,int *ino,struct buffer_head **res_bh,
    struct msdos_dir_entry **res_de)
{
	struct buffer_head *bh;
	struct msdos_dir_entry *data;
	int entry,start,done;

	if (!(bh = fat_bread(sb,sector)))
		return -EIO;
	data = (struct msdos_dir_entry *) bh->b_data;
	for (entry = 0; entry < MSDOS_DPS; entry++) {
/* RSS_COUNT:  if (data[entry].name == name) done=true else done=false. */
		if (name) {
			RSS_NAME
		} else {
			if (!ino) RSS_COUNT
			else {
				if (number) RSS_START
				else RSS_FREE
			}
		}
		if (done) {
			if (ino) *ino = sector*MSDOS_DPS+entry;
			start = CF_LE_W(data[entry].start);
			if (MSDOS_SB(sb)->fat_bits == 32) {
				start |= (CF_LE_W(data[entry].starthi) << 16);
			}
			if (!res_bh)
				fat_brelse(sb, bh);
			else {
				*res_bh = bh;
				*res_de = &data[entry];
			}
			return start;
		}
	}
	fat_brelse(sb, bh);
	return -ENOENT;
}


/*
 * raw_scan_root performs raw_scan_sector on the root directory until the
 * requested entry is found or the end of the directory is reached.
 */

static int raw_scan_root(struct super_block *sb,const char *name,int *number,int *ino,
    struct buffer_head **res_bh,struct msdos_dir_entry **res_de)
{
	int count,cluster;

	for (count = 0; count < MSDOS_SB(sb)->dir_entries/MSDOS_DPS; count++) {
		if ((cluster = raw_scan_sector(sb,MSDOS_SB(sb)->dir_start+count,
		    name,number,ino,res_bh,res_de)) >= 0) return cluster;
	}
	return -ENOENT;
}


/*
 * raw_scan_nonroot performs raw_scan_sector on a non-root directory until the
 * requested entry is found or the end of the directory is reached.
 */

static int raw_scan_nonroot(struct super_block *sb,int start,const char *name,
    int *number,int *ino,struct buffer_head **res_bh,struct msdos_dir_entry
    **res_de)
{
	int count,cluster;

#ifdef DEBUG
	printk("raw_scan_nonroot: start=%d\n",start);
#endif
	do {
		for (count = 0; count < MSDOS_SB(sb)->cluster_size; count++) {
			if ((cluster = raw_scan_sector(sb,(start-2)*
			    MSDOS_SB(sb)->cluster_size+MSDOS_SB(sb)->data_start+
			    count,name,number,ino,res_bh,res_de)) >= 0)
				return cluster;
		}
		if (!(start = fat_access(sb,start,-1))) {
			fat_fs_panic(sb,"FAT error");
			break;
		}
#ifdef DEBUG
	printk("next start: %d\n",start);
#endif
	}
	while (start != -1);
	return -ENOENT;
}


/*
 * raw_scan performs raw_scan_sector on any sector.
 *
 * NOTE: raw_scan must not be used on a directory that is is the process of
 *       being created.
 */

static int raw_scan(struct super_block *sb, int start, const char *name,
    int *number, int *ino, struct buffer_head **res_bh,
    struct msdos_dir_entry **res_de)
{
	if (start) return raw_scan_nonroot
		(sb,start,name,number,ino,res_bh,res_de);
	else return raw_scan_root
		(sb,name,number,ino,res_bh,res_de);
}


/*
 * fat_parent_ino returns the inode number of the parent directory of dir.
 * File creation has to be deferred while fat_parent_ino is running to
 * prevent renames.
 *
 * AV. Bad, bad, bad... We need a mapping that would give us inode by
 * first cluster. Sheeeeit... OK, we can do it on fat_fill_inode() and
 * update on fat_add_cluster(). When will we remove it? fat_clear_inode()
 * and fat_truncate() to zero?
 */

int fat_parent_ino(struct inode *dir,int locked)
{
	static int zero = 0;
	int error,curr,prev,nr;

	PRINTK(("fat_parent_ino: Debug 0\n"));
	if (!S_ISDIR(dir->i_mode)) panic("Non-directory fed to m_p_i");
	if (dir->i_ino == MSDOS_ROOT_INO) return dir->i_ino;
	if (!locked) fat_lock_creation(); /* prevent renames */
	if ((curr = raw_scan(dir->i_sb,MSDOS_I(dir)->i_start,MSDOS_DOTDOT,
	    &zero,NULL,NULL,NULL)) < 0) {
		if (!locked) fat_unlock_creation();
		return curr;
	}
	PRINTK(("fat_parent_ino: Debug 1 curr=%d\n", curr));
	if (!curr) nr = MSDOS_ROOT_INO;
	else {
		PRINTK(("fat_parent_ino: Debug 2\n"));
		if ((prev = raw_scan(dir->i_sb,curr,MSDOS_DOTDOT,&zero,NULL,
		    NULL,NULL)) < 0) {
			PRINTK(("fat_parent_ino: Debug 3 prev=%d\n", prev));
			if (!locked) fat_unlock_creation();
			return prev;
		}
		PRINTK(("fat_parent_ino: Debug 4 prev=%d\n", prev));
		if (prev == 0 && MSDOS_SB(dir->i_sb)->fat_bits == 32) {
			prev = MSDOS_SB(dir->i_sb)->root_cluster;
		}
		if ((error = raw_scan(dir->i_sb,prev,NULL,&curr,&nr,NULL,
		    NULL)) < 0) {
			PRINTK(("fat_parent_ino: Debug 5 error=%d\n", error));
			if (!locked) fat_unlock_creation();
			return error;
		}
		PRINTK(("fat_parent_ino: Debug 6 nr=%d\n", nr));
	}
	if (!locked) fat_unlock_creation();
	return nr;
}


/*
 * fat_subdirs counts the number of sub-directories of dir. It can be run
 * on directories being created.
 */

int fat_subdirs(struct inode *dir)
{
	int count;

	count = 0;
	if ((dir->i_ino == MSDOS_ROOT_INO) &&
	    (MSDOS_SB(dir->i_sb)->fat_bits != 32)) {
		(void) raw_scan_root(dir->i_sb,NULL,&count,NULL,NULL,NULL);
	} else {
		if ((dir->i_ino != MSDOS_ROOT_INO) &&
		    !MSDOS_I(dir)->i_start) return 0; /* in mkdir */
		else (void) raw_scan_nonroot(dir->i_sb,MSDOS_I(dir)->i_start,
		    NULL,&count,NULL,NULL,NULL);
	}
	return count;
}


/*
 * Scans a directory for a given file (name points to its formatted name) or
 * for an empty directory slot (name is NULL). Returns an error code or zero.
 */

int fat_scan(struct inode *dir,const char *name,struct buffer_head **res_bh,
    struct msdos_dir_entry **res_de,int *ino)
{
	int res;

	res = raw_scan(dir->i_sb,MSDOS_I(dir)->i_start,
		       name, NULL, ino, res_bh, res_de);
	return res<0 ? res : 0;
}
