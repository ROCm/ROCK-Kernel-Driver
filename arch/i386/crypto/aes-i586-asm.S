// -------------------------------------------------------------------------
// Copyright (c) 2001, Dr Brian Gladman <                 >, Worcester, UK.
// All rights reserved.
//
// LICENSE TERMS
//
// The free distribution and use of this software in both source and binary 
// form is allowed (with or without changes) provided that:
//
//   1. distributions of this source code include the above copyright 
//      notice, this list of conditions and the following disclaimer//
//
//   2. distributions in binary form include the above copyright
//      notice, this list of conditions and the following disclaimer
//      in the documentation and/or other associated materials//
//
//   3. the copyright holder's name is not used to endorse products 
//      built using this software without specific written permission.
//
//
// ALTERNATIVELY, provided that this notice is retained in full, this product
// may be distributed under the terms of the GNU General Public License (GPL),
// in which case the provisions of the GPL apply INSTEAD OF those given above.
//
// Copyright (c) 2004 Linus Torvalds <torvalds@osdl.org>
// Copyright (c) 2004 Red Hat, Inc., James Morris <jmorris@redhat.com>

// DISCLAIMER
//
// This software is provided 'as is' with no explicit or implied warranties
// in respect of its properties including, but not limited to, correctness 
// and fitness for purpose.
// -------------------------------------------------------------------------
// Issue Date: 29/07/2002

.file "aes-i586-asm.S"
.text

// aes_rval aes_enc_blk(const unsigned char in_blk[], unsigned char out_blk[], const aes_ctx cx[1])//
// aes_rval aes_dec_blk(const unsigned char in_blk[], unsigned char out_blk[], const aes_ctx cx[1])//
	
#define tlen 1024   // length of each of 4 'xor' arrays (256 32-bit words)

// offsets to parameters with one register pushed onto stack

#define in_blk    8  // input byte array address parameter
#define out_blk  12  // output byte array address parameter
#define ctx      16  // AES context structure

// offsets in context structure

#define ekey     0   // encryption key schedule base address
#define nrnd   256   // number of rounds
#define dkey   260   // decryption key schedule base address

// register mapping for encrypt and decrypt subroutines

#define r0  eax
#define r1  ebx
#define r2  ecx
#define r3  edx
#define r4  esi
#define r5  edi
#define r6  ebp

#define eaxl  al
#define eaxh  ah
#define ebxl  bl
#define ebxh  bh
#define ecxl  cl
#define ecxh  ch
#define edxl  dl
#define edxh  dh

#define _h(reg) reg##h
#define h(reg) _h(reg)

#define _l(reg) reg##l
#define l(reg) _l(reg)

// This macro takes a 32-bit word representing a column and uses
// each of its four bytes to index into four tables of 256 32-bit
// words to obtain values that are then xored into the appropriate
// output registers r0, r1, r4 or r5.  

// Parameters:
//   %1  out_state[0]
//   %2  out_state[1]
//   %3  out_state[2]
//   %4  out_state[3]
//   %5  table base address
//   %6  input register for the round (destroyed)
//   %7  scratch register for the round

#define do_col(a1, a2, a3, a4, a5, a6, a7)	\
	movzx   %l(a6),%a7;			\
	xor     a5(,%a7,4),%a1;			\
	movzx   %h(a6),%a7;			\
	shr     $16,%a6;			\
	xor     a5+tlen(,%a7,4),%a2;		\
	movzx   %l(a6),%a7;			\
	movzx   %h(a6),%a6;			\
	xor     a5+2*tlen(,%a7,4),%a3;		\
	xor     a5+3*tlen(,%a6,4),%a4;

// initialise output registers from the key schedule

#define do_fcol(a1, a2, a3, a4, a5, a6, a7, a8)	\
	mov     0 a8,%a1;			\
	movzx   %l(a6),%a7;			\
	mov     12 a8,%a2;			\
	xor     a5(,%a7,4),%a1;			\
	mov     4 a8,%a4;			\
	movzx   %h(a6),%a7;			\
	shr     $16,%a6;			\
	xor     a5+tlen(,%a7,4),%a2;		\
	movzx   %l(a6),%a7;			\
	movzx   %h(a6),%a6;			\
	xor     a5+3*tlen(,%a6,4),%a4;		\
	mov     %a3,%a6;			\
	mov     8 a8,%a3;			\
	xor     a5+2*tlen(,%a7,4),%a3;

// initialise output registers from the key schedule

#define do_icol(a1, a2, a3, a4, a5, a6, a7, a8)	\
	mov     0 a8,%a1;			\
	movzx   %l(a6),%a7;			\
	mov     4 a8,%a2;			\
	xor     a5(,%a7,4),%a1;			\
	mov     12 a8,%a4;			\
	movzx   %h(a6),%a7;			\
	shr     $16,%a6;			\
	xor     a5+tlen(,%a7,4),%a2;		\
	movzx   %l(a6),%a7;			\
	movzx   %h(a6),%a6;			\
	xor     a5+3*tlen(,%a6,4),%a4;		\
	mov     %a3,%a6;			\
	mov     8 a8,%a3;			\
	xor     a5+2*tlen(,%a7,4),%a3;


// original Gladman had conditional saves to MMX regs.
#define save(a1, a2)		\
	mov     %a2,4*a1(%esp)

#define restore(a1, a2)		\
	mov     4*a2(%esp),%a1

// This macro performs a forward encryption cycle. It is entered with
// the first previous round column values in r0, r1, r4 and r5 and
// exits with the final values in the same registers, using the MMX
// registers mm0-mm1 or the stack for temporary storage

// mov current column values into the MMX registers
#define fwd_rnd(arg, table)					\
	/* mov current column values into the MMX registers */	\
	mov     %r0,%r2;					\
	save   (0,r1);						\
	save   (1,r5);						\
								\
	/* compute new column values */				\
	do_fcol(r0,r5,r4,r1,table, r2,r3, arg);			\
	do_col (r4,r1,r0,r5,table, r2,r3);			\
	restore(r2,0);						\
	do_col (r1,r0,r5,r4,table, r2,r3);			\
	restore(r2,1);						\
	do_col (r5,r4,r1,r0,table, r2,r3);

// This macro performs an inverse encryption cycle. It is entered with
// the first previous round column values in r0, r1, r4 and r5 and
// exits with the final values in the same registers, using the MMX
// registers mm0-mm1 or the stack for temporary storage

#define inv_rnd(arg, table)					\
	/* mov current column values into the MMX registers */	\
	mov     %r0,%r2;					\
	save    (0,r1);						\
	save    (1,r5);						\
								\
	/* compute new column values */				\
	do_icol(r0,r1,r4,r5, table, r2,r3, arg);		\
	do_col (r4,r5,r0,r1, table, r2,r3);			\
	restore(r2,0);						\
	do_col (r1,r4,r5,r0, table, r2,r3);			\
	restore(r2,1);						\
	do_col (r5,r0,r1,r4, table, r2,r3);

// AES (Rijndael) Encryption Subroutine

.global  aes_enc_blk

.extern  ft_tab
.extern  fl_tab

.align 4

aes_enc_blk:
	push    %ebp
	mov     ctx(%esp),%ebp      // pointer to context
	xor     %eax,%eax

// CAUTION: the order and the values used in these assigns 
// rely on the register mappings

1:	push    %ebx
	mov     in_blk+4(%esp),%r2
	push    %esi
	mov     nrnd(%ebp),%r3   // number of rounds
	push    %edi
	lea     ekey(%ebp),%r6   // key pointer

// input four columns and xor in first round key

	mov     (%r2),%r0
	mov     4(%r2),%r1
	mov     8(%r2),%r4
	mov     12(%r2),%r5
	xor     (%r6),%r0
	xor     4(%r6),%r1
	xor     8(%r6),%r4
	xor     12(%r6),%r5

	sub     $8,%esp           // space for register saves on stack
	add     $16,%r6           // increment to next round key   
	sub     $10,%r3          
	je      4f              // 10 rounds for 128-bit key
	add     $32,%r6
	sub     $2,%r3
	je      3f              // 12 rounds for 128-bit key
	add     $32,%r6

2:	fwd_rnd( -64(%r6) ,ft_tab)	// 14 rounds for 128-bit key
	fwd_rnd( -48(%r6) ,ft_tab)
3:	fwd_rnd( -32(%r6) ,ft_tab)	// 12 rounds for 128-bit key
	fwd_rnd( -16(%r6) ,ft_tab)
4:	fwd_rnd(    (%r6) ,ft_tab)	// 10 rounds for 128-bit key
	fwd_rnd( +16(%r6) ,ft_tab)
	fwd_rnd( +32(%r6) ,ft_tab)
	fwd_rnd( +48(%r6) ,ft_tab)
	fwd_rnd( +64(%r6) ,ft_tab)
	fwd_rnd( +80(%r6) ,ft_tab)
	fwd_rnd( +96(%r6) ,ft_tab)
	fwd_rnd(+112(%r6) ,ft_tab)
	fwd_rnd(+128(%r6) ,ft_tab)
	fwd_rnd(+144(%r6) ,fl_tab)	// last round uses a different table

// move final values to the output array.  CAUTION: the 
// order of these assigns rely on the register mappings

	add     $8,%esp
	mov     out_blk+12(%esp),%r6
	mov     %r5,12(%r6)
	pop     %edi
	mov     %r4,8(%r6)
	pop     %esi
	mov     %r1,4(%r6)
	pop     %ebx
	mov     %r0,(%r6)
	pop     %ebp
	mov     $1,%eax
	ret

// AES (Rijndael) Decryption Subroutine

.global  aes_dec_blk

.extern  it_tab
.extern  il_tab

.align 4

aes_dec_blk:
	push    %ebp
	mov     ctx(%esp),%ebp       // pointer to context
	xor     %eax,%eax

// CAUTION: the order and the values used in these assigns 
// rely on the register mappings

1:	push    %ebx
	mov     in_blk+4(%esp),%r2
	push    %esi
	mov     nrnd(%ebp),%r3   // number of rounds
	push    %edi
	lea     dkey(%ebp),%r6   // key pointer
	mov     %r3,%r0
	shl     $4,%r0
	add     %r0,%r6
	
// input four columns and xor in first round key

	mov     (%r2),%r0
	mov     4(%r2),%r1
	mov     8(%r2),%r4
	mov     12(%r2),%r5
	xor     (%r6),%r0
	xor     4(%r6),%r1
	xor     8(%r6),%r4
	xor     12(%r6),%r5

	sub     $8,%esp           // space for register saves on stack
	sub     $16,%r6           // increment to next round key   
	sub     $10,%r3          
	je      4f              // 10 rounds for 128-bit key
	sub     $32,%r6
	sub     $2,%r3
	je      3f              // 12 rounds for 128-bit key
	sub     $32,%r6

2:	inv_rnd( +64(%r6), it_tab)	// 14 rounds for 128-bit key 
	inv_rnd( +48(%r6), it_tab)
3:	inv_rnd( +32(%r6), it_tab)	// 12 rounds for 128-bit key
	inv_rnd( +16(%r6), it_tab)
4:	inv_rnd(    (%r6), it_tab)	// 10 rounds for 128-bit key
	inv_rnd( -16(%r6), it_tab)
	inv_rnd( -32(%r6), it_tab)
	inv_rnd( -48(%r6), it_tab)
	inv_rnd( -64(%r6), it_tab)
	inv_rnd( -80(%r6), it_tab)
	inv_rnd( -96(%r6), it_tab)
	inv_rnd(-112(%r6), it_tab)
	inv_rnd(-128(%r6), it_tab)
	inv_rnd(-144(%r6), il_tab)	// last round uses a different table

// move final values to the output array.  CAUTION: the 
// order of these assigns rely on the register mappings

	add     $8,%esp
	mov     out_blk+12(%esp),%r6
	mov     %r5,12(%r6)
	pop     %edi
	mov     %r4,8(%r6)
	pop     %esi
	mov     %r1,4(%r6)
	pop     %ebx
	mov     %r0,(%r6)
	pop     %ebp
	mov     $1,%eax
	ret

