#ifndef __UM_SLIP_H
#define __UM_SLIP_H

#define BUF_SIZE 1500

struct slip_data {
	void *dev;
	char name[sizeof("slnnnnn\0")];
	char *addr;
	char *gate_addr;
	int slave;
	char buf[2 * BUF_SIZE];
	int pos;
	int esc;
};

extern struct net_user_info slip_user_info;

extern int set_umn_addr(int fd, char *addr, char *ptp_addr);
extern int slip_user_read(int fd, void *buf, int len, struct slip_data *pri);
extern int slip_user_write(int fd, void *buf, int len, struct slip_data *pri);

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
