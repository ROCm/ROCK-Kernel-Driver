/*
 * BIOS Enhanced Disk Drive support
 * by Matt Domsch <Matt_Domsch@dell.com> October 2002
 * conformant to T13 Committee www.t13.org
 *   projects 1572D, 1484D, 1386D, 1226DT
 * disk signature read by Matt Domsch <Matt_Domsch@dell.com>
 *	and Andrew Wilks <Andrew_Wilks@dell.com> September 2003, June 2004
 * legacy CHS retreival by Patrick J. LoPresti <patl@users.sourceforge.net>
 *      March 2004
 * Use EXTENDED READ calls if possible, Matt Domsch, October 2004
 */

#include <linux/edd.h>

#if defined(CONFIG_EDD) || defined(CONFIG_EDD_MODULE)
	movb	$0, (EDD_MBR_SIG_NR_BUF)	# zero value at EDD_MBR_SIG_NR_BUF
#ifndef CONFIG_EDD_SKIP_MBR
	xorl	%edx, %edx
	movb	$0x80, %dl			# from device 80

edd_mbr_check_ext:
	xorl	%eax, %eax
	xorl	%ebx, %ebx
	xorl	%ecx, %ecx
	movb	$CHECKEXTENSIONSPRESENT, %ah    # Function 41
	movw	$EDDMAGIC1, %bx			# magic
	pushw	%dx		                # work around buggy BIOSes
	stc					# work around buggy BIOSes
	int	$0x13				# make the call
	sti					# work around buggy BIOSes
	popw	%dx
	jc	edd_start			# no more BIOS devices
	cmpw	$EDDMAGIC2, %bx			# is magic right?
	jne	edd_mbr_sig_next		# nope, next...
	testw	$FIXEDDISKSUBSET, %cx		# EXTENDED READ supported?
	jz	edd_mbr_read_sectors		# nope, use READ SECTORS

edd_mbr_extended_read:
# Fill out the device address packet here, make the fn42 call
	xorl	%eax, %eax
	xorl	%ebx, %ebx
	xorl	%ecx, %ecx
	subw	$EDD_DEV_ADDR_PACKET_LEN, %sp	# put packet on stack
	pushw	%si
	movw	%sp, %si
	movl	$0,   (%si)                      # zero out packet
	movl	$0,  4(%si)
	movl	$0,  8(%si)
	movl	$0, 12(%si)
	movb	$EDD_DEV_ADDR_PACKET_LEN, (%si) # length of packet
	movb	$1, 2(%si)			# move 1 sector
	movw	$EDDBUF, 4(%si)			# into EDDBUF
	movw	%ds, 6(%si)			# EDDBUF seg is ds
	movb	$EXTENDEDREAD, %ah
	pushw	%dx		                # work around buggy BIOSes
	stc					# work around buggy BIOSes
	int	$0x13
	sti					# work around buggy BIOSes
	popw	%dx
	popw	%si
	addw	$EDD_DEV_ADDR_PACKET_LEN, %sp	# remove packet from stack
	jnc   edd_mbr_store_sig
	# otherwise, fall through to the legacy read function

edd_mbr_read_sectors:
# Read the first sector of each BIOS disk device and store the 4-byte signature
	xorl	%eax, %eax
	xorl	%ebx, %ebx
	xorl	%ecx, %ecx
	movb	$READ_SECTORS, %ah
	movb	$1, %al				# read 1 sector
	movb	$0, %dh				# at head 0
	movw	$1, %cx				# cylinder 0, sector 0
	pushw	%es
	pushw	%ds
	popw	%es
	movw	$EDDBUF, %bx			# disk's data goes into EDDBUF
	pushw	%dx		                # work around buggy BIOSes
	stc					# work around buggy BIOSes
	int	$0x13
	sti					# work around buggy BIOSes
	popw	%dx
	popw	%es
	jc	edd_mbr_sig_done		# on failure, we're done.

edd_mbr_store_sig:
	xorl	%ebx, %ebx			# clear ebx
	movb	%dl, %bl			# copy drive number to ebx
	sub	$0x80, %bl			# subtract 80h from drive number
	shlw	$2, %bx				# multiply by 4
	addw	$EDD_MBR_SIG_BUF, %bx		# add to sig_buf
						# bx now points to the right sig slot
	movl	(EDDBUF+EDD_MBR_SIG_OFFSET), %eax # read sig out of the MBR
	movl	%eax, (%bx)			# store success
	incb	(EDD_MBR_SIG_NR_BUF)		# note that we stored something
edd_mbr_sig_next:
	incb	%dl				# increment to next device
	cmpb	$EDD_MBR_SIG_MAX, (EDD_MBR_SIG_NR_BUF)	# Out of space?
	jb	edd_mbr_check_ext		# keep looping
edd_mbr_sig_done:
#endif

# Do the BIOS Enhanced Disk Drive calls
# This consists of two calls:
#    int 13h ah=41h "Check Extensions Present"
#    int 13h ah=48h "Get Device Parameters"
#    int 13h ah=08h "Legacy Get Device Parameters"
#
# A buffer of size EDDMAXNR*(EDDEXTSIZE+EDDPARMSIZE) is reserved for our use
# in the boot_params at EDDBUF.  The first four bytes of which are
# used to store the device number, interface support map and version
# results from fn41.  The next four bytes are used to store the legacy
# cylinders, heads, and sectors from fn08. The following 74 bytes are used to
# store the results from fn48.  Starting from device 80h, fn41, then fn48
# are called and their results stored in EDDBUF+n*(EDDEXTSIZE+EDDPARMIZE).
# Then the pointer is incremented to store the data for the next call.
# This repeats until either a device doesn't exist, or until EDDMAXNR
# devices have been stored.
# The one tricky part is that ds:si always points EDDEXTSIZE bytes into
# the structure, and the fn41 and fn08 results are stored at offsets
# from there.  This removes the need to increment the pointer for
# every store, and leaves it ready for the fn48 call.
# A second one-byte buffer, EDDNR, in the boot_params stores
# the number of BIOS devices which exist, up to EDDMAXNR.
# In setup.c, copy_edd() stores both boot_params buffers away
# for later use, as they would get overwritten otherwise.
# This code is sensitive to the size of the structs in edd.h
edd_start:
						# %ds points to the bootsector
       						# result buffer for fn48
	movw	$EDDBUF+EDDEXTSIZE, %si		# in ds:si, fn41 results
						# kept just before that
	movb	$0, (EDDNR)			# zero value at EDDNR
	xorl	%edx, %edx
	movb	$0x80, %dl			# BIOS device 0x80

edd_check_ext:
	pushw	%di				# zero out this edd_info block
	pushw	%es
	movw	%ds, %ax
	movw	%ax, %es
	movw	%si, %ax
	subw	$EDDEXTSIZE, %ax
	movw	%ax, %di
	movl	$EDDEXTSIZE+EDDPARMSIZE, %ecx
	xorl	%eax, %eax
	cld
	rep
	stosb
	popw	%es
	popw	%di

	xorl	%eax, %eax
	xorl	%ebx, %ebx
	xorl	%ecx, %ecx
	movb	$CHECKEXTENSIONSPRESENT, %ah    # Function 41
	movw	$EDDMAGIC1, %bx			# magic
	pushw	%dx		                # work around buggy BIOSes
	stc					# work around buggy BIOSes
	int	$0x13				# make the call
	sti					# work around buggy BIOSes
	popw	%dx
	jc	edd_done			# no more BIOS devices

	cmpw	$EDDMAGIC2, %bx			# is magic right?
	jne	edd_next			# nope, next...

	movb	%dl, %ds:-8(%si)		# store device number
	movb	%ah, %ds:-7(%si)		# store version
	movw	%cx, %ds:-6(%si)		# store extensions
	incb	(EDDNR)				# note that we stored something

     	testw	$GET_DEVICE_PARAMETERS_SUPPORTED, %cx
	jz	edd_get_legacy_chs	# nope, skip fn48

edd_get_device_params:
	xorl	%eax, %eax
	xorl	%ebx, %ebx
	xorl	%ecx, %ecx
	movw	$EDDPARMSIZE, %ds:(%si)		# put size
	movb	$GETDEVICEPARAMETERS, %ah	# Function 48
	pushw	%dx		                # work around buggy BIOSes
	stc					# work around buggy BIOSes
	int	$0x13				# make the call
	sti					# work around buggy BIOSes
	popw	%dx
						# Don't check for fail return
						# it doesn't matter.
edd_get_legacy_chs:
	xorl	%eax, %eax
	xorl	%ebx, %ebx
	xorl	%ecx, %ecx
	movw    %ax, %ds:-4(%si)
	movw    %ax, %ds:-2(%si)
	# Ralf Brown's Interrupt List says to set ES:DI to
	# 0000h:0000h "to guard against BIOS bugs"
     	pushw   %es
	movw    %ax, %es
	movw    %ax, %di
	pushw   %dx                             # legacy call clobbers %dl
	movb    $LEGACYGETDEVICEPARAMETERS, %ah # Function 08
	stc					# work around buggy BIOSes
	int     $0x13                           # make the call
	sti					# work around buggy BIOSes
	jc      edd_legacy_done                 # failed
	movb    %cl, %al                        # Low 6 bits are max
	andb    $0x3F, %al                      #   sector number
	movb	%al, %ds:-1(%si)                # Record max sect
	movb    %dh, %ds:-2(%si)                # Record max head number
	movb    %ch, %al                        # Low 8 bits of max cyl
	shr     $6, %cl
	movb    %cl, %ah                        # High 2 bits of max cyl
	movw    %ax, %ds:-4(%si)

edd_legacy_done:
	popw    %dx
	popw    %es
	movw	%si, %ax			# increment si
	addw	$EDDPARMSIZE+EDDEXTSIZE, %ax
	movw	%ax, %si

edd_next:
	incb	%dl				# increment to next device
	cmpb	$EDDMAXNR, (EDDNR) 		# Out of space?
	jb	edd_check_ext			# keep looping

edd_done:
#endif
