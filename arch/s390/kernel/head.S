/*
 *  arch/s390/kernel/head.S
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hp@de.ibm.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com),
 *
 * There are 4 different IPL methods
 *  1) load the image directly into ram at address 0 and do an PSW restart
 *  2) linload will load the image from address 0x10000 to memory 0x10000
 *     and start the code thru LPSW 0x0008000080010000 (VM only, deprecated)
 *  3) generate the tape ipl header, store the generated image on a tape
 *     and ipl from it
 *  4) generate the vm reader ipl header, move the generated image to the
 *     VM reader (use option NOH!) and do a ipl from reader (VM only)
 *  We use the cpuid to distinguish between VM and native ipl
 *  params for kernel are pushed to 0x10400 (see setup.h)
 */

#include <linux/config.h>
#include <asm/setup.h>
#include <asm/lowcore.h>

#ifndef CONFIG_IPL
        .org   0
        .long  0x00080000,0x80000000+iplstart  # Just a restart PSW

iplstart:
        l     %r12,.Lparm                      # pointer to parameter area
#
# find out memory size
#
        mvc   104(8,0),.Lpcmem0          # setup program check handler
        slr   %r2,%r2
        lhi   %r3,1
        sll   %r3,20
.Lloop0:
        l     %r0,0(%r2)                 # test page
        ar    %r2,%r3                    # add 1M
        jnm   .Lloop0                    # r1 < 0x80000000 -> loop
.Lchkmem0:
        n     %r2,.L4malign0             # align to multiples of 4M
        st    %r2,MEMORY_SIZE-PARMAREA(%r12)  # store memory size
        slr   %r2,%r2
        st    %r2,INITRD_SIZE-PARMAREA(%r12)  # null ramdisk
        st    %r2,INITRD_START-PARMAREA(%r12)
        j     start

.Lparm: .long  PARMAREA
.L4malign0:.long 0xffc00000
        .align 8
.Lpcmem0:.long  0x00080000,0x80000000 + .Lchkmem0

#else
#ifdef CONFIG_IPL_TAPE
#define IPL_BS 1024
        .org   0
        .long  0x00080000,0x80000000+iplstart  # The first 24 bytes are loaded
        .long  0x07000000,0x60000001           # by ipl to addresses 0-23.
        .long  0x02000000,0x20000000+IPL_BS    # (a PSW and two CCWs).
        .long  0x00000000,0x00000000           # external old psw
        .long  0x00000000,0x00000000           # svc old psw
        .long  0x00000000,0x00000000           # program check old psw
        .long  0x00000000,0x00000000           # machine check old psw
        .long  0x00000000,0x00000000           # io old psw
        .long  0x00000000,0x00000000
        .long  0x00000000,0x00000000
        .long  0x00000000,0x00000000
        .long  0x000a0000,0x00000058           # external new psw
        .long  0x000a0000,0x00000060           # svc new psw
        .long  0x000a0000,0x00000068           # program check new psw
        .long  0x000a0000,0x00000070           # machine check new psw
        .long  0x00080000,0x80000000+.Lioint   # io new psw

        .org   0x100
iplstart:
	l     %r1,0xb8                         # load ipl subchannel number
        lhi   %r2,IPL_BS                       # load start address
        bras  %r14,.Lloader                    # load rest of ipl image
        st    %r1,__LC_IPLDEV                  # store ipl device number
        l     %r12,.Lparm                      # pointer to parameter area

#
# find out memory size
#
        mvc   104(8,0),.Lpcmem0          # setup program check handler
        slr   %r2,%r2
        lhi   %r3,1
        sll   %r3,20
.Lloop0:
        l     %r0,0(%r2)                 # test page
        ar    %r2,%r3                    # add 1M
        jnm   .Lloop0                    # r1 < 0x80000000 -> loop
.Lchkmem0:
        n     %r2,.L4malign0             # align to multiples of 4M
        st    %r2,MEMORY_SIZE-PARMAREA(%r12)  # store memory size
        c     %r2,.Lbigmem               # more than 64 MB of memory ?
        jl    .Lmemok                    # if yes load ramdisk to 32 MB
        mvc   INITRD_START-PARMAREA(4,%r12),.Lrdstart
.Lmemok:

#
# load parameter file from tape
#
 	l     %r2,INITRD_START-PARMAREA(%r12)  # use ramdisk location as temp
        bras  %r14,.Lloader                    # load parameter file
        ltr   %r2,%r2                          # got anything ?
        jz    .Lnopf
	chi   %r2,895
	jnh   .Lnotrunc
	lhi   %r2,895
.Lnotrunc:
        l     %r4,INITRD_START-PARMAREA(%r12)
        la    %r5,0(%r4,%r2)
        lr    %r3,%r2
.Lidebc:
        tm    0(%r5),0x80                      # high order bit set ?
        jo    .Ldocv                           #  yes -> convert from EBCDIC
        ahi   %r5,-1
        brct  %r3,.Lidebc
        j     .Lnocv
.Ldocv:
        l     %r3,.Lcvtab
        tr    0(256,%r4),0(%r3)                # convert parameters to ascii
        tr    256(256,%r4),0(%r3)
        tr    512(256,%r4),0(%r3)
        tr    768(122,%r4),0(%r3)
.Lnocv: la    %r3,COMMAND_LINE-PARMAREA(%r12)  # load adr. of command line
	mvc   0(256,%r3),0(%r4)
	mvc   256(256,%r3),256(%r4)
	mvc   512(256,%r3),512(%r4)
	mvc   768(122,%r3),768(%r4)
        slr   %r0,%r0
        j     .Lcntlp
.Ldelspc:
        ic    %r0,0(%r2,%r3)
        chi   %r0,0x20                         # is it a space ?
        je    .Lcntlp
        ahi   %r2,1
        j     .Leolp
.Lcntlp:
        brct  %r2,.Ldelspc
.Leolp:
        slr   %r0,%r0
        stc   %r0,0(%r2,%r3)                   # terminate buffer
.Lnopf:

#
# load ramdisk from tape
#	
 	l     %r2,INITRD_START-PARMAREA(%r12)  # load adr. of ramdisk
        bras  %r14,.Lloader                    # load ramdisk
 	st    %r2,INITRD_SIZE-PARMAREA(%r12)   # store size of ramdisk
        ltr   %r2,%r2
        jnz   .Lrdcont
        st    %r2,INITRD_START-PARMAREA(%r12)  # no ramdisk found, null it
.Lrdcont:
#
# everything loaded, go for it
#
        j     start
#
# subroutine for loading from tape
# Paramters:	
#  R1 = device number
#  R2 = load address
.Lloader:	
        st    %r14,.Lldret
        la    %r3,.Lorbread                    # r3 = address of orb 
	la    %r5,.Lirb                        # r5 = address of irb
        st    %r2,.Lccwread+4                  # initialize CCW data addresses
        lctl  %c6,%c6,.Lcr6               
        slr   %r2,%r2
.Lldlp:
        lhi   %r6,3                            # 3 retries
.Lssch:
        ssch  0(%r3)                           # load chunk of IPL_BS bytes
        jnz   .Llderr
.Lw4end:
        bras  %r14,.Lwait4io
        tm    8(%r5),0x82                      # do we have a problem ?
        jnz   .Lrecov
        slr   %r7,%r7
        icm   %r7,3,10(%r5)                    # get residual count
        lcr   %r7,%r7
        ahi   %r7,IPL_BS                       # IPL_BS-residual=#bytes read
        ar    %r2,%r7                          # add to total size
        tm    8(%r5),0x01                      # found a tape mark ?
        jnz   .Ldone
        l     %r0,.Lccwread+4                  # update CCW data addresses
        ar    %r0,%r7
        st    %r0,.Lccwread+4                
        j     .Lldlp
.Ldone:
        l     %r14,.Lldret
        br    %r14                             # r2 contains the total size
.Lrecov:
        bras  %r14,.Lsense                     # do the sensing
        brct  %r6,.Lssch                       # dec. retry count & branch
        j     .Llderr
#
# Sense subroutine
#
.Lsense:
        st    %r14,.Lsnsret
        la    %r7,.Lorbsense              
        ssch  0(%r7)                           # start sense command
        jnz   .Llderr
        bras  %r14,.Lwait4io
        l     %r14,.Lsnsret
        tm    8(%r5),0x82                      # do we have a problem ?
        jnz   .Llderr
        br    %r14
#
# Wait for interrupt subroutine
#
.Lwait4io:
        lpsw  .Lwaitpsw                 
.Lioint:
        c     %r1,0xb8                         # compare subchannel number
        jne   .Lwait4io
        tsch  0(%r5)
        slr   %r0,%r0
        tm    8(%r5),0x82                      # do we have a problem ?
        jnz   .Lwtexit
        tm    8(%r5),0x04                      # got device end ?
        jz    .Lwait4io
.Lwtexit:
        br    %r14
.Llderr:
        lpsw  .Lcrash              

        .align 8
.Lorbread:
	.long  0x00000000,0x0080ff00,.Lccwread
        .align 8
.Lorbsense:
        .long  0x00000000,0x0080ff00,.Lccwsense
        .align 8
.Lccwread:
        .long  0x02200000+IPL_BS,0x00000000
.Lccwsense:
        .long  0x04200001,0x00000000
.Lwaitpsw:
	.long  0x020a0000,0x80000000+.Lioint

.Lirb:	.long  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
.Lcr6:  .long  0xff000000
        .align 8
.Lcrash:.long  0x000a0000,0x00000000
.Lpcmem0:.long  0x00080000,0x80000000 + .Lchkmem0
.Lparm: .long  PARMAREA
.L4malign0:.long 0xffc00000
.Lbigmem:.long 0x04000000
.Lrdstart:.long 0x02000000
.Lldret:.long  0
.Lsnsret: .long 0
.Lcvtab:.long  _ebcasc                         # ebcdic to ascii table

#endif  /* CONFIG_IPL_TAPE */

#ifdef CONFIG_IPL_VM
        .org   0
        .long  0x00080000,0x80000000+iplstart  # The first 24 bytes are loaded
        .long  0x02000018,0x60000050           # by ipl to addresses 0-23.
        .long  0x02000068,0x60000050           # (a PSW and two CCWs).
        .fill  80-24,1,0x40                    # bytes 24-79 are discarded !!
        .long  0x020000f0,0x60000050           # The next 160 byte are loaded
        .long  0x02000140,0x60000050           # to addresses 0x18-0xb7
        .long  0x02000190,0x60000050           # They form the continuation
        .long  0x020001e0,0x60000050           # of the CCW program started
        .long  0x02000230,0x60000050           # by ipl and load the range
        .long  0x02000280,0x60000050           # 0x0f0-0x730 from the image
        .long  0x020002d0,0x60000050           # to the range 0x0f0-0x730
        .long  0x02000320,0x60000050           # in memory. At the end of
        .long  0x02000370,0x60000050           # the channel program the PSW
        .long  0x020003c0,0x60000050           # at location 0 is loaded.
        .long  0x02000410,0x60000050           # Initial processing starts
        .long  0x02000460,0x60000050           # at 0xf0 = iplstart.
        .long  0x020004b0,0x60000050
        .long  0x02000500,0x60000050
        .long  0x02000550,0x60000050
        .long  0x020005a0,0x60000050
        .long  0x020005f0,0x60000050
        .long  0x02000640,0x60000050
        .long  0x02000690,0x60000050
        .long  0x020006e0,0x20000050


        .org   0xf0
iplstart:
	l     %r1,0xb8                         # load ipl subchannel number
        lhi   %r2,0x730                        # load start address
        bras  %r14,.Lloader                    # load rest of ipl image
        st    %r1,__LC_IPLDEV                  # store ipl device number
	l     %r12,.Lparm                      # pointer to parameter area

#
# find out memory size
#
        mvc   104(8,0),.Lpcmem0           # setup program check handler
        slr   %r2,%r2
        lhi   %r3,1
        sll   %r3,20
.Lloop0:
        l     %r0,0(%r2)                 # test page
        ar    %r2,%r3                    # add 1M
        jnm   .Lloop0                    # r1 < 0x80000000 -> loop
.Lchkmem0:
        n     %r2,.L4malign0             # align to multiples of 4M
        st    %r2,MEMORY_SIZE-PARMAREA(%r12)  # store memory size
        c     %r2,.Lbigmem               # more than 64 MB of memory ?
        jl    .Lmemok                    # if yes load ramdisk to 32 MB
        mvc   INITRD_START-PARMAREA(4,%r12),.Lrdstart
.Lmemok:

#
# load parameter file from reader
#
        l     %r2,INITRD_START-PARMAREA(%r12)  # use ramdisk location as temp
        bras  %r14,.Lloader                    # load parameter file
        ltr   %r2,%r2                          # got anything ?
        jz    .Lnopf
        chi   %r2,895
        jnh   .Lnotrunc
        lhi   %r2,895
.Lnotrunc:
        l     %r4,INITRD_START-PARMAREA(%r12)
        la    %r5,0(%r4,%r2)
        lr    %r3,%r2
.Lidebc:
        tm    0(%r5),0x80                      # high order bit set ?
        jo    .Ldocv                           #  yes -> convert from EBCDIC
        ahi   %r5,-1
        brct  %r3,.Lidebc
        j     .Lnocv
.Ldocv:
        l     %r3,.Lcvtab
        tr    0(256,%r4),0(%r3)                # convert parameters to ascii
        tr    256(256,%r4),0(%r3)
        tr    512(256,%r4),0(%r3)
        tr    768(122,%r4),0(%r3)
.Lnocv: la    %r3,COMMAND_LINE-PARMAREA(%r12)  # load adr. of command line
        mvc   0(256,%r3),0(%r4)
        mvc   256(256,%r3),256(%r4)
        mvc   512(256,%r3),512(%r4)
        mvc   768(122,%r3),768(%r4)
        slr   %r0,%r0
        j     .Lcntlp
.Ldelspc:
        ic    %r0,0(%r2,%r3)
        chi   %r0,0x20                         # is it a space ?
        je    .Lcntlp
        ahi   %r2,1
        j     .Leolp
.Lcntlp:
        brct  %r2,.Ldelspc
.Leolp:
        slr   %r0,%r0
        stc   %r0,0(%r2,%r3)                   # terminate buffer
.Lnopf:

#
# load ramdisk from reader
#	
	l     %r2,INITRD_START-PARMAREA(%r12)  # load adr. of ramdisk
        bras  %r14,.Lloader                    # load ramdisk
	st    %r2,INITRD_SIZE-PARMAREA(%r12)   # store size of ramdisk
        ltr   %r2,%r2
        jnz   .Lrdcont
        st    %r2,INITRD_START-PARMAREA(%r12)  # no ramdisk found, null it
.Lrdcont:

#
# everything loaded, reset files in reader, then go for it
#
        stidp __LC_CPUID                       # store cpuid
        lh    %r0,__LC_CPUID+4                 # get cpu version
        chi   %r0,0x7490                       # running on P/390 ?
        je   start                             #   no -> skip reset
        la    %r2,.Lreset              
        lhi   %r3,26
        .long 0x83230008
	j     start

#
# subroutine for loading cards from the reader
#
.Lloader:	
	la    %r3,.Lorb                        # r2 = address of orb into r2
	la    %r5,.Lirb                        # r4 = address of irb
        la    %r6,.Lccws              
        la    %r7,20
.Linit:
        st    %r2,4(%r6)                       # initialize CCW data addresses
        ahi   %r2,0x50
        ahi   %r6,8
        brct  7,.Linit

        lctl  %c6,%c6,.Lcr6                    # set IO subclass mask
	slr   %r2,%r2
.Lldlp:
        ssch  0(%r3)                           # load chunk of 1600 bytes
        jnz   .Llderr
.Lwait4irq:
        mvc   __LC_IO_NEW_PSW(8),.Lnewpsw      # set up IO interrupt psw
        lpsw  .Lwaitpsw              
.Lioint:
        c     %r1,0xb8                         # compare subchannel number
	jne   .Lwait4irq
	tsch  0(%r5)

	slr   %r0,%r0
	ic    %r0,8(%r5)                       # get device status
	chi   %r0,8                            # channel end ?
	je    .Lcont
	chi   %r0,12                           # channel end + device end ?
	je    .Lcont

        l     %r0,4(%r5)
        s     %r0,8(%r3)                       # r0/8 = number of ccws executed
        mhi   %r0,10                           # *10 = number of bytes in ccws
        lh    %r3,10(%r5)                      # get residual count
        sr    %r0,%r3                          # #ccws*80-residual=#bytes read
	ar    %r2,%r0
	
        br    %r14                             # r2 contains the total size

.Lcont:
	ahi   %r2,0x640                        # add 0x640 to total size
        la    %r6,.Lccws             
        la    %r7,20
.Lincr:
        l     %r0,4(%r6)                       # update CCW data addresses
        ahi   %r0,0x640
        st    %r0,4(%r6)
        ahi   %r6,8
        brct  7,.Lincr

        j     .Lldlp
.Llderr:
        lpsw  .Lcrash              

        .align 8
.Lorb:	.long  0x00000000,0x0080ff00,.Lccws
.Lirb:	.long  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
.Lcr6:  .long  0xff000000
.Lloadp:.long  0,0
.Lparm:	.long  PARMAREA
.L4malign0:.long 0xffc00000
.Lbigmem:.long 0x04000000
.Lrdstart:.long 0x02000000
.Lcvtab:.long  _ebcasc                         # ebcdic to ascii table
.Lreset:.byte  0xc3,0xc8,0xc1,0xd5,0xc7,0xc5,0x40,0xd9,0xc4,0xd9,0x40
        .byte  0xc1,0xd3,0xd3,0x40,0xd2,0xc5,0xc5,0xd7,0x40,0xd5,0xd6
        .byte  0xc8,0xd6,0xd3,0xc4             # "change rdr all keep nohold"
        .align 8
.Lpcmem0:.long  0x00080000,0x80000000 + .Lchkmem0
.Lcrash:.long  0x000a0000,0x00000000
.Lnewpsw:
        .long  0x00080000,0x80000000+.Lioint
.Lwaitpsw:
        .long  0x020a0000,0x80000000+.Lioint

        .align 8
.Lccws: .rept  19
        .long  0x02600050,0x00000000
        .endr
        .long  0x02200050,0x00000000
	
        .org   0x730     # end of the area loaded by the ipl channel program
#endif  /* CONFIG_IPL_VM */

#endif  /* CONFIG_IPL */

#
# startup-code at 0x10000, running in real mode
# this is called either by the ipl loader or directly by PSW restart or linload
#
        .org  0x10000
        .globl start
start:  basr  %r13,0                     # get base
.LPG1:  lctl  %c1,%c1,.Lpstd-.LPG1(%r13) # load pstd
        lctl  %c7,%c7,.Lpstd-.LPG1(%r13) # load sstd
        lctl  %c13,%c13,.Lpstd-.LPG1(%r13) # load hstd
        lctl  %c0,%c0,.Lcr0-.LPG1(%r13)  # set CR0
	l     %r12,.Lparm1-.LPG1(%r13)   # pointer to parameter area

#
# find out memory size. That is done in the ipl loader too but for
# ipl from dasd the size of the memory has to be detected too...
#
        icm   %r0,15,MEMORY_SIZE-PARMAREA(%r12)
        jnz   .Lsizeok
	mvc   104(8,0),.Lpcmem-.LPG1(%r13) # setup program check handler
        slr   %r1,%r1
        lhi   %r2,1
        sll   %r2,20
.Lloop:	
	l     %r0,0(%r1)                 # test page
	ar    %r1,%r2                    # add 1M
	jnm   .Lloop                     # r1 < 0x80000000 -> loop
.Lchkmem:
	n     %r1,.L4malign-.LPG1(%r13)  # align to multiples of 4M
	st    %r1,MEMORY_SIZE-PARMAREA(%r12)  # store memory size
.Lsizeok:

#
# find out if we are running under VM
#
        stidp  __LC_CPUID               # store cpuid
	tm     __LC_CPUID,0xff          # running under VM ?
	jno    .Lnovm
        oi     MACHINE_FLAGS+3-PARMAREA(%r12),1    # set VM flag
.Lnovm:
        lh     %r0,__LC_CPUID+4         # get cpu version
        chi    %r0,0x7490               # running on a P/390 ?
        jne    .Lnop390
        oi     MACHINE_FLAGS+3-PARMAREA(%r12),4    # set P/390 flag
.Lnop390:

#
# find out if we have an IEEE fpu
#
        mvc    104(8,0),.Lpcfpu-.LPG1(%r13) # setup program check handler
        ld     %f0,.Lflt0-.LPG1(%r13)   # load (float) 0.0
        ldr    %f2,%f0
        adbr   %f0,%f2                  # test IEEE add instruction
        oi     MACHINE_FLAGS+3-PARMAREA(%r12),2    # set IEEE fpu flag
.Lchkfpu:

        lpsw  .Lentry-.LPG1(13)         # jump to _stext in primary-space,
                                        # virtual and never return ...
        .align 8
.Lentry:.long  0x04080000,0x80000000 + _stext
.Lpstd: .long  .Lpgd+0x7F               # segment-table
.Lcr0:  .long  0x04b50002
.Lpcmem:.long  0x00080000,0x80000000 + .Lchkmem
.Lpcfpu:.long  0x00080000,0x80000000 + .Lchkfpu
.Lflt0: .double 0
.Lparm1:.long  PARMAREA
.L4malign:.long 0xffc00000

#
# params at 10400 (setup.h)
#
	.org   PARMAREA
        .long  0x0100                   # ORIG_ROOT_DEV: ramdisk major/minor
        .word  0                        # MOUNT_ROOT_RDONLY: no
	.long  0                        # MEMORY_SIZE
	.long  0                        # MACHINE_FLAGS (bit 0:VM, bit 1:IEEE)
        .long  RAMDISK_ORIGIN           # INITRD_START
        .long  0x800000                 # INITRD_SIZE
	.word  0                        # RAMDISK_FLAGS

        .org   COMMAND_LINE
#       .byte  "root=/dev/nfs rw nfsroot=9.164.160.7:/home/mschwide/nfsboot "
#       .byte  "ip=9.164.147.12:9.164.160.7:9.164.147.1:255.255.255.0:vmlinux:tr0:off"
#       .byte  "root=/dev/nfs nfsroot=9.164.160.7:/home/mschwide/nfsboot "
#       .byte  "ip=9.164.181.228:9.164.160.7:9.164.181.1:255.255.224.0:vmlinux:tr0:off"
#       .byte  "root=/dev/nfs nfsroot=9.164.160.7:/home/pasch/nfsboot "
#       .byte  "ip=9.164.185.120:9.164.160.7:9.164.181.1:255.255.224.0:vmlinux:tr0:off"
#       .byte  "mdisk=402:65536:1229,403:131072:2780 root=/dev/mnda ro"
#       .byte  "root=/dev/nfs rw nfsroot=9.164.160.209:/usr/local/nfsboot "
#       .byte  "ip=9.164.181.228:9.164.160.209:9.164.181.1:255.255.224.0:vmlinux:tr0:off"
    	.byte  "root=/dev/ram0 ro"
#       .byte  0

#
# startup-code, running in virtual mode
#
        .org   0x10800
        .globl _stext
_stext:	basr  %r13,0                    # get base
.LPG2:
#
# Setup lowcore
#
        l     %r1,__LC_IPLDEV           # load ipl device number
        spx   .Lprefix-.LPG2(%r13)      # set prefix to linux lowcore
        st    %r1,__LC_IPLDEV           # store ipl device number
        l     %r15,.Linittu-.LPG2(%r13)
        ahi   %r15,8192                 # init_task_union + 8191
        st    %r15,__LC_KERNEL_STACK    # set end of kernel stack
        ahi   %r15,-96
        xc    0(4,%r15),0(%r15)         # set backchain to zero
        lhi   %r0,-1
        st    %r0,__LC_KERNEL_LEVEL     # set interrupt count to -1
#
# clear bss memory
#
        l     %r2,.Lbss_bgn-.LPG2(%r13) # start of bss
        l     %r3,.Lbss_end-.LPG2(%r13) # end of bss
        sr    %r3,%r2                   # length of bss
        sr    %r4,%r4                   #
        sr    %r5,%r5                   # set src,length and pad to zero
        sr    %r0,%r0                   #
        mvcle %r2,%r4,0                 # clear mem
        jo    .-4                       # branch back, if not finish
# check control registers
        stctl  %c0,%c15,0(%r15)
        oc     2(1,%r15),.Locbits+5-.LPG2(%r13) # enable sigp external ints.
        oc     0(1,%r15),.Locbits+4-.LPG2(%r13) # low addresss proctection
        lctl   %c0,%c15,0(%r15)

#
        lam    0,15,.Laregs-.LPG2(%r13) # load access regs needed by uaccess
        l      %r14,.Lstart-.LPG2(%r13)
        basr   %r14,%r14                # call start_kernel
#
# We returned from start_kernel ?!? PANIK
#
        basr  %r13,0
	lpsw  .Ldw-.(%r13)           # load disabled wait psw
#
.Lstart:    .long  start_kernel
            .align 8
.Lprefix:   .long  init_S390_lowcore	
.Linittu:   .long  init_task_union
.Lbss_bgn:  .long  __bss_start
.Lbss_end:  .long  _end
.Locbits:   .long  0x01020408,0x10204080
            .align 4
.Laregs:    .long  0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0
	    .align 8
.Ldw:	    .long  0x000a0000,0x00000000

#
#        tempory segment-table at 0x11000
#
        .org   0x11000
.Lpgd:  .long  .Lpt0+0x1f               # 00000000-000fffff
        .long  .Lpt1+0x1f               # 00100000-001fffff
        .long  .Lpt2+0x1f               # 00200000-002fffff
        .long  .Lpt3+0x1f               # 00300000-003fffff
        .fill  2044,4,0x20              # 00400000-7fffffff

#
#        tempory page-tables at 0x12000-0x15fff
#
        .macro  mktable from,to
        .long   \from*0x10000
        .long   \from*0x10000+0x1000
        .long   \from*0x10000+0x2000
        .long   \from*0x10000+0x3000
        .long   \from*0x10000+0x4000
        .long   \from*0x10000+0x5000
        .long   \from*0x10000+0x6000
        .long   \from*0x10000+0x7000
        .long   \from*0x10000+0x8000
        .long   \from*0x10000+0x9000
        .long   \from*0x10000+0xa000
        .long   \from*0x10000+0xb000
        .long   \from*0x10000+0xc000
        .long   \from*0x10000+0xd000
        .long   \from*0x10000+0xe000
        .long   \from*0x10000+0xf000
        .if     \to-\from
        mktable "(\from+1)",\to
        .endif
        .endm

.Lpt0:  mktable 0,15
.Lpt1:  mktable 16,31
.Lpt2:  mktable 32,47
.Lpt3:  mktable 48,63

