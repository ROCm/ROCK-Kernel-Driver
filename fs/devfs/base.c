/*  devfs (Device FileSystem) driver.

    Copyright (C) 1998-2000  Richard Gooch

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    Richard Gooch may be reached by email at  rgooch@atnf.csiro.au
    The postal address is:
      Richard Gooch, c/o ATNF, P. O. Box 76, Epping, N.S.W., 2121, Australia.

    ChangeLog

    19980110   Richard Gooch <rgooch@atnf.csiro.au>
               Original version.
  v0.1
    19980111   Richard Gooch <rgooch@atnf.csiro.au>
               Created per-fs inode table rather than using inode->u.generic_ip
  v0.2
    19980111   Richard Gooch <rgooch@atnf.csiro.au>
               Created .epoch inode which has a ctime of 0.
	       Fixed loss of named pipes when dentries lost.
	       Fixed loss of inode data when devfs_register() follows mknod().
  v0.3
    19980111   Richard Gooch <rgooch@atnf.csiro.au>
               Fix for when compiling with CONFIG_KERNELD.
    19980112   Richard Gooch <rgooch@atnf.csiro.au>
               Fix for readdir() which sometimes didn't show entries.
	       Added <<tolerant>> option to <devfs_register>.
  v0.4
    19980113   Richard Gooch <rgooch@atnf.csiro.au>
               Created <devfs_fill_file> function.
  v0.5
    19980115   Richard Gooch <rgooch@atnf.csiro.au>
               Added subdirectory support. Major restructuring.
    19980116   Richard Gooch <rgooch@atnf.csiro.au>
               Fixed <find_by_dev> to not search major=0,minor=0.
	       Added symlink support.
  v0.6
    19980120   Richard Gooch <rgooch@atnf.csiro.au>
               Created <devfs_mk_dir> function and support directory unregister
    19980120   Richard Gooch <rgooch@atnf.csiro.au>
               Auto-ownership uses real uid/gid rather than effective uid/gid.
  v0.7
    19980121   Richard Gooch <rgooch@atnf.csiro.au>
               Supported creation of sockets.
  v0.8
    19980122   Richard Gooch <rgooch@atnf.csiro.au>
               Added DEVFS_FL_HIDE_UNREG flag.
	       Interface change to <devfs_mk_symlink>.
               Created <devfs_symlink> to support symlink(2).
  v0.9
    19980123   Richard Gooch <rgooch@atnf.csiro.au>
               Added check to <devfs_fill_file> to check inode is in devfs.
	       Added optional traversal of symlinks.
  v0.10
    19980124   Richard Gooch <rgooch@atnf.csiro.au>
               Created <devfs_get_flags> and <devfs_set_flags>.
  v0.11
    19980125   C. Scott Ananian <cananian@alumni.princeton.edu>
               Created <devfs_find_handle>.
    19980125   Richard Gooch <rgooch@atnf.csiro.au>
               Allow removal of symlinks.
  v0.12
    19980125   Richard Gooch <rgooch@atnf.csiro.au>
               Created <devfs_set_symlink_destination>.
    19980126   Richard Gooch <rgooch@atnf.csiro.au>
               Moved DEVFS_SUPER_MAGIC into header file.
	       Added DEVFS_FL_HIDE flag.
	       Created <devfs_get_maj_min>.
	       Created <devfs_get_handle_from_inode>.
	       Fixed minor bug in <find_by_dev>.
    19980127   Richard Gooch <rgooch@atnf.csiro.au>
	       Changed interface to <find_by_dev>, <find_entry>,
	       <devfs_unregister>, <devfs_fill_file> and <devfs_find_handle>.
	       Fixed inode times when symlink created with symlink(2).
  v0.13
    19980129   C. Scott Ananian <cananian@alumni.princeton.edu>
               Exported <devfs_set_symlink_destination>, <devfs_get_maj_min>
	       and <devfs_get_handle_from_inode>.
    19980129   Richard Gooch <rgooch@atnf.csiro.au>
	       Created <devfs_unlink> to support unlink(2).
  v0.14
    19980129   Richard Gooch <rgooch@atnf.csiro.au>
	       Fixed kerneld support for entries in devfs subdirectories.
    19980130   Richard Gooch <rgooch@atnf.csiro.au>
	       Bugfixes in <call_kerneld>.
  v0.15
    19980207   Richard Gooch <rgooch@atnf.csiro.au>
	       Call kerneld when looking up unregistered entries.
  v0.16
    19980326   Richard Gooch <rgooch@atnf.csiro.au>
	       Modified interface to <devfs_find_handle> for symlink traversal.
  v0.17
    19980331   Richard Gooch <rgooch@atnf.csiro.au>
	       Fixed persistence bug with device numbers for manually created
	       device files.
	       Fixed problem with recreating symlinks with different content.
  v0.18
    19980401   Richard Gooch <rgooch@atnf.csiro.au>
	       Changed to CONFIG_KMOD.
	       Hide entries which are manually unlinked.
	       Always invalidate devfs dentry cache when registering entries.
	       Created <devfs_rmdir> to support rmdir(2).
	       Ensure directories created by <devfs_mk_dir> are visible.
  v0.19
    19980402   Richard Gooch <rgooch@atnf.csiro.au>
	       Invalidate devfs dentry cache when making directories.
	       Invalidate devfs dentry cache when removing entries.
	       Fixed persistence bug with fifos.
  v0.20
    19980421   Richard Gooch <rgooch@atnf.csiro.au>
	       Print process command when debugging kerneld/kmod.
	       Added debugging for register/unregister/change operations.
    19980422   Richard Gooch <rgooch@atnf.csiro.au>
	       Added "devfs=" boot options.
  v0.21
    19980426   Richard Gooch <rgooch@atnf.csiro.au>
	       No longer lock/unlock superblock in <devfs_put_super>.
	       Drop negative dentries when they are released.
	       Manage dcache more efficiently.
  v0.22
    19980427   Richard Gooch <rgooch@atnf.csiro.au>
	       Added DEVFS_FL_AUTO_DEVNUM flag.
  v0.23
    19980430   Richard Gooch <rgooch@atnf.csiro.au>
	       No longer set unnecessary methods.
  v0.24
    19980504   Richard Gooch <rgooch@atnf.csiro.au>
	       Added PID display to <call_kerneld> debugging message.
	       Added "after" debugging message to <call_kerneld>.
    19980519   Richard Gooch <rgooch@atnf.csiro.au>
	       Added "diread" and "diwrite" boot options.
    19980520   Richard Gooch <rgooch@atnf.csiro.au>
	       Fixed persistence problem with permissions.
  v0.25
    19980602   Richard Gooch <rgooch@atnf.csiro.au>
	       Support legacy device nodes.
	       Fixed bug where recreated inodes were hidden.
  v0.26
    19980602   Richard Gooch <rgooch@atnf.csiro.au>
	       Improved debugging in <get_vfs_inode>.
    19980607   Richard Gooch <rgooch@atnf.csiro.au>
	       No longer free old dentries in <devfs_mk_dir>.
	       Free all dentries for a given entry when deleting inodes.
  v0.27
    19980627   Richard Gooch <rgooch@atnf.csiro.au>
	       Limit auto-device numbering to majors 128 to 239.
  v0.28
    19980629   Richard Gooch <rgooch@atnf.csiro.au>
	       Fixed inode times persistence problem.
  v0.29
    19980704   Richard Gooch <rgooch@atnf.csiro.au>
	       Fixed spelling in <devfs_readlink> debug.
	       Fixed bug in <devfs_setup> parsing "dilookup".
  v0.30
    19980705   Richard Gooch <rgooch@atnf.csiro.au>
	       Fixed devfs inode leak when manually recreating inodes.
	       Fixed permission persistence problem when recreating inodes.
  v0.31
    19980727   Richard Gooch <rgooch@atnf.csiro.au>
	       Removed harmless "unused variable" compiler warning.
	       Fixed modes for manually recreated device nodes.
  v0.32
    19980728   Richard Gooch <rgooch@atnf.csiro.au>
	       Added NULL devfs inode warning in <devfs_read_inode>.
	       Force all inode nlink values to 1.
  v0.33
    19980730   Richard Gooch <rgooch@atnf.csiro.au>
	       Added "dimknod" boot option.
	       Set inode nlink to 0 when freeing dentries.
	       Fixed modes for manually recreated symlinks.
  v0.34
    19980802   Richard Gooch <rgooch@atnf.csiro.au>
	       Fixed bugs in recreated directories and symlinks.
  v0.35
    19980806   Richard Gooch <rgooch@atnf.csiro.au>
	       Fixed bugs in recreated device nodes.
    19980807   Richard Gooch <rgooch@atnf.csiro.au>
	       Fixed bug in currently unused <devfs_get_handle_from_inode>.
	       Defined new <devfs_handle_t> type.
	       Improved debugging when getting entries.
	       Fixed bug where directories could be emptied.
  v0.36
    19980809   Richard Gooch <rgooch@atnf.csiro.au>
	       Replaced dummy .epoch inode with .devfsd character device.
    19980810   Richard Gooch <rgooch@atnf.csiro.au>
	       Implemented devfsd protocol revision 0.
  v0.37
    19980819   Richard Gooch <rgooch@atnf.csiro.au>
	       Added soothing message to warning in <devfs_d_iput>.
  v0.38
    19980829   Richard Gooch <rgooch@atnf.csiro.au>
	       Use GCC extensions for structure initialisations.
	       Implemented async open notification.
	       Incremented devfsd protocol revision to 1.
  v0.39
    19980908   Richard Gooch <rgooch@atnf.csiro.au>
	       Moved async open notification to end of <devfs_open>.
  v0.40
    19980910   Richard Gooch <rgooch@atnf.csiro.au>
	       Prepended "/dev/" to module load request.
	       Renamed <call_kerneld> to <call_kmod>.
  v0.41
    19980910   Richard Gooch <rgooch@atnf.csiro.au>
	       Fixed typo "AYSNC" -> "ASYNC".
  v0.42
    19980910   Richard Gooch <rgooch@atnf.csiro.au>
	       Added open flag for files.
  v0.43
    19980927   Richard Gooch <rgooch@atnf.csiro.au>
	       Set i_blocks=0 and i_blksize=1024 in <devfs_read_inode>.
  v0.44
    19981005   Richard Gooch <rgooch@atnf.csiro.au>
	       Added test for empty <<name>> in <devfs_find_handle>.
	       Renamed <generate_path> to <devfs_generate_path> and published.
  v0.45
    19981006   Richard Gooch <rgooch@atnf.csiro.au>
	       Created <devfs_get_fops>.
  v0.46
    19981007   Richard Gooch <rgooch@atnf.csiro.au>
	       Limit auto-device numbering to majors 144 to 239.
  v0.47
    19981010   Richard Gooch <rgooch@atnf.csiro.au>
	       Updated <devfs_follow_link> for VFS change in 2.1.125.
  v0.48
    19981022   Richard Gooch <rgooch@atnf.csiro.au>
	       Created DEVFS_ FL_COMPAT flag.
  v0.49
    19981023   Richard Gooch <rgooch@atnf.csiro.au>
	       Created "nocompat" boot option.
  v0.50
    19981025   Richard Gooch <rgooch@atnf.csiro.au>
	       Replaced "mount" boot option with "nomount".
  v0.51
    19981110   Richard Gooch <rgooch@atnf.csiro.au>
	       Created "only" boot option.
  v0.52
    19981112   Richard Gooch <rgooch@atnf.csiro.au>
	       Added DEVFS_FL_REMOVABLE flag.
  v0.53
    19981114   Richard Gooch <rgooch@atnf.csiro.au>
	       Only call <scan_dir_for_removable> on first call to
	       <devfs_readdir>.
  v0.54
    19981205   Richard Gooch <rgooch@atnf.csiro.au>
	       Updated <devfs_rmdir> for VFS change in 2.1.131.
  v0.55
    19981218   Richard Gooch <rgooch@atnf.csiro.au>
	       Created <devfs_mk_compat>.
    19981220   Richard Gooch <rgooch@atnf.csiro.au>
	       Check for partitions on removable media in <devfs_lookup>.
  v0.56
    19990118   Richard Gooch <rgooch@atnf.csiro.au>
	       Added support for registering regular files.
	       Created <devfs_set_file_size>.
	       Update devfs inodes from entries if not changed through FS.
  v0.57
    19990124   Richard Gooch <rgooch@atnf.csiro.au>
	       Fixed <devfs_fill_file> to only initialise temporary inodes.
	       Trap for NULL fops in <devfs_register>.
	       Return -ENODEV in <devfs_fill_file> for non-driver inodes.
  v0.58
    19990126   Richard Gooch <rgooch@atnf.csiro.au>
	       Switched from PATH_MAX to DEVFS_PATHLEN.
  v0.59
    19990127   Richard Gooch <rgooch@atnf.csiro.au>
	       Created "nottycompat" boot option.
  v0.60
    19990318   Richard Gooch <rgooch@atnf.csiro.au>
	       Fixed <devfsd_read> to not overrun event buffer.
  v0.61
    19990329   Richard Gooch <rgooch@atnf.csiro.au>
	       Created <devfs_auto_unregister>.
  v0.62
    19990330   Richard Gooch <rgooch@atnf.csiro.au>
	       Don't return unregistred entries in <devfs_find_handle>.
	       Panic in <devfs_unregister> if entry unregistered.
    19990401   Richard Gooch <rgooch@atnf.csiro.au>
	       Don't panic in <devfs_auto_unregister> for duplicates.
  v0.63
    19990402   Richard Gooch <rgooch@atnf.csiro.au>
	       Don't unregister already unregistered entries in <unregister>.
  v0.64
    19990510   Richard Gooch <rgooch@atnf.csiro.au>
	       Disable warning messages when unable to read partition table for
	       removable media.
  v0.65
    19990512   Richard Gooch <rgooch@atnf.csiro.au>
	       Updated <devfs_lookup> for VFS change in 2.3.1-pre1.
	       Created "oops-on-panic" boot option.
	       Improved debugging in <devfs_register> and <devfs_unregister>.
  v0.66
    19990519   Richard Gooch <rgooch@atnf.csiro.au>
	       Added documentation for some functions.
    19990525   Richard Gooch <rgooch@atnf.csiro.au>
	       Removed "oops-on-panic" boot option: now always Oops.
  v0.67
    19990531   Richard Gooch <rgooch@atnf.csiro.au>
	       Improved debugging in <devfs_register>.
  v0.68
    19990604   Richard Gooch <rgooch@atnf.csiro.au>
	       Added "diunlink" and "nokmod" boot options.
	       Removed superfluous warning message in <devfs_d_iput>.
  v0.69
    19990611   Richard Gooch <rgooch@atnf.csiro.au>
	       Took account of change to <d_alloc_root>.
  v0.70
    19990614   Richard Gooch <rgooch@atnf.csiro.au>
	       Created separate event queue for each mounted devfs.
	       Removed <devfs_invalidate_dcache>.
	       Created new ioctl()s.
	       Incremented devfsd protocol revision to 3.
	       Fixed bug when re-creating directories: contents were lost.
	       Block access to inodes until devfsd updates permissions.
    19990615   Richard Gooch <rgooch@atnf.csiro.au>
	       Support 2.2.x kernels.
  v0.71
    19990623   Richard Gooch <rgooch@atnf.csiro.au>
	       Switched to sending process uid/gid to devfsd.
	       Renamed <call_kmod> to <try_modload>.
	       Added DEVFSD_NOTIFY_LOOKUP event.
    19990624   Richard Gooch <rgooch@atnf.csiro.au>
	       Added DEVFSD_NOTIFY_CHANGE event.
	       Incremented devfsd protocol revision to 4.
  v0.72
    19990713   Richard Gooch <rgooch@atnf.csiro.au>
	       Return EISDIR rather than EINVAL for read(2) on directories.
  v0.73
    19990809   Richard Gooch <rgooch@atnf.csiro.au>
	       Changed <devfs_setup> to new __init scheme.
  v0.74
    19990901   Richard Gooch <rgooch@atnf.csiro.au>
	       Changed remaining function declarations to new __init scheme.
  v0.75
    19991013   Richard Gooch <rgooch@atnf.csiro.au>
	       Created <devfs_get_info>, <devfs_set_info>,
	       <devfs_get_first_child> and <devfs_get_next_sibling>.
	       Added <<dir>> parameter to <devfs_register>, <devfs_mk_compat>,
	       <devfs_mk_dir> and <devfs_find_handle>.
	       Work sponsored by SGI.
  v0.76
    19991017   Richard Gooch <rgooch@atnf.csiro.au>
	       Allow multiple unregistrations.
	       Work sponsored by SGI.
  v0.77
    19991026   Richard Gooch <rgooch@atnf.csiro.au>
	       Added major and minor number to devfsd protocol.
	       Incremented devfsd protocol revision to 5.
	       Work sponsored by SGI.
  v0.78
    19991030   Richard Gooch <rgooch@atnf.csiro.au>
	       Support info pointer for all devfs entry types.
	       Added <<info>> parameter to <devfs_mk_dir> and
	       <devfs_mk_symlink>.
	       Work sponsored by SGI.
  v0.79
    19991031   Richard Gooch <rgooch@atnf.csiro.au>
	       Support "../" when searching devfs namespace.
	       Work sponsored by SGI.
  v0.80
    19991101   Richard Gooch <rgooch@atnf.csiro.au>
	       Created <devfs_get_unregister_slave>.
	       Work sponsored by SGI.
  v0.81
    19991103   Richard Gooch <rgooch@atnf.csiro.au>
	       Exported <devfs_get_parent>.
	       Work sponsored by SGI.
  v0.82
    19991104   Richard Gooch <rgooch@atnf.csiro.au>
               Removed unused <devfs_set_symlink_destination>.
    19991105   Richard Gooch <rgooch@atnf.csiro.au>
               Do not hide entries from devfsd or children.
	       Removed DEVFS_ FL_TTY_COMPAT flag.
	       Removed "nottycompat" boot option.
	       Removed <devfs_mk_compat>.
	       Work sponsored by SGI.
  v0.83
    19991107   Richard Gooch <rgooch@atnf.csiro.au>
	       Added DEVFS_ FL_WAIT flag.
	       Work sponsored by SGI.
  v0.84
    19991107   Richard Gooch <rgooch@atnf.csiro.au>
	       Support new "disc" naming scheme in <get_removable_partition>.
	       Allow NULL fops in <devfs_register>.
	       Work sponsored by SGI.
  v0.85
    19991110   Richard Gooch <rgooch@atnf.csiro.au>
	       Fall back to major table if NULL fops given to <devfs_register>.
	       Work sponsored by SGI.
  v0.86
    19991204   Richard Gooch <rgooch@atnf.csiro.au>
	       Support fifos when unregistering.
	       Work sponsored by SGI.
  v0.87
    19991209   Richard Gooch <rgooch@atnf.csiro.au>
	       Removed obsolete DEVFS_ FL_COMPAT and DEVFS_ FL_TOLERANT flags.
	       Work sponsored by SGI.
  v0.88
    19991214   Richard Gooch <rgooch@atnf.csiro.au>
	       Removed kmod support.
	       Work sponsored by SGI.
  v0.89
    19991216   Richard Gooch <rgooch@atnf.csiro.au>
	       Improved debugging in <get_vfs_inode>.
	       Ensure dentries created by devfsd will be cleaned up.
	       Work sponsored by SGI.
  v0.90
    19991223   Richard Gooch <rgooch@atnf.csiro.au>
	       Created <devfs_get_name>.
	       Work sponsored by SGI.
  v0.91
    20000203   Richard Gooch <rgooch@atnf.csiro.au>
	       Ported to kernel 2.3.42.
	       Removed <devfs_fill_file>.
	       Work sponsored by SGI.
  v0.92
    20000306   Richard Gooch <rgooch@atnf.csiro.au>
	       Added DEVFS_FL_NO_PERSISTENCE flag.
	       Removed unnecessary call to <update_devfs_inode_from_entry> in
	       <devfs_readdir>.
	       Work sponsored by SGI.
  v0.93
    20000413   Richard Gooch <rgooch@atnf.csiro.au>
	       Set inode->i_size to correct size for symlinks.
    20000414   Richard Gooch <rgooch@atnf.csiro.au>
	       Only give lookup() method to directories to comply with new VFS
	       assumptions.
	       Work sponsored by SGI.
    20000415   Richard Gooch <rgooch@atnf.csiro.au>
	       Remove unnecessary tests in symlink methods.
	       Don't kill existing block ops in <devfs_read_inode>.
	       Work sponsored by SGI.
  v0.94
    20000424   Richard Gooch <rgooch@atnf.csiro.au>
	       Don't create missing directories in <devfs_find_handle>.
	       Work sponsored by SGI.
  v0.95
    20000430   Richard Gooch <rgooch@atnf.csiro.au>
	       Added CONFIG_DEVFS_MOUNT.
	       Work sponsored by SGI.
  v0.96
    20000608   Richard Gooch <rgooch@atnf.csiro.au>
	       Disabled multi-mount capability (use VFS bindings instead).
	       Work sponsored by SGI.
  v0.97
    20000610   Richard Gooch <rgooch@atnf.csiro.au>
	       Switched to FS_SINGLE to disable multi-mounts.
    20000612   Richard Gooch <rgooch@atnf.csiro.au>
	       Removed module support.
	       Removed multi-mount code.
	       Removed compatibility macros: VFS has changed too much.
	       Work sponsored by SGI.
  v0.98
    20000614   Richard Gooch <rgooch@atnf.csiro.au>
	       Merged devfs inode into devfs entry.
	       Work sponsored by SGI.
  v0.99
    20000619   Richard Gooch <rgooch@atnf.csiro.au>
	       Removed dead code in <devfs_register> which used to call
	       <free_dentries>.
	       Work sponsored by SGI.
  v0.100
    20000621   Richard Gooch <rgooch@atnf.csiro.au>
	       Changed interface to <devfs_register>.
	       Work sponsored by SGI.
  v0.101
    20000622   Richard Gooch <rgooch@atnf.csiro.au>
	       Simplified interface to <devfs_mk_symlink> and <devfs_mk_dir>.
	       Simplified interface to <devfs_find_handle>.
	       Work sponsored by SGI.
  v0.102
*/
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/timer.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/string.h>
#include <linux/malloc.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/ctype.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/locks.h>
#include <linux/kdev_t.h>
#include <linux/devfs_fs.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/smp_lock.h>
#include <linux/smp.h>
#include <linux/version.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/segment.h>
#include <asm/bitops.h>
#include <asm/atomic.h>

#define DEVFS_VERSION            "0.102 (20000622)"

#define DEVFS_NAME "devfs"

#define INODE_TABLE_INC 250
#define FIRST_INODE 1

#define STRING_LENGTH 256

#define MIN_DEVNUM 36864  /*  Use major numbers 144   */
#define MAX_DEVNUM 61439  /*  through 239, inclusive  */

#ifndef TRUE
#  define TRUE 1
#  define FALSE 0
#endif

#define IS_HIDDEN(de) (( ((de)->hide && !is_devfsd_or_child(fs_info)) || (!(de)->registered&& !(de)->show_unreg)))

#define DEBUG_NONE         0x00000
#define DEBUG_MODULE_LOAD  0x00001
#define DEBUG_REGISTER     0x00002
#define DEBUG_UNREGISTER   0x00004
#define DEBUG_SET_FLAGS    0x00008
#define DEBUG_S_PUT        0x00010
#define DEBUG_I_LOOKUP     0x00020
#define DEBUG_I_CREATE     0x00040
#define DEBUG_I_READ       0x00080
#define DEBUG_I_WRITE      0x00100
#define DEBUG_I_UNLINK     0x00200
#define DEBUG_I_RLINK      0x00400
#define DEBUG_I_FLINK      0x00800
#define DEBUG_I_MKNOD      0x01000
#define DEBUG_F_READDIR    0x02000
#define DEBUG_D_DELETE     0x04000
#define DEBUG_D_RELEASE    0x08000
#define DEBUG_D_IPUT       0x10000
#define DEBUG_ALL          (DEBUG_MODULE_LOAD | DEBUG_REGISTER | \
			    DEBUG_SET_FLAGS | DEBUG_I_LOOKUP |   \
			    DEBUG_I_UNLINK | DEBUG_I_MKNOD |     \
			    DEBUG_D_RELEASE | DEBUG_D_IPUT)
#define DEBUG_DISABLED     DEBUG_NONE

#define OPTION_NONE             0x00
#define OPTION_SHOW             0x01
#define OPTION_NOMOUNT          0x02
#define OPTION_ONLY             0x04

#define OOPS(format, args...) {printk (format, ## args); \
                               printk ("Forcing Oops\n"); \
                               BUG();}

struct directory_type
{
    struct devfs_entry *first;
    struct devfs_entry *last;
    unsigned int num_removable;
};

struct file_type
{
    unsigned long size;
};

struct device_type
{
    unsigned short major;
    unsigned short minor;
};

struct fcb_type  /*  File, char, block type  */
{
    uid_t default_uid;
    gid_t default_gid;
    void *ops;
    union 
    {
	struct file_type file;
	struct device_type device;
    }
    u;
    unsigned char auto_owner:1;
    unsigned char aopen_notify:1;
    unsigned char removable:1;  /*  Belongs in device_type, but save space   */
    unsigned char open:1;       /*  Not entirely correct                     */
};

struct symlink_type
{
    unsigned int length;  /*  Not including the NULL-termimator  */
    char *linkname;       /*  This is NULL-terminated            */
};

struct fifo_type
{
    uid_t uid;
    gid_t gid;
};

struct devfs_inode  /*  This structure is for "persistent" inode storage  */
{
    time_t atime;
    time_t mtime;
    time_t ctime;
    unsigned int ino;          /*  Inode number as seen in the VFS  */
    struct dentry *dentry;
    umode_t mode;
    uid_t uid;
    gid_t gid;
    nlink_t nlink;
};

struct devfs_entry
{
    void *info;
    union 
    {
	struct directory_type dir;
	struct fcb_type fcb;
	struct symlink_type symlink;
	struct fifo_type fifo;
    }
    u;
    struct devfs_entry *prev;    /*  Previous entry in the parent directory  */
    struct devfs_entry *next;    /*  Next entry in the parent directory      */
    struct devfs_entry *parent;  /*  The parent directory                    */
    struct devfs_entry *slave;   /*  Another entry to unregister             */
    struct devfs_inode inode;
    umode_t mode;
    unsigned short namelen;  /*  I think 64k+ filenames are a way off...  */
    unsigned char registered:1;
    unsigned char show_unreg:1;
    unsigned char hide:1;
    unsigned char no_persistence:1;
    char name[1];            /*  This is just a dummy: the allocated array is
				 bigger. This is NULL-terminated  */
};

/*  The root of the device tree  */
static struct devfs_entry *root_entry;

struct devfsd_buf_entry
{
    void *data;
    unsigned int type;
    umode_t mode;
    uid_t uid;
    gid_t gid;
};

struct fs_info      /*  This structure is for each mounted devfs  */
{
    unsigned int num_inodes;    /*  Number of inodes created         */
    unsigned int table_size;    /*  Size of the inode pointer table  */
    struct devfs_entry **table;
    struct super_block *sb;
    volatile struct devfsd_buf_entry *devfsd_buffer;
    volatile unsigned int devfsd_buf_in;
    volatile unsigned int devfsd_buf_out;
    volatile int devfsd_sleeping;
    volatile int devfsd_buffer_in_use;
    volatile struct task_struct *devfsd_task;
    volatile struct file *devfsd_file;
    volatile unsigned long devfsd_event_mask;
    atomic_t devfsd_overrun_count;
    wait_queue_head_t devfsd_wait_queue;
    wait_queue_head_t revalidate_wait_queue;
};

static struct fs_info fs_info;
static unsigned int next_devnum_char = MIN_DEVNUM;
static unsigned int next_devnum_block = MIN_DEVNUM;
static const int devfsd_buf_size = PAGE_SIZE / sizeof(struct devfsd_buf_entry);
#ifdef CONFIG_DEVFS_DEBUG
static unsigned int devfs_debug_init __initdata = DEBUG_NONE;
static unsigned int devfs_debug = DEBUG_NONE;
#endif

#ifdef CONFIG_DEVFS_MOUNT
static unsigned int boot_options = OPTION_NONE;
#else
static unsigned int boot_options = OPTION_NOMOUNT;
#endif

/*  Forward function declarations  */
static struct devfs_entry *search_for_entry (struct devfs_entry *dir,
					     const char *name,
					     unsigned int namelen, int mkdir,
					     int mkfile, int *is_new,
					     int traverse_symlink);
static ssize_t devfsd_read (struct file *file, char *buf, size_t len,
			    loff_t *ppos);
static int devfsd_ioctl (struct inode *inode, struct file *file,
			 unsigned int cmd, unsigned long arg);
static int devfsd_close (struct inode *inode, struct file *file);


/*  Devfs daemon file operations  */
static struct file_operations devfsd_fops =
{
    read:    devfsd_read,
    ioctl:   devfsd_ioctl,
    release: devfsd_close,
};


/*  Support functions follow  */


/**
 *	search_for_entry_in_dir - Search for a devfs entry inside another devfs entry.
 *	@parent:  The parent devfs entry.
 *	@name:  The name of the entry.
 *	@namelen:  The number of characters in @name.
 *	@traverse_symlink:  If %TRUE then the entry is traversed if it is a symlink.
 *
 *  Search for a devfs entry inside another devfs entry and returns a pointer
 *   to the entry on success, else %NULL.
 */

static struct devfs_entry *search_for_entry_in_dir (struct devfs_entry *parent,
						    const char *name,
						    unsigned int namelen,
						    int traverse_symlink)
{
    struct devfs_entry *curr;

    if ( !S_ISDIR (parent->mode) )
    {
	printk ("%s: entry is not a directory\n", DEVFS_NAME);
	return NULL;
    }
    for (curr = parent->u.dir.first; curr != NULL; curr = curr->next)
    {
	if (curr->namelen != namelen) continue;
	if (memcmp (curr->name, name, namelen) == 0) break;
	/*  Not found: try the next one  */
    }
    if (curr == NULL) return NULL;
    if (!S_ISLNK (curr->mode) || !traverse_symlink) return curr;
    /*  Need to follow the link: this is a stack chomper  */
    return search_for_entry (parent,
			     curr->u.symlink.linkname, curr->u.symlink.length,
			     FALSE, FALSE, NULL, TRUE);
}   /*  End Function search_for_entry_in_dir  */

static struct devfs_entry *create_entry (struct devfs_entry *parent,
					 const char *name,unsigned int namelen)
{
    struct devfs_entry *new, **table;

    /*  First ensure table size is enough  */
    if (fs_info.num_inodes >= fs_info.table_size)
    {
	if ( ( table = kmalloc (sizeof *table *
				(fs_info.table_size + INODE_TABLE_INC),
				GFP_KERNEL) ) == NULL ) return NULL;
	fs_info.table_size += INODE_TABLE_INC;
#ifdef CONFIG_DEVFS_DEBUG
	if (devfs_debug & DEBUG_I_CREATE)
	    printk ("%s: create_entry(): grew inode table to: %u entries\n",
		    DEVFS_NAME, fs_info.table_size);
#endif
	if (fs_info.table)
	{
	    memcpy (table, fs_info.table, sizeof *table *fs_info.num_inodes);
	    kfree (fs_info.table);
	}
	fs_info.table = table;
    }
    if ( name && (namelen < 1) ) namelen = strlen (name);
    if ( ( new = kmalloc (sizeof *new + namelen, GFP_KERNEL) ) == NULL )
	return NULL;
    /*  Magic: this will set the ctime to zero, thus subsequent lookups will
	trigger the call to <update_devfs_inode_from_entry>  */
    memset (new, 0, sizeof *new + namelen);
    new->parent = parent;
    if (name) memcpy (new->name, name, namelen);
    new->namelen = namelen;
    new->inode.ino = fs_info.num_inodes + FIRST_INODE;
    new->inode.nlink = 1;
    fs_info.table[fs_info.num_inodes] = new;
    ++fs_info.num_inodes;
    if (parent == NULL) return new;
    new->prev = parent->u.dir.last;
    /*  Insert into the parent directory's list of children  */
    if (parent->u.dir.first == NULL) parent->u.dir.first = new;
    else parent->u.dir.last->next = new;
    parent->u.dir.last = new;
    return new;
}   /*  End Function create_entry  */

static void update_devfs_inode_from_entry (struct devfs_entry *de)
{
    if (de == NULL) return;
    if ( S_ISDIR (de->mode) )
    {
	de->inode.mode = S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO;
	de->inode.uid = 0;
	de->inode.gid = 0;
    }
    else if ( S_ISLNK (de->mode) )
    {
	de->inode.mode = S_IFLNK | S_IRUGO | S_IXUGO;
	de->inode.uid = 0;
	de->inode.gid = 0;
    }
    else if ( S_ISFIFO (de->mode) )
    {
	de->inode.mode = de->mode;
	de->inode.uid = de->u.fifo.uid;
	de->inode.gid = de->u.fifo.gid;
    }
    else
    {
	if (de->u.fcb.auto_owner)
	    de->inode.mode = (de->mode & ~S_IALLUGO) | S_IRUGO | S_IWUGO;
	else de->inode.mode = de->mode;
	de->inode.uid = de->u.fcb.default_uid;
	de->inode.gid = de->u.fcb.default_gid;
    }
}   /*  End Function update_devfs_inode_from_entry  */

/**
 *	get_root_entry - Get the root devfs entry.
 *
 *	Returns the root devfs entry on success, else %NULL.
 */

static struct devfs_entry *get_root_entry (void)
{
    struct devfs_entry *new;

    /*  Always ensure the root is created  */
    if (root_entry != NULL) return root_entry;
    if ( ( root_entry = create_entry (NULL, NULL, 0) ) == NULL ) return NULL;
    root_entry->registered = TRUE;
    root_entry->mode = S_IFDIR;
    /*  Force an inode update, because lookup() is never done for the root  */
    update_devfs_inode_from_entry (root_entry);
    /*  And create the entry for ".devfsd"  */
    if ( ( new = create_entry (root_entry, ".devfsd", 0) ) == NULL )
	return NULL;
    new->registered = TRUE;
    new->u.fcb.u.device.major = next_devnum_char >> 8;
    new->u.fcb.u.device.minor = next_devnum_char & 0xff;
    ++next_devnum_char;
    new->mode = S_IFCHR | S_IRUSR | S_IWUSR;
    new->u.fcb.default_uid = 0;
    new->u.fcb.default_gid = 0;
    new->u.fcb.ops = &devfsd_fops;
    return root_entry;
}   /*  End Function get_root_entry  */


/**
 *	search_for_entry - Search for an entry in the devfs tree.
 *	@dir: The parent directory to search from. If this is %NULL the root is used
 *	@name: The name of the entry.
 *	@namelen: The number of characters in @name.
 *	@mkdir: If %TRUE intermediate directories are created as needed.
 *	@mkfile: If %TRUE the file entry is created if it doesn't exist.
 *	@is_new: If the returned entry was newly made, %TRUE is written here. If
 * 		this is %NULL nothing is written here.
 *	@traverse_symlink: If %TRUE then symbolic links are traversed.
 *
 *	If the entry is created, then it will be in the unregistered state.
 *	Returns a pointer to the entry on success, else %NULL.
 */

static struct devfs_entry *search_for_entry (struct devfs_entry *dir,
					     const char *name,
					     unsigned int namelen, int mkdir,
					     int mkfile, int *is_new,
					     int traverse_symlink)
{
    int len;
    const char *subname, *stop, *ptr;
    struct devfs_entry *entry;

    if (is_new) *is_new = FALSE;
    if (dir == NULL) dir = get_root_entry ();
    if (dir == NULL) return NULL;
    /*  Extract one filename component  */
    subname = name;
    stop = name + namelen;
    while (subname < stop)
    {
	/*  Search for a possible '/'  */
	for (ptr = subname; (ptr < stop) && (*ptr != '/'); ++ptr);
	if (ptr >= stop)
	{
	    /*  Look for trailing component  */
	    len = stop - subname;
	    entry = search_for_entry_in_dir (dir, subname, len,
					     traverse_symlink);
	    if (entry != NULL) return entry;
	    if (!mkfile) return NULL;
	    entry = create_entry (dir, subname, len);
	    if (entry && is_new) *is_new = TRUE;
	    return entry;
	}
	/*  Found '/': search for directory  */
	if (strncmp (subname, "../", 3) == 0)
	{
	    /*  Going up  */
	    dir = dir->parent;
	    if (dir == NULL) return NULL;  /*  Cannot escape from devfs  */
	    subname += 3;
	    continue;
	}
	len = ptr - subname;
	entry = search_for_entry_in_dir (dir, subname, len, traverse_symlink);
	if (!entry && !mkdir) return NULL;
	if (entry == NULL)
	{
	    /*  Make it  */
	    if ( ( entry = create_entry (dir, subname, len) ) == NULL )
		return NULL;
	    entry->mode = S_IFDIR | S_IRUGO | S_IXUGO | S_IWUSR;
	    if (is_new) *is_new = TRUE;
	}
	if ( !S_ISDIR (entry->mode) )
	{
	    printk ("%s: existing non-directory entry\n", DEVFS_NAME);
	    return NULL;
	}
	/*  Ensure an unregistered entry is re-registered and visible  */
	entry->registered = TRUE;
	entry->hide = FALSE;
	subname = ptr + 1;
	dir = entry;
    }
    return NULL;
}   /*  End Function search_for_entry  */


/**
 *	find_by_dev - Find a devfs entry in a directory.
 *	@dir: The directory where to search
 *	@major: The major number to search for.
 *	@minor: The minor number to search for.
 *	@type: The type of special file to search for. This may be either
 *		%DEVFS_SPECIAL_CHR or %DEVFS_SPECIAL_BLK.
 *
 *	Returns the devfs_entry pointer on success, else %NULL.
 */

static struct devfs_entry *find_by_dev (struct devfs_entry *dir,
					unsigned int major, unsigned int minor,
					char type)
{
    struct devfs_entry *entry, *de;

    if (dir == NULL) return NULL;
    if ( !S_ISDIR (dir->mode) )
    {
	printk ("%s: find_by_dev(): not a directory\n", DEVFS_NAME);
	return NULL;
    }
    /*  First search files in this directory  */
    for (entry = dir->u.dir.first; entry != NULL; entry = entry->next)
    {
	if ( !S_ISCHR (entry->mode) && !S_ISBLK (entry->mode) ) continue;
	if ( S_ISCHR (entry->mode) && (type != DEVFS_SPECIAL_CHR) ) continue;
	if ( S_ISBLK (entry->mode) && (type != DEVFS_SPECIAL_BLK) ) continue;
	if ( (entry->u.fcb.u.device.major == major) &&
	     (entry->u.fcb.u.device.minor == minor) ) return entry;
	/*  Not found: try the next one  */
    }
    /*  Now recursively search the subdirectories: this is a stack chomper  */
    for (entry = dir->u.dir.first; entry != NULL; entry = entry->next)
    {
	if ( !S_ISDIR (entry->mode) ) continue;
	de = find_by_dev (entry, major, minor, type);
	if (de) return de;
    }
    return NULL;
}   /*  End Function find_by_dev  */


/**
 *	find_entry - Find a devfs entry.
 *	@dir: The handle to the parent devfs directory entry. If this is %NULL the
 *		name is relative to the root of the devfs.
 *	@name: The name of the entry. This is ignored if @handle is not %NULL.
 *	@namelen: The number of characters in @name, not including a %NULL
 *		terminator. If this is 0, then @name must be %NULL-terminated and the
 *		length is computed internally.
 *	@major: The major number. This is used if @handle and @name are %NULL.
 *	@minor: The minor number. This is used if @handle and @name are %NULL.
 *		NOTE: If @major and @minor are both 0, searching by major and minor
 *		numbers is disabled.
 *	@type: The type of special file to search for. This may be either
 *		%DEVFS_SPECIAL_CHR or %DEVFS_SPECIAL_BLK.
 *	@traverse_symlink: If %TRUE then symbolic links are traversed.
 *
 *	FIXME: What the hell is @handle? - ch
 *	Returns the devfs_entry pointer on success, else %NULL.
 */

static struct devfs_entry *find_entry (devfs_handle_t dir,
				       const char *name, unsigned int namelen,
				       unsigned int major, unsigned int minor,
				       char type, int traverse_symlink)
{
    struct devfs_entry *entry;

    if (name != NULL)
    {
	if (namelen < 1) namelen = strlen (name);
	if (name[0] == '/')
	{
	    /*  Skip leading pathname component  */
	    if (namelen < 2)
	    {
		printk ("%s: find_entry(%s): too short\n", DEVFS_NAME, name);
		return NULL;
	    }
	    for (++name, --namelen; (*name != '/') && (namelen > 0);
		 ++name, --namelen);
	    if (namelen < 2)
	    {
		printk ("%s: find_entry(%s): too short\n", DEVFS_NAME, name);
		return NULL;
	    }
	    ++name;
	    --namelen;
	}
	entry = search_for_entry (dir, name, namelen, FALSE, FALSE, NULL,
				  traverse_symlink);
	if (entry != NULL) return entry;
    }
    /*  Have to search by major and minor: slow  */
    if ( (major == 0) && (minor == 0) ) return NULL;
    return find_by_dev (root_entry, major, minor, type);
}   /*  End Function find_entry  */

static struct devfs_entry *get_devfs_entry_from_vfs_inode (struct inode *inode)
{
    struct fs_info *fs_info;

    if (inode == NULL) return NULL;
    if (inode->i_ino < FIRST_INODE) return NULL;
    fs_info = inode->i_sb->u.generic_sbp;
    if (fs_info == NULL) return NULL;
    if (inode->i_ino - FIRST_INODE >= fs_info->num_inodes) return NULL;
    return fs_info->table[inode->i_ino - FIRST_INODE];
}   /*  End Function get_devfs_entry_from_vfs_inode  */


/**
 *	free_dentries - Free the dentries for a device entry and invalidate inodes.
 *	@de: The entry.
 */

static void free_dentries (struct devfs_entry *de)
{
    struct dentry *dentry;

    spin_lock(&dcache_lock);
    dentry = de->inode.dentry;
    if (dentry != NULL)
    {
	dget_locked (dentry);
	de->inode.dentry = NULL;
	spin_unlock(&dcache_lock);
	/*  Forcefully remove the inode  */
	if (dentry->d_inode != NULL) dentry->d_inode->i_nlink = 0;
	d_drop (dentry);
	dput (dentry);
    } else
	spin_unlock(&dcache_lock);
}   /*  End Function free_dentries  */


/**
 *	is_devfsd_or_child - Test if the current process is devfsd or one of its children.
 *	@fs_info: The filesystem information.
 *
 *	Returns %TRUE if devfsd or child, else %FALSE.
 */

static int is_devfsd_or_child (struct fs_info *fs_info)
{
    struct task_struct *p;

    for (p = current; p != &init_task; p = p->p_opptr)
    {
	if (p == fs_info->devfsd_task) return (TRUE);
    }
    return (FALSE);
}   /*  End Function is_devfsd_or_child  */


/**
 *	devfsd_queue_empty - Test if devfsd has work pending in its event queue.
 *	@fs_info: The filesystem information.
 *
 *	Returns %TRUE if the queue is empty, else %FALSE.
 */

static inline int devfsd_queue_empty (struct fs_info *fs_info)
{
    return (fs_info->devfsd_buf_out == fs_info->devfsd_buf_in) ? TRUE : FALSE;
}   /*  End Function devfsd_queue_empty  */


/**
 *	wait_for_devfsd_finished - Wait for devfsd to finish processing its event queue.
 *	@fs_info: The filesystem information.
 *
 *	Returns %TRUE if no more waiting will be required, else %FALSE.
 */

static int wait_for_devfsd_finished (struct fs_info *fs_info)
{
    DECLARE_WAITQUEUE (wait, current);

    if (fs_info->devfsd_task == NULL) return (TRUE);
    if (devfsd_queue_empty (fs_info) && fs_info->devfsd_sleeping) return TRUE;
    if ( is_devfsd_or_child (fs_info) ) return (FALSE);
    add_wait_queue (&fs_info->revalidate_wait_queue, &wait);
    current->state = TASK_UNINTERRUPTIBLE;
    if (!devfsd_queue_empty (fs_info) || !fs_info->devfsd_sleeping)
	if (fs_info->devfsd_task) schedule();
    remove_wait_queue (&fs_info->revalidate_wait_queue, &wait);
    current->state = TASK_RUNNING;
    return (TRUE);
}   /*  End Function wait_for_devfsd_finished  */


/**
 *	devfsd_notify_one - Notify a single devfsd daemon of a change.
 *	@data: Data to be passed.
 *	@type: The type of change.
 *	@mode: The mode of the entry.
 *	@uid: The user ID.
 *	@gid: The group ID.
 *	@fs_info: The filesystem info.
 *
 *	Returns %TRUE if an event was queued and devfsd woken up, else %FALSE.
 */

static int devfsd_notify_one (void *data, unsigned int type, umode_t mode,
			      uid_t uid, gid_t gid, struct fs_info *fs_info)
{
    unsigned int next_pos;
    unsigned long flags;
    struct devfsd_buf_entry *entry;
    static spinlock_t lock = SPIN_LOCK_UNLOCKED;

    if ( !( fs_info->devfsd_event_mask & (1 << type) ) ) return (FALSE);
    next_pos = fs_info->devfsd_buf_in + 1;
    if (next_pos >= devfsd_buf_size) next_pos = 0;
    if (next_pos == fs_info->devfsd_buf_out)
    {
	/*  Running up the arse of the reader: drop it  */
	atomic_inc (&fs_info->devfsd_overrun_count);
	return (FALSE);
    }
    spin_lock_irqsave (&lock, flags);
    fs_info->devfsd_buffer_in_use = TRUE;
    next_pos = fs_info->devfsd_buf_in + 1;
    if (next_pos >= devfsd_buf_size) next_pos = 0;
    entry = (struct devfsd_buf_entry *) fs_info->devfsd_buffer +
	fs_info->devfsd_buf_in;
    entry->data = data;
    entry->type = type;
    entry->mode = mode;
    entry->uid = uid;
    entry->gid = gid;
    fs_info->devfsd_buf_in = next_pos;
    fs_info->devfsd_buffer_in_use = FALSE;
    spin_unlock_irqrestore (&lock, flags);
    wake_up_interruptible (&fs_info->devfsd_wait_queue);
    return (TRUE);
}   /*  End Function devfsd_notify_one  */


/**
 *	devfsd_notify - Notify all devfsd daemons of a change.
 *	@de: The devfs entry that has changed.
 *	@type: The type of change event.
 *	@wait: If TRUE, the functions waits for all daemons to finish processing
 *		the event.
 */

static void devfsd_notify (struct devfs_entry *de, unsigned int type, int wait)
{
    if (devfsd_notify_one (de, type, de->mode, current->euid,
			   current->egid, &fs_info) && wait)
	wait_for_devfsd_finished (&fs_info);
}   /*  End Function devfsd_notify  */


/**
 *	devfs_register - Register a device entry.
 *	@dir: The handle to the parent devfs directory entry. If this is %NULL the
 *		new name is relative to the root of the devfs.
 *	@name: The name of the entry.
 *	@flags: A set of bitwise-ORed flags (DEVFS_FL_*).
 *	@major: The major number. Not needed for regular files.
 *	@minor: The minor number. Not needed for regular files.
 *	@mode: The default file mode.
 *	@ops: The &file_operations or &block_device_operations structure.
 *		This must not be externally deallocated.
 *	@info: An arbitrary pointer which will be written to the @private_data
 *		field of the &file structure passed to the device driver. You can set
 *		this to whatever you like, and change it once the file is opened (the next
 *		file opened will not see this change).
 *
 *	Returns a handle which may later be used in a call to devfs_unregister().
 *	On failure %NULL is returned.
 */

devfs_handle_t devfs_register (devfs_handle_t dir, const char *name,
			       unsigned int flags,
			       unsigned int major, unsigned int minor,
			       umode_t mode, void *ops, void *info)
{
    int is_new;
    struct devfs_entry *de;

    if (name == NULL)
    {
	printk ("%s: devfs_register(): NULL name pointer\n", DEVFS_NAME);
	return NULL;
    }
    if (ops == NULL)
    {
	if ( S_ISBLK (mode) ) ops = (void *) get_blkfops (major);
	if (ops == NULL)
	{
	    printk ("%s: devfs_register(%s): NULL ops pointer\n",
		    DEVFS_NAME, name);
	    return NULL;
	}
	printk ("%s: devfs_register(%s): NULL ops, got %p from major table\n",
		DEVFS_NAME, name, ops);
    }
    if ( S_ISDIR (mode) )
    {
	printk("%s: devfs_register(%s): creating directories is not allowed\n",
	       DEVFS_NAME, name);
	return NULL;
    }
    if ( S_ISLNK (mode) )
    {
	printk ("%s: devfs_register(%s): creating symlinks is not allowed\n",
		DEVFS_NAME, name);
	return NULL;
    }
    if ( S_ISCHR (mode) && (flags & DEVFS_FL_AUTO_DEVNUM) )
    {
	if (next_devnum_char >= MAX_DEVNUM)
	{
	    printk ("%s: devfs_register(%s): exhausted char device numbers\n",
		    DEVFS_NAME, name);
	    return NULL;
	}
	major = next_devnum_char >> 8;
	minor = next_devnum_char & 0xff;
	++next_devnum_char;
    }
    if ( S_ISBLK (mode) && (flags & DEVFS_FL_AUTO_DEVNUM) )
    {
	if (next_devnum_block >= MAX_DEVNUM)
	{
	    printk ("%s: devfs_register(%s): exhausted block device numbers\n",
		    DEVFS_NAME, name);
	    return NULL;
	}
	major = next_devnum_block >> 8;
	minor = next_devnum_block & 0xff;
	++next_devnum_block;
    }
    de = search_for_entry (dir, name, strlen (name), TRUE, TRUE, &is_new,
			   FALSE);
    if (de == NULL)
    {
	printk ("%s: devfs_register(): could not create entry: \"%s\"\n",
		DEVFS_NAME, name);
	return NULL;
    }
#ifdef CONFIG_DEVFS_DEBUG
    if (devfs_debug & DEBUG_REGISTER)
	printk ("%s: devfs_register(%s): de: %p %s\n",
		DEVFS_NAME, name, de, is_new ? "new" : "existing");
#endif
    if (!is_new)
    {
	/*  Existing entry  */
	if ( !S_ISCHR (de->mode) && !S_ISBLK (de->mode) &&
	     !S_ISREG (de->mode) )
	{
	    printk ("%s: devfs_register(): existing non-device/file entry: \"%s\"\n",
		    DEVFS_NAME, name);
	    return NULL;
	}
	if (de->registered)
	{
	    printk("%s: devfs_register(): device already registered: \"%s\"\n",
		   DEVFS_NAME, name);
	    return NULL;
	}
    }
    de->registered = TRUE;
    if ( S_ISCHR (mode) || S_ISBLK (mode) )
    {
	de->u.fcb.u.device.major = major;
	de->u.fcb.u.device.minor = minor;
    }
    else if ( S_ISREG (mode) ) de->u.fcb.u.file.size = 0;
    else
    {
	printk ("%s: devfs_register(): illegal mode: %x\n",
		DEVFS_NAME, mode);
	return (NULL);
    }
    de->info = info;
    de->mode = mode;
    if (flags & DEVFS_FL_CURRENT_OWNER)
    {
	de->u.fcb.default_uid = current->uid;
	de->u.fcb.default_gid = current->gid;
    }
    else
    {
	de->u.fcb.default_uid = 0;
	de->u.fcb.default_gid = 0;
    }
    de->registered = TRUE;
    de->u.fcb.ops = ops;
    de->u.fcb.auto_owner = (flags & DEVFS_FL_AUTO_OWNER) ? TRUE : FALSE;
    de->u.fcb.aopen_notify = (flags & DEVFS_FL_AOPEN_NOTIFY) ? TRUE : FALSE;
    if (flags & DEVFS_FL_REMOVABLE)
    {
	de->u.fcb.removable = TRUE;
	++de->parent->u.dir.num_removable;
    }
    de->u.fcb.open = FALSE;
    de->show_unreg = ( (boot_options & OPTION_SHOW)
			|| (flags & DEVFS_FL_SHOW_UNREG) ) ? TRUE : FALSE;
    de->hide = (flags & DEVFS_FL_HIDE) ? TRUE : FALSE;
    de->no_persistence = (flags & DEVFS_FL_NO_PERSISTENCE) ? TRUE : FALSE;
    devfsd_notify (de, DEVFSD_NOTIFY_REGISTERED, flags & DEVFS_FL_WAIT);
    return de;
}   /*  End Function devfs_register  */


/**
 *	unregister - Unregister a device entry.
 *	@de: The entry to unregister.
 */

static void unregister (struct devfs_entry *de)
{
    struct devfs_entry *child;

    if ( (child = de->slave) != NULL )
    {
	de->slave = NULL;  /* Unhook first in case slave is parent directory */
	unregister (child);
    }
    if (de->registered)
    {
	devfsd_notify (de, DEVFSD_NOTIFY_UNREGISTERED, 0);
	free_dentries (de);
    }
    de->info = NULL;
    if ( S_ISCHR (de->mode) || S_ISBLK (de->mode) || S_ISREG (de->mode) )
    {
	de->registered = FALSE;
	de->u.fcb.ops = NULL;
	return;
    }
    if ( S_ISLNK (de->mode) )
    {
	de->registered = FALSE;
	if (de->u.symlink.linkname != NULL) kfree (de->u.symlink.linkname);
	de->u.symlink.linkname = NULL;
	return;
    }
    if ( S_ISFIFO (de->mode) )
    {
	de->registered = FALSE;
	return;
    }
    if (!de->registered) return;
    if ( !S_ISDIR (de->mode) )
    {
	printk ("%s: unregister(): unsupported type\n", DEVFS_NAME);
	return;
    }
    de->registered = FALSE;
    /*  Now recursively search the subdirectories: this is a stack chomper  */
    for (child = de->u.dir.first; child != NULL; child = child->next)
    {
#ifdef CONFIG_DEVFS_DEBUG
	if (devfs_debug & DEBUG_UNREGISTER)
	    printk ("%s: unregister(): child->name: \"%s\" child: %p\n",
		    DEVFS_NAME, child->name, child);
#endif
	unregister (child);
    }
}   /*  End Function unregister  */


/**
 *	devfs_unregister - Unregister a device entry.
 *	@de: A handle previously created by devfs_register() or returned from
 *		devfs_find_handle(). If this is %NULL the routine does nothing.
 */

void devfs_unregister (devfs_handle_t de)
{
    if (de == NULL) return;
#ifdef CONFIG_DEVFS_DEBUG
    if (devfs_debug & DEBUG_UNREGISTER)
	printk ("%s: devfs_unregister(): de->name: \"%s\" de: %p\n",
		DEVFS_NAME, de->name, de);
#endif
    unregister (de);
}   /*  End Function devfs_unregister  */


/**
 *	devfs_mk_symlink Create a symbolic link in the devfs namespace.
 *	@dir: The handle to the parent devfs directory entry. If this is %NULL the
 *		new name is relative to the root of the devfs.
 *	@name: The name of the entry.
 *	@flags: A set of bitwise-ORed flags (DEVFS_FL_*).
 *	@link: The destination name.
 *	@handle: The handle to the symlink entry is written here. This may be %NULL.
 *	@info: An arbitrary pointer which will be associated with the entry.
 *
 *	Returns 0 on success, else a negative error code is returned.
 */

int devfs_mk_symlink (devfs_handle_t dir, const char *name, unsigned int flags,
		      const char *link, devfs_handle_t *handle, void *info)
{
    int is_new;
    unsigned int linklength;
    char *newname;
    struct devfs_entry *de;

    if (handle != NULL) *handle = NULL;
    if (name == NULL)
    {
	printk ("%s: devfs_mk_symlink(): NULL name pointer\n", DEVFS_NAME);
	return -EINVAL;
    }
#ifdef CONFIG_DEVFS_DEBUG
    if (devfs_debug & DEBUG_REGISTER)
	printk ("%s: devfs_mk_symlink(%s)\n", DEVFS_NAME, name);
#endif
    if (link == NULL)
    {
	printk ("%s: devfs_mk_symlink(): NULL link pointer\n", DEVFS_NAME);
	return -EINVAL;
    }
    linklength = strlen (link);
    de = search_for_entry (dir, name, strlen (name), TRUE, TRUE, &is_new,
			   FALSE);
    if (de == NULL) return -ENOMEM;
    if (!S_ISLNK (de->mode) && de->registered)
    {
	printk ("%s: devfs_mk_symlink(): non-link entry already exists\n",
		DEVFS_NAME);
	return -EEXIST;
    }
    if (handle != NULL) *handle = de;
    de->mode = S_IFLNK | S_IRUGO | S_IXUGO;
    de->info = info;
    de->show_unreg = ( (boot_options & OPTION_SHOW)
		       || (flags & DEVFS_FL_SHOW_UNREG) ) ? TRUE : FALSE;
    de->hide = (flags & DEVFS_FL_HIDE) ? TRUE : FALSE;
    /*  Note there is no need to fiddle the dentry cache if the symlink changes
	as the symlink follow method is called every time it's needed  */
    if ( de->registered && (linklength == de->u.symlink.length) )
    {
	/*  New link is same length as old link  */
	if (memcmp (link, de->u.symlink.linkname, linklength) == 0) return 0;
	return -EEXIST;  /*  Contents would change  */
    }
    /*  Have to create/update  */
    if (de->registered) return -EEXIST;
    de->registered = TRUE;
    if ( ( newname = kmalloc (linklength + 1, GFP_KERNEL) ) == NULL )
    {
	struct devfs_entry *parent = de->parent;

	if (!is_new) return -ENOMEM;
	/*  Have to clean up  */
	if (de->prev == NULL) parent->u.dir.first = de->next;
	else de->prev->next = de->next;
	if (de->next == NULL) parent->u.dir.last = de->prev;
	else de->next->prev = de->prev;
	kfree (de);
	return -ENOMEM;
    }
    if (de->u.symlink.linkname != NULL) kfree (de->u.symlink.linkname);
    de->u.symlink.linkname = newname;
    memcpy (de->u.symlink.linkname, link, linklength);
    de->u.symlink.linkname[linklength] = '\0';
    de->u.symlink.length = linklength;
    return 0;
}   /*  End Function devfs_mk_symlink  */


/**
 *	devfs_mk_dir - Create a directory in the devfs namespace.
 *	@dir: The handle to the parent devfs directory entry. If this is %NULL the
 *		new name is relative to the root of the devfs.
 *	@name: The name of the entry.
 *	@info: An arbitrary pointer which will be associated with the entry.
 *
 *	Use of this function is optional. The devfs_register() function
 *	will automatically create intermediate directories as needed. This function
 *	is provided for efficiency reasons, as it provides a handle to a directory.
 *	Returns a handle which may later be used in a call to devfs_unregister().
 *	On failure %NULL is returned.
 */

devfs_handle_t devfs_mk_dir (devfs_handle_t dir, const char *name, void *info)
{
    int is_new;
    struct devfs_entry *de;

    if (name == NULL)
    {
	printk ("%s: devfs_mk_dir(): NULL name pointer\n", DEVFS_NAME);
	return NULL;
    }
    de = search_for_entry (dir, name, strlen (name), TRUE, TRUE, &is_new,
			   FALSE);
    if (de == NULL)
    {
	printk ("%s: devfs_mk_dir(): could not create entry: \"%s\"\n",
		DEVFS_NAME, name);
	return NULL;
    }
    if (!S_ISDIR (de->mode) && de->registered)
    {
	printk ("%s: devfs_mk_dir(): existing non-directory entry: \"%s\"\n",
		DEVFS_NAME, name);
	return NULL;
    }
#ifdef CONFIG_DEVFS_DEBUG
    if (devfs_debug & DEBUG_REGISTER)
	printk ("%s: devfs_mk_dir(%s): de: %p %s\n",
		DEVFS_NAME, name, de, is_new ? "new" : "existing");
#endif
    if (!S_ISDIR (de->mode) && !is_new)
    {
	/*  Transmogrifying an old entry  */
	de->u.dir.first = NULL;
	de->u.dir.last = NULL;
    }
    de->mode = S_IFDIR | S_IRUGO | S_IXUGO;
    de->info = info;
    if (!de->registered) de->u.dir.num_removable = 0;
    de->registered = TRUE;
    de->show_unreg = (boot_options & OPTION_SHOW) ? TRUE : FALSE;
    de->hide = FALSE;
    return de;
}   /*  End Function devfs_mk_dir  */


/**
 *	devfs_find_handle - Find the handle of a devfs entry.
 *	@dir: The handle to the parent devfs directory entry. If this is %NULL the
 *		name is relative to the root of the devfs.
 *	@name: The name of the entry.
 *	@major: The major number. This is used if @name is %NULL.
 *	@minor: The minor number. This is used if @name is %NULL.
 *	@type: The type of special file to search for. This may be either
 *		%DEVFS_SPECIAL_CHR or %DEVFS_SPECIAL_BLK.
 *	@traverse_symlinks: If %TRUE then symlink entries in the devfs namespace are
 *		traversed. Symlinks pointing out of the devfs namespace will cause a
 *		failure. Symlink traversal consumes stack space.
 *
 *	Returns a handle which may later be used in a call to devfs_unregister(),
 *	devfs_get_flags(), or devfs_set_flags(). On failure %NULL is returned.
 */

devfs_handle_t devfs_find_handle (devfs_handle_t dir, const char *name,
				  unsigned int major, unsigned int minor,
				  char type, int traverse_symlinks)
{
    devfs_handle_t de;

    if ( (name != NULL) && (name[0] == '\0') ) name = NULL;
    de = find_entry (dir, name, 0, major, minor, type,
		     traverse_symlinks);
    if (de == NULL) return NULL;
    if (!de->registered) return NULL;
    return de;
}   /*  End Function devfs_find_handle  */


/**
 *	devfs_get_flags - Get the flags for a devfs entry.
 *	@de: The handle to the device entry.
 *	@flags: The flags are written here.
 *
 *	Returns 0 on success, else a negative error code.
 */

int devfs_get_flags (devfs_handle_t de, unsigned int *flags)
{
    unsigned int fl = 0;

    if (de == NULL) return -EINVAL;
    if (!de->registered) return -ENODEV;
    if (de->show_unreg) fl |= DEVFS_FL_SHOW_UNREG;
    if (de->hide) fl |= DEVFS_FL_HIDE;
    if ( S_ISCHR (de->mode) || S_ISBLK (de->mode) || S_ISREG (de->mode) )
    {
	if (de->u.fcb.auto_owner) fl |= DEVFS_FL_AUTO_OWNER;
	if (de->u.fcb.aopen_notify) fl |= DEVFS_FL_AOPEN_NOTIFY;
	if (de->u.fcb.removable) fl |= DEVFS_FL_REMOVABLE;
    }
    *flags = fl;
    return 0;
}   /*  End Function devfs_get_flags  */


/*
 *	devfs_set_flags - Set the flags for a devfs entry.
 *	@de: The handle to the device entry.
 *	@flags: The flags to set. Unset flags are cleared.
 *
 *	Returns 0 on success, else a negative error code.
 */

int devfs_set_flags (devfs_handle_t de, unsigned int flags)
{
    if (de == NULL) return -EINVAL;
    if (!de->registered) return -ENODEV;
#ifdef CONFIG_DEVFS_DEBUG
    if (devfs_debug & DEBUG_SET_FLAGS)
	printk ("%s: devfs_set_flags(): de->name: \"%s\"\n",
		DEVFS_NAME, de->name);
#endif
    de->show_unreg = (flags & DEVFS_FL_SHOW_UNREG) ? TRUE : FALSE;
    de->hide = (flags & DEVFS_FL_HIDE) ? TRUE : FALSE;
    if ( S_ISCHR (de->mode) || S_ISBLK (de->mode) || S_ISREG (de->mode) )
    {
	de->u.fcb.auto_owner = (flags & DEVFS_FL_AUTO_OWNER) ? TRUE : FALSE;
	de->u.fcb.aopen_notify = (flags & DEVFS_FL_AOPEN_NOTIFY) ? TRUE:FALSE;
	if ( de->u.fcb.removable && !(flags & DEVFS_FL_REMOVABLE) )
	{
	    de->u.fcb.removable = FALSE;
	    --de->parent->u.dir.num_removable;
	}
	else if ( !de->u.fcb.removable && (flags & DEVFS_FL_REMOVABLE) )
	{
	    de->u.fcb.removable = TRUE;
	    ++de->parent->u.dir.num_removable;
	}
    }
    return 0;
}   /*  End Function devfs_set_flags  */


/**
 *	devfs_get_maj_min - Get the major and minor numbers for a devfs entry.
 *	@de: The handle to the device entry.
 *	@major: The major number is written here. This may be %NULL.
 *	@minor: The minor number is written here. This may be %NULL.
 *
 *	Returns 0 on success, else a negative error code.
 */

int devfs_get_maj_min (devfs_handle_t de, unsigned int *major,
		       unsigned int *minor)
{
    if (de == NULL) return -EINVAL;
    if (!de->registered) return -ENODEV;
    if ( S_ISDIR (de->mode) ) return -EISDIR;
    if ( !S_ISCHR (de->mode) && !S_ISBLK (de->mode) ) return -EINVAL;
    if (major != NULL) *major = de->u.fcb.u.device.major;
    if (minor != NULL) *minor = de->u.fcb.u.device.minor;
    return 0;
}   /*  End Function devfs_get_maj_min  */


/**
 *	devfs_get_handle_from_inode - Get the devfs handle for a VFS inode.
 *	@inode: The VFS inode.
 *
 *	Returns the devfs handle on success, else %NULL.
 */

devfs_handle_t devfs_get_handle_from_inode (struct inode *inode)
{
    if (!inode || !inode->i_sb) return NULL;
    if (inode->i_sb->s_magic != DEVFS_SUPER_MAGIC) return NULL;
    return get_devfs_entry_from_vfs_inode (inode);
}   /*  End Function devfs_get_handle_from_inode  */


/**
 *	devfs_generate_path - Generate a pathname for an entry, relative to the devfs root.
 *	@de: The devfs entry.
 *	@path: The buffer to write the pathname to. The pathname and '\0'
 *		terminator will be written at the end of the buffer.
 *	@buflen: The length of the buffer.
 *
 *	Returns the offset in the buffer where the pathname starts on success,
 *	else a negative error code.
 */

int devfs_generate_path (devfs_handle_t de, char *path, int buflen)
{
    int pos;

    if (de == NULL) return -EINVAL;
    if (de->namelen >= buflen) return -ENAMETOOLONG; /*  Must be first       */
    if (de->parent == NULL) return buflen;           /*  Don't prepend root  */
    pos = buflen - de->namelen - 1;
    memcpy (path + pos, de->name, de->namelen);
    path[buflen - 1] = '\0';
    for (de = de->parent; de->parent != NULL; de = de->parent)
    {
	if (pos - de->namelen - 1 < 0) return -ENAMETOOLONG;
	path[--pos] = '/';
	pos -= de->namelen;
	memcpy (path + pos, de->name, de->namelen);
    }
    return pos;
}   /*  End Function devfs_generate_path  */


/**
 *	devfs_get_ops - Get the device operations for a devfs entry.
 *	@de: The handle to the device entry.
 *
 *	Returns a pointer to the device operations on success, else NULL.
 */

void *devfs_get_ops (devfs_handle_t de)
{
    if (de == NULL) return NULL;
    if (!de->registered) return NULL;
    if ( S_ISCHR (de->mode) || S_ISBLK (de->mode) || S_ISREG (de->mode) )
	return de->u.fcb.ops;
    return NULL;
}   /*  End Function devfs_get_ops  */


/**
 *	devfs_set_file_size - Set the file size for a devfs regular file.
 *	@de: The handle to the device entry.
 *	@size: The new file size.
 *
 *	Returns 0 on success, else a negative error code.
 */

int devfs_set_file_size (devfs_handle_t de, unsigned long size)
{
    if (de == NULL) return -EINVAL;
    if (!de->registered) return -EINVAL;
    if ( !S_ISREG (de->mode) ) return -EINVAL;
    if (de->u.fcb.u.file.size == size) return 0;
    de->u.fcb.u.file.size = size;
    if (de->inode.dentry == NULL) return 0;
    if (de->inode.dentry->d_inode == NULL) return 0;
    de->inode.dentry->d_inode->i_size = size;
    return 0;
}   /*  End Function devfs_set_file_size  */


/**
 *	devfs_get_info - Get the info pointer written to private_data of @de upon open.
 *	@de: The handle to the device entry.
 *
 *	Returns the info pointer.
 */
void *devfs_get_info (devfs_handle_t de)
{
    if (de == NULL) return NULL;
    if (!de->registered) return NULL;
    return de->info;
}   /*  End Function devfs_get_info  */


/**
 *	devfs_set_info - Set the info pointer written to private_data upon open.
 *	@de: The handle to the device entry.
 *	@info: pointer to the data
 *
 *	Returns 0 on success, else a negative error code.
 */
int devfs_set_info (devfs_handle_t de, void *info)
{
    if (de == NULL) return -EINVAL;
    if (!de->registered) return -EINVAL;
    de->info = info;
    return 0;
}   /*  End Function devfs_set_info  */


/**
 *	devfs_get_parent - Get the parent device entry.
 *	@de: The handle to the device entry.
 *
 *	Returns the parent device entry if it exists, else %NULL.
 */
devfs_handle_t devfs_get_parent (devfs_handle_t de)
{
    if (de == NULL) return NULL;
    if (!de->registered) return NULL;
    return de->parent;
}   /*  End Function devfs_get_parent  */


/**
 *	devfs_get_first_child - Get the first leaf node in a directory.
 *	@de: The handle to the device entry.
 *
 *	Returns the leaf node device entry if it exists, else %NULL.
 */

devfs_handle_t devfs_get_first_child (devfs_handle_t de)
{
    if (de == NULL) return NULL;
    if (!de->registered) return NULL;
    if ( !S_ISDIR (de->mode) ) return NULL;
    return de->u.dir.first;
}   /*  End Function devfs_get_first_child  */


/**
 *	devfs_get_next_sibling - Get the next sibling leaf node. for a device entry.
 *	@de: The handle to the device entry.
 *
 *	Returns the leaf node device entry if it exists, else %NULL.
 */

devfs_handle_t devfs_get_next_sibling (devfs_handle_t de)
{
    if (de == NULL) return NULL;
    if (!de->registered) return NULL;
    return de->next;
}   /*  End Function devfs_get_next_sibling  */


/**
 *	devfs_auto_unregister - Configure a devfs entry to be automatically unregistered.
 *	@master: The master devfs entry. Only one slave may be registered.
 *	@slave: The devfs entry which will be automatically unregistered when the
 *		master entry is unregistered. It is illegal to call devfs_unregister()
 *		on this entry.
 */

void devfs_auto_unregister (devfs_handle_t master, devfs_handle_t slave)
{
    if (master == NULL) return;
    if (master->slave != NULL)
    {
	/*  Because of the dumbness of the layers above, ignore duplicates  */
	if (master->slave == slave) return;
	printk ("%s: devfs_auto_unregister(): only one slave allowed\n",
		DEVFS_NAME);
	OOPS ("  master: \"%s\"  old slave: \"%s\"  new slave: \"%s\"\n",
	      master->name, master->slave->name, slave->name);
    }
    master->slave = slave;
}   /*  End Function devfs_auto_unregister  */


/**
 *	devfs_get_unregister_slave - Get the slave entry which will be automatically unregistered.
 *	@master: The master devfs entry.
 *
 *	Returns the slave which will be unregistered when @master is unregistered.
 */

devfs_handle_t devfs_get_unregister_slave (devfs_handle_t master)
{
    if (master == NULL) return NULL;
    return master->slave;
}   /*  End Function devfs_get_unregister_slave  */


/**
 *	devfs_get_name - Get the name for a device entry in its parent directory.
 *	@de: The handle to the device entry.
 *	@namelen: The length of the name is written here. This may be %NULL.
 *
 *	Returns the name on success, else %NULL.
 */

const char *devfs_get_name (devfs_handle_t de, unsigned int *namelen)
{
    if (de == NULL) return NULL;
    if (!de->registered) return NULL;
    if (namelen != NULL) *namelen = de->namelen;
    return de->name;
}   /*  End Function devfs_get_name  */


/**
 *	devfs_register_chrdev - Optionally register a conventional character driver.
 *	@major: The major number for the driver.
 *	@name: The name of the driver (as seen in /proc/devices).
 *	@fops: The &file_operations structure pointer.
 *
 *	This function will register a character driver provided the "devfs=only"
 *	option was not provided at boot time.
 *	Returns 0 on success, else a negative error code on failure.
 */

int devfs_register_chrdev (unsigned int major, const char *name,
			   struct file_operations *fops)
{
    if (boot_options & OPTION_ONLY) return 0;
    return register_chrdev (major, name, fops);
}   /*  End Function devfs_register_chrdev  */


/**
 *	devfs_register_blkdev - Optionally register a conventional block driver.
 *	@major: The major number for the driver.
 *	@name: The name of the driver (as seen in /proc/devices).
 *	@bdops: The &block_device_operations structure pointer.
 *
 *	This function will register a block driver provided the "devfs=only"
 *	option was not provided at boot time.
 *	Returns 0 on success, else a negative error code on failure.
 */

int devfs_register_blkdev (unsigned int major, const char *name,
			   struct block_device_operations *bdops)
{
    if (boot_options & OPTION_ONLY) return 0;
    return register_blkdev (major, name, bdops);
}   /*  End Function devfs_register_blkdev  */


/**
 *	devfs_unregister_chrdev - Optionally unregister a conventional character driver.
 *	@major: The major number for the driver.
 *	@name: The name of the driver (as seen in /proc/devices).
 *
 *	This function will unregister a character driver provided the "devfs=only"
 *	option was not provided at boot time.
 *	Returns 0 on success, else a negative error code on failure.
 */

int devfs_unregister_chrdev (unsigned int major, const char *name)
{
    if (boot_options & OPTION_ONLY) return 0;
    return unregister_chrdev (major, name);
}   /*  End Function devfs_unregister_chrdev  */


/**
 *	devfs_unregister_blkdev - Optionally unregister a conventional block driver.
 *	@major: The major number for the driver.
 *	@name: The name of the driver (as seen in /proc/devices).
 *
 *	This function will unregister a block driver provided the "devfs=only"
 *	option was not provided at boot time.
 *	Returns 0 on success, else a negative error code on failure.
 */

int devfs_unregister_blkdev (unsigned int major, const char *name)
{
    if (boot_options & OPTION_ONLY) return 0;
    return unregister_blkdev (major, name);
}   /*  End Function devfs_unregister_blkdev  */

/**
 *	devfs_setup - Process kernel boot options.
 *	@str: The boot options after the "devfs=".
 */

static int __init devfs_setup (char *str)
{
    while ( (*str != '\0') && !isspace (*str) )
    {
#ifdef CONFIG_DEVFS_DEBUG
	if (strncmp (str, "dall", 4) == 0)
	{
	    devfs_debug_init |= DEBUG_ALL;
	    str += 4;
	}
	else if (strncmp (str, "dmod", 4) == 0)
	{
	    devfs_debug_init |= DEBUG_MODULE_LOAD;
	    str += 4;
	}
	else if (strncmp (str, "dreg", 4) == 0)
	{
	    devfs_debug_init |= DEBUG_REGISTER;
	    str += 4;
	}
	else if (strncmp (str, "dunreg", 6) == 0)
	{
	    devfs_debug_init |= DEBUG_UNREGISTER;
	    str += 6;
	}
	else if (strncmp (str, "diread", 6) == 0)
	{
	    devfs_debug_init |= DEBUG_I_READ;
	    str += 6;
	}
	else if (strncmp (str, "dchange", 7) == 0)
	{
	    devfs_debug_init |= DEBUG_SET_FLAGS;
	    str += 7;
	}
	else if (strncmp (str, "diwrite", 7) == 0)
	{
	    devfs_debug_init |= DEBUG_I_WRITE;
	    str += 7;
	}
	else if (strncmp (str, "dimknod", 7) == 0)
	{
	    devfs_debug_init |= DEBUG_I_MKNOD;
	    str += 7;
	}
	else if (strncmp (str, "dilookup", 8) == 0)
	{
	    devfs_debug_init |= DEBUG_I_LOOKUP;
	    str += 8;
	}
	else if (strncmp (str, "diunlink", 8) == 0)
	{
	    devfs_debug_init |= DEBUG_I_UNLINK;
	    str += 8;
	}
	else
#endif  /*  CONFIG_DEVFS_DEBUG  */
	if (strncmp (str, "show", 4) == 0)
	{
	    boot_options |= OPTION_SHOW;
	    str += 4;
	}
	else if (strncmp (str, "only", 4) == 0)
	{
	    boot_options |= OPTION_ONLY;
	    str += 4;
	}
	else if (strncmp (str, "mount", 5) == 0)
	{
	    boot_options &= ~OPTION_NOMOUNT;
	    str += 5;
	}
	else if (strncmp (str, "nomount", 7) == 0)
	{
	    boot_options |= OPTION_NOMOUNT;
	    str += 7;
	}
	else
	return 0;
	if (*str != ',') return 0;
	++str;
    }
    return 1;
}   /*  End Function devfs_setup  */

__setup("devfs=", devfs_setup);

EXPORT_SYMBOL(devfs_register);
EXPORT_SYMBOL(devfs_unregister);
EXPORT_SYMBOL(devfs_mk_symlink);
EXPORT_SYMBOL(devfs_mk_dir);
EXPORT_SYMBOL(devfs_find_handle);
EXPORT_SYMBOL(devfs_get_flags);
EXPORT_SYMBOL(devfs_set_flags);
EXPORT_SYMBOL(devfs_get_maj_min);
EXPORT_SYMBOL(devfs_get_handle_from_inode);
EXPORT_SYMBOL(devfs_generate_path);
EXPORT_SYMBOL(devfs_get_ops);
EXPORT_SYMBOL(devfs_set_file_size);
EXPORT_SYMBOL(devfs_get_info);
EXPORT_SYMBOL(devfs_set_info);
EXPORT_SYMBOL(devfs_get_parent);
EXPORT_SYMBOL(devfs_get_first_child);
EXPORT_SYMBOL(devfs_get_next_sibling);
EXPORT_SYMBOL(devfs_auto_unregister);
EXPORT_SYMBOL(devfs_get_unregister_slave);
EXPORT_SYMBOL(devfs_register_chrdev);
EXPORT_SYMBOL(devfs_register_blkdev);
EXPORT_SYMBOL(devfs_unregister_chrdev);
EXPORT_SYMBOL(devfs_unregister_blkdev);


/**
 *	try_modload - Notify devfsd of an inode lookup.
 *	@parent: The parent devfs entry.
 *	@fs_info: The filesystem info.
 *	@name: The device name.
 *	@namelen: The number of characters in @name.
 *	@buf: A working area that will be used. This must not go out of scope until
 *		devfsd is idle again.
 *
 *	Returns 0 on success, else a negative error code.
 */

static int try_modload (struct devfs_entry *parent, struct fs_info *fs_info,
			const char *name, unsigned namelen,
			char buf[STRING_LENGTH])
{
    int pos;

    if ( !( fs_info->devfsd_event_mask & (1 << DEVFSD_NOTIFY_LOOKUP) ) )
	return -ENOENT;
    if ( is_devfsd_or_child (fs_info) ) return -ENOENT;
    if (namelen >= STRING_LENGTH) return -ENAMETOOLONG;
    memcpy (buf + STRING_LENGTH - namelen - 1, name, namelen);
    buf[STRING_LENGTH - 1] = '\0';
    pos = devfs_generate_path (parent, buf, STRING_LENGTH - namelen - 1);
    if (pos < 0) return pos;
    buf[STRING_LENGTH - namelen - 2] = '/';
    if ( !devfsd_notify_one (buf + pos, DEVFSD_NOTIFY_LOOKUP, 0,
			     current->euid, current->egid, fs_info) )
	return -ENOENT;
    /*  Possible success  */
    return 0;
}   /*  End Function try_modload  */


/**
 *	check_disc_changed - Check if a removable disc was changed.
 *	@de: The device.
 *
 *	Returns 1 if the media was changed, else 0.
 */

static int check_disc_changed (struct devfs_entry *de)
{
    int tmp;
    kdev_t dev = MKDEV (de->u.fcb.u.device.major, de->u.fcb.u.device.minor);
    struct block_device_operations *bdops = de->u.fcb.ops;
    struct super_block * sb;
    extern int warn_no_part;

    if ( !S_ISBLK (de->mode) ) return 0;
    if (bdops == NULL) return 0;
    if (bdops->check_media_change == NULL) return 0;
    if ( !bdops->check_media_change (dev) ) return 0;
    printk ( KERN_DEBUG "VFS: Disk change detected on device %s\n",
	     kdevname (dev) );
    sb = get_super (dev);
    if ( sb && invalidate_inodes (sb) )
	printk("VFS: busy inodes on changed media..\n");
    invalidate_buffers (dev);
    /*  Ugly hack to disable messages about unable to read partition table  */
    tmp = warn_no_part;
    warn_no_part = 0;
    if (bdops->revalidate) bdops->revalidate (dev);
    warn_no_part = tmp;
    return 1;
}   /*  End Function check_disc_changed  */


/**
 *	scan_dir_for_removable - Scan a directory for removable media devices and check media.
 *	@dir: The directory.
 */

static void scan_dir_for_removable (struct devfs_entry *dir)
{
    struct devfs_entry *de;

    if (dir->u.dir.num_removable < 1) return;
    for (de = dir->u.dir.first; de != NULL; de = de->next)
    {
	if (!de->registered) continue;
	if ( !S_ISBLK (de->mode) ) continue;
	if (!de->u.fcb.removable) continue;
	check_disc_changed (de);
    }
}   /*  End Function scan_dir_for_removable  */

/**
 *	get_removable_partition - Get removable media partition.
 *	@dir: The parent directory.
 *	@name: The name of the entry.
 *	@namelen: The number of characters in <<name>>.
 *
 *	Returns 1 if the media was changed, else 0.
 */

static int get_removable_partition (struct devfs_entry *dir, const char *name,
				    unsigned int namelen)
{
    struct devfs_entry *de;

    for (de = dir->u.dir.first; de != NULL; de = de->next)
    {
	if (!de->registered) continue;
	if ( !S_ISBLK (de->mode) ) continue;
	if (!de->u.fcb.removable) continue;
	if (strcmp (de->name, "disc") == 0) return check_disc_changed (de);
	/*  Support for names where the partition is appended to the disc name
	 */
	if (de->namelen >= namelen) continue;
	if (strncmp (de->name, name, de->namelen) != 0) continue;
	return check_disc_changed (de);
    }
    return 0;
}   /*  End Function get_removable_partition  */


/*  Superblock operations follow  */

static struct inode_operations devfs_iops;
static struct inode_operations devfs_dir_iops;
static struct file_operations devfs_fops;
static struct inode_operations devfs_symlink_iops;

static void devfs_read_inode (struct inode *inode)
{
    struct devfs_entry *de;

    de = get_devfs_entry_from_vfs_inode (inode);
    if (de == NULL)
    {
	printk ("%s: read_inode(%d): VFS inode: %p  NO devfs_entry\n",
		DEVFS_NAME, (int) inode->i_ino, inode);
	return;
    }
#ifdef CONFIG_DEVFS_DEBUG
    if (devfs_debug & DEBUG_I_READ)
	printk ("%s: read_inode(%d): VFS inode: %p  devfs_entry: %p\n",
		DEVFS_NAME, (int) inode->i_ino, inode, de);
#endif
    inode->i_blocks = 0;
    inode->i_blksize = 1024;
    inode->i_op = &devfs_iops;
    inode->i_fop = &devfs_fops;
    inode->i_rdev = NODEV;
    if ( S_ISCHR (de->inode.mode) )
    {
	inode->i_rdev = MKDEV (de->u.fcb.u.device.major,
			       de->u.fcb.u.device.minor);
    }
    else if ( S_ISBLK (de->inode.mode) )
    {
	inode->i_rdev = MKDEV (de->u.fcb.u.device.major,
			       de->u.fcb.u.device.minor);
	inode->i_bdev = bdget (inode->i_rdev);
	if (inode->i_bdev)
	{
	    if (!inode->i_bdev->bd_op && de->u.fcb.ops)
		inode->i_bdev->bd_op = de->u.fcb.ops;
	}
	else printk ("%s: read_inode(%d): no block device from bdget()\n",
		     DEVFS_NAME, (int) inode->i_ino);
    }
    else if ( S_ISFIFO (de->inode.mode) ) inode->i_fop = &def_fifo_fops;
    else if ( S_ISREG (de->inode.mode) ) inode->i_size = de->u.fcb.u.file.size;
    else if ( S_ISDIR (de->inode.mode) ) inode->i_op = &devfs_dir_iops;
    else if ( S_ISLNK (de->inode.mode) )
    {
	inode->i_op = &devfs_symlink_iops;
	inode->i_size = de->u.symlink.length;
    }
    inode->i_mode = de->inode.mode;
    inode->i_uid = de->inode.uid;
    inode->i_gid = de->inode.gid;
    inode->i_atime = de->inode.atime;
    inode->i_mtime = de->inode.mtime;
    inode->i_ctime = de->inode.ctime;
    inode->i_nlink = de->inode.nlink;
#ifdef CONFIG_DEVFS_DEBUG
    if (devfs_debug & DEBUG_I_READ)
	printk ("%s:   mode: 0%o  uid: %d  gid: %d\n",
		DEVFS_NAME, (int) inode->i_mode,
		(int) inode->i_uid, (int) inode->i_gid);
#endif
}   /*  End Function devfs_read_inode  */

static void devfs_write_inode (struct inode *inode, int wait)
{
    int index;
    struct devfs_entry *de;
    struct fs_info *fs_info = inode->i_sb->u.generic_sbp;

    if (inode->i_ino < FIRST_INODE) return;
    index = inode->i_ino - FIRST_INODE;
    lock_kernel();
    if (index >= fs_info->num_inodes)
    {
	printk ("%s: writing inode: %lu for which there is no entry!\n",
		DEVFS_NAME, inode->i_ino);
        unlock_kernel();
	return;
    }
    de = fs_info->table[index];
#ifdef CONFIG_DEVFS_DEBUG
    if (devfs_debug & DEBUG_I_WRITE)
    {
	printk ("%s: write_inode(%d): VFS inode: %p  devfs_entry: %p\n",
		DEVFS_NAME, (int) inode->i_ino, inode, de);
	printk ("%s:   mode: 0%o  uid: %d  gid: %d\n",
		DEVFS_NAME, (int) inode->i_mode,
		(int) inode->i_uid, (int) inode->i_gid);
    }
#endif
    de->inode.mode = inode->i_mode;
    de->inode.uid = inode->i_uid;
    de->inode.gid = inode->i_gid;
    de->inode.atime = inode->i_atime;
    de->inode.mtime = inode->i_mtime;
    de->inode.ctime = inode->i_ctime;
    unlock_kernel();
}   /*  End Function devfs_write_inode  */

static int devfs_notify_change (struct dentry *dentry, struct iattr *iattr)
{
    int retval;
    struct devfs_entry *de;
    struct inode *inode = dentry->d_inode;
    struct fs_info *fs_info = inode->i_sb->u.generic_sbp;

    de = get_devfs_entry_from_vfs_inode (inode);
    if (de == NULL) return -ENODEV;
    retval = inode_change_ok (inode, iattr);
    if (retval != 0) return retval;
    inode_setattr (inode, iattr);
    if ( iattr->ia_valid & (ATTR_MODE | ATTR_UID | ATTR_GID) )
	devfsd_notify_one (de, DEVFSD_NOTIFY_CHANGE, inode->i_mode,
			   inode->i_uid, inode->i_gid, fs_info);
    return 0;
}   /*  End Function devfs_notify_change  */

static int devfs_statfs (struct super_block *sb, struct statfs *buf)
{
    buf->f_type = DEVFS_SUPER_MAGIC;
    buf->f_bsize = PAGE_SIZE / sizeof (long);
    buf->f_bfree = 0;
    buf->f_bavail = 0;
    buf->f_ffree = 0;
    buf->f_namelen = NAME_MAX;
    return 0;
}   /*  End Function devfs_statfs  */

static struct super_operations devfs_sops =
{ 
    read_inode:    devfs_read_inode,
    write_inode:   devfs_write_inode,
    statfs:        devfs_statfs,
};


/**
 *	get_vfs_inode - Get a VFS inode.
 *	@sb: The super block.
 *	@de: The devfs inode.
 *	@dentry: The dentry to register with the devfs inode.
 *
 *	Returns the inode on success, else %NULL.
 */

static struct inode *get_vfs_inode (struct super_block *sb,
				    struct devfs_entry *de,
				    struct dentry *dentry)
{
    struct inode *inode;

    if (de->inode.dentry != NULL)
    {
	printk ("%s: get_vfs_inode(%u): old de->inode.dentry: %p \"%s\"  new dentry: %p \"%s\"\n",
		DEVFS_NAME, de->inode.ino,
		de->inode.dentry, de->inode.dentry->d_name.name,
		dentry, dentry->d_name.name);
	printk ("  old inode: %p\n", de->inode.dentry->d_inode);
	return NULL;
    }
    if ( ( inode = iget (sb, de->inode.ino) ) == NULL ) return NULL;
    de->inode.dentry = dentry;
    return inode;
}   /*  End Function get_vfs_inode  */


/*  File operations for device entries follow  */

static ssize_t devfs_read (struct file *file, char *buf, size_t len, loff_t *ppos)
{
    if ( S_ISDIR (file->f_dentry->d_inode->i_mode) ) return -EISDIR;
    return -EINVAL;
}   /*  End Function devfs_read  */

static int devfs_readdir (struct file *file, void *dirent, filldir_t filldir)
{
    int err, count;
    int stored = 0;
    struct fs_info *fs_info;
    struct devfs_entry *parent, *de;
    struct inode *inode = file->f_dentry->d_inode;

    if ( !S_ISDIR (inode->i_mode) )
    {
	printk ("%s: readdir(): inode is not a directory\n", DEVFS_NAME);
	return -ENOTDIR;
    }
    fs_info = inode->i_sb->u.generic_sbp;
    parent = get_devfs_entry_from_vfs_inode (file->f_dentry->d_inode);
    if ( (long) file->f_pos < 0 ) return -EINVAL;
#ifdef CONFIG_DEVFS_DEBUG
    if (devfs_debug & DEBUG_F_READDIR)
	printk ("%s: readdir(): fs_info: %p  pos: %ld\n", DEVFS_NAME,
		fs_info, (long) file->f_pos);
#endif
    switch ( (long) file->f_pos )
    {
      case 0:
	scan_dir_for_removable (parent);
	err = (*filldir) (dirent, "..", 2, file->f_pos,
			  file->f_dentry->d_parent->d_inode->i_ino, DT_DIR);
	if (err == -EINVAL) break;
	if (err < 0) return err;
	file->f_pos++;
	++stored;
	/*  Fall through  */
      case 1:
	err = (*filldir) (dirent, ".", 1, file->f_pos, inode->i_ino, DT_DIR);
	if (err == -EINVAL) break;
	if (err < 0) return err;
	file->f_pos++;
	++stored;
	/*  Fall through  */
      default:
	/*  Skip entries  */
	count = file->f_pos - 2;
	for (de = parent->u.dir.first; (de != NULL) && (count > 0);
	     de = de->next)
	    if ( !IS_HIDDEN (de) ) --count;
	/*  Now add all remaining entries  */
	for (; de != NULL; de = de->next)
	{
	    if ( IS_HIDDEN (de) ) continue;
	    err = (*filldir) (dirent, de->name, de->namelen,
			      file->f_pos, de->inode.ino, de->mode >> 12);
	    if (err == -EINVAL) break;
	    if (err < 0) return err;
	    file->f_pos++;
	    ++stored;
	}
	break;
    }
    return stored;
}   /*  End Function devfs_readdir  */

static int devfs_open (struct inode *inode, struct file *file)
{
    int err;
    struct fcb_type *df;
    struct devfs_entry *de;
    struct fs_info *fs_info = inode->i_sb->u.generic_sbp;

    lock_kernel();
    de = get_devfs_entry_from_vfs_inode (inode);
    err = -ENODEV;
    if (de == NULL)
	goto out;
    err = 0;
    if ( S_ISDIR (de->mode) )
	goto out;
    df = &de->u.fcb;
    err = -ENODEV;
    if (!de->registered)
	goto out;
    file->private_data = de->info;
    if ( S_ISBLK (inode->i_mode) )
    {
	file->f_op = &def_blk_fops;
	if (df->ops) inode->i_bdev->bd_op = df->ops;
    }
    else file->f_op = fops_get((struct file_operations*)df->ops);
    if (file->f_op)
	err = file->f_op->open ? (*file->f_op->open) (inode, file) : 0;
    else
    {
	/*  Fallback to legacy scheme  */
	/*
	 * Do we need it? Richard, could you verify it?
	 * It can legitimately happen if
	 *	it is a character device and 
	 *	df->ops == NULL and
	 *	de->registered is true,
	 * but AFAICS it can't happen - in devfs_register() we never set
	 * ->ops to NULL, in unregister() we set ->registered to false,
	 * in devfs_mknod() we set it to NULL only if ->register is false.
	 *
	 * Looks like this fallback is not needed at all.
	 *							AV
	 */
	if ( S_ISCHR (inode->i_mode) ) err = chrdev_open (inode, file);
	else err = -ENODEV;
    }
    if (err < 0) goto out;
    /*  Open was successful  */
    err = 0;
    if (df->open) goto out;
    df->open = TRUE;  /*  This is the first open  */
    if (df->auto_owner)
    {
	/*  Change the ownership/protection  */
	de->inode.mode = (de->inode.mode & ~S_IALLUGO) |(de->mode & S_IRWXUGO);
	de->inode.uid = current->euid;
	de->inode.gid = current->egid;
	inode->i_mode = de->inode.mode;
	inode->i_uid = de->inode.uid;
	inode->i_gid = de->inode.gid;
    }
    if (df->aopen_notify)
	devfsd_notify_one (de, DEVFSD_NOTIFY_ASYNC_OPEN, inode->i_mode,
			   current->euid, current->egid, fs_info);
out:
    unlock_kernel();
    return err;
}   /*  End Function devfs_open  */

static struct file_operations devfs_fops =
{
    read: devfs_read,
    readdir: devfs_readdir,
    open: devfs_open,
};


/*  Dentry operations for device entries follow  */


/**
 *	devfs_d_release - Callback for when a dentry is freed.
 *	@dentry: The dentry.
 */

static void devfs_d_release (struct dentry *dentry)
{
#ifdef CONFIG_DEVFS_DEBUG
    struct inode *inode = dentry->d_inode;

    if (devfs_debug & DEBUG_D_RELEASE)
	printk ("%s: d_release(): dentry: %p inode: %p\n",
		DEVFS_NAME, dentry, inode);
#endif
}   /*  End Function devfs_d_release  */

/**
 *	devfs_d_iput - Callback for when a dentry loses its inode.
 *	@dentry: The dentry.
 *	@inode:	The inode.
 */

static void devfs_d_iput (struct dentry *dentry, struct inode *inode)
{
    struct devfs_entry *de;

    lock_kernel ();
    de = get_devfs_entry_from_vfs_inode (inode);
#ifdef CONFIG_DEVFS_DEBUG
    if (devfs_debug & DEBUG_D_IPUT)
	printk ("%s: d_iput(): dentry: %p inode: %p de: %p  de->dentry: %p\n",
		DEVFS_NAME, dentry, inode, de, de->inode.dentry);
#endif
    if (de->inode.dentry == dentry) de->inode.dentry = NULL;
    unlock_kernel ();
    iput (inode);
}   /*  End Function devfs_d_iput  */

static int devfs_d_delete (struct dentry *dentry);

static struct dentry_operations devfs_dops =
{
    d_delete:     devfs_d_delete,
    d_release:    devfs_d_release,
    d_iput:       devfs_d_iput,
};

static int devfs_d_revalidate_wait (struct dentry *dentry, int flags);

static struct dentry_operations devfs_wait_dops =
{
    d_delete:     devfs_d_delete,
    d_release:    devfs_d_release,
    d_iput:       devfs_d_iput,
    d_revalidate: devfs_d_revalidate_wait,
};

/**
 *	devfs_d_delete - Callback for when all files for a dentry are closed.
 *	@dentry: The dentry.
 */

static int devfs_d_delete (struct dentry *dentry)
{
    struct inode *inode = dentry->d_inode;
    struct devfs_entry *de;
    struct fs_info *fs_info;

    if (dentry->d_op == &devfs_wait_dops) dentry->d_op = &devfs_dops;
    /*  Unhash dentry if negative (has no inode)  */
    if (inode == NULL)
    {
#ifdef CONFIG_DEVFS_DEBUG
	if (devfs_debug & DEBUG_D_DELETE)
	    printk ("%s: d_delete(): dropping negative dentry: %p\n",
		    DEVFS_NAME, dentry);
#endif
	return 1;
    }
    fs_info = inode->i_sb->u.generic_sbp;
    de = get_devfs_entry_from_vfs_inode (inode);
#ifdef CONFIG_DEVFS_DEBUG
    if (devfs_debug & DEBUG_D_DELETE)
	printk ("%s: d_delete(): dentry: %p  inode: %p  devfs_entry: %p\n",
		DEVFS_NAME, dentry, inode, de);
#endif
    if (de == NULL) return 0;
    if ( !S_ISCHR (de->mode) && !S_ISBLK (de->mode) && !S_ISREG (de->mode) )
	return 0;
    if (!de->u.fcb.open) return 0;
    de->u.fcb.open = FALSE;
    if (de->u.fcb.aopen_notify)
	devfsd_notify_one (de, DEVFSD_NOTIFY_CLOSE, inode->i_mode,
			   current->euid, current->egid, fs_info);
    if (!de->u.fcb.auto_owner) return 0;
    /*  Change the ownership/protection back  */
    de->inode.mode = (de->inode.mode & ~S_IALLUGO) | S_IRUGO | S_IWUGO;
    de->inode.uid = de->u.fcb.default_uid;
    de->inode.gid = de->u.fcb.default_gid;
    inode->i_mode = de->inode.mode;
    inode->i_uid = de->inode.uid;
    inode->i_gid = de->inode.gid;
    return 0;
}   /*  End Function devfs_d_delete  */

static int devfs_d_revalidate_wait (struct dentry *dentry, int flags)
{
    devfs_handle_t de = dentry->d_fsdata;
    struct inode *dir;
    struct fs_info *fs_info;

    lock_kernel ();
    dir = dentry->d_parent->d_inode;
    fs_info = dir->i_sb->u.generic_sbp;
    if (!de || de->registered)
    {
	if ( !dentry->d_inode && is_devfsd_or_child (fs_info) )
	{
	    struct inode *inode;

#ifdef CONFIG_DEVFS_DEBUG
	    char txt[STRING_LENGTH];

	    memset (txt, 0, STRING_LENGTH);
	    memcpy (txt, dentry->d_name.name,
		    (dentry->d_name.len >= STRING_LENGTH) ?
		    (STRING_LENGTH - 1) : dentry->d_name.len);
	    if (devfs_debug & DEBUG_I_LOOKUP)
		printk ("%s: d_revalidate(): dentry: %p name: \"%s\" by: \"%s\"\n",
			DEVFS_NAME, dentry, txt, current->comm);
#endif
	    if (de == NULL)
	    {
		devfs_handle_t parent;

		parent = get_devfs_entry_from_vfs_inode (dir);
		de = search_for_entry_in_dir (parent, dentry->d_name.name,
					      dentry->d_name.len, FALSE);
	    }
	    if (de == NULL) goto out;
	    /*  Create an inode, now that the driver information is available
	     */
	    if (de->no_persistence) update_devfs_inode_from_entry (de);
	    else if (de->inode.ctime == 0) update_devfs_inode_from_entry (de);
	    else de->inode.mode =
		     (de->mode & ~S_IALLUGO) | (de->inode.mode & S_IALLUGO);
	    if ( ( inode = get_vfs_inode (dir->i_sb, de, dentry) ) == NULL )
		goto out;
#ifdef CONFIG_DEVFS_DEBUG
	    if (devfs_debug & DEBUG_I_LOOKUP)
		printk ("%s: d_revalidate(): new VFS inode(%u): %p  devfs_entry: %p\n",
			DEVFS_NAME, de->inode.ino, inode, de);
#endif
	    d_instantiate (dentry, inode);
	    goto out;
	}
    }
    if ( wait_for_devfsd_finished (fs_info) ) dentry->d_op = &devfs_dops;
out:
    unlock_kernel ();
    return 1;
}   /*  End Function devfs_d_revalidate_wait  */


/*  Inode operations for device entries follow  */

static struct dentry *devfs_lookup (struct inode *dir, struct dentry *dentry)
{
    struct fs_info *fs_info;
    struct devfs_entry *parent, *de;
    struct inode *inode;
    char txt[STRING_LENGTH];

    /*  Set up the dentry operations before anything else, to ensure cleaning
	up on any error  */
    dentry->d_op = &devfs_dops;
    if (dir == NULL)
    {
	printk ("%s: lookup(): NULL directory inode\n", DEVFS_NAME);
	return ERR_PTR (-ENOTDIR);
    }
    if ( !S_ISDIR (dir->i_mode) ) return ERR_PTR (-ENOTDIR);
    memset (txt, 0, STRING_LENGTH);
    memcpy (txt, dentry->d_name.name,
	    (dentry->d_name.len >= STRING_LENGTH) ?
	    (STRING_LENGTH - 1) : dentry->d_name.len);
#ifdef CONFIG_DEVFS_DEBUG
    if (devfs_debug & DEBUG_I_LOOKUP)
	printk ("%s: lookup(%s): dentry: %p by: \"%s\"\n",
		DEVFS_NAME, txt, dentry, current->comm);
#endif
    fs_info = dir->i_sb->u.generic_sbp;
    /*  First try to get the devfs entry for this directory  */
    parent = get_devfs_entry_from_vfs_inode (dir);
    if (parent == NULL) return ERR_PTR (-EINVAL);
    if (!parent->registered) return ERR_PTR (-ENOENT);
    /*  Try to reclaim an existing devfs entry  */
    de = search_for_entry_in_dir (parent,
				  dentry->d_name.name, dentry->d_name.len,
				  FALSE);
    if ( ( (de == NULL) || !de->registered ) &&
	 (parent->u.dir.num_removable > 0) &&
	 get_removable_partition (parent, dentry->d_name.name,
				  dentry->d_name.len) )
    {
	if (de == NULL)
	    de = search_for_entry_in_dir (parent, dentry->d_name.name,
					  dentry->d_name.len, FALSE);
    }
    if ( (de == NULL) || !de->registered )
    {
	/*  Try with devfsd. For any kind of failure, leave a negative dentry
	    so someone else can deal with it (in the case where the sysadmin
	    does a mknod()). It's important to do this before hashing the
	    dentry, so that the devfsd queue is filled before revalidates
	    can start  */
	if (try_modload (parent, fs_info,
			 dentry->d_name.name, dentry->d_name.len, txt) < 0)
	{
	    d_add (dentry, NULL);
	    return NULL;
	}
	/*  devfsd claimed success  */
	dentry->d_op = &devfs_wait_dops;
	dentry->d_fsdata = de;
	d_add (dentry, NULL);  /*  Open the floodgates  */
	/*  Unlock directory semaphore, which will release any waiters. They
	    will get the hashed dentry, and may be forced to wait for
	    revalidation  */
	up (&dir->i_sem);
	devfs_d_revalidate_wait (dentry, 0);  /*  I might have to wait too  */
	down (&dir->i_sem);      /*  Grab it again because them's the rules  */
	/*  If someone else has been so kind as to make the inode, we go home
	    early  */
	if (dentry->d_inode) return NULL;
	if (de && !de->registered) return NULL;
	if (de == NULL)
	    de = search_for_entry_in_dir (parent, dentry->d_name.name,
					  dentry->d_name.len, FALSE);
	if (de == NULL) return NULL;
	/*  OK, there's an entry now, but no VFS inode yet  */
    }
    else
    {
	dentry->d_op = &devfs_wait_dops;
	d_add (dentry, NULL);  /*  Open the floodgates  */
    }
    /*  Create an inode, now that the driver information is available  */
    if (de->no_persistence) update_devfs_inode_from_entry (de);
    else if (de->inode.ctime == 0) update_devfs_inode_from_entry (de);
    else de->inode.mode =
	     (de->mode & ~S_IALLUGO) | (de->inode.mode & S_IALLUGO);
    if ( ( inode = get_vfs_inode (dir->i_sb, de, dentry) ) == NULL )
	return ERR_PTR (-ENOMEM);
#ifdef CONFIG_DEVFS_DEBUG
    if (devfs_debug & DEBUG_I_LOOKUP)
	printk ("%s: lookup(): new VFS inode(%u): %p  devfs_entry: %p\n",
		DEVFS_NAME, de->inode.ino, inode, de);
#endif
    d_instantiate (dentry, inode);
    /*  Unlock directory semaphore, which will release any waiters. They will
	get the hashed dentry, and may be forced to wait for revalidation  */
    up (&dir->i_sem);
    if (dentry->d_op == &devfs_wait_dops)
	devfs_d_revalidate_wait (dentry, 0);  /*  I might have to wait too  */
    down (&dir->i_sem);          /*  Grab it again because them's the rules  */
    return NULL;
}   /*  End Function devfs_lookup  */

static int devfs_link (struct dentry *old_dentry, struct inode *dir,
		       struct dentry *dentry)
{
    /*struct inode *inode = old_dentry->d_inode;*/
    char txt[STRING_LENGTH];

    memset (txt, 0, STRING_LENGTH);
    memcpy (txt, old_dentry->d_name.name, old_dentry->d_name.len);
    txt[STRING_LENGTH - 1] = '\0';
    printk ("%s: link of \"%s\"\n", DEVFS_NAME, txt);
    return -EPERM;
}   /*  End Function devfs_link  */

static int devfs_unlink (struct inode *dir, struct dentry *dentry)
{
    struct devfs_entry *de;

#ifdef CONFIG_DEVFS_DEBUG
    char txt[STRING_LENGTH];

    if (devfs_debug & DEBUG_I_UNLINK)
    {
	memset (txt, 0, STRING_LENGTH);
	memcpy (txt, dentry->d_name.name, dentry->d_name.len);
	txt[STRING_LENGTH - 1] = '\0';
	printk ("%s: unlink(%s)\n", DEVFS_NAME, txt);
    }
#endif

    if ( !dir || !S_ISDIR (dir->i_mode) ) return -ENOTDIR;
    if (!dentry || !dentry->d_inode) return -ENOENT;
    de = get_devfs_entry_from_vfs_inode (dentry->d_inode);
    if (de == NULL) return -ENOENT;
    if (!de->registered) return -ENOENT;
    de->registered = FALSE;
    de->hide = TRUE;
    free_dentries (de);
    return 0;
}   /*  End Function devfs_unlink  */

static int devfs_symlink (struct inode *dir, struct dentry *dentry,
			  const char *symname)
{
    int err;
    struct fs_info *fs_info;
    struct devfs_entry *parent, *de;
    struct inode *inode;

    if ( !dir || !S_ISDIR (dir->i_mode) ) return -ENOTDIR;
    fs_info = dir->i_sb->u.generic_sbp;
    /*  First try to get the devfs entry for this directory  */
    parent = get_devfs_entry_from_vfs_inode (dir);
    if (parent == NULL) return -EINVAL;
    if (!parent->registered) return -ENOENT;
    err = devfs_mk_symlink (parent, dentry->d_name.name, DEVFS_FL_NONE,
			    symname, &de, NULL);
#ifdef CONFIG_DEVFS_DEBUG
    if (devfs_debug & DEBUG_DISABLED)
	printk ("%s: symlink(): errcode from <devfs_mk_symlink>: %d\n",
		DEVFS_NAME, err);
#endif
    if (err < 0) return err;
    de->inode.mode = de->mode;
    de->inode.atime = CURRENT_TIME;
    de->inode.mtime = CURRENT_TIME;
    de->inode.ctime = CURRENT_TIME;
    if ( ( inode = get_vfs_inode (dir->i_sb, de, dentry) ) == NULL )
	return -ENOMEM;
#ifdef CONFIG_DEVFS_DEBUG
    if (devfs_debug & DEBUG_DISABLED)
	printk ("%s: symlink(): new VFS inode(%u): %p  dentry: %p\n",
		DEVFS_NAME, de->inode.ino, inode, dentry);
#endif
    de->hide = FALSE;
    d_instantiate (dentry, inode);
    devfsd_notify_one (de, DEVFSD_NOTIFY_CREATE, inode->i_mode,
		       inode->i_uid, inode->i_gid, fs_info);
    return 0;
}   /*  End Function devfs_symlink  */

static int devfs_mkdir (struct inode *dir, struct dentry *dentry, int mode)
{
    int is_new;
    struct fs_info *fs_info;
    struct devfs_entry *parent, *de;
    struct inode *inode;

    mode = (mode & ~S_IFMT) | S_IFDIR;
    if ( !dir || !S_ISDIR (dir->i_mode) ) return -ENOTDIR;
    fs_info = dir->i_sb->u.generic_sbp;
    /*  We are allowed to create the directory  */
    /*  First try to get the devfs entry for this directory  */
    parent = get_devfs_entry_from_vfs_inode (dir);
    if (parent == NULL) return -EINVAL;
    if (!parent->registered) return -ENOENT;
    /*  Try to reclaim an existing devfs entry, create if there isn't one  */
    de = search_for_entry (parent, dentry->d_name.name, dentry->d_name.len,
			   FALSE, TRUE, &is_new, FALSE);
    if (de == NULL) return -ENOMEM;
    if (de->registered)
    {
	printk ("%s: mkdir(): existing entry\n", DEVFS_NAME);
	return -EEXIST;
    }
    de->registered = TRUE;
    de->hide = FALSE;
    if (!S_ISDIR (de->mode) && !is_new)
    {
	/*  Transmogrifying an old entry  */
	de->u.dir.first = NULL;
	de->u.dir.last = NULL;
    }
    de->mode = mode;
    de->u.dir.num_removable = 0;
    de->inode.mode = mode;
    de->inode.uid = current->euid;
    de->inode.gid = current->egid;
    de->inode.atime = CURRENT_TIME;
    de->inode.mtime = CURRENT_TIME;
    de->inode.ctime = CURRENT_TIME;
    if ( ( inode = get_vfs_inode (dir->i_sb, de, dentry) ) == NULL )
	return -ENOMEM;
#ifdef CONFIG_DEVFS_DEBUG
    if (devfs_debug & DEBUG_DISABLED)
	printk ("%s: mkdir(): new VFS inode(%u): %p  dentry: %p\n",
		DEVFS_NAME, de->inode.ino, inode, dentry);
#endif
    d_instantiate (dentry, inode);
    devfsd_notify_one (de, DEVFSD_NOTIFY_CREATE, inode->i_mode,
		       inode->i_uid, inode->i_gid, fs_info);
    return 0;
}   /*  End Function devfs_mkdir  */

static int devfs_rmdir (struct inode *dir, struct dentry *dentry)
{
    int has_children = FALSE;
    struct fs_info *fs_info;
    struct devfs_entry *de, *child;
    struct inode *inode = dentry->d_inode;

    if ( !dir || !S_ISDIR (dir->i_mode) ) return -ENOTDIR;
    if (dir->i_sb->u.generic_sbp != inode->i_sb->u.generic_sbp) return -EINVAL;
    if (inode == dir) return -EPERM;
    fs_info = dir->i_sb->u.generic_sbp;
    de = get_devfs_entry_from_vfs_inode (inode);
    if (de == NULL) return -ENOENT;
    if (!de->registered) return -ENOENT;
    if ( !S_ISDIR (de->mode) ) return -ENOTDIR;
    for (child = de->u.dir.first; child != NULL; child = child->next)
    {
	if (child->registered)
	{
	    has_children = TRUE;
	    break;
	}
    }
    if (has_children) return -ENOTEMPTY;
    de->registered = FALSE;
    de->hide = TRUE;
    free_dentries (de);
    return 0;
}   /*  End Function devfs_rmdir  */

static int devfs_mknod (struct inode *dir, struct dentry *dentry, int mode,
			int rdev)
{
    int is_new;
    struct fs_info *fs_info;
    struct devfs_entry *parent, *de;
    struct inode *inode;

#ifdef CONFIG_DEVFS_DEBUG
    char txt[STRING_LENGTH];

    if (devfs_debug & DEBUG_I_MKNOD)
    {
	memset (txt, 0, STRING_LENGTH);
	memcpy (txt, dentry->d_name.name, dentry->d_name.len);
	txt[STRING_LENGTH - 1] = '\0';
	printk ("%s: mknod(%s): mode: 0%o  dev: %d\n",
		DEVFS_NAME, txt, mode, rdev);
    }
#endif

    if ( !dir || !S_ISDIR (dir->i_mode) ) return -ENOTDIR;
    fs_info = dir->i_sb->u.generic_sbp;
    if ( !S_ISBLK (mode) && !S_ISCHR (mode) && !S_ISFIFO (mode) &&
	 !S_ISSOCK (mode) ) return -EPERM;
    /*  We are allowed to create the node  */
    /*  First try to get the devfs entry for this directory  */
    parent = get_devfs_entry_from_vfs_inode (dir);
    if (parent == NULL) return -EINVAL;
    if (!parent->registered) return -ENOENT;
    /*  Try to reclaim an existing devfs entry, create if there isn't one  */
    de = search_for_entry (parent, dentry->d_name.name, dentry->d_name.len,
			   FALSE, TRUE, &is_new, FALSE);
    if (de == NULL) return -ENOMEM;
    if (!de->registered)
    {
	/*  Since we created the devfs entry we get to choose things  */
	de->info = NULL;
	de->mode = mode;
	if ( S_ISBLK (mode) || S_ISCHR (mode) )
	{
	    de->u.fcb.u.device.major = MAJOR (rdev);
	    de->u.fcb.u.device.minor = MINOR (rdev);
	    de->u.fcb.default_uid = current->euid;
	    de->u.fcb.default_gid = current->egid;
	    de->u.fcb.ops = NULL;
	    de->u.fcb.auto_owner = FALSE;
	    de->u.fcb.aopen_notify = FALSE;
	    de->u.fcb.open = FALSE;
	}
	else if ( S_ISFIFO (mode) )
	{
	    de->u.fifo.uid = current->euid;
	    de->u.fifo.gid = current->egid;
	}
    }
    de->registered = TRUE;
    de->show_unreg = FALSE;
    de->hide = FALSE;
    de->inode.mode = mode;
    de->inode.uid = current->euid;
    de->inode.gid = current->egid;
    de->inode.atime = CURRENT_TIME;
    de->inode.mtime = CURRENT_TIME;
    de->inode.ctime = CURRENT_TIME;
    if ( ( inode = get_vfs_inode (dir->i_sb, de, dentry) ) == NULL )
	return -ENOMEM;
#ifdef CONFIG_DEVFS_DEBUG
    if (devfs_debug & DEBUG_I_MKNOD)
	printk ("%s:   new VFS inode(%u): %p  dentry: %p\n",
		DEVFS_NAME, de->inode.ino, inode, dentry);
#endif
    d_instantiate (dentry, inode);
    devfsd_notify_one (de, DEVFSD_NOTIFY_CREATE, inode->i_mode,
		       inode->i_uid, inode->i_gid, fs_info);
    return 0;
}   /*  End Function devfs_mknod  */

static int devfs_readlink (struct dentry *dentry, char *buffer, int buflen)
{
    struct devfs_entry *de;

    lock_kernel ();
    de = get_devfs_entry_from_vfs_inode (dentry->d_inode);
    unlock_kernel ();
    return vfs_readlink (dentry, buffer, buflen, de->u.symlink.linkname);
}   /*  End Function devfs_readlink  */

static int devfs_follow_link (struct dentry *dentry, struct nameidata *nd)
{
    struct devfs_entry *de;

    lock_kernel ();
    de = get_devfs_entry_from_vfs_inode (dentry->d_inode);
    unlock_kernel ();
    return vfs_follow_link (nd, de->u.symlink.linkname);
}   /*  End Function devfs_follow_link  */

static struct inode_operations devfs_iops =
{
    link:           devfs_link,
    unlink:         devfs_unlink,
    symlink:        devfs_symlink,
    mkdir:          devfs_mkdir,
    rmdir:          devfs_rmdir,
    mknod:          devfs_mknod,
    setattr:        devfs_notify_change,
};

static struct inode_operations devfs_dir_iops =
{
    lookup:         devfs_lookup,
    link:           devfs_link,
    unlink:         devfs_unlink,
    symlink:        devfs_symlink,
    mkdir:          devfs_mkdir,
    rmdir:          devfs_rmdir,
    mknod:          devfs_mknod,
    setattr:        devfs_notify_change,
};

static struct inode_operations devfs_symlink_iops =
{
    readlink:       devfs_readlink,
    follow_link:    devfs_follow_link,
    setattr:        devfs_notify_change,
};

static struct super_block *devfs_read_super (struct super_block *sb,
					     void *data, int silent)
{
    struct inode *root_inode = NULL;

    if (get_root_entry () == NULL) goto out_no_root;
    atomic_set (&fs_info.devfsd_overrun_count, 0);
    init_waitqueue_head (&fs_info.devfsd_wait_queue);
    init_waitqueue_head (&fs_info.revalidate_wait_queue);
    fs_info.sb = sb;
    sb->u.generic_sbp = &fs_info;
    sb->s_blocksize = 1024;
    sb->s_blocksize_bits = 10;
    sb->s_magic = DEVFS_SUPER_MAGIC;
    sb->s_op = &devfs_sops;
    if ( ( root_inode = get_vfs_inode (sb, root_entry, NULL) ) == NULL )
	goto out_no_root;
    sb->s_root = d_alloc_root (root_inode);
    if (!sb->s_root) goto out_no_root;
#ifdef CONFIG_DEVFS_DEBUG
    if (devfs_debug & DEBUG_DISABLED)
	printk ("%s: read super, made devfs ptr: %p\n",
		DEVFS_NAME, sb->u.generic_sbp);
#endif
    return sb;

out_no_root:
    printk ("devfs_read_super: get root inode failed\n");
    if (root_inode) iput (root_inode);
    return NULL;
}   /*  End Function devfs_read_super  */


static DECLARE_FSTYPE (devfs_fs_type, DEVFS_NAME, devfs_read_super, FS_SINGLE);


/*  File operations for devfsd follow  */

static ssize_t devfsd_read (struct file *file, char *buf, size_t len,
			    loff_t *ppos)
{
    int done = FALSE;
    int ival;
    loff_t pos, devname_offset, tlen, rpos;
    struct devfsd_notify_struct info;
    struct devfsd_buf_entry *entry;
    struct fs_info *fs_info = file->f_dentry->d_inode->i_sb->u.generic_sbp;
    DECLARE_WAITQUEUE (wait, current);

    /*  Can't seek (pread) on this device  */
    if (ppos != &file->f_pos) return -ESPIPE;
    /*  Verify the task has grabbed the queue  */
    if (fs_info->devfsd_task != current) return -EPERM;
    info.major = 0;
    info.minor = 0;
    /*  Block for a new entry  */
    add_wait_queue (&fs_info->devfsd_wait_queue, &wait);
    current->state = TASK_INTERRUPTIBLE;
    while ( devfsd_queue_empty (fs_info) )
    {
	fs_info->devfsd_sleeping = TRUE;
	wake_up (&fs_info->revalidate_wait_queue);
	schedule ();
	fs_info->devfsd_sleeping = FALSE;
	if ( signal_pending (current) )
	{
	    remove_wait_queue (&fs_info->devfsd_wait_queue, &wait);
	    current->state = TASK_RUNNING;
	    return -EINTR;
	}
	set_current_state(TASK_INTERRUPTIBLE);
    }
    remove_wait_queue (&fs_info->devfsd_wait_queue, &wait);
    current->state = TASK_RUNNING;
    /*  Now play with the data  */
    ival = atomic_read (&fs_info->devfsd_overrun_count);
    if (ival > 0) atomic_sub (ival, &fs_info->devfsd_overrun_count);
    info.overrun_count = ival;
    entry = (struct devfsd_buf_entry *) fs_info->devfsd_buffer +
	fs_info->devfsd_buf_out;
    info.type = entry->type;
    info.mode = entry->mode;
    info.uid = entry->uid;
    info.gid = entry->gid;
    if (entry->type == DEVFSD_NOTIFY_LOOKUP)
    {
	info.namelen = strlen (entry->data);
	pos = 0;
	memcpy (info.devname, entry->data, info.namelen + 1);
    }
    else
    {
	devfs_handle_t de = entry->data;

	if ( S_ISCHR (de->mode) || S_ISBLK (de->mode) || S_ISREG (de->mode) )
	{
	    info.major = de->u.fcb.u.device.major;
	    info.minor = de->u.fcb.u.device.minor;
	}
	pos = devfs_generate_path (de, info.devname, DEVFS_PATHLEN);
	if (pos < 0) return pos;
	info.namelen = DEVFS_PATHLEN - pos - 1;
	if (info.mode == 0) info.mode = de->mode;
    }
    devname_offset = info.devname - (char *) &info;
    rpos = *ppos;
    if (rpos < devname_offset)
    {
	/*  Copy parts of the header  */
	tlen = devname_offset - rpos;
	if (tlen > len) tlen = len;
	if ( copy_to_user (buf, (char *) &info + rpos, tlen) )
	{
	    return -EFAULT;
	}
	rpos += tlen;
	buf += tlen;
	len -= tlen;
    }
    if ( (rpos >= devname_offset) && (len > 0) )
    {
	/*  Copy the name  */
	tlen = info.namelen + 1;
	if (tlen > len) tlen = len;
	else done = TRUE;
	if ( copy_to_user (buf, info.devname + pos + rpos - devname_offset,
			   tlen) )
	{
	    return -EFAULT;
	}
	rpos += tlen;
    }
    tlen = rpos - *ppos;
    if (done)
    {
	unsigned int next_pos = fs_info->devfsd_buf_out + 1;

	if (next_pos >= devfsd_buf_size) next_pos = 0;
	fs_info->devfsd_buf_out = next_pos;
	*ppos = 0;
    }
    else *ppos = rpos;
    return tlen;
}   /*  End Function devfsd_read  */

static int devfsd_ioctl (struct inode *inode, struct file *file,
			 unsigned int cmd, unsigned long arg)
{
    int ival;
    struct fs_info *fs_info = inode->i_sb->u.generic_sbp;

    switch (cmd)
    {
      case DEVFSDIOC_GET_PROTO_REV:
	ival = DEVFSD_PROTOCOL_REVISION_KERNEL;
	if ( copy_to_user ( (void *)arg, &ival, sizeof ival ) ) return -EFAULT;
	break;
      case DEVFSDIOC_SET_EVENT_MASK:
	/*  Ensure only one reader has access to the queue. This scheme will
	    work even if the global kernel lock were to be removed, because it
	    doesn't matter who gets in first, as long as only one gets it  */
	if (fs_info->devfsd_task == NULL)
	{
#ifdef CONFIG_SMP
	    /*  Looks like no-one has it: check again and grab, with interrupts
		disabled  */
	    __cli ();
	    if (fs_info->devfsd_task == NULL)
#endif
	    {
		fs_info->devfsd_event_mask = 0;  /*  Temporary disable  */
		fs_info->devfsd_task = current;
	    }
#ifdef CONFIG_SMP
	    __sti ();
#endif
	}
	/*  Verify the task has grabbed the queue  */
	if (fs_info->devfsd_task != current) return -EBUSY;
	fs_info->devfsd_file = file;
	fs_info->devfsd_buffer = (void *) __get_free_page (GFP_KERNEL);
	if (fs_info->devfsd_buffer == NULL)
	{
	    devfsd_close (inode, file);
	    return -ENOMEM;
	}
	fs_info->devfsd_buf_out = fs_info->devfsd_buf_in;
	fs_info->devfsd_event_mask = arg;  /*  Let the masses come forth  */
	break;
      case DEVFSDIOC_RELEASE_EVENT_QUEUE:
	if (fs_info->devfsd_file != file) return -EPERM;
	return devfsd_close (inode, file);
	/*break;*/
#ifdef CONFIG_DEVFS_DEBUG
      case DEVFSDIOC_SET_DEBUG_MASK:
	if ( copy_from_user (&ival, (void *) arg, sizeof ival) )return -EFAULT;
	devfs_debug = ival;
	break;
#endif
      default:
	return -ENOIOCTLCMD;
    }
    return 0;
}   /*  End Function devfsd_ioctl  */

static int devfsd_close (struct inode *inode, struct file *file)
{
    struct fs_info *fs_info = inode->i_sb->u.generic_sbp;

    lock_kernel();
    if (fs_info->devfsd_file != file)
    {
	unlock_kernel();
	return 0;
    }
    fs_info->devfsd_event_mask = 0;
    fs_info->devfsd_file = NULL;
    if (fs_info->devfsd_buffer)
    {
	while (fs_info->devfsd_buffer_in_use) schedule ();
	free_page ( (unsigned long) fs_info->devfsd_buffer );
    }
    fs_info->devfsd_buffer = NULL;
    fs_info->devfsd_task = NULL;
    wake_up (&fs_info->revalidate_wait_queue);
    unlock_kernel();
    return 0;
}   /*  End Function devfsd_close  */


int __init init_devfs_fs (void)
{
    int err;

    printk ("%s: v%s Richard Gooch (rgooch@atnf.csiro.au)\n",
	    DEVFS_NAME, DEVFS_VERSION);
#ifdef CONFIG_DEVFS_DEBUG
    devfs_debug = devfs_debug_init;
    printk ("%s: devfs_debug: 0x%0x\n", DEVFS_NAME, devfs_debug);
#endif
    printk ("%s: boot_options: 0x%0x\n", DEVFS_NAME, boot_options);
    err = register_filesystem (&devfs_fs_type);
    if (!err)
    {
	struct vfsmount *devfs_mnt = kern_mount (&devfs_fs_type);
	err = PTR_ERR (devfs_mnt);
	if ( !IS_ERR (devfs_mnt) ) err = 0;
    }
    return err;
}   /*  End Function init_devfs_fs  */

void __init mount_devfs_fs (void)
{
    int err;

    if ( (boot_options & OPTION_NOMOUNT) ) return;
    err = do_mount ("none", "/dev", "devfs", 0, "");
    if (err == 0) printk ("Mounted devfs on /dev\n");
    else printk ("Warning: unable to mount devfs, err: %d\n", err);
}   /*  End Function mount_devfs_fs  */
