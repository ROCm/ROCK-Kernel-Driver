/*
 *
 * Hardware accelerated Matrox Millennium I, II, Mystique, G100, G200, G400 and G450.
 *
 * (c) 1998-2001 Petr Vandrovec <vandrove@vc.cvut.cz>
 *
 * Version: 1.51 2001/01/19
 *
 * See matroxfb_base.c for contributors.
 *
 */

#include "matroxfb_base.h"
#include "matroxfb_maven.h"
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>

/* MGA-TVO I2C for G200, G400 */
#define MAT_CLK		0x20
#define MAT_DATA	0x10
/* primary head DDC for Mystique(?), G100, G200, G400 */
#define DDC1_CLK	0x08
#define DDC1_DATA	0x02
/* primary head DDC for Millennium, Millennium II */
#define DDC1B_CLK	0x10
#define DDC1B_DATA	0x04
/* secondary head DDC for G400 */
#define DDC2_CLK	0x04
#define DDC2_DATA	0x01

/******************************************************/

static int matroxfb_read_gpio(struct matrox_fb_info* minfo) {
	unsigned long flags;
	int v;

	matroxfb_DAC_lock_irqsave(flags);
	v = matroxfb_DAC_in(PMINFO DAC_XGENIODATA);
	matroxfb_DAC_unlock_irqrestore(flags);
	return v;
}

static inline void matroxfb_set_gpio(struct matrox_fb_info* minfo, int mask, int val) {
	unsigned long flags;
	int v;

	matroxfb_DAC_lock_irqsave(flags);
	v = (matroxfb_DAC_in(PMINFO DAC_XGENIOCTRL) & mask) | val;
	matroxfb_DAC_out(PMINFO DAC_XGENIOCTRL, v);
	/* We must reset GENIODATA very often... XFree plays with this register */
	matroxfb_DAC_out(PMINFO DAC_XGENIODATA, 0x00);
	matroxfb_DAC_unlock_irqrestore(flags);
}

/* software I2C functions */
static void matroxfb_i2c_set(struct matrox_fb_info* minfo, int mask, int state) {
	if (state)
		state = 0;
	else
		state = mask;
	matroxfb_set_gpio(minfo, ~mask, state);
}

static void matroxfb_maven_setsda(void* data, int state) {
	matroxfb_i2c_set(data, MAT_DATA, state);
}

static void matroxfb_maven_setscl(void* data, int state) {
	matroxfb_i2c_set(data, MAT_CLK, state);
}

static int matroxfb_maven_getsda(void* data) {
	return (matroxfb_read_gpio(data) & MAT_DATA) ? 1 : 0;
}

static int matroxfb_maven_getscl(void* data) {
	return (matroxfb_read_gpio(data) & MAT_CLK) ? 1 : 0;
}

static void matroxfb_ddc1_setsda(void* data, int state) {
	matroxfb_i2c_set(data, DDC1_DATA, state);
}

static void matroxfb_ddc1_setscl(void* data, int state) {
	matroxfb_i2c_set(data, DDC1_CLK, state);
}

static int matroxfb_ddc1_getsda(void* data) {
	return (matroxfb_read_gpio(data) & DDC1_DATA) ? 1 : 0;
}

static int matroxfb_ddc1_getscl(void* data) {
	return (matroxfb_read_gpio(data) & DDC1_CLK) ? 1 : 0;
}

static void matroxfb_ddc1b_setsda(void* data, int state) {
	matroxfb_i2c_set(data, DDC1B_DATA, state);
}

static void matroxfb_ddc1b_setscl(void* data, int state) {
	matroxfb_i2c_set(data, DDC1B_CLK, state);
}

static int matroxfb_ddc1b_getsda(void* data) {
	return (matroxfb_read_gpio(data) & DDC1B_DATA) ? 1 : 0;
}

static int matroxfb_ddc1b_getscl(void* data) {
	return (matroxfb_read_gpio(data) & DDC1B_CLK) ? 1 : 0;
}

static void matroxfb_ddc2_setsda(void* data, int state) {
	matroxfb_i2c_set(data, DDC2_DATA, state);
}

static void matroxfb_ddc2_setscl(void* data, int state) {
	matroxfb_i2c_set(data, DDC2_CLK, state);
}

static int matroxfb_ddc2_getsda(void* data) {
	return (matroxfb_read_gpio(data) & DDC2_DATA) ? 1 : 0;
}

static int matroxfb_ddc2_getscl(void* data) {
	return (matroxfb_read_gpio(data) & DDC2_CLK) ? 1 : 0;
}

static void matroxfb_dh_inc_use(struct i2c_adapter* dummy) {
	MOD_INC_USE_COUNT;
}

static void matroxfb_dh_dec_use(struct i2c_adapter* dummy) {
	MOD_DEC_USE_COUNT;
}

static struct i2c_adapter matroxmaven_i2c_adapter_template =
{
	"",
	I2C_HW_B_G400,

	NULL,
	NULL,

	matroxfb_dh_inc_use,
	matroxfb_dh_dec_use,
	NULL,
	NULL,
	NULL,
};

static struct i2c_algo_bit_data matroxmaven_i2c_algo_template =
{
	NULL,
	matroxfb_maven_setsda,
	matroxfb_maven_setscl,
	matroxfb_maven_getsda,
	matroxfb_maven_getscl,
	10, 10, 100,
};

static struct i2c_adapter matrox_ddc1_adapter_template =
{
	"",
	I2C_HW_B_G400, /* DDC */

	NULL,
	NULL,

	matroxfb_dh_inc_use,
	matroxfb_dh_dec_use,
	NULL,
	NULL,
	NULL,
};

static struct i2c_algo_bit_data matrox_ddc1_algo_template =
{
	NULL,
	matroxfb_ddc1_setsda,
	matroxfb_ddc1_setscl,
	matroxfb_ddc1_getsda,
	matroxfb_ddc1_getscl,
	10, 10, 100,
};

static struct i2c_algo_bit_data matrox_ddc1b_algo_template =
{
	NULL,
	matroxfb_ddc1b_setsda,
	matroxfb_ddc1b_setscl,
	matroxfb_ddc1b_getsda,
	matroxfb_ddc1b_getscl,
	10, 10, 100,
};

static struct i2c_adapter matrox_ddc2_adapter_template =
{
	"",
	I2C_HW_B_G400,	/* DDC */

	NULL,
	NULL,

	matroxfb_dh_inc_use, /* should increment matroxfb_maven usage too, this DDC is coupled with maven_client */
	matroxfb_dh_dec_use, /* should decrement matroxfb_maven usage too */
	NULL,
	NULL,
	NULL,
};

static struct i2c_algo_bit_data matrox_ddc2_algo_template =
{
	NULL,
	matroxfb_ddc2_setsda,
	matroxfb_ddc2_setscl,
	matroxfb_ddc2_getsda,
	matroxfb_ddc2_getscl,
	10, 10, 100,
};

static int i2c_bus_reg(struct i2c_bit_adapter* b, struct matrox_fb_info* minfo) {
	int err;

	b->adapter.data = minfo;
	b->adapter.algo_data = &b->bac;
	b->bac.data = minfo;
	err = i2c_bit_add_bus(&b->adapter);
	b->initialized = !err;
	return err;
}

static void i2c_bit_bus_del(struct i2c_bit_adapter* b) {
	if (b->initialized) {
		i2c_bit_del_bus(&b->adapter);
		b->initialized = 0;
	}
}

static inline int i2c_maven_init(struct matroxfb_dh_maven_info* minfo2) {
	struct i2c_bit_adapter *b = &minfo2->maven;

	b->adapter = matroxmaven_i2c_adapter_template;
	b->bac = matroxmaven_i2c_algo_template;
	sprintf(b->adapter.name, "MAVEN:fb%u on i2c-matroxfb", GET_FB_IDX(minfo2->primary_dev->fbcon.node));
	return i2c_bus_reg(b, minfo2->primary_dev);
}

static inline void i2c_maven_done(struct matroxfb_dh_maven_info* minfo2) {
	i2c_bit_bus_del(&minfo2->maven);
}

static inline int i2c_ddc1_init(struct matroxfb_dh_maven_info* minfo2) {
	struct i2c_bit_adapter *b = &minfo2->ddc1;

	b->adapter = matrox_ddc1_adapter_template;
	b->bac = matrox_ddc1_algo_template;
	sprintf(b->adapter.name, "DDC:fb%u #0 on i2c-matroxfb", GET_FB_IDX(minfo2->primary_dev->fbcon.node));
	return i2c_bus_reg(b, minfo2->primary_dev);
}

static inline int i2c_ddc1b_init(struct matroxfb_dh_maven_info* minfo2) {
	struct i2c_bit_adapter *b = &minfo2->ddc1;

	b->adapter = matrox_ddc1_adapter_template;
	b->bac = matrox_ddc1b_algo_template;
	sprintf(b->adapter.name, "DDC:fb%u #0 on i2c-matroxfb", GET_FB_IDX(minfo2->primary_dev->fbcon.node));
	return i2c_bus_reg(b, minfo2->primary_dev);
}

static inline void i2c_ddc1_done(struct matroxfb_dh_maven_info* minfo2) {
	i2c_bit_bus_del(&minfo2->ddc1);
}

static inline int i2c_ddc2_init(struct matroxfb_dh_maven_info* minfo2) {
	struct i2c_bit_adapter *b = &minfo2->ddc2;

	b->adapter = matrox_ddc2_adapter_template;
	b->bac = matrox_ddc2_algo_template;
	sprintf(b->adapter.name, "DDC:fb%u #1 on i2c-matroxfb", GET_FB_IDX(minfo2->primary_dev->fbcon.node));
	return i2c_bus_reg(b, minfo2->primary_dev);
}

static inline void i2c_ddc2_done(struct matroxfb_dh_maven_info* minfo2) {
	i2c_bit_bus_del(&minfo2->ddc2);
}

static void* i2c_matroxfb_probe(struct matrox_fb_info* minfo) {
	int err;
	unsigned long flags;
	struct matroxfb_dh_maven_info* m2info;

	m2info = (struct matroxfb_dh_maven_info*)kmalloc(sizeof(*m2info), GFP_KERNEL);
	if (!m2info)
		return NULL;

	matroxfb_DAC_lock_irqsave(flags);
	matroxfb_DAC_out(PMINFO DAC_XGENIODATA, 0xFF);
	matroxfb_DAC_out(PMINFO DAC_XGENIOCTRL, 0x00);
	matroxfb_DAC_unlock_irqrestore(flags);

	memset(m2info, 0, sizeof(*m2info));
	m2info->maven.minfo = m2info;
	m2info->ddc1.minfo = m2info;
	m2info->ddc2.minfo = m2info;
	m2info->primary_dev = minfo;

	if (ACCESS_FBINFO(devflags.accelerator) == FB_ACCEL_MATROX_MGA2064W ||
	    ACCESS_FBINFO(devflags.accelerator) == FB_ACCEL_MATROX_MGA2164W ||
	    ACCESS_FBINFO(devflags.accelerator) == FB_ACCEL_MATROX_MGA2164W_AGP)
		err = i2c_ddc1b_init(m2info);
	else
		err = i2c_ddc1_init(m2info);
	if (err)
		goto fail_ddc1;
	if (ACCESS_FBINFO(devflags.maven_capable)) {
		err = i2c_ddc2_init(m2info);
		if (err)
			printk(KERN_INFO "i2c-matroxfb: Could not register secondary output i2c bus. Continuing anyway.\n");
		err = i2c_maven_init(m2info);
		if (err)
			printk(KERN_INFO "i2c-matroxfb: Could not register Maven i2c bus. Continuing anyway.\n");
	}
	return m2info;
fail_ddc1:;
	kfree(m2info);
	printk(KERN_ERR "i2c-matroxfb: Could not register primary adapter DDC bus.\n");
	return NULL;
}

static void i2c_matroxfb_remove(struct matrox_fb_info* minfo, void* data) {
	struct matroxfb_dh_maven_info* m2info = data;

	i2c_maven_done(m2info);
	i2c_ddc2_done(m2info);
	i2c_ddc1_done(m2info);
	kfree(m2info);
}

static struct matroxfb_driver i2c_matroxfb = {
	LIST_HEAD_INIT(i2c_matroxfb.node),
	"i2c-matroxfb",
	i2c_matroxfb_probe,
	i2c_matroxfb_remove,
};

static int __init i2c_matroxfb_init(void) {
	if (matroxfb_register_driver(&i2c_matroxfb)) {
		printk(KERN_ERR "i2c-matroxfb: failed to register driver\n");
		return -ENXIO;
	}
	return 0;
}

static void __exit i2c_matroxfb_exit(void) {
	matroxfb_unregister_driver(&i2c_matroxfb);
}

MODULE_AUTHOR("(c) 1999-2001 Petr Vandrovec <vandrove@vc.cvut.cz>");
MODULE_DESCRIPTION("Support module providing I2C buses present on Matrox videocards");

module_init(i2c_matroxfb_init);
module_exit(i2c_matroxfb_exit);
/* no __setup required */
MODULE_LICENSE("GPL");
