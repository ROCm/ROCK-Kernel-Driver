/*
 *  ntfstypes.h
 *  This file defines four things:
 *   - generic platform independent fixed-size types (e.g. ntfs_u32)
 *   - specific fixed-size types (e.g. ntfs_offset_t)
 *   - macros that read and write those types from and to byte arrays
 *   - types derived from OS specific ones
 *
 *  Copyright (C) 1996,1998, 1999 Martin von Löwis
 */

#ifdef NTFS_IN_LINUX_KERNEL
/* get installed types if we compile the kernel*/
#include <linux/fs.h>
#endif

/* We don't need to define __LITTLE_ENDIAN, as we use
   <asm/byteorder>. */

#include "ntfsendian.h"
#include <asm/types.h>

/* integral types */
#ifndef NTFS_INTEGRAL_TYPES
#define NTFS_INTEGRAL_TYPES
typedef u8  ntfs_u8;
typedef u16 ntfs_u16;
typedef u32 ntfs_u32;
typedef u64 ntfs_u64;
typedef s8  ntfs_s8;
typedef s16 ntfs_s16;
typedef s32 ntfs_s32;
typedef s64 ntfs_s64;
#endif

/* unicode character type */
#ifndef NTFS_WCHAR_T
#define NTFS_WCHAR_T
typedef u16 ntfs_wchar_t;
#endif
/* file offset */
#ifndef NTFS_OFFSET_T
#define NTFS_OFFSET_T
typedef u64 ntfs_offset_t;
#endif
/* UTC */
#ifndef NTFS_TIME64_T
#define NTFS_TIME64_T
typedef u64 ntfs_time64_t;
#endif
/* This is really unsigned long long. So we support only volumes up to 2 TB */
#ifndef NTFS_CLUSTER_T
#define NTFS_CLUSTER_T
typedef u32 ntfs_cluster_t;
#endif

#ifndef MAX_CLUSTER_T
#define MAX_CLUSTER_T (~((ntfs_cluster_t)0))
#endif

/* architecture independent macros */

/* PUTU32 would not clear all bytes */
#define NTFS_PUTINUM(p,i)    NTFS_PUTU64(p,i->i_number);\
                             NTFS_PUTU16(((char*)p)+6,i->sequence_number)

/* system dependent types */
#include <asm/posix_types.h>
#ifndef NTMODE_T
#define NTMODE_T
typedef __kernel_mode_t ntmode_t;
#endif
#ifndef NTFS_UID_T
#define NTFS_UID_T
typedef uid_t ntfs_uid_t;
#endif
#ifndef NTFS_GID_T
#define NTFS_GID_T
typedef gid_t ntfs_gid_t;
#endif
#ifndef NTFS_SIZE_T
#define NTFS_SIZE_T
typedef __kernel_size_t ntfs_size_t;
#endif
#ifndef NTFS_TIME_T
#define NTFS_TIME_T
typedef __kernel_time_t ntfs_time_t;
#endif

/*
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
