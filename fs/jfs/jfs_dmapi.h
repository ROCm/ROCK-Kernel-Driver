/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.	 Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */
#ifndef __JFS_DMAPI_H__
#define __JFS_DMAPI_H__

/*	Values used to define the on-disk version of dm_attrname_t. All
 *	on-disk attribute names start with the 8-byte string "app.dmi.".
 *
 *	In the on-disk inode, DMAPI attribute names consist of the user-provided
 *	name with the DMATTR_PREFIXSTRING pre-pended.  This string must NEVER be
 *	changed.
 */


#define DM_EVENT_ENABLED(inode, event) ( \
	unlikely (JFS_SBI(inode->i_sb)->flag & JFS_DMI) && \
		 ((JFS_IP(inode)->dmattrs.da_dmevmask & (1 << event)) || \
		  (JFS_SBI(inode->i_sb)->dm_evmask & (1 << event))))


/*
 *	Definitions used for the flags field on dm_send_*_event().
 */

#define DM_FLAGS_NDELAY		0x001	/* return EAGAIN after dm_pending() */
#define DM_FLAGS_UNWANTED	0x002	/* event not in fsys dm_eventset_t */

/*
 *	Macro to turn caller specified delay/block flags into
 *	dm_send_xxxx_event flag DM_FLAGS_NDELAY.
 */

#define FILP_DELAY_FLAG(filp) ((filp->f_flags&(O_NDELAY|O_NONBLOCK)) ? \
			DM_FLAGS_NDELAY : 0)


extern int dmapi_init(void);
extern void dmapi_uninit(void);

int	jfs_dm_send_data_event(int, struct inode *,
			dm_off_t, size_t, int, int /*vrwlock_t*/ *);
int	jfs_dm_send_mmap_event(struct vm_area_struct *, unsigned int);
int	dm_send_destroy_event(struct inode *, dm_right_t);
int	dm_send_namesp_event(dm_eventtype_t, struct inode *,
			dm_right_t, struct inode *, dm_right_t,
			char *, char *, mode_t, int, int);
void	dm_send_unmount_event(struct super_block *, struct inode *,
			dm_right_t, mode_t, int, int);

#define JFS_SEND_DATA(ev,ip,off,len,fl,lock) \
	jfs_dm_send_data_event(ev,ip,off,len,fl,lock)
#define JFS_SEND_MMAP(vma,fl) \
	jfs_dm_send_mmap_event(vma,fl)
#define JFS_SEND_DESTROY(ip,right) \
	dm_send_destroy_event(ip,right)
#define JFS_SEND_NAMESP(ev,i1,r1,i2,r2,n1,n2,mode,rval,fl) \
	dm_send_namesp_event(ev,i1,r1,i2,r2,n1,n2,mode,rval,fl)
#define JFS_SEND_UNMOUNT(sbp,ip,right,mode,rval,fl) \
	dm_send_unmount_event(sbp,ip,right,mode,rval,fl)

void	jfs_dm_umount_begin(struct super_block *);	
int	jfs_dm_read_pers_data(struct jfs_inode_info *jfs_ip);

#define DMATTR_PERS_REGIONS	"system.dmi.persistent.regions"
#endif	/* __JFS_DMAPI_H__ */
