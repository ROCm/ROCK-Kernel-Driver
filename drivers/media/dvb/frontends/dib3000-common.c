#include "dib3000-common.h"

#ifdef CONFIG_DVB_DIBCOM_DEBUG
static int debug;
module_param(debug, int, 0x644);
MODULE_PARM_DESC(debug, "set debugging level (1=info,2=i2c,4=srch (|-able)).");
#endif
#define deb_info(args...) dprintk(0x01,args)
#define deb_i2c(args...) dprintk(0x02,args)
#define deb_srch(args...) dprintk(0x04,args)


int dib3000_read_reg(struct dib3000_state *state, u16 reg)
{
	u8 wb[] = { ((reg >> 8) | 0x80) & 0xff, reg & 0xff };
	u8 rb[2];
	struct i2c_msg msg[] = {
		{ .addr = state->config.demod_address, .flags = 0,        .buf = wb, .len = 2 },
		{ .addr = state->config.demod_address, .flags = I2C_M_RD, .buf = rb, .len = 2 },
	};

	if (i2c_transfer(state->i2c, msg, 2) != 2)
		deb_i2c("i2c read error\n");

	deb_i2c("reading i2c bus (reg: %5d 0x%04x, val: %5d 0x%04x)\n",reg,reg,
			(rb[0] << 8) | rb[1],(rb[0] << 8) | rb[1]);

	return (rb[0] << 8) | rb[1];
}

int dib3000_write_reg(struct dib3000_state *state, u16 reg, u16 val)
{
	u8 b[] = {
		(reg >> 8) & 0xff, reg & 0xff,
		(val >> 8) & 0xff, val & 0xff,
	};
	struct i2c_msg msg[] = {
		{ .addr = state->config.demod_address, .flags = 0, .buf = b, .len = 4 }
	};
	deb_i2c("writing i2c bus (reg: %5d 0x%04x, val: %5d 0x%04x)\n",reg,reg,val,val);

	return i2c_transfer(state->i2c,msg, 1) != 1 ? -EREMOTEIO : 0;
}

int dib3000_init_pid_list(struct dib3000_state *state, int num)
{
	int i;
	if (state != NULL) {
		state->pid_list = kmalloc(sizeof(struct dib3000_pid) * num,GFP_KERNEL);
		if (state->pid_list == NULL)
			return -ENOMEM;

		deb_info("initializing %d pids for the pid_list.\n",num);
		state->pid_list_lock = SPIN_LOCK_UNLOCKED;
		memset(state->pid_list,0,num*(sizeof(struct dib3000_pid)));
		for (i=0; i < num; i++) {
			state->pid_list[i].pid = 0;
			state->pid_list[i].active = 0;
		}
		state->feedcount = 0;
	} else
		return -EINVAL;

	return 0;
}

void dib3000_dealloc_pid_list(struct dib3000_state *state)
{
	if (state != NULL && state->pid_list != NULL)
		kfree(state->pid_list);
}

/* fetch a pid from pid_list */
int dib3000_get_pid_index(struct dib3000_pid pid_list[], int num_pids, int pid,
		spinlock_t *pid_list_lock,int onoff)
{
	int i,ret = -1;
	unsigned long flags;

	spin_lock_irqsave(pid_list_lock,flags);
	for (i=0; i < num_pids; i++)
		if (onoff) {
			if (!pid_list[i].active) {
				pid_list[i].pid = pid;
				pid_list[i].active = 1;
				ret = i;
				break;
			}
		} else {
			if (pid_list[i].active && pid_list[i].pid == pid) {
				pid_list[i].pid = 0;
				pid_list[i].active = 0;
				ret = i;
				break;
			}
		}

	deb_info("setting pid: %5d %04x at index %d '%s'\n",pid,pid,ret,onoff ? "on" : "off");

	spin_unlock_irqrestore(pid_list_lock,flags);
	return ret;
}

int dib3000_search_status(u16 irq,u16 lock)
{
	if (irq & 0x02) {
		if (lock & 0x01) {
			deb_srch("auto search succeeded\n");
			return 1; // auto search succeeded
		} else {
			deb_srch("auto search not successful\n");
			return 0; // auto search failed
		}
	} else if (irq & 0x01)  {
		deb_srch("auto search failed\n");
		return 0; // auto search failed
	}
	return -1; // try again
}

/* for auto search */
u16 dib3000_seq[2][2][2] =     /* fft,gua,   inv   */
	{ /* fft */
		{ /* gua */
			{ 0, 1 },                   /*  0   0   { 0,1 } */
			{ 3, 9 },                   /*  0   1   { 0,1 } */
		},
		{
			{ 2, 5 },                   /*  1   0   { 0,1 } */
			{ 6, 11 },                  /*  1   1   { 0,1 } */
		}
	};

MODULE_AUTHOR("Patrick Boettcher <patrick.boettcher@desy.de");
MODULE_DESCRIPTION("Common functions for the dib3000mb/dib3000mc dvb frontend drivers");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(dib3000_seq);

EXPORT_SYMBOL(dib3000_read_reg);
EXPORT_SYMBOL(dib3000_write_reg);
EXPORT_SYMBOL(dib3000_init_pid_list);
EXPORT_SYMBOL(dib3000_dealloc_pid_list);
EXPORT_SYMBOL(dib3000_get_pid_index);
EXPORT_SYMBOL(dib3000_search_status);
