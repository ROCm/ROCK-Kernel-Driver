/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * PowerMac G5 SMU driver
 *
 * Copyright 2004 J. Mayer <l_indien@magic.fr>
 *
 * Released under the term of the GNU GPL v2.
 */

/*
 * For now, this driver includes:
 * - RTC get & set
 * - reboot & shutdown commands
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/dmapool.h>
#include <linux/bootmem.h>
#include <linux/vmalloc.h>
#include <linux/highmem.h>
#include <linux/jiffies.h>
#include <linux/interrupt.h>
#include <linux/rtc.h>
#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/pmac_feature.h>
#include <asm/sections.h>

#undef DEBUG_SMU

#if defined(DEBUG_SMU)
#define DPRINTK(fmt, args...) do { printk(fmt , ##args); } while (0)
#else
#define DPRINTK(fmt, args...) do { } while (0)
#endif

typedef struct cmd_buf_t cmd_buf_t;
struct cmd_buf_t {
	uint8_t cmd;
	uint8_t length;
	uint8_t data[0x0FFE];
};

struct SMU_dev_t {
	spinlock_t lock;
	struct device_node *np;
	unsigned char name[16];
	int db_ack;             /* SMU doorbell ack GPIO */
	int db_req;             /* SMU doorbell req GPIO */
	u32 db_buff0;
	u32 db_buff;            /* SMU doorbell buffer location */
	void *db_buff_remap;    /* SMU doorbell buffer location remapped */
	int SMU_irq;            /* SMU IRQ */
	int SMU_irq_gpio;       /* SMU interrupt GPIO */
	int programmer_switch;  /* SMU programmer switch GPIO */
	int programmer_irq;     /* SMU programmer switch IRQ */
	int WOR_disable;
	int WOR_enable;
	cmd_buf_t *cmd_buf;
};

#define SMU_MAX 4
static struct SMU_dev_t SMU_devices[SMU_MAX];
static int SMU_nb;

/* SMU low level stuff */
static inline int cmd_stat (cmd_buf_t *cmd_buf, u8 cmd_ack)
{
	return cmd_buf->cmd == cmd_ack && cmd_buf->length != 0;
}

static inline u8 save_ack_cmd (cmd_buf_t *cmd_buf)
{
	return (~cmd_buf->cmd) & 0xff;
}

static void send_cmd (struct SMU_dev_t *dev)
{
	cmd_buf_t *cmd_buf;

	cmd_buf = dev->cmd_buf;
	out_le32(dev->db_buff_remap, virt_to_phys(cmd_buf));
	/* Ring the SMU doorbell */
	pmac_do_feature_call(PMAC_FTR_WRITE_GPIO, NULL, dev->db_req, 4);
}

static int cmd_done (struct SMU_dev_t *dev)
{
	unsigned long wait;
	int gpio;
    
	/* Check the SMU doorbell */
	for (wait = jiffies + HZ; time_before(jiffies, wait); ) {
		gpio = pmac_do_feature_call(PMAC_FTR_READ_GPIO,
					    NULL, dev->db_ack);
		if ((gpio & 7) == 7)
			return 0;
	}

	return -1;
}

static int do_cmd (struct SMU_dev_t *dev)
{
	int ret;
	u8 cmd_ack;
    
	DPRINTK("SMU do_cmd %02x len=%d %02x\n",
		dev->cmd_buf->cmd, dev->cmd_buf->length,
		dev->cmd_buf->data[0]);
	cmd_ack = save_ack_cmd(dev->cmd_buf);
	/* Clear cmd_buf cache lines */
	flush_inval_dcache_phys_range(virt_to_phys(dev->cmd_buf),
				      virt_to_phys(dev->cmd_buf + 1));
	mb();
	send_cmd(dev);
	ret = cmd_done(dev);
	mb();
	if (ret == 0)
		ret = cmd_stat(dev->cmd_buf, cmd_ack) ? 0 : -1;
	DPRINTK("SMU do_cmd %02x len=%d %02x => %d (%02x)\n",
		dev->cmd_buf->cmd,
		dev->cmd_buf->length, dev->cmd_buf->data[0], ret, cmd_ack);

	return ret;
}

static irqreturn_t SMU_irq_handler (int irq, void *arg, struct pt_regs *regs)
{
	/* Fake handler for now */
	//    printk("SMU irq %d\n", irq);

	return 0;
}

/* RTC low level commands */
static inline int bcd2hex (int n)
{
	return (((n & 0xf0) >> 4) * 10) + (n & 0xf);
}

static inline int hex2bcd (int n)
{
	return ((n / 10) << 4) + (n % 10);
}

static inline void set_pwrup_timer_cmd (cmd_buf_t *cmd_buf)
{
	cmd_buf->cmd = 0x8e;
	cmd_buf->length = 8;
	cmd_buf->data[0] = 0x00;
	memset(cmd_buf->data + 1, 0, 7);
}

static inline void get_pwrup_timer_cmd (cmd_buf_t *cmd_buf)
{
	cmd_buf->cmd = 0x8e;
	cmd_buf->length = 1;
	cmd_buf->data[0] = 0x01;
}

static inline void disable_pwrup_timer_cmd (cmd_buf_t *cmd_buf)
{
	cmd_buf->cmd = 0x8e;
	cmd_buf->length = 1;
	cmd_buf->data[0] = 0x02;
}

static inline void set_rtc_cmd (cmd_buf_t *cmd_buf, struct rtc_time *time)
{
	cmd_buf->cmd = 0x8e;
	cmd_buf->length = 8;
	cmd_buf->data[0] = 0x80;
	cmd_buf->data[1] = hex2bcd(time->tm_sec);
	cmd_buf->data[2] = hex2bcd(time->tm_min);
	cmd_buf->data[3] = hex2bcd(time->tm_hour);
	cmd_buf->data[4] = time->tm_wday;
	cmd_buf->data[5] = hex2bcd(time->tm_mday);
	cmd_buf->data[6] = hex2bcd(time->tm_mon) + 1;
	cmd_buf->data[7] = hex2bcd(time->tm_year - 100);
}

static inline void get_rtc_cmd (cmd_buf_t *cmd_buf)
{
	cmd_buf->cmd = 0x8e;
	cmd_buf->length = 1;
	cmd_buf->data[0] = 0x81;
}

/* RTC interface */
void __pmac smu_get_rtc_time (struct rtc_time *time)
{
	struct SMU_dev_t *dev;
	cmd_buf_t *cmd_buf;
	long flags;

	if (SMU_nb == 0 || SMU_devices[0].cmd_buf == NULL)
		return;
	memset(time, 0, sizeof(struct rtc_time));
	dev = &SMU_devices[0];
	cmd_buf = dev->cmd_buf;
	DPRINTK("SMU get_rtc_time %p\n", cmd_buf);
	spin_lock_irqsave(&dev->lock, flags);
	get_rtc_cmd(cmd_buf);
	if (do_cmd(dev) == 0) {
		time->tm_sec = bcd2hex(cmd_buf->data[0]);
		time->tm_min = bcd2hex(cmd_buf->data[1]);
		time->tm_hour = bcd2hex(cmd_buf->data[2]);
		time->tm_wday = bcd2hex(cmd_buf->data[3]);
		time->tm_mday = bcd2hex(cmd_buf->data[4]);
		time->tm_mon = bcd2hex(cmd_buf->data[5]) - 1;
		time->tm_year = bcd2hex(cmd_buf->data[6]) + 100;
	}
	spin_unlock_irqrestore(&dev->lock, flags);
	DPRINTK("SMU get_rtc_time done\n");
}

int  __pmac smu_set_rtc_time (struct rtc_time *time)
{
	struct SMU_dev_t *dev;
	cmd_buf_t *cmd_buf;
	long flags;
	int ret;

	if (SMU_nb == 0 || SMU_devices[0].cmd_buf == NULL)
		return -1;
	DPRINTK("SMU set_rtc_time\n");
	dev = &SMU_devices[0];
	cmd_buf = dev->cmd_buf;
	spin_lock_irqsave(&dev->lock, flags);
	set_rtc_cmd(cmd_buf, time);
	ret = do_cmd(dev);
	spin_unlock_irqrestore(&dev->lock, flags);
	DPRINTK("SMU set_rtc_time done\n");

	return ret;
}

void __init smu_get_boot_time (struct rtc_time *tm)
{
	DPRINTK("SMU get_boot_time...\n");
	if (SMU_nb == 0 || SMU_devices[0].cmd_buf == NULL) {
		printk(KERN_ERR "%s: no SMU registered\n", __func__);
		return;
	}
	DPRINTK("SMU get_boot_time\n");
	smu_get_rtc_time(tm);
}

/* Misc functions */
void __pmac smu_shutdown (void)
{
	const unsigned char *command = "SHUTDOWN";
	struct SMU_dev_t *dev;
	cmd_buf_t *cmd_buf;
	long flags;

	if (SMU_nb == 0)
		return;
	dev = &SMU_devices[0];
	cmd_buf = dev->cmd_buf;
	spin_lock_irqsave(&dev->lock, flags);
	cmd_buf->cmd = 0xaa;
	cmd_buf->length = strlen(command);
	strcpy(cmd_buf->data, command);
	do_cmd(dev);
	/* If we get here, we got a problem */
	{ 
		int i;
		for (i = 0; i < cmd_buf->length; i++)
			printk("%02x ", cmd_buf->data[i]);
		printk("\n");
	}
	spin_unlock_irqrestore(&dev->lock, flags);
}

void __pmac smu_restart (char *cmd)
{
	const unsigned char *command = "RESTART";
	struct SMU_dev_t *dev;
	cmd_buf_t *cmd_buf;
	long flags;

	if (SMU_nb == 0)
		return;
	dev = &SMU_devices[0];
	cmd_buf = dev->cmd_buf;
	spin_lock_irqsave(&dev->lock, flags);
	cmd_buf->cmd = 0xaa;
	cmd_buf->length = strlen(command);
	strcpy(cmd_buf->data, command);
	do_cmd(dev);
	/* If we get here, we got a problem */
	{ 
		int i;
		for (i = 0; i < cmd_buf->length; i++)
			printk("%02x ", cmd_buf->data[i]);
		printk("\n");
	}
	spin_unlock_irqrestore(&dev->lock, flags);
}

/* SMU initialisation */
static int smu_register (struct device_node *np,
                         int db_ack, int db_req, u32 db_buff,
                         int SMU_irq, int SMU_irq_gpio)
{
	void *db_buff_remap;
	cmd_buf_t *cmd_buf;
    
	DPRINTK("register one SMU\n");
	sprintf(SMU_devices[SMU_nb].name, "SMU%d\n", SMU_nb);
	if (SMU_nb == SMU_MAX) {
		DPRINTK("Too many SMUs\n");
		return -1;
	}
	db_buff_remap = ioremap(0x8000860C, 16);
	if (db_buff_remap == NULL) {
		DPRINTK("SMU remap fail\n");
		return -1;
	}
	cmd_buf = alloc_bootmem(sizeof(cmd_buf_t));
	if (cmd_buf == NULL) {
		DPRINTK("SMU alloc fail\n");
		iounmap(db_buff_remap);
		return -1;
	}
#if O
	{
		unsigned long flags;
		DPRINTK("SMU IRQ\n");
		flags = SA_SHIRQ | SA_INTERRUPT;
		if (request_irq(SMU_irq, &SMU_irq_handler, flags,
				SMU_devices[SMU_nb].name,
				&SMU_devices[SMU_nb]) < 0) {
			DPRINTK("SMU IRQ fail\n");
			iounmap(db_buff_remap);
			return -1;
		}
	}
#endif
	SMU_devices[SMU_nb].np = np;
	SMU_devices[0].cmd_buf = cmd_buf;
	/* XXX: 0x50 should be retrieved from MacIO properties */ 
	SMU_devices[SMU_nb].db_ack = db_ack + 0x50;
	SMU_devices[SMU_nb].db_req = db_req + 0x50;
	SMU_devices[SMU_nb].db_buff = 0x8000860C;
	SMU_devices[SMU_nb].db_buff0 = db_buff;
	SMU_devices[SMU_nb].db_buff_remap = db_buff_remap;
	SMU_devices[SMU_nb].SMU_irq = SMU_irq;
	SMU_devices[SMU_nb].SMU_irq_gpio = SMU_irq_gpio;
	SMU_nb++;

	return 0;
}

static int smu_locate_resource (struct device_node **np, u32 **pp,
                                u32 **rp, int *nr, u32 **ip, int *ni,
                                struct device_node *SMU_node,
                                const unsigned char *name)
{
	unsigned int *r;
	int l;

	r = (unsigned int *)get_property(SMU_node, name, &l);
	if (l == 0)
		return -1;
	*np = of_find_node_by_phandle(*r);
	if (*np == NULL)
		return -1;
	*pp = (unsigned int *)get_property(*np, name, &l);
	if (l == 0)
		return -1;
	*rp = (unsigned int *)get_property(*np, "reg", nr);
	*ip = (unsigned int *)get_property(*np, "interrupts", ni);

	return 0;
}

int __openfirmware smu_init (void)
{
	struct device_node *SMU_node, *np;
	u32 *pp, *rp, *ip;
	u32 doorbell_buf;
	int doorbell_ack, doorbell_req, SMU_irq, SMU_irq_gpio;
	int nr, ni;

	DPRINTK("Starting SMU probe\n");
	SMU_node = NULL;
	while (1) {
		SMU_node = of_find_node_by_type(SMU_node, "smu");
		if (SMU_node == NULL)
			break;
		/* Locate doorbell ACK and REQ */
		if (smu_locate_resource(&np, &pp, &rp, &nr, &ip, &ni,
					SMU_node,
					"platform-doorbell-ack") < 0) {
			DPRINTK("SMU 'ack'\n");
			continue;
		}
		if (nr == 0) {
			DPRINTK("MacIO GPIO 'ack'\n");
			continue;
		}
		doorbell_ack = *rp;
		if (smu_locate_resource(&np, &pp, &rp, &nr, &ip, &ni,
					SMU_node,
					"platform-doorbell-req") < 0) {
			DPRINTK("SMU 'req'\n");
			continue;
		}
		if (nr == 0) {
			DPRINTK("MacIO GPIO 'req'\n");
			continue;
		}
		doorbell_req = *rp;
		/* Locate doorbell buffer */
		if (smu_locate_resource(&np, &pp, &rp, &nr, &ip, &ni,
					SMU_node,
					"platform-doorbell-buff") < 0) {
			DPRINTK("SMU 'buff'\n");
			continue;
		}
		if (nr < 4) {
			DPRINTK("MacIO regs 'buff'\n");
			continue;
		}
		doorbell_buf = rp[1] | rp[3];
		/* Locate programmer switch */
		if (smu_locate_resource(&np, &pp, &rp, &nr, &ip, &ni,
					SMU_node,
					"platform-programmer-switch") < 0) {
			DPRINTK("SMU 'switch'\n");
			continue;
		}
		/* Locate SMU IRQ */
		if (smu_locate_resource(&np, &pp, &rp, &nr, &ip, &ni,
					SMU_node,
					"platform-smu-interrupt") < 0) {
			DPRINTK("SMU 'interrupt'\n");
			continue;
		}
		if (np == 0 || ni == 0) {
			DPRINTK("MacIO GPIO 'interrupt'\n");
			continue;
		}
		SMU_irq_gpio = *rp;
		SMU_irq = *ip;
		/* Locate wor disable/enable */
		if (smu_locate_resource(&np, &pp, &rp, &nr, &ip, &ni,
					SMU_node,
					"platform-wor-disable") < 0) {
			DPRINTK("SMU 'wor-disable'\n");
			continue;
		}
		if (smu_locate_resource(&np, &pp, &rp, &nr, &ip, &ni,
					SMU_node, "platform-wor-enable") < 0) {
			DPRINTK("SMU 'wor-enable'\n");
			continue;
		}
#if 0
		/* Locate powertune step point */
		if (smu_locate_resource(&np, &pp, &rp, &nr, &ip, &ni,
					SMU_node,
					"powertune-step-point") < 0) {
			DPRINTK("SMU 'step-point'\n");
			continue;
		}
#endif
		smu_register(SMU_node, doorbell_ack, doorbell_req,
			     doorbell_buf, SMU_irq, SMU_irq_gpio);
	}
	DPRINTK("SMU probe done\n");

	return SMU_nb;
}
