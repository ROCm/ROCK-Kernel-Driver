/*
 *  linux/arch/m68k/lib/semaphore.S
 *
 *  Copyright (C) 1996  Linus Torvalds
 *
 *  m68k version by Andreas Schwab
 */

#include <linux/linkage.h>
#include <asm/semaphore.h>

/*
 * The semaphore operations have a special calling sequence that
 * allow us to do a simpler in-line version of them. These routines
 * need to convert that sequence back into the C sequence when
 * there is contention on the semaphore.
 */
ENTRY(__down_failed)
	moveml %a0/%d0/%d1,-(%sp)
	movel %a1,-(%sp)
	jbsr SYMBOL_NAME(__down)
	movel (%sp)+,%a1
	moveml (%sp)+,%a0/%d0/%d1
	rts

ENTRY(__down_failed_interruptible)
	movel %a0,-(%sp)
	movel %d1,-(%sp)
	movel %a1,-(%sp)
	jbsr SYMBOL_NAME(__down_interruptible)
	movel (%sp)+,%a1
	movel (%sp)+,%d1
	movel (%sp)+,%a0
	rts

ENTRY(__down_failed_trylock)
	movel %a0,-(%sp)
	movel %d1,-(%sp)
	movel %a1,-(%sp)
	jbsr SYMBOL_NAME(__down_trylock)
	movel (%sp)+,%a1
	movel (%sp)+,%d1
	movel (%sp)+,%a0
	rts

ENTRY(__up_wakeup)
	moveml %a0/%d0/%d1,-(%sp)
	movel %a1,-(%sp)
	jbsr SYMBOL_NAME(__up)
	movel (%sp)+,%a1
	moveml (%sp)+,%a0/%d0/%d1
	rts

ENTRY(__down_read_failed)
	moveml %a0/%d0/%d1,-(%sp)
	jcc 3f
1:	movel %a1,-(%sp)
	jbsr SYMBOL_NAME(down_read_failed_biased)
	movel (%sp)+,%a1
2:	moveml (%sp)+,%a0/%d0/%d1
	rts

3:	movel %a1,-(%sp)
	jbsr SYMBOL_NAME(down_read_failed)
	movel (%sp)+,%a1
	subql #1,%a1@
	jpl 2b
	jcc 3b
	jra 1b

ENTRY(__down_write_failed)
	moveml %a0/%d0/%d1,-(%sp)
	jcc 3f
1:	movel %a1,-(%sp)
	jbsr SYMBOL_NAME(down_write_failed_biased)
	movel (%sp)+,%a1
2:	moveml (%sp)+,%a0/%d0/%d1
	rts

3:	movel %a1,-(%sp)
	jbsr SYMBOL_NAME(down_write_failed)
	movel (%sp)+,%a1
	subl #RW_LOCK_BIAS,%a1@
	jpl 2b
	jcc 3b
	jra 1b

ENTRY(__rwsem_wake)
	moveml %a0/%d0/%d1,-(%sp)
	jeq 1f
	movel %a1,-(%sp)
	jbsr SYMBOL_NAME(rwsem_wake_readers)
	jra 2f
1:	movel %a1,-(%sp)
	jbsr rwsem_wake_writer
2:	movel (%sp)+,%a1
	moveml (%sp)+,%a0/%d0/%d1
	rts

