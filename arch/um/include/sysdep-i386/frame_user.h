/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __FRAME_USER_I386_H
#define __FRAME_USER_I386_H

#include <asm/page.h>
#include "sysdep/frame.h"

/* This stuff is to calculate the size of the fp state struct at runtime
 * because it has changed between 2.2 and 2.4 and it would be good for a
 * UML compiled on one to work on the other.
 * So, setup_arch_frame_raw fills in the arch struct with the raw data, which
 * just contains the address of the end of the sigcontext.  This is invoked
 * from the signal handler.
 * setup_arch_frame uses that data to figure out what 
 * arch_frame_data.fpstate_size should be.  It really has no idea, since it's
 * not allowed to do sizeof(struct fpstate) but it's safe to consider that it's
 * everything from the end of the sigcontext up to the top of the stack.  So,
 * it masks off the page number to get the offset within the page and subtracts
 * that from the page size, and that's how big the fpstate struct will be
 * considered to be.
 */

static inline void setup_arch_frame_raw(struct arch_frame_data_raw *data,
					void *end, unsigned long srp)
{
	unsigned long sr = *((unsigned long *) srp);

	data->fp_start = (unsigned long) end;
	if((sr & PAGE_MASK) == ((unsigned long) end & PAGE_MASK))
		data->sr = sr;
	else data->sr = 0;
}

static inline void setup_arch_frame(struct arch_frame_data_raw *in, 
				    struct arch_frame_data *out)
{
	unsigned long fpstate_start = in->fp_start;

	if(in->sr == 0){
		fpstate_start &= ~PAGE_MASK;
		out->fpstate_size = PAGE_SIZE - fpstate_start;
	}
	else {
		out->fpstate_size = in->sr - fpstate_start;
	}
}

/* This figures out where on the stack the SA_RESTORER function address
 * is stored.  For i386, it's the signal handler return address, so it's
 * located next to the frame pointer.
 * This is inlined, so __builtin_frame_address(0) is correct.  Otherwise,
 * it would have to be __builtin_frame_address(1).
 */

static inline unsigned long frame_restorer(void)
{
	unsigned long *fp;

	fp = __builtin_frame_address(0);
	return((unsigned long) (fp + 1));
}

/* Similarly, this returns the value of sp when the handler was first
 * entered.  This is used to calculate the proper sp when delivering
 * signals.
 */

static inline unsigned long frame_sp(void)
{
	unsigned long *fp;

	fp = __builtin_frame_address(0);
	return((unsigned long) (fp + 1));
}

#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
