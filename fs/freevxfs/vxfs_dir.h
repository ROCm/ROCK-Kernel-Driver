/*
 * Copyright (c) 2000-2001 Christoph Hellwig.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
#ifndef _VXFS_DIR_H_
#define _VXFS_DIR_H_

#ident "$Id: vxfs_dir.h,v 1.4 2001/04/24 19:28:36 hch Exp hch $"

/*
 * Veritas filesystem driver - directory structure.
 *
 * This file contains the definition of the vxfs directory format.
 */


/*
 * VxFS directory block header.
 */
struct vxfs_dirblk {
	u_int16_t	d_free;
	u_int16_t	d_nhash;
	u_int16_t	d_hash[1];
};

/*
 * Special dirblk for immed inodes:  no hash.
 */
struct vxfs_immed_dirblk {
	u_int16_t	d_free;
	u_int16_t	d_nhash;
};

/*
 * VxFS directory entry.
 */
#define VXFS_NAME_LEN	256
struct vxfs_direct {
	vx_ino_t	d_ino;
	u_int16_t	d_reclen;
	u_int16_t	d_namelen;
	u_int16_t	d_hashnext;
	char		d_name[VXFS_NAME_LEN];
};

#endif /* _VXFS_DIR_H_ */
