/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <signal.h>
#include "sysdep/ptrace.h"
#include "sysdep/sigcontext.h"

extern unsigned long search_exception_table(unsigned long addr);

int arch_fixup(unsigned long address, void *sc_ptr)
{
	struct sigcontext *sc = sc_ptr;
	unsigned long fixup;

	fixup = search_exception_tables(address);
	if(fixup != 0){
		sc->eip = fixup;
		return(1);
	}
	return(0);
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
