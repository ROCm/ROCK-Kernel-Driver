#
#  vtxsys64.S
#
#  Copyright (C) 2002-2004 Intel Corporation
#  Maintainer - Juan Villacis <juan.villacis@intel.com>
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# ===========================================================================
#
#	File: vtxsys64.S
#
#	Description: routines for sampling on Itanium(R)-based systems
#
#	Author(s): George Artz, Intel Corp.
#                  Tony Luck, Intel Corp.
#
#	System: VTune(TM) Performance Analyzer Driver Kit for Linux*
#
# ===========================================================================
#

#include <asm/asmmacro.h>

        .file "vtxsys64.s"
        

        .text
        .align 16
        .global vt_sys_mmap_stub
vt_sys_mmap_stub:
        alloc   r2=ar.pfs,8,4,8,0
        ;;
        movl    r8=original_sys_mmap
        mov     loc0=r2
        mov     loc1=rp
        ;;
        ld8     r8=[r8]
        mov     loc3=gp

        mov     out0=in0
        mov     out1=in1
        mov     out2=in2
        mov     out3=in3
        mov     out4=in4
        mov     out5=in5
        mov     out6=in6
        mov     out7=in7
        ;;
        mov     b6=r8

        br.call.sptk.many rp=b6

        mov     loc2=r8

        movl    r9=@fptr(vt_sys_mmap)

        mov     out0=r8
        mov     out1=in1        // length
        mov     out2=in2        // prot
        mov     out3=in4        // fd
        ;;
        add     r8=8,r9
        ;;
        ld8     gp=[r8]
        br.call.sptk.many rp=vt_sys_mmap

        mov     rp=loc1
        mov     r8=loc2
        mov     gp=loc3
        mov     ar.pfs=loc0
        ;;
        br.ret.sptk.many rp



        .text
        .align 16
        .global vt_sys_mmap2_stub
vt_sys_mmap2_stub:
        alloc   r2=ar.pfs,8,4,8,0
        ;;
        movl    r8=original_sys_mmap2
        mov     loc0=r2
        mov     loc1=rp
        ;;
        ld8     r8=[r8]
        mov     loc3=gp

        mov     out0=in0
        mov     out1=in1
        mov     out2=in2
        mov     out3=in3
        mov     out4=in4
        mov     out5=in5
        mov     out6=in6
        mov     out7=in7
        ;;
        mov     b6=r8

        br.call.sptk.many rp=b6

        mov     loc2=r8

        movl    r9=@fptr(vt_sys_mmap2)

        mov     out0=r8
        mov     out1=in1        // length
        mov     out2=in2        // prot
        mov     out3=in4        // fd
        ;;
        add     r8=8,r9
        ;;
        ld8     gp=[r8]
        br.call.sptk.many rp=vt_sys_mmap2

        mov     rp=loc1
        mov     r8=loc2
        mov     gp=loc3
        mov     ar.pfs=loc0
        ;;
        br.ret.sptk.many rp


        .text
        .align 16
        .global vt_sys_clone2_stub
vt_sys_clone2_stub:
        alloc   r2=ar.pfs,95,0,1,0
        ;;
        mov     r40 = r0            // force rse if it hasn't already happened
        mov     r41 = r0
        mov     r42 = r0
        mov     r43 = r0
        mov     r44 = r0
        mov     r45 = r0
        mov     r46 = r0
        mov     r47 = r0
        mov     r48 = r0
        mov     r49 = r0
        mov     r50 = r0
        mov     r51 = r0
        mov     r52 = r0
        mov     r53 = r0
        mov     r54 = r0
        mov     r55 = r0
        mov     r56 = r0
        mov     r57 = r0
        mov     r58 = r0
        mov     r59 = r0
        mov     r60 = r0
        mov     r61 = r0
        mov     r62 = r0
        mov     r63 = r0
        mov     r64 = r0
        mov     r65 = r0
        mov     r66 = r0
        mov     r67 = r0
        mov     r68 = r0
        mov     r69 = r0
        mov     r70 = r0
        mov     r71 = r0
        mov     r72 = r0
        mov     r73 = r0
        mov     r74 = r0
        mov     r75 = r0
        mov     r76 = r0
        mov     r77 = r0
        mov     r78 = r0
        mov     r79 = r0
        mov     r80 = r0
        mov     r81 = r0
        mov     r82 = r0
        mov     r83 = r0
        mov     r84 = r0
        mov     r85 = r0
        mov     r86 = r0
        mov     r87 = r0
        mov     r88 = r0
        mov     r89 = r0
        mov     r90 = r0
        mov     r91 = r0
        mov     r92 = r0
        mov     r93 = r0
        mov     r94 = r0
        mov     r95 = r0
        mov     r96 = r0
        mov     r97 = r0
        mov     r98 = r0
        mov     r99 = r0
        mov     r100 = r0
        mov     r101 = r0
        mov     r102 = r0
        mov     r103 = r0
        mov     r104 = r0
        mov     r105 = r0
        mov     r106 = r0
        mov     r107 = r0
        mov     r108 = r0
        mov     r109 = r0
        mov     r110 = r0
        mov     r111 = r0
        mov     r112 = r0
        mov     r113 = r0
        mov     r114 = r0
        mov     r115 = r0
        mov     r116 = r0
        mov     r117 = r0
        mov     r118 = r0
        mov     r119 = r0
        mov     r120 = r0
        mov     r121 = r0
        mov     r122 = r0
        mov     r123 = r0
        mov     r124 = r0
        mov     r125 = r0
        mov     r126 = r0
        mov     r127 = r0
        ;;
        mov     r40 = gp                // loc 0
        mov     r41 = rp                // loc 1
        mov     r42 = r2                // loc 2

        mov     r127 = rp               // arg 0

        movl    r60 = @fptr(vt_sys_clone2_before)       // need gp to make call
        ;;
        add     r61 = 8, r60                            // gp is second entry
        ;;
        ld8     gp = [r61]                              // load gp
        br.call.sptk.many   rp=vt_sys_clone2_before     // make call
        ;;
        // r8 indicates if the the rp was added to the clone table.
        // if it was (r8 non-zero) then call back to our post code
        // if it wasn't (r8 zero) then use the original rp
        // so that we don't do a lookup of something that won't be
        // there. this lets the system continue running even if the
        // clone table is full for some reason.
        //
        cmp4.eq.unc p6,p7=r0,r8         // p6 true means r8 == 0
        mov     gp = r40                // restore gp
        movl    r62=original_sys_clone2 // setup to make original call
        ;;
        mov     r127 = r0
(p7)    movl    r70  = vt_sys_clone2_stub_ret   // added, want to come back
        ;;
(p7)    mov     rp = r70                // we added so come back to us
(p6)    mov     rp = r41                // we didn't add, so don't bother
        ld8     r62=[r62]
        ;;
        mov     b6=r62
        mov ar.pfs = r42
        ;;
        br.sptk.many b6

//
// Taken fron entry.S - a copy of invoke_syscall_trace
//
// According to notes there, we can't allow ar.pfs to change in the
// return path of a syscall, so we do a short call to this routine
// which takes care of the call to the bigger routine and as
// long as we pass values in a non-standard way (aka scratch registers
// then life can be good... At least in theory...
//

GLOBAL_ENTRY(vt_post_helper)
    .prologue ASM_UNW_PRLG_RP|ASM_UNW_PRLG_PFS, ASM_UNW_PRLG_GRSAVE(8)
    alloc loc1=ar.pfs,8,5,1,0
    mov loc0=rp
    .body
    mov loc2=b6
    ;;
    //
    // Now that the precursor is out of the way... We can do what we want...
    //
    mov loc3 = r8           // save the return value from the syscall
    mov loc4 = gp           // save the gp

    mov out0 = r8           // and prepare to pass it along
    ;;

    movl r8 = @fptr(vt_sys_clone2_after)
    ;;

    adds    r8 = 8, r8      // don't care about func addr, just func gp
    ;;

    ld8     gp = [r8]
    br.call.sptk.many rp=vt_sys_clone2_after
    ;;

    mov r14 = r8            // private spot of old return address
    ;;

    mov r8 = loc3           // so the rest of the syscall people can see it
    mov gp = loc4
    
    //
    // Carefule here, this is for unwinding too...
    //
.ret3:  mov rp=loc0
    mov ar.pfs=loc1
    mov b6=loc2
    br.ret.sptk.many rp
END(vt_post_helper)


//
// At this point we are on the original break alloc/stack. Need to not
// interfere with any locals they were using.
//
vt_sys_clone2_stub_ret:
        nop.m 0
        nop.i 0
        //
        // looks like the people in entry.S knew what they were doing...
        //
        br.call.sptk.many rp=vt_post_helper
        ;;

        //
        // Put in by the vt_post_helper routine...
        //
        mov     rp = r14
        ;;
        br.sptk.many rp


        .text
        .align 16
        .global vt_sys_clone_stub
vt_sys_clone_stub:
        alloc   r2=ar.pfs,2,5,2,0
        ;;
        mov     loc3=gp
        mov     loc0=r2
        mov     loc1=rp

        movl    r9=@fptr(vt_sys_clone_before)
        ;;
        add     r8=8,r9
        ;;
        ld8     gp=[r8]
        br.call.sptk.many rp=vt_sys_clone_before
        mov     loc4=r8
        ;;
        movl    r8=original_sys_clone
        ;;
        ld8     r8=[r8]

        mov     out0=in0
        mov     out1=in1
        ;;
        mov     b6=r8

        mov     gp=loc3
        br.call.sptk.many rp=b6

        mov     loc2=r8

        movl    r9=@fptr(vt_sys_clone_after)

        mov     out0=r8
        ;;
        add     r8=8,r9
        ;;
        ld8     gp=[r8]
        mov     out1=loc4
        br.call.sptk.many rp=vt_sys_clone_after

        mov     rp=loc1
        mov     r8=loc2
        mov     gp=loc3
        mov     ar.pfs=loc0
        ;;
        br.ret.sptk.many rp

        .text
        .align 16
        .global vt_sys_create_module_stub
vt_sys_create_module_stub:
        alloc   r2=ar.pfs,8,4,8,0
        ;;
        movl    r8=original_sys_create_module
        mov     loc0=r2
        mov     loc1=rp
        ;;
        ld8     r8=[r8]
        mov     loc3=gp

        mov     out0=in0
        mov     out1=in1
        mov     out2=in2
        mov     out3=in3
        mov     out4=in4
        mov     out5=in5
        mov     out6=in6
        mov     out7=in7
        ;;
        mov     b6=r8

        br.call.sptk.many rp=b6

        mov     loc2=r8

        movl    r9=@fptr(vt_sys_create_module)

        mov     out0=r8
        ;;
        add     r8=8,r9
        ;;
        ld8     gp=[r8]
        br.call.sptk.many rp=vt_sys_create_module

        mov     rp=loc1
        mov     r8=loc2
        mov     gp=loc3
        mov     ar.pfs=loc0
        ;;
        br.ret.sptk.many rp

        .text
        .align 16
        .global vt_sys_execve_stub
vt_sys_execve_stub:
        alloc   r2=ar.pfs,3,2,3,0
        ;;
        movl    r8=original_sys_execve
        movl    r9=vt_save_execve_ret
        mov     loc0=r2
        mov     loc1=rp
        ;;
        ld8     r8=[r8]
        st8     [r9]=loc1

        mov     out0=in0
        mov     out1=in1
        mov     out2=in2
        ;;
        mov     b6=r8

        /* call original exec function in kernel */
        br.call.sptk.many rp=b6

        cmp4.ge p6,p7=r8,r0
        ;;

        /*
         * if exec failed, we still have a frame, so we can restore
         * registers and return.
         */
(p7)    mov     rp=loc1
(p7)    mov     ar.pfs=loc0
        ;;
(p7)    br.ret.sptk.many rp

        /*
         * exec succeeded, we are on a new register window frame
         * set gp for module and call our stub
         */
        alloc   r4=ar.pfs,0,3,0,0
        ;;
        mov     loc1=r8
        mov     loc2=gp

        mov     loc0=r4
        movl    r9=@fptr(vt_sys_execve)
        ;;
        add     r8=8,r9
        ;;
        ld8     gp=[r8]
        br.call.sptk.many rp=vt_sys_execve

        movl    r9=vt_save_execve_ret
        ;;
        ld8     r4=[r9]
        mov     gp=loc2
        mov     r8=loc1
        mov     ar.pfs=loc0
        ;;
        mov     rp=r4
        ;;
        mov     r4=r0
        

        br.ret.sptk.many rp
