/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdlib.h>
#include <asm/sigcontext.h>
#include "kern_util.h"
#include "sysdep/frame.h"

int copy_sc_from_user_tt(void *to_ptr, void *from_ptr, void *data)
{
	struct arch_frame_data *arch = data;
	struct sigcontext *to = to_ptr, *from = from_ptr;
	struct _fpstate *to_fp, *from_fp;
	unsigned long sigs;
	int err;

	to_fp = to->fpstate;
	from_fp = from->fpstate;
	sigs = to->oldmask;
	err = copy_from_user_proc(to, from, sizeof(*to));
	to->oldmask = sigs;
	if(to_fp != NULL){
		err |= copy_from_user_proc(&to->fpstate, &to_fp,
					   sizeof(to->fpstate));
		err |= copy_from_user_proc(to_fp, from_fp, arch->fpstate_size);
	}
	return(err);
}

int copy_sc_to_user_tt(void *to_ptr, void *fp, void *from_ptr, void *data)
{
	struct arch_frame_data *arch = data;
	struct sigcontext *to = to_ptr, *from = from_ptr;
	struct _fpstate *to_fp, *from_fp;
	int err;

	to_fp = (struct _fpstate *) 
		(fp ? (unsigned long) fp : ((unsigned long) to + sizeof(*to)));
	from_fp = from->fpstate;
	err = copy_to_user_proc(to, from, sizeof(*to));
	if(from_fp != NULL){
		err |= copy_to_user_proc(&to->fpstate, &to_fp,
					 sizeof(to->fpstate));
		err |= copy_to_user_proc(to_fp, from_fp, arch->fpstate_size);
	}
	return(err);
}

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
