/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __FRAME_H_
#define __FRAME_H_

#include "sysdep/frame.h"

struct sc_frame {
	void *data;
	int len;
	int sig_index;
	int sc_index;
	int sr_index;
	int sr_relative;
	int sp_index;
	struct arch_frame_data arch;
};

extern struct sc_frame signal_frame_sc;

struct si_frame {
	void *data;
	int len;
	int sig_index;
	int sip_index;
	int si_index;
	int sr_index;
	int sr_relative;
	int sp_index;
};

extern struct si_frame signal_frame_si;

extern void capture_signal_stack(void);
extern void set_sc_ip_sp(void *sc_ptr, unsigned long ip, unsigned long sp);

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
