/*
Colour conversion routines (RGB <-> YUV) in x86 assembly
 
(C) 2000 Nemosoft Unv.    nemosoft@smcc.demon.nl
   
This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.   

*/


/* The ccvt_* functions always start with width and height, so these
   parameters are in 8(%ebp) and 12 (%ebp). The other parameters can be
   2 to 4 pointers, and one of these combinations:
   *src, *dst
   *srcy, *srcu, *srv, *dst
   *src, *dsty, *dstu, *dstv
 */   

#define __ASSEMBLY__
#include <linux/linkage.h>

#define Width   8(%ebp)
#define Height 12(%ebp)

/* 2 parameters, 1 in, 1 out */
#define Src2 16(%ebp)
#define Dst2 20(%ebp)

/* 4 parameters, 3 in, 1 out */
#define SrcY 16(%ebp)
#define SrcU 20(%ebp)
#define SrcV 24(%ebp)
#define Dst4 28(%ebp)

/* 4 parameters, 1 in, 3 out */
#define Src4 16(%ebp)
#define DstY 20(%ebp)
#define DstU 24(%ebp)
#define DstV 28(%ebp)

/* This buffer space used to be staticly allocted, but this is going to
   give problems with multiple cams (though I have yet to see it).
   Therefor, we reserve at least 64 + 8 = 72 bytes on the stack with 
   `enter'.
 */

#define PixelBuffer -64(%ebp)
#define Uptr        -68(%ebp)
#define Vptr        -72(%ebp)

	.text

/* This function will load the src and destination pointers, including
   Uptr/Vptr when necessary, and test the width/height parameters.
   - %esi will be set to Src or SrcY
   - %edi will be set to Dst or DstY
   the carry flag will be set if any of these tests fail. 
   It assumes %ebp has been set.
 */
/* 2 parameters, src & dst */
test_param_2:
	mov Src2, %esi
	mov Dst2, %edi
	
	cmp $0, %esi		# NULL pointers?
	je param_fail
	cmp $0, %edi
	je param_fail

	jmp test_width_height

/* 3 inputs, 1 output */
test_param_31:
	mov Dst4, %edi		# NULL pointers
	cmp $0, %edi
	je param_fail
	
	mov SrcV, %esi
	cmp $0, %esi
	je param_fail
	mov %esi, Vptr

	mov SrcU, %esi
	cmp $0, %esi
	je param_fail
	mov %esi, Uptr
	
	mov SrcY, %esi
	cmp $0, %esi
	je param_fail
	
	jmp test_width_height

/* 1 input, 3 output */	
test_param_13:
	mov Src4, %esi		# NULL pointers
	cmp $0, %esi
	je param_fail
	
	mov DstV, %edi
	cmp $0, %edi
	je param_fail
	mov %edi, Vptr
	
	mov DstU, %edi
	cmp $0, %edi
	je param_fail
	mov %edi, Uptr
	
	mov DstY, %edi
	cmp $0, %edi
	je param_fail
	
	jmp test_width_height
	
	nop

test_width_height:
	cmpl $0, Width
	jbe param_fail
	testl $3, Width		# multiple of 4?
	jnz param_fail		# Nope...

	cmp $0, Height		# check illegal height
	jbe param_fail
	testl $1, Height	# Odd no. of lines?
	jnz param_fail		# Aye

	/* fall through */

/* exit points */
param_ok:
	clc			# Success: clear carry
	ret

param_fail:
	stc			# Fail: set carry
	ret



# This will fill PixelBuffer with 4 grey scale pixels (Y)
# In: 		%eax = Value (Y3Y2Y1Y0)
# Out:
# Modifies:	%ecx (-4)
# Destroys: 	%edx
expand_4_y:
	mov %eax, %edx		# Keep in edx (we need eax)
	lea PixelBuffer, %edi	
	
0:	# This code is executed 4 times
	movzbl %dl, %eax	# move, zero extending byte-to-long
	shl $8, %eax		# 8 digit precision
	
	stosl			# Expand into PixelBuffer
	stosl
	stosl
	add $4, %edi		# Skip alpha

	shr $8, %edx		# next Y

	dec %ecx
	test $3, %ecx
	jnz 0b

	ret			# from expand_4_y
	
# This will add the color factors to the (grey) values in PixelBuffer
# In:		%ebx (U1U0V1V0)
# Out:
# Modifies:
# Destroys:	%edi, %ebx, %eax, %edx
expand_4_uv:
	lea PixelBuffer, %edi	# reset pointer

	# V0
	sub $128, %bl
	movsbl %bl, %eax
	mov $359, %edx		# Vr
	mul %edx
	add %eax, 0x00(%edi)
	add %eax, 0x10(%edi)
	
	movsbl %bl, %eax
	mov $183, %edx		# Vg
	mul %edx
	sub %eax, 0x04(%edi)
	sub %eax, 0x14(%edi)
	
	# V1
	sub $128, %bh
	movsbl %bh, %eax
	mov $359, %edx		# Vr
	mul %edx
	add %eax, 0x20(%edi)
	add %eax, 0x30(%edi)
	
	movsbl %bh, %eax
	mov $183, %edx		# Vg
	mul %edx
	sub %eax, 0x24(%edi)
	sub %eax, 0x34(%edi)
	
	# U0
	bswap %ebx		# Get U values in lower half
	sub $128, %bh
	movsbl %bh, %eax
	mov $88, %edx		# Ug
	mul %edx
	sub %eax, 0x04(%edi)
	sub %eax, 0x14(%edi)

	movsbl %bh, %eax
	mov $454, %edx		# Ub
	mul %edx
	add %eax, 0x08(%edi)
	add %eax, 0x18(%edi)
	
	# U1
	sub $128, %bl
	movsbl %bl, %eax
	mov $88, %edx		# Ug
	mul %edx
	sub %eax, 0x24(%edi)
	sub %eax, 0x34(%edi)
	
	movsbl %bl, %eax
	mov $454, %edx		# Ub
	mul %edx
	add %eax, 0x28(%edi)
	add %eax, 0x38(%edi)
	ret			# expand_4_uv


/* This function expands 4 420i pixels into PixelBuffer */
do_four_yuvi:
	push %edi

	lodsl			# 4 bytes at a time
	
	call expand_4_y
	
	# now do UV values. on even lines, Y is followed by U values; on 
	# odd lines V values follow. The U and V values are always pushed
	# on the stack in this order:
	# U V
	
	# First, calculate offset per line (1.5 * width)
	mov Width, %ebx	# width
	shl %ebx		# 2 *
	add Width, %ebx	# 3 * 
	shr %ebx		# 1.5 *

	# even or odd lines	
	testl $1, Height
	jz 2f

	# odd line; we are at V data, but do U data first
	neg %ebx		# make ebx offset negative
	mov (%esi,%ebx),%ax	# U
	push %ax
	lodsw			# V
	push %ax
	jmp 3f	
	
2:	# even line
	lodsw			# U
	push %ax
	sub $2, %ebx
	mov (%esi,%ebx), %ax	# V
	push %ax

3:	# Okay, so we now have the U and V values... expand into PixelBuffer

	pop %ebx
	call expand_4_uv

	pop %edi
	ret			# from do_four_yuvi


# Do four pixels, in planar format
do_four_yuvp:
	push %edi

	# The first part is the same as for interlaced (4 bytes Y)
	lodsl			# 4 bytes at a time
	call expand_4_y
	
	# now gather U and V values... 
	mov Uptr, %ebx		# Use Uptr/Vptr
	mov (%ebx), %ax
	push %ax
	add $2, %ebx
	mov %ebx, Uptr

	mov Vptr, %ebx
	mov (%ebx), %ax
	push %ax
	add $2, %ebx
	mov %ebx, Vptr
	
	pop %ebx
	call expand_4_uv
	
	pop %edi
	ret


# Do four pixels, in yuyv interlaced format
do_four_yuyv:
	push %edi

	lodsl			# v0y1u0y0
	mov %eax, %ebx
	bswap %ebx		# y0u0y1v0
	mov %bh, %ah		# v0y1y1y0
	and $0x00ff00ff, %ebx	# __u0__v0
	push %ax		# y1y0

	lodsl			# v1y3u1y2	# mix register instructions
	mov %eax, %edx				# so CPU pipeline doesnt stall
	rol $16, %eax		# u1y2v1y3	
	mov %dl, %dh		# v1y3y2y2
	and $0xff00ff00, %eax	# u1__v1__
	mov $0, %dl		# v1y3y2__
	or %eax, %ebx		# u1u0v1v0
	shl $8, %edx		# y3y2____
	pop %dx			# y3y2y1y0
	mov %edx, %eax
	call expand_4_y
	call expand_4_uv
	
	pop %edi
	ret

limit_pixels:
	# Limit all values in PixelBuffer
	push %esi
	push %edi
	push %ecx
	lea PixelBuffer, %esi
	mov %esi, %edi
	mov $16, %ecx
0:	lodsl
	cmp $0, %eax		# this would have been a perfect spot for CMOVxx instructions...
	jl 2f			#  except they only work on Pentium Pro processors,
	cmp $0xff00, %eax	#  and not even all of them
	jg 3f
	add $4, %edi		# no use for stosl here
	loop 0b
	jmp 9f
2:	mov $0, %eax
	stosl
	loop 0b
	jmp 9f
3:	mov $0xff00, %eax
	stosl
	loop 0b
	jmp 9f

9:	pop %ecx
	pop %edi
	pop %esi
	ret			# from limit_pixels

/* Copy RGB values from PixelBuffer into destination buffer, 4 bytes
   with alpha 
 */

/* Push 3 pixel (12 bytes), in correct order */
push_rgb24:
	push %ecx
	push %esi
	lea PixelBuffer, %esi
	mov $4, %ecx
0:	lodsl
	shr $8, %eax
	mov %al, (%edi)  	# Red
	lodsl
	shr $8, %eax
	mov %al, 1(%edi)	# Green
	lodsl
	shr $8, %eax
	mov %al, 2(%edi)	# Blue
	add $3, %edi
	lodsl			# dummy
	loop 0b
	pop %esi
	pop %ecx
	ret

/* Push 3 pixels (12 bytes), in wrong order */
push_bgr24:
	push %ecx
	push %esi
	lea PixelBuffer, %esi
	mov $4, %ecx
0:	lodsl
	shr $8, %eax
	mov %al, 2(%edi)	# Red
	lodsl
	shr $8, %eax
	mov %al, 1(%edi)	# Green
	lodsl
	shr $8, %eax
	mov %al, (%edi)		# Blue
	add $3, %edi
	lodsl			# dummy
	loop 0b
	pop %esi
	pop %ecx
	ret

/* The simplest format: push 4 bytes, RGBa */
push_rgb32:
	push %ecx
	push %esi
	mov $16, %ecx
	lea PixelBuffer, %esi
0:	lodsl			# red
	shr $8, %eax		# 8 bit precision
	stosb
	loop 0b
	pop %esi
	pop %ecx
	ret


/* Gosh. Would you believe it. They even made this format... (Qt 2.*) */
push_bgr32:
	# copy all 4 values to output buffer
	push %ecx
	push %esi
	mov $4, %ecx
	lea PixelBuffer, %esi
0:	lodsl			# red
	shr $8, %eax		# 8 bit precision
	mov %al, 2(%edi)
	lodsl			# green
	shr $8, %eax
	mov %al, 1(%edi)
	lodsl			# blue
	shr $8, %eax
	mov %al, (%edi)
	add $4, %edi
	lodsl			# dummy
	loop 0b	
	pop %esi
	pop %ecx
	ret

/*************************************/

/* Functions to go from YUV interlaced formats to RGB */

/* Go from interlaced to RGB, red first */

ENTRY(ccvt_420i_rgb24)
	enter $72, $0		# no extra space, no stackframes
	push %ebx
	push %esi
	push %edi

	call test_param_2
	jc 9f
	
0:	mov Width, %ecx		# width
1:	call do_four_yuvi
	call limit_pixels
	call push_rgb24
			
	cmp $0, %ecx
	jnz 1b			# end of line?
	decl Height		# yes; decrement line counter
	jnz 0b

9:	pop %edi
	pop %esi
	pop %ebx
	leave
	ret

/* Go from interlaced to BGR, blue first */

ENTRY(ccvt_420i_bgr24)
	enter $72, $0		# no extra space, no stackframes
	push %ebx
	push %esi
	push %edi

	call test_param_2
	jc 9f
	
0:	mov Width, %ecx	# width
1:	call do_four_yuvi
	call limit_pixels
	call push_bgr24
			
	cmp $0, %ecx
	jnz 1b			# end of line?
	decl Height		# yes; decrement line counter
	jnz 0b

9:	pop %edi
	pop %esi
	pop %ebx
	leave
	ret


/* From interlaced to RGBa */

ENTRY(ccvt_420i_rgb32)
	enter $72, $0		# no extra space, no stackframes
	push %ebx
	push %esi
	push %edi

	call test_param_2
	jc 9f

0:	mov Width, %ecx		# width
1:	call do_four_yuvi
	call limit_pixels
	call push_rgb32
		
	cmp $0, %ecx		# end of line?
	jnz 1b
	decl Height		# yes; decrement line counter
	jnz 0b

9:	pop %edi
	pop %esi
	pop %ebx
	leave
	ret

/* Guess what? Go from interlaced to BGRa */

ENTRY(ccvt_420i_bgr32)
	enter $72, $0		# no extra space, no stackframes
	push %ebx
	push %esi
	push %edi

	call test_param_2
	jc 9f

0:	mov Width, %ecx		# width
1:	call do_four_yuvi
	call limit_pixels
	call push_bgr32
		
	cmp $0, %ecx		# end of line?
	jnz 1b
	decl Height		# yes; decrement line counter
	jnz 0b

9:	pop %edi
	pop %esi
	pop %ebx
	leave
	ret




/* From YUYV to RGBa */

ENTRY(ccvt_yuyv_rgb32)
	enter $72, $0		# no extra space, no stackframes
	push %ebx
	push %esi
	push %edi

	call test_param_2
	jc 9f
	
0:	mov Width, %ecx		# width
1:	call do_four_yuyv
	call limit_pixels
	call push_rgb32
		
	cmp $0, %ecx		# end of line?
	jnz 1b

8:	decl Height		# yes; decrement line counter
	jnz 0b

9:	pop %edi
	pop %esi
	pop %ebx
	leave
	ret

/* From YUYV to BGRa */
ENTRY(ccvt_yuyv_bgr32)
	enter $72, $0		# no extra space, no stackframes
	push %ebx
	push %esi
	push %edi

	call test_param_2
	jc 9f
	
	# YUYV -> RGBa RGBa

0:	mov Width, %ecx		# width
1:	call do_four_yuyv
	call limit_pixels
	call push_bgr32
		
	cmp $0, %ecx		# end of line?
	jnz 1b

8:	decl Height		# yes; decrement line counter
	jnz 0b

9:	pop %edi
	pop %esi
	pop %ebx
	leave
	ret




/* Planar to RGBa */

ENTRY(ccvt_420p_rgb32)
	enter $72, $0
	push %ebx
	push %esi
	push %edi
	
	call test_param_31
	jc 9f

	mov Width, %eax		# width
	mull Height		# * height
	mov SrcU, %eax		# Copy U/V pointers
	mov %eax, Uptr
	mov SrcV, %eax
	mov %eax, Vptr

0:	mov Width, %ecx		# width
1:	call do_four_yuvp
	call limit_pixels
	call push_rgb32	
		
	cmp $0, %ecx		# end of line?
	jnz 1b

	testl $1, Height	# odd/even line
	jnz 8f
	
	mov Width, %eax		# Even: rewind U/V pointers
	shr %eax
	sub %eax, Uptr
	sub %eax, Vptr

8:	decl Height		# yes; decrement line counter
	jnz 0b

9:	pop %edi
	pop %esi
	pop %ebx
	leave
	ret

/* Okay... eventually, you end up with a very complete set of conversion
   routines. I just wished things were a bit simpler. */

ENTRY(ccvt_420p_bgr32)
	enter $72, $0
	push %ebx
	push %esi
	push %edi
	
	call test_param_31
	jc 9f
	
	mov Width, %eax		# width
	mull Height		# * height
	mov SrcU, %eax		# Copy U/V pointers
	mov %eax, Uptr
	mov SrcV, %eax
	mov %eax, Vptr

0:	mov Width, %ecx		# width
1:	call do_four_yuvp
	call limit_pixels
	call push_bgr32
		
	cmp $0, %ecx		# end of line?
	jnz 1b

	testl $1, Height	# odd/even line
	jnz 8f
	
	mov Width, %eax		# Even: rewind U/V pointers
	shr %eax
	sub %eax, Uptr
	sub %eax, Vptr

8:	decl Height		# yes; decrement line counter
	jnz 0b

9:	pop %edi
	pop %esi
	pop %ebx
	leave
	ret




/* Go from RGB (red first) to 4:2:0 planar.
 * Note: this requires decimation of the U/V space by 2 in both directions 
 * Also, a matrix multiply would be QUITE convenient...

   This is the matrix:
     (Y )   ( 77  150   29)   (R)
     (Cb) = (-43  -85  128) * (G)
     (Cr)   (128 -107  -21)   (B)
 */

ENTRY(ccvt_rgb24_420p)
	enter $96, $0		# 24 bytes extra stack, no stackframes
	push %ebx		#  -76: line width in bytes
	push %esi		#  -80: height (copy)
	push %edi		#  -84: width (copy)
				#  -88: red factor
				#  -92: green factor
				#  -96: blue factor
	call test_param_13
	jc 9f

	mov Width, %eax
	shl %eax
	add Width, %eax		# 3 * width = line increment
	mov %eax, -76(%ebp)

	mov Height, %eax
	mov %eax, -80(%ebp)	# copy height into stackframe
	
	/*
	  This is a bit complicated... since U/V decimation is taking 
	  place both in horizontal and vertical direction, we have to
	  process 2 lines in parallel. Also, 2 adjacent pixels are
	  considered. We average the U/V values over these 4 pixels
	  (of course, we could have just taken the U/V value of the first
	  pixel and be done with it, but that's not how we do things around
	  here)
	 */
	
	# 1st pass: Y values. Set factors	
	movl $77 , -88(%ebp)	# 0.299
	movl $150, -92(%ebp)	# 0.587
	movl $29 , -96(%ebp)	# 0.114

0:	mov Width, %ecx		# width
1:	xor %ebx, %ebx		# 0
	call rgb_multiply
	shr $8, %ebx		# divide by 256 (no need for limitor, since 77 + 150 + 29 = 256)
	mov %bl, %al
	stosb			# store it into Y buffer
	
	dec %ecx		# end of line?
	jnz 1b
	decl -80(%ebp)		# end of image?
	jnz 0b

	# Okay, now the U/V pointers... 
	# The following code is passed twice, with different factors
	# Note that the %esi pointer jumps around quite a bit

	# factors for U
	movl $-43, -88(%ebp)	# -0.1687
	movl $-85, -92(%ebp)	# -0.3313
	movl $128, -96(%ebp)	# 0.5
	mov DstU, %edi		# Set %edi register now
		
7:	mov Src4, %esi		# Rewind source pointer

	mov Height, %eax	# height
	shr %eax		#  / 2
	mov %eax, -80(%ebp)	#   copy

2:	mov Width, %eax		# width
	shr %eax		#  / 2
	mov %eax, -84(%ebp)	#   copy

3:	xor %ebx, %ebx		# 0
	mov $4, %ecx		# average over 4 pixels

4:	call rgb_multiply

	dec %ecx
	jz 5f			# done?
	cmp $2, %ecx		# 3rd pixel.. move %esi to next line, with offset
	jne 4b
	sub $6, %esi		# backup to where we started
	add -76(%ebp), %esi	# add line increment
	jmp 4b

5:	# okay, 4 pixels done... 
	sub -76(%ebp), %esi	# Get %esi back to its proper place

	add $0x20000, %ebx	# add 0.5 factor
	shr $10, %ebx		# Divide by 4 * 256
	mov %bl, %al
	stosb			# store it!

	decl -84(%ebp)		# end of line?
	jnz 3b
	add -76(%ebp), %esi	# %esi to next line (actually, 2 lines further)
	decl -80(%ebp)		# end of image?
	jnz 2b

	# check if 3rd pass has been done
	cmpl $128, -88(%ebp)
	je 9f			# Done!
	# Set factors for V pass
	movl $128 , -88(%ebp)	# 0.5
	movl $-107, -92(%ebp)	# -0.4187
	movl $-21 , -96(%ebp)	# -0.0813
	mov DstV, %edi		# %edi to V buffer
	jmp 7b			# "Do it to me one more time..."

9:	pop %edi
	pop %esi
	pop %ebx
	leave
	ret




ENTRY(ccvt_bgr24_420p)
	enter $96, $0		# 24 bytes extra stack, no stackframes
	push %ebx		#   -4: line width in bytes
	push %esi		#   -8: height (copy)
	push %edi		#  -12: width (copy)
				#  -16: red factor
				#  -20: green factor
				#  -24: blue factor
	call test_param_13
	jc 9f

	/* No surprise, this code looks just like rgb24_420p, but with swapped factors */
	 
	mov Width, %eax
	shl %eax
	add Width, %eax		# 3 * width = line increment
	mov %eax, -76(%ebp)

	mov Height, %eax
	mov %eax, -80(%ebp)	# copy height into stackframe
	
	# 1st pass: Y values. Set factors	
	movl $29 , -88(%ebp)	# 0.114
	movl $150, -92(%ebp)	# 0.587
	movl $77 , -96(%ebp)	# 0.299

0:	mov Width, %ecx		# width
1:	xor %ebx, %ebx		# 0
	call rgb_multiply
	shr $8, %ebx		# divide by 256 (no need for limitor, since 77 + 150 + 29 = 256)
	mov %bl, %al
	stosb			# store it into Y buffer
	
	dec %ecx		# end of line?
	jnz 1b
	decl -80(%ebp)		# end of image?
	jnz 0b

	# Okay, now the U/V pointers... 
	# The following code is passed twice, with different factors
	# Note that the %esi pointer jumps around quite a bit

	# factors for U
	movl $123, -88(%ebp)	#  0.5
	movl $-85, -92(%ebp)	# -0.3313
	movl $-43, -96(%ebp)	# -0.1687
	mov DstU, %edi		# Set %edi register now
		
7:	mov Src4, %esi		# Rewind source pointer

	mov Height, %eax	# height
	shr %eax		#  / 2
	mov %eax, -80(%ebp)	#   copy

2:	mov Width, %eax		# width
	shr %eax		#  / 2
	mov %eax, -84(%ebp)	#   copy

3:	xor %ebx, %ebx		# 0
	mov $4, %ecx		# average over 4 pixels

4:	call rgb_multiply

	dec %ecx
	jz 5f			# done?
	cmp $2, %ecx		# 3rd pixel.. move %esi to next line, with offset
	jne 4b
	sub $6, %esi		# backup to where we started
	add -76(%ebp), %esi	# add line increment
	jmp 4b

5:	# okay, 4 pixels done... 
	sub -76(%ebp), %esi	# Get %esi back to its proper place

	add $0x20000, %ebx	# add 0.5 factor
	shr $10, %ebx		# Divide by 4 * 256
	mov %bl, %al
	stosb			# store it!

	decl -84(%ebp)		# end of line?
	jnz 3b
	add -76(%ebp), %esi	# %esi to next line (actually, 2 lines further)
	decl -80(%ebp)		# end of image?
	jnz 2b

	# check if 3rd pass has been done
	cmpl $-21, -88(%ebp)
	je 9f			# Done!
	# Set factors for V pass
	movl $-21 , -88(%ebp)	# -0.0813
	movl $-107, -92(%ebp)	# -0.4187
	movl $128 , -96(%ebp)	#  0.5
	mov DstV, %edi		# %edi to V buffer
	jmp 7b			# "Do it to me one more time..."

9:	pop %edi
	pop %esi
	pop %ebx
	leave
	ret


/* RGB-to-YUV helper functions */

rgb_multiply:
	# do one RGB vector multiplication; its assumed the RGB factors
	# are set on the stack. The data is accumulated in ebx.
	lodsb			# red byte
	and $0xff, %eax
	mov -88(%ebp), %edx	# red factor
	mul %edx
	add %eax, %ebx
	lodsb			# green byte
	and $0xff, %eax
	mov -92(%ebp), %edx	# green factor
	mul %edx
	add %eax, %ebx
	lodsb			# blue byte
	and $0xff, %eax
	mov -96(%ebp), %edx	# blue factor
	mul %edx
	add %eax, %ebx		# ebx now contains sum
	ret



/**************************************************************************/


/* Go from 'interlaced' (YYYY UU/VV) format to planar */

ENTRY(ccvt_420i_420p)
	enter $76, $0		# 4 bytes extra space, no stackframes
	push %ebx		# -4: width / 4
	push %esi
	push %edi

	call test_param_13
	jc 9f

	# Okay, this is fairly easy... we first grab the Y values (4 bytes
	#  at a time), then rewind and do the U values, and repeat for V.
	#  This leaves us with a nice planar format

	mov Width, %eax
	shr %eax
	shr %eax		# width / 4
	mov %eax, -76(%ebp)	# Store

	# Y
	mov Height, %edx	# line counter
0:	mov -76(%ebp), %ecx
1:	lodsl			# get 4 bytes...
	stosl			# ...push 4 bytes
	add $2, %esi		# Skip U or V
	loop 1b
	dec %edx
	jnz 0b

	# U
	mov Src4, %esi		# rewind source pointer
	mov DstU, %edi
	add $4, %esi		# set to U 
	mov Height, %edx
	shr %edx		# height / 2
	mov Width, %ebx
	shl %ebx
	add Width, %ebx
	shr %ebx		# Width * 1.5 (line offset)

2:	mov -76(%ebp), %ecx	# width / 4
3:	lodsw			# 2 bytes at a time
	stosw
	add $4, %esi		# skip Y
	loop 3b
	add %ebx, %esi		# Skip line (U is on even lines)
	dec %edx
	jnz 2b
	
	# V
	mov Src4, %esi		# rewind, set to V in first odd line
	add $4, %esi
	add %ebx, %esi		# register re-use; no compiler can beat that :)
	mov DstV, %edi		# V ptr
	mov Height, %edx
	shr %edx		# height / 2
	
4:	mov -76(%ebp), %ecx	# Get width/4
5:	lodsw
	stosw
	add $4, %esi		# Skip Y
	loop 5b
	add %ebx, %esi		# Skip line (V is on odd lines)
	dec %edx
	jnz 4b
	
	/* That's it! */
	
9:	pop %edi
	pop %esi
	pop %ebx
	leave
	ret


/* Go from 4:2:0 interlaced to 'normal' YUYV */

ENTRY(ccvt_420i_yuyv)
	enter $80, $0		# 8 bytes extra space, no stackframes
	push %ebx
	push %esi
	push %edi

	call test_param_2
	jc 9f
	
	mov Width, %ecx		# -4: width / 4 = no. loops per line
	shr %ecx
	shr %ecx
	mov %ecx, -76(%ebp)

	mov Width, %ebx		# -8: width * 1.5 = line offset
	shl %ebx
	add Width, %ebx
	shr %ebx
	mov %ebx, -80(%ebp)
	
	# Okay, this requires a bit of byte shuffling... we go from
	#  YYYY UU
	#  YYYY VV
	# to
	#  YUYV YUYV
	#  YUYV YUYV
	# which indeed takes up more space

	# 
	
0:	mov -76(%ebp), %ecx

1:	lodsl			# 4 Y in eax
	testl $1, Height	# even or odd line?
	jnz 2f
	
	# Even
	mov -80(%ebp), %ebx
	mov (%ebx, %esi), %dx	# 16 bits V 
	shl $16, %edx		# store in high word
	mov (%esi), %dx		# 16 bits U 
	add $2, %esi
	jmp 3f
	
2:	# Odd
	mov -80(%ebp), %ebx
	neg %ebx		# negative offset
	mov (%esi), %dx		# 16 bits V
	shl $16, %edx		# store in high word
	mov (%ebx, %esi), %dx	# 16 bits U
	add $2, %esi

3:	# eax = Y3Y2Y1Y0, edx = V1V0U1U0, ebx is free
	push %eax

	movzbl %al, %ebx	# ______y0
	and $0xFF00, %eax	# ____y1__
	shl $8, %eax		# __y1____
	or %ebx, %eax		# __y1__y0
	mov %edx, %ebx		# v1v0u1u0
	shl $8, %ebx		# v0u1u0__
	and $0xff00ff00, %ebx	# v0__u0__
	or %ebx, %eax		# v0y1u0y0
	stosl	

	pop %eax		# y3y2y1y0
	# Second half
	shr $8, %eax		# __y3y2y1
	shr $8, %ax		# __y3__y2
	and $0xff00ff00, %edx	# v1__u1__
	or %edx, %eax		# v1y3u1y2
	stosl
	
	loop 1b


	decl Height		# height--
	jnz 0b
	# Done

9:	pop %edi
	pop %esi
	pop %ebx
	leave
	ret
