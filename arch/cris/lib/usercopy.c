/*
 * User address space access functions.
 * The non-inlined parts of asm-cris/uaccess.h are here.
 *
 * Copyright (C) 2000, Axis Communications AB.
 *
 * Written by Hans-Peter Nilsson.
 * Pieces used from memcpy, originally by Kenny Ranerup long time ago.
 */

#include <asm/uaccess.h>

/* Asm:s have been tweaked (within the domain of correctness) to give
   satisfactory results for "gcc version 2.96 20000427 (experimental)".

   Check regularly...

   Note that the PC saved at a bus-fault is the address *after* the
   faulting instruction, which means the branch-target for instructions in
   delay-slots for taken branches.  Note also that the postincrement in
   the instruction is performed regardless of bus-fault; the register is
   seen updated in fault handlers.

   Oh, and on the code formatting issue, to whomever feels like "fixing
   it" to Conformity: I'm too "lazy", but why don't you go ahead and "fix"
   string.c too.  I just don't think too many people will hack this file
   for the code format to be an issue.  */


/* Copy to userspace.  This is based on the memcpy used for
   kernel-to-kernel copying; see "string.c".  */

unsigned long
__copy_user (void *pdst, const void *psrc, unsigned long pn)
{
  /* We want the parameters put in special registers.
     Make sure the compiler is able to make something useful of this.
     As it is now: r10 -> r13; r11 -> r11 (nop); r12 -> r12 (nop).

     FIXME: Comment for old gcc version.  Check.
     If gcc was allright, it really would need no temporaries, and no
     stack space to save stuff on. */

  register char *dst __asm__ ("r13") = pdst;
  register const char *src __asm__ ("r11") = psrc;
  register int n __asm__ ("r12") = pn;
  register int retn __asm__ ("r10") = 0;


  /* When src is aligned but not dst, this makes a few extra needless
     cycles.  I believe it would take as many to check that the
     re-alignment was unnecessary.  */
  if (((unsigned long) dst & 3) != 0
      /* Don't align if we wouldn't copy more than a few bytes; so we
	 don't have to check further for overflows.  */
      && n >= 3)
  {
    if ((unsigned long) dst & 1)
    {
      __asm_copy_to_user_1 (dst, src, retn);
      n--;
    }

    if ((unsigned long) dst & 2)
    {
      __asm_copy_to_user_2 (dst, src, retn);
      n -= 2;
    }
  }

  /* Decide which copying method to use. */
  if (n >= 44*2)		/* Break even between movem and
				   move16 is at 38.7*2, but modulo 44. */
  {
    /* For large copies we use 'movem'.  */

    /* It is not optimal to tell the compiler about clobbering any
       registers; that will move the saving/restoring of those registers
       to the function prologue/epilogue, and make non-movem sizes
       suboptimal.

       This method is not foolproof; it assumes that the "asm reg"
       declarations at the beginning of the function really are used
       here (beware: they may be moved to temporary registers).
       This way, we do not have to save/move the registers around into
       temporaries; we can safely use them straight away.

       If you want to check that the allocation was right; then
       check the equalities in the first comment.  It should say
       "r13=r13, r11=r11, r12=r12".  */
    __asm__ volatile ("
	;; Check that the following is true (same register names on
	;; both sides of equal sign, as in r8=r8):
	;; %0=r13, %1=r11, %2=r12 %3=r10
	;;
	;; Save the registers we'll use in the movem process
	;; on the stack.
	subq	11*4,$sp
	movem	$r10,[$sp]

	;; Now we've got this:
	;; r11 - src
	;; r13 - dst
	;; r12 - n

	;; Update n for the first loop
	subq	44,$r12

; Since the noted PC of a faulting instruction in a delay-slot of a taken
; branch, is that of the branch target, we actually point at the from-movem
; for this case.  There is no ambiguity here; if there was a fault in that
; instruction (meaning a kernel oops), the faulted PC would be the address
; after *that* movem.

0:
	movem	[$r11+],$r10
	subq   44,$r12
	bge	0b
	movem	$r10,[$r13+]
1:
	addq   44,$r12  ;; compensate for last loop underflowing n

	;; Restore registers from stack
	movem [$sp+],$r10
2:
	.section .fixup,\"ax\"

; To provide a correct count in r10 of bytes that failed to be copied,
; we jump back into the loop if the loop-branch was taken.  There is no
; performance penalty for sany use; the program will segfault soon enough.

3:
	move.d [$sp],$r10
	addq 44,$r10
	move.d $r10,[$sp]
	jump 0b
4:
	movem [$sp+],$r10
	addq 44,$r10
	addq 44,$r12
	jump 2b

	.previous
	.section __ex_table,\"a\"
	.dword 0b,3b
	.dword 1b,4b
	.previous"

     /* Outputs */ : "=r" (dst), "=r" (src), "=r" (n), "=r" (retn)
     /* Inputs */ : "0" (dst), "1" (src), "2" (n), "3" (retn));

  }

  /* Either we directly start copying, using dword copying in a loop, or
     we copy as much as possible with 'movem' and then the last block (<44
     bytes) is copied here.  This will work since 'movem' will have
     updated SRC, DST and N.  */

  while (n >= 16)
  {
    __asm_copy_to_user_16 (dst, src, retn);
    n -= 16;
  }

  /* Having a separate by-four loops cuts down on cache footprint.
     FIXME:  Test with and without; increasing switch to be 0..15.  */
  while (n >= 4)
  {
    __asm_copy_to_user_4 (dst, src, retn);
    n -= 4;
  }

  switch (n)
  {
    case 0:
      break;
    case 1:
      __asm_copy_to_user_1 (dst, src, retn);
      break;
    case 2:
      __asm_copy_to_user_2 (dst, src, retn);
      break;
    case 3:
      __asm_copy_to_user_3 (dst, src, retn);
      break;
  }

  return retn;
}

/* Copy from user to kernel, zeroing the bytes that were inaccessible in
   userland.  */

unsigned long
__copy_user_zeroing (void *pdst, const void *psrc, unsigned long pn)
{
  /* We want the parameters put in special registers.
     Make sure the compiler is able to make something useful of this.
     As it is now: r10 -> r13; r11 -> r11 (nop); r12 -> r12 (nop).

     FIXME: Comment for old gcc version.  Check.
     If gcc was allright, it really would need no temporaries, and no
     stack space to save stuff on.  */

  register char *dst __asm__ ("r13") = pdst;
  register const char *src __asm__ ("r11") = psrc;
  register int n __asm__ ("r12") = pn;
  register int retn __asm__ ("r10") = 0;

  /* When src is aligned but not dst, this makes a few extra needless
     cycles.  I believe it would take as many to check that the
     re-alignment was unnecessary.  */
  if (((unsigned long) dst & 3) != 0
      /* Don't align if we wouldn't copy more than a few bytes; so we
	 don't have to check further for overflows.  */
      && n >= 3)
  {
    if ((unsigned long) dst & 1)
    {
      __asm_copy_from_user_1 (dst, src, retn);
      n--;
    }

    if ((unsigned long) dst & 2)
    {
      __asm_copy_from_user_2 (dst, src, retn);
      n -= 2;
    }
  }

  /* Decide which copying method to use. */
  if (n >= 44*2)		/* Break even between movem and
				   move16 is at 38.7*2, but modulo 44. */
  {
    /* For large copies we use 'movem' */

    /* It is not optimal to tell the compiler about clobbering any
       registers; that will move the saving/restoring of those registers
       to the function prologue/epilogue, and make non-movem sizes
       suboptimal.

       This method is not foolproof; it assumes that the "asm reg"
       declarations at the beginning of the function really are used
       here (beware: they may be moved to temporary registers).
       This way, we do not have to save/move the registers around into
       temporaries; we can safely use them straight away.

       If you want to check that the allocation was right; then
       check the equalities in the first comment.  It should say
       "r13=r13, r11=r11, r12=r12" */
    __asm__ volatile ("
	;; Check that the following is true (same register names on
	;; both sides of equal sign, as in r8=r8):
	;; %0=r13, %1=r11, %2=r12 %3=r10
	;;
	;; Save the registers we'll use in the movem process
	;; on the stack.
	subq	11*4,$sp
	movem	$r10,[$sp]

	;; Now we've got this:
	;; r11 - src
	;; r13 - dst
	;; r12 - n

	;; Update n for the first loop
	subq	44,$r12
0:
	movem	[$r11+],$r10
1:
	subq   44,$r12
	bge	0b
	movem	$r10,[$r13+]

	addq   44,$r12  ;; compensate for last loop underflowing n
8:
	;; Restore registers from stack
	movem [$sp+],$r10

	.section .fixup,\"ax\"

;; Do not jump back into the loop if we fail.  For some uses, we get a
;; page fault but for performance reasons we care to not get further
;; faults.  For example, fs/super.c at one time did
;;  i = size - copy_from_user((void *)page, data, size);
;; which would cause repeated faults while clearing the remainder of
;; the SIZE bytes at PAGE after the first fault.

3:
	move.d [$sp],$r10

;; Number of remaining bytes, cleared but not copied, is r12 + 44.

	add.d $r12,$r10
	addq 44,$r10

	move.d $r10,[$sp]
	clear.d $r0
	clear.d $r1
	clear.d $r2
	clear.d $r3
	clear.d $r4
	clear.d $r5
	clear.d $r6
	clear.d $r7
	clear.d $r8
	clear.d $r9
	clear.d $r10

;; Perform clear similar to the copy-loop.

4:
	subq 44,$r12
	bge 4b
	movem $r10,[$r13+]

;; Clear by four for the remaining multiples.

	addq 40,$r12
	bmi 6f
	nop
5:
	subq 4,$r12
	bpl 5b
	clear.d [$r13+]
6:
	addq 4,$r12
	beq 7f
	nop

	subq 1,$r12
	beq 7f
	clear.b [$r13+]

	subq 1,$r12
	beq 7f
	clear.b [$r13+]

	clear.d $r12
	clear.b [$r13+]
7:
	jump 8b

	.previous
	.section __ex_table,\"a\"
	.dword 1b,3b
	.previous"

     /* Outputs */ : "=r" (dst), "=r" (src), "=r" (n), "=r" (retn)
     /* Inputs */ : "0" (dst), "1" (src), "2" (n), "3" (retn));

  }

  /* Either we directly start copying here, using dword copying in a loop,
     or we copy as much as possible with 'movem' and then the last block
     (<44 bytes) is copied here.  This will work since 'movem' will have
     updated src, dst and n. */

  while (n >= 16)
  {
    __asm_copy_from_user_16 (dst, src, retn);
    n -= 16;
  }

  /* Having a separate by-four loops cuts down on cache footprint.
     FIXME:  Test with and without; increasing switch to be 0..15.  */
  while (n >= 4)
  {
    __asm_copy_from_user_4 (dst, src, retn);
    n -= 4;
  }

  switch (n)
  {
    case 0:
      break;
    case 1:
      __asm_copy_from_user_1 (dst, src, retn);
      break;
    case 2:
      __asm_copy_from_user_2 (dst, src, retn);
      break;
    case 3:
      __asm_copy_from_user_3 (dst, src, retn);
      break;
  }

  return retn;
}

/* Zero userspace.  */

unsigned long
__do_clear_user (void *pto, unsigned long pn)
{
  /* We want the parameters put in special registers.
     Make sure the compiler is able to make something useful of this.
      As it is now: r10 -> r13; r11 -> r11 (nop); r12 -> r12 (nop).

     FIXME: Comment for old gcc version.  Check.
     If gcc was allright, it really would need no temporaries, and no
     stack space to save stuff on. */

  register char *dst __asm__ ("r13") = pto;
  register int n __asm__ ("r12") = pn;
  register int retn __asm__ ("r10") = 0;


  if (((unsigned long) dst & 3) != 0
     /* Don't align if we wouldn't copy more than a few bytes.  */
      && n >= 3)
  {
    if ((unsigned long) dst & 1)
    {
      __asm_clear_1 (dst, retn);
      n--;
    }

    if ((unsigned long) dst & 2)
    {
      __asm_clear_2 (dst, retn);
      n -= 2;
    }
  }

  /* Decide which copying method to use.
     FIXME: This number is from the "ordinary" kernel memset.  */
  if (n >= (1*48))
  {
    /* For large clears we use 'movem' */

    /* It is not optimal to tell the compiler about clobbering any
       call-saved registers; that will move the saving/restoring of
       those registers to the function prologue/epilogue, and make
       non-movem sizes suboptimal.

       This method is not foolproof; it assumes that the "asm reg"
       declarations at the beginning of the function really are used
       here (beware: they may be moved to temporary registers).
       This way, we do not have to save/move the registers around into
       temporaries; we can safely use them straight away.

      If you want to check that the allocation was right; then
      check the equalities in the first comment.  It should say
      something like "r13=r13, r11=r11, r12=r12". */
    __asm__ volatile ("
	;; Check that the following is true (same register names on
	;; both sides of equal sign, as in r8=r8):
	;; %0=r13, %1=r12 %2=r10
	;;
	;; Save the registers we'll clobber in the movem process
	;; on the stack.  Don't mention them to gcc, it will only be
	;; upset.
	subq	11*4,$sp
	movem	$r10,[$sp]

	clear.d $r0
	clear.d $r1
	clear.d $r2
	clear.d $r3
	clear.d $r4
	clear.d $r5
	clear.d $r6
	clear.d $r7
	clear.d $r8
	clear.d $r9
	clear.d $r10
	clear.d $r11

	;; Now we've got this:
	;; r13 - dst
	;; r12 - n

	;; Update n for the first loop
	subq	12*4,$r12
0:
	subq   12*4,$r12
	bge	0b
	movem	$r11,[$r13+]
1:
	addq   12*4,$r12        ;; compensate for last loop underflowing n

	;; Restore registers from stack
	movem [$sp+],$r10
2:
	.section .fixup,\"ax\"
3:
	move.d [$sp],$r10
	addq 12*4,$r10
	move.d $r10,[$sp]
	clear.d $r10
	jump 0b

4:
	movem [$sp+],$r10
	addq 12*4,$r10
	addq 12*4,$r12
	jump 2b

	.previous
	.section __ex_table,\"a\"
	.dword 0b,3b
	.dword 1b,4b
	.previous"

     /* Outputs */ : "=r" (dst), "=r" (n), "=r" (retn)
     /* Inputs */ : "0" (dst), "1" (n), "2" (retn)
     /* Clobber */ : "r11");
  }

  while (n >= 16)
  {
    __asm_clear_16 (dst, retn);
    n -= 16;
  }

  /* Having a separate by-four loops cuts down on cache footprint.
     FIXME:  Test with and without; increasing switch to be 0..15.  */
  while (n >= 4)
  {
    __asm_clear_4 (dst, retn);
    n -= 4;
  }

  switch (n)
  {
    case 0:
      break;
    case 1:
      __asm_clear_1 (dst, retn);
      break;
    case 2:
      __asm_clear_2 (dst, retn);
      break;
    case 3:
      __asm_clear_3 (dst, retn);
      break;
  }

  return retn;
}
