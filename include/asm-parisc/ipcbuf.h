#ifndef __PARISC_IPCBUF_H__
#define __PARISC_IPCBUF_H__

/*
 * The ipc64_perm structure for PA-RISC is identical to kern_ipc_perm
 * as we have always had 32-bit UIDs and GIDs in the kernel.
 */

#define ipc64_perm	kern_ipc_perm

#endif /* __PARISC_IPCBUF_H__ */
