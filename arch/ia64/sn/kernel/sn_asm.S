/*
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.  All Rights Reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it 
 * under the terms of version 2 of the GNU General Public License 
 * as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it would be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
 * 
 * Further, this software is distributed without any warranty that it is 
 * free of the rightful claim of any third person regarding infringement 
 * or the like.  Any license provided herein, whether implied or 
 * otherwise, applies only to this software file.  Patent licenses, if 
 * any, provided herein do not apply to combinations of this program with 
 * other software, or any other product whatsoever.
 * 
 * You should have received a copy of the GNU General Public 
 * License along with this program; if not, write the Free Software 
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 * 
 * Contact information:  Silicon Graphics, Inc., 1600 Amphitheatre Pkwy, 
 * Mountain View, CA  94043, or:
 * 
 * http://www.sgi.com 
 * 
 * For further information regarding this notice, see: 
 * 
 * http://oss.sgi.com/projects/GenInfo/NoticeExplan
 */

#include <linux/config.h>
#ifdef CONFIG_IA64_SGI_AUTOTEST

// Testing only.
// Routine will cause MCAs
//   zzzmca(n)
//      n=0 MCA via duplicate TLB dropin
//      n=1 MCA via read of garbage address
//      n=2 MCA via lfetch read of garbage address
//

#define ITIR(key, ps)           ((key<<8) | (ps<<2))
#define TLB_PAGESIZE            28                      // Use 256MB pages for now.

                        .global zzzmca
                        .proc   zzzmca
zzzmca:
                        alloc   loc4       = ar.pfs,2,8,1,0;;
                        cmp.ne  p6,p0=r32,r0;;
                        movl    r2=0x2dead
                        movl    r3=0x3dead
                        movl    r15=0x15dead
                        movl    r16=0x16dead
                        movl    r31=0x31dead
                        movl    loc0=0x34beef
                        movl    loc1=0x35beef
                        movl    loc2=0x36beef
                        movl    loc3=0x37beef
                        movl    out0=0x42beef

                        movl    r20=0x32feed;;
                        mov     ar32=r20
                        movl    r20=0x36feed;;
                        mov     ar36=r20
                        movl    r20=0x65feed;;
                        mov     ar65=r20
                        movl    r20=0x66feed;;
                        mov     ar66=r20

(p6)                    br.cond.sptk    1f

                        rsm      0x2000;;
                        srlz.d;
                        mov      r11      = 5
                        mov      r3       = ITIR(0,TLB_PAGESIZE);;
                        mov      cr.itir  = r3
                        mov      r10      = 0;;
                        itr.d    dtr[r11] = r10;;
                        mov      r11      = 6

                        itr.d    dtr[r11] = r10;;
                        br      9f

1:
			cmp.eq	p6,p7=1,r32
#ifdef CONFIG_IA64_SGI_SN1
                        movl    r8=0xe00000fe00000048;;
#else
                        movl    r8=0xe0007fb000000048;;
#endif
                 (p6)   ld8     r9=[r8]
                 (p7)   lfetch.fault.nt2  [r8]
			;;
                        mf
			;;
                        mf.a
			;;
                        srlz.d

9:                      mov     ar.pfs=loc4
                        br.ret.sptk     rp

                        .endp   zzzmca

#endif
