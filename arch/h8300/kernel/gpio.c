/*
 *  linux/arch/h8300/kernel/gpio.c
 *
 *  Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 */

/*
 * H8/300H Internal I/O Port Management
 */

#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fs.h>

#if defined(CONFIG_H83007) || defined(CONFIG_H83068)
#define P1DDR (unsigned char *)0xfee000
#define P2DDR (unsigned char *)0xfee001
#define P3DDR (unsigned char *)0xfee002
#define P4DDR (unsigned char *)0xfee003
#define P5DDR (unsigned char *)0xfee004
#define P6DDR (unsigned char *)0xfee005
#define P8DDR (unsigned char *)0xfee007
#define P9DDR (unsigned char *)0xfee008
#define PADDR (unsigned char *)0xfee009
#define PBDDR (unsigned char *)0xfee00A
#endif
#if defined(CONFIG_H83002) || defined(CONFIG_H8048)
#define P1DDR (unsigned char *)0xffffc0
#define P2DDR (unsigned char *)0xffffc1
#define P3DDR (unsigned char *)0xffffc4
#define P4DDR (unsigned char *)0xffffc5
#define P5DDR (unsigned char *)0xffffc8
#define P6DDR (unsigned char *)0xffffc9
#define P8DDR (unsigned char *)0xffffcd
#define P9DDR (unsigned char *)0xffffd0
#define PADDR (unsigned char *)0xffffd1
#define PBDDR (unsigned char *)0xffffd4
#endif

#if defined(P1DDR)

#define MAX_PORT 11

static struct {
	unsigned char used;
	unsigned char ddr;
} gpio_regs[MAX_PORT];

static volatile unsigned char *ddrs[] = {
	P1DDR,P2DDR,P3DDR,P4DDR,P5DDR,P6DDR,NULL,P8DDR,P9DDR,PADDR,PBDDR,
};

extern char *_platform_gpio_table(int length);

int h8300_reserved_gpio(int port, unsigned int bits)
{
	unsigned char *used;
	if (port < 0 || port >= MAX_PORT)
		return -1;
	used = &(gpio_regs[port].used);
	if ((*used & bits) != 0)
		return 0;
	*used |= bits;
	return 1;
}

int h8300_free_gpio(int port, unsigned int bits)
{
	unsigned char *used;
	if (port < 0 || port >= MAX_PORT)
		return -1;
	used = &(gpio_regs[port].used);
	if ((*used & bits) != bits)
		return 0;
	*used &= (~bits);
	return 1;
}

int h8300_set_gpio_dir(int port_bit,int dir)
{
	const unsigned char mask[]={0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80};
	int port = (port_bit >> 8) & 0xff;
	int bit  = port_bit & 0x07;
	if (ddrs[port] == NULL)
		return 0;
	if (gpio_regs[port].used & mask[bit]) {
		if (dir)
			gpio_regs[port].ddr |= mask[bit];
		else
			gpio_regs[port].ddr &= ~mask[bit];
		*ddrs[port] = gpio_regs[port].ddr;
		return 1;
	} else
		return 0;
}

int h8300_get_gpio_dir(int port_bit)
{
	const unsigned char mask[]={0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80};
	int port = (port_bit >> 8) & 0xff;
	int bit  = port_bit & 0x07;
	if (ddrs[port] == NULL)
		return 0;
	if (gpio_regs[port].used & mask[bit]) {
		return (gpio_regs[port].ddr & mask[bit]) != 0;
	} else
		return -1;
}

#if defined(CONFIG_PROC_FS)
static char *port_status(int portno)
{
	static char result[10];
	const static char io[2]={'I','O'};
	char *rp;
	int c;
	unsigned char used,ddr;
	
	used = gpio_regs[portno].used;
	ddr  = gpio_regs[portno].ddr;
	result[8]='\0';
	rp = result + 7;
	for (c = 8; c > 0; c--,rp--,used >>= 1, ddr >>= 1)
		if (used & 0x01)
			*rp = io[ ddr & 0x01];
		else	
			*rp = '-';
	return result;
}

static int gpio_proc_read(char *buf, char **start, off_t offset, int len, int unused)
{
	int c,outlen;
	const static char port_name[]="123456789AB";
	outlen = 0;
	for (c = 0; c < MAX_PORT; c++) {
		if (ddrs[c] == NULL)
			continue ;
		len = sprintf(buf,"P%c: %s\n",port_name[c],port_status(c));
		buf += len;
		outlen += len;
	}
	return outlen;
}

static const struct proc_dir_entry proc_gpio = {
	0, 4,"gpio",S_IFREG | S_IRUGO, 1, 0, 0, 0, NULL, gpio_proc_read,
};
#endif

int h8300_gpio_init(void)
{
	memcpy(gpio_regs,_platform_gpio_table(sizeof(gpio_regs)),sizeof(gpio_regs));
#if 0 && defined(CONFIG_PROC_FS)
	proc_register(&proc_root,&proc_gpio);
#endif
	return 0;
}

#else
#error Unsuppoted CPU Selection
#endif
