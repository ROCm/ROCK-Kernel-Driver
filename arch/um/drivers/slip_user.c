#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>
#include <sched.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/errno.h>
#include <sys/termios.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/signal.h>
#include "user_util.h"
#include "kern_util.h"
#include "user.h"
#include "net_user.h"
#include "slip.h"
#include "helper.h"
#include "os.h"

void slip_user_init(void *data, void *dev)
{
	struct slip_data *pri = data;

	pri->dev = dev;
}

static int set_up_tty(int fd)
{
	int i;
	struct termios tios;

	if (tcgetattr(fd, &tios) < 0) {
		printk("could not get initial terminal attributes\n");
		return(-1);
	}

	tios.c_cflag = CS8 | CREAD | HUPCL | CLOCAL;
	tios.c_iflag = IGNBRK | IGNPAR;
	tios.c_oflag = 0;
	tios.c_lflag = 0;
	for (i = 0; i < NCCS; i++)
		tios.c_cc[i] = 0;
	tios.c_cc[VMIN] = 1;
	tios.c_cc[VTIME] = 0;

	cfsetospeed(&tios, B38400);
	cfsetispeed(&tios, B38400);

	if (tcsetattr(fd, TCSAFLUSH, &tios) < 0) {
		printk("failed to set terminal attributes\n");
		return(-1);
	}
	return(0);
}

struct slip_pre_exec_data {
	int stdin;
	int stdout;
	int close_me;
};

static void slip_pre_exec(void *arg)
{
	struct slip_pre_exec_data *data = arg;

	if(data->stdin != -1) dup2(data->stdin, 0);
	dup2(data->stdout, 1);
	close(data->close_me);
}

static int slip_tramp(char **argv, int fd)
{
	struct slip_pre_exec_data pe_data;
	char *output;
	int status, pid, fds[2], err, output_len;

	err = os_pipe(fds, 1, 0);
	if(err){
		printk("slip_tramp : pipe failed, errno = %d\n", -err);
		return(err);
	}

	err = 0;
	pe_data.stdin = fd;
	pe_data.stdout = fds[1];
	pe_data.close_me = fds[0];
	pid = run_helper(slip_pre_exec, &pe_data, argv, NULL);

	if(pid < 0) err = pid;
	else {
		output_len = page_size();
		output = um_kmalloc(output_len);
		if(output == NULL)
			printk("slip_tramp : failed to allocate output "
			       "buffer\n");

		close(fds[1]);
		read_output(fds[0], output, output_len);
		if(output != NULL){
			printk("%s", output);
			kfree(output);
		}
		if(waitpid(pid, &status, 0) < 0) err = errno;
		else if(!WIFEXITED(status) || (WEXITSTATUS(status) != 0)){
			printk("'%s' didn't exit with status 0\n", argv[0]);
			err = EINVAL;
		}
	}
	return(err);
}

static int slip_open(void *data)
{
	struct slip_data *pri = data;
	char version_buf[sizeof("nnnnn\0")];
	char gate_buf[sizeof("nnn.nnn.nnn.nnn\0")];
	char *argv[] = { "uml_net", version_buf, "slip", "up", gate_buf, 
			 NULL };
	int sfd, mfd, disc, sencap, err;

	if((mfd = get_pty()) < 0){
		printk("umn : Failed to open pty\n");
		return(-1);
	}
	if((sfd = os_open_file(ptsname(mfd), of_rdwr(OPENFLAGS()), 0)) < 0){
		printk("Couldn't open tty for slip line\n");
		return(-1);
	}
	if(set_up_tty(sfd)) return(-1);
	pri->slave = sfd;
	pri->pos = 0;
	pri->esc = 0;
	if(pri->gate_addr != NULL){
		sprintf(version_buf, "%d", UML_NET_VERSION);
		strcpy(gate_buf, pri->gate_addr);

		err = slip_tramp(argv, sfd);

		if(err != 0){
			printk("slip_tramp failed - errno = %d\n", err);
			return(-err);
		}
		if(ioctl(pri->slave, SIOCGIFNAME, pri->name) < 0){
			printk("SIOCGIFNAME failed, errno = %d\n", errno);
			return(-errno);
		}
		iter_addresses(pri->dev, open_addr, pri->name);
	}
	else {
		disc = N_SLIP;
		if(ioctl(sfd, TIOCSETD, &disc) < 0){
			printk("Failed to set slip line discipline - "
			       "errno = %d\n", errno);
			return(-errno);
		}
		sencap = 0;
		if(ioctl(sfd, SIOCSIFENCAP, &sencap) < 0){
			printk("Failed to sett slip encapsulation - "
			       "errno = %d\n", errno);
			return(-errno);
		}
	}
	return(mfd);
}

static void slip_close(int fd, void *data)
{
	struct slip_data *pri = data;
	char version_buf[sizeof("nnnnn\0")];
	char *argv[] = { "uml_net", version_buf, "slip", "down", pri->name, 
			 NULL };
	int err;

	if(pri->gate_addr != NULL)
		iter_addresses(pri->dev, close_addr, pri->name);

	sprintf(version_buf, "%d", UML_NET_VERSION);

	err = slip_tramp(argv, -1);

	if(err != 0)
		printk("slip_tramp failed - errno = %d\n", err);
	close(fd);
	close(pri->slave);
	pri->slave = -1;
}

/* SLIP protocol characters. */
#define END             0300		/* indicates end of frame	*/
#define ESC             0333		/* indicates byte stuffing	*/
#define ESC_END         0334		/* ESC ESC_END means END 'data'	*/
#define ESC_ESC         0335		/* ESC ESC_ESC means ESC 'data'	*/

static int slip_unesc(struct slip_data *sl, unsigned char c)
{
	int ret;

	switch(c){
	case END:
		sl->esc = 0;
		ret = sl->pos;
		sl->pos = 0;
		return(ret);
	case ESC:
		sl->esc = 1;
		return(0);
	case ESC_ESC:
		if(sl->esc){
			sl->esc = 0;
			c = ESC;
		}
		break;
	case ESC_END:
		if(sl->esc){
			sl->esc = 0;
			c = END;
		}
		break;
	}
	sl->buf[sl->pos++] = c;
	return(0);
}

int slip_user_read(int fd, void *buf, int len, struct slip_data *pri)
{
	int i, n, size, start;

	n = net_read(fd, &pri->buf[pri->pos], sizeof(pri->buf) - pri->pos);
	if(n <= 0) return(n);

	start = pri->pos;
	for(i = 0; i < n; i++){
		size = slip_unesc(pri, pri->buf[start + i]);
		if(size){
			memcpy(buf, pri->buf, size);
			return(size);
		}
	}
	return(0);
}

static int slip_esc(unsigned char *s, unsigned char *d, int len)
{
	unsigned char *ptr = d;
	unsigned char c;

	/*
	 * Send an initial END character to flush out any
	 * data that may have accumulated in the receiver
	 * due to line noise.
	 */

	*ptr++ = END;

	/*
	 * For each byte in the packet, send the appropriate
	 * character sequence, according to the SLIP protocol.
	 */

	while (len-- > 0) {
		switch(c = *s++) {
		case END:
			*ptr++ = ESC;
			*ptr++ = ESC_END;
			break;
		case ESC:
			*ptr++ = ESC;
			*ptr++ = ESC_ESC;
			break;
		default:
			*ptr++ = c;
			break;
		}
	}
	*ptr++ = END;
	return (ptr - d);
}

int slip_user_write(int fd, void *buf, int len, struct slip_data *pri)
{
	int actual, n;

	actual = slip_esc(buf, pri->buf, len);
	n = net_write(fd, pri->buf, actual);
	if(n < 0) return(n);
	else return(len);
}

static int slip_set_mtu(int mtu, void *data)
{
	return(mtu);
}

static void slip_add_addr(unsigned char *addr, unsigned char *netmask,
			  void *data)
{
	struct slip_data *pri = data;

	if(pri->slave == -1) return;
	open_addr(addr, netmask, pri->name);
}

static void slip_del_addr(unsigned char *addr, unsigned char *netmask,
			    void *data)
{
	struct slip_data *pri = data;

	if(pri->slave == -1) return;
	close_addr(addr, netmask, pri->name);
}

struct net_user_info slip_user_info = {
	init:		slip_user_init,
	open:		slip_open,
	close:	 	slip_close,
	remove:	 	NULL,
	set_mtu:	slip_set_mtu,
	add_address:	slip_add_addr,
	delete_address: slip_del_addr,
	max_packet:	BUF_SIZE
};

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
