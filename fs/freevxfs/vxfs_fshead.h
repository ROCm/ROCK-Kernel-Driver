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
#ifndef _VXFS_FSHEAD_H_
#define _VXFS_FSHEAD_H_

#ident "$Id: vxfs_fshead.h,v 1.5 2001/04/24 19:28:36 hch Exp hch $"

/*
 * Veritas filesystem driver - fileset header structures.
 *
 * This file contains the physical structure of the VxFS
 * fileset header.
 */


/*
 * Fileset header 
 */
struct vxfs_fsh {
	u_int32_t	fsh_version;			/* Fileset header version */
	u_int32_t	fsh_fsindex;
	u_int32_t	fsh_time;
	u_int32_t	fsh_utime;
	u_int32_t	fsh_extop;
	vx_ino_t	fsh_ninodes;
	u_int32_t	fsh_nau;
	u_int32_t	fsh_old_ilesize;
	u_int32_t	fsh_dflags;
	u_int32_t	fsh_quota;
	vx_ino_t	fsh_maxinode;
	vx_ino_t	fsh_iauino;
	vx_ino_t	fsh_ilistino[2];
	vx_ino_t	fsh_lctino;

	/*
	 * Slightly more fields follow, but they
	 *  a) are not of any interested for us, and
	 *  b) differ much in different vxfs versions/ports
	 */
};

#endif /* _VXFS_FSHEAD_H_ */
