/* 
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __UMN_H
#define __UMN_H

extern int open_umn_tty(int *slave_out, int *slipno_out);
extern void close_umn_tty(int master, int slave);
extern int umn_send_packet(int fd, void *data, int len);
extern int set_umn_addr(int fd, char *addr, char *ptp_addr);
extern void slip_unesc(unsigned char s);
extern void umn_read(int fd);

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
