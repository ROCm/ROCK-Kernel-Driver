/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __FRAME_H_
#define __FRAME_H_

#include "sysdep/frame.h"

struct frame_common {
	void *data;
	int len;
	int sig_index;
	int sr_index;
	int sr_relative;
	int sp_index;
	struct arch_frame_data arch;
};

struct sc_frame {
	struct frame_common common;
	int sc_index;
};

extern struct sc_frame signal_frame_sc;

extern struct sc_frame signal_frame_sc_sr;

struct si_frame {
	struct frame_common common;
	int sip_index;
	int si_index;
	int ucp_index;
	int uc_index;
};

extern struct si_frame signal_frame_si;

extern void capture_signal_stack(void);

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
