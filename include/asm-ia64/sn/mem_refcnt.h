/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_MEM_REFCNT_H
#define _ASM_SN_MEM_REFCNT_H

extern int mem_refcnt_attach(devfs_handle_t hub);
extern int mem_refcnt_open(devfs_handle_t *devp, mode_t oflag, int otyp, cred_t *crp);
extern int mem_refcnt_close(devfs_handle_t dev, int oflag, int otyp, cred_t *crp);
extern int mem_refcnt_mmap(devfs_handle_t dev, vhandl_t *vt, off_t off, size_t len, uint prot);
extern int mem_refcnt_unmap(devfs_handle_t dev, vhandl_t *vt);
extern int mem_refcnt_ioctl(devfs_handle_t dev,
                 int cmd,
                 void *arg,
                 int mode,
                 cred_t *cred_p,
                 int *rvalp);
        

#endif /* _ASM_SN_MEM_REFCNT_H */
