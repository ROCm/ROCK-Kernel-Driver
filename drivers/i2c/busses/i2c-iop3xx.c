/* ------------------------------------------------------------------------- */
/* i2c-iop3xx.c i2c driver algorithms for Intel XScale IOP3xx                */
/* ------------------------------------------------------------------------- */
/*   Copyright (C) 2003 Peter Milne, D-TACQ Solutions Ltd
 *                      <Peter dot Milne at D hyphen TACQ dot com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 2.


    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.                */
/* ------------------------------------------------------------------------- */
/*
   With acknowledgements to i2c-algo-ibm_ocp.c by 
   Ian DaSilva, MontaVista Software, Inc. idasilva@mvista.com

   And i2c-algo-pcf.c, which was created by Simon G. Vogl and Hans Berglund:

     Copyright (C) 1995-1997 Simon G. Vogl, 1998-2000 Hans Berglund
   
   And which acknowledged Kyösti Mälkki <kmalkki@cc.hut.fi>,
   Frodo Looijaard <frodol@dds.nl>, Martin Bailey<mbailey@littlefeet-inc.com>

  ---------------------------------------------------------------------------*/

#include <linux/config.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/i2c.h>


#include <asm/arch-iop3xx/iop321.h>
#include <asm/arch-iop3xx/iop321-irqs.h>
#include "i2c-iop3xx.h"


/* ----- global defines ----------------------------------------------- */
#define PASSERT(x) do { if (!(x) ) \
		printk(KERN_CRIT "PASSERT %s in %s:%d\n", #x, __FILE__, __LINE__ );\
	} while (0)


/* ----- global variables ---------------------------------------------	*/


static inline unsigned char iic_cook_addr(struct i2c_msg *msg) 
{
	unsigned char addr;

	addr = (msg->addr << 1);

	if (msg->flags & I2C_M_RD)
		addr |= 1;

	/* PGM: what is M_REV_DIR_ADDR - do we need it ?? */
	if (msg->flags & I2C_M_REV_DIR_ADDR)
		addr ^= 1;

	return addr;   
}


static inline void iop3xx_adap_reset(struct i2c_algo_iop3xx_data *iop3xx_adap)
{
	/* Follows devman 9.3 */
	*iop3xx_adap->biu->CR = IOP321_ICR_UNIT_RESET;
	*iop3xx_adap->biu->SR = IOP321_ISR_CLEARBITS;
	*iop3xx_adap->biu->CR = 0;
} 

static inline void iop3xx_adap_set_slave_addr(struct i2c_algo_iop3xx_data *iop3xx_adap)
{
	*iop3xx_adap->biu->SAR = MYSAR;
}

static inline void iop3xx_adap_enable(struct i2c_algo_iop3xx_data *iop3xx_adap)
{
	u32 cr = IOP321_ICR_GCD|IOP321_ICR_SCLEN|IOP321_ICR_UE;

	/* NB SR bits not same position as CR IE bits :-( */
	iop3xx_adap->biu->SR_enabled = 
		IOP321_ISR_ALD | IOP321_ISR_BERRD |
		IOP321_ISR_RXFULL | IOP321_ISR_TXEMPTY;

	cr |= IOP321_ICR_ALDIE | IOP321_ICR_BERRIE |
		IOP321_ICR_RXFULLIE | IOP321_ICR_TXEMPTYIE;

	*iop3xx_adap->biu->CR = cr;
}

static void iop3xx_adap_transaction_cleanup(struct i2c_algo_iop3xx_data *iop3xx_adap)
{
	unsigned cr = *iop3xx_adap->biu->CR;
	
	cr &= ~(IOP321_ICR_MSTART | IOP321_ICR_TBYTE | 
		IOP321_ICR_MSTOP | IOP321_ICR_SCLEN);
	*iop3xx_adap->biu->CR = cr;
}

static void iop3xx_adap_final_cleanup(struct i2c_algo_iop3xx_data *iop3xx_adap)
{
	unsigned cr = *iop3xx_adap->biu->CR;
	
	cr &= ~(IOP321_ICR_ALDIE | IOP321_ICR_BERRIE |
		IOP321_ICR_RXFULLIE | IOP321_ICR_TXEMPTYIE);
	iop3xx_adap->biu->SR_enabled = 0;
	*iop3xx_adap->biu->CR = cr;
}

/* 
 * NB: the handler has to clear the source of the interrupt! 
 * Then it passes the SR flags of interest to BH via adap data
 */
static irqreturn_t iop3xx_i2c_handler(int this_irq, 
				void *dev_id, 
				struct pt_regs *regs) 
{
	struct i2c_algo_iop3xx_data *iop3xx_adap = dev_id;

	u32 sr = *iop3xx_adap->biu->SR;

	if ((sr &= iop3xx_adap->biu->SR_enabled)) {
		*iop3xx_adap->biu->SR = sr;
		iop3xx_adap->biu->SR_received |= sr;
		wake_up_interruptible(&iop3xx_adap->waitq);
	}
	return IRQ_HANDLED;
}

/* check all error conditions, clear them , report most important */
static int iop3xx_adap_error(u32 sr)
{
	int rc = 0;

	if ((sr&IOP321_ISR_BERRD)) {
		if ( !rc ) rc = -I2C_ERR_BERR;
	}
	if ((sr&IOP321_ISR_ALD)) {
		if ( !rc ) rc = -I2C_ERR_ALD;		
	}
	return rc;	
}

static inline u32 get_srstat(struct i2c_algo_iop3xx_data *iop3xx_adap)
{
	unsigned long flags;
	u32 sr;

	spin_lock_irqsave(&iop3xx_adap->lock, flags);
	sr = iop3xx_adap->biu->SR_received;
	iop3xx_adap->biu->SR_received = 0;
	spin_unlock_irqrestore(&iop3xx_adap->lock, flags);

	return sr;
}

/*
 * sleep until interrupted, then recover and analyse the SR
 * saved by handler
 */
typedef int (* compare_func)(unsigned test, unsigned mask);
/* returns 1 on correct comparison */

static int iop3xx_adap_wait_event(struct i2c_algo_iop3xx_data *iop3xx_adap, 
				  unsigned flags, unsigned* status,
				  compare_func compare)
{
	unsigned sr = 0;
	int interrupted;
	int done;
	int rc = 0;

	do {
		interrupted = wait_event_interruptible_timeout (
			iop3xx_adap->waitq,
			(done = compare( sr = get_srstat(iop3xx_adap),flags )),
			iop3xx_adap->timeout
			);
		if ((rc = iop3xx_adap_error(sr)) < 0) {
			*status = sr;
			return rc;
		}else if (!interrupted) {
			*status = sr;
			return -ETIMEDOUT;
		}
	} while(!done);

	*status = sr;

	return 0;
}

/*
 * Concrete compare_funcs 
 */
static int all_bits_clear(unsigned test, unsigned mask)
{
	return (test & mask) == 0;
}
static int any_bits_set(unsigned test, unsigned mask)
{
	return (test & mask) != 0;
}

static int iop3xx_adap_wait_tx_done(struct i2c_algo_iop3xx_data *iop3xx_adap, int *status)
{
	return iop3xx_adap_wait_event( 
		iop3xx_adap, 
	        IOP321_ISR_TXEMPTY|IOP321_ISR_ALD|IOP321_ISR_BERRD,
		status, any_bits_set);
}

static int iop3xx_adap_wait_rx_done(struct i2c_algo_iop3xx_data *iop3xx_adap, int *status)
{
	return iop3xx_adap_wait_event( 
		iop3xx_adap, 
		IOP321_ISR_RXFULL|IOP321_ISR_ALD|IOP321_ISR_BERRD,
		status,	any_bits_set);
}

static int iop3xx_adap_wait_idle(struct i2c_algo_iop3xx_data *iop3xx_adap, int *status)
{
	return iop3xx_adap_wait_event( 
		iop3xx_adap, IOP321_ISR_UNITBUSY, status, all_bits_clear);
}

/*
 * Description: This performs the IOP3xx initialization sequence
 * Valid for IOP321. Maybe valid for IOP310?.
 */
static int iop3xx_adap_init (struct i2c_algo_iop3xx_data *iop3xx_adap)
{
	*IOP321_GPOD &= ~(iop3xx_adap->channel==0 ?
			  IOP321_GPOD_I2C0:
			  IOP321_GPOD_I2C1);

	iop3xx_adap_reset(iop3xx_adap);
	iop3xx_adap_set_slave_addr(iop3xx_adap);
	iop3xx_adap_enable(iop3xx_adap);
	
        return 0;
}

static int iop3xx_adap_send_target_slave_addr(struct i2c_algo_iop3xx_data *iop3xx_adap,
					      struct i2c_msg* msg)
{
	unsigned cr = *iop3xx_adap->biu->CR;
	int status;
	int rc;

	*iop3xx_adap->biu->DBR = iic_cook_addr(msg);
	
	cr &= ~(IOP321_ICR_MSTOP | IOP321_ICR_NACK);
	cr |= IOP321_ICR_MSTART | IOP321_ICR_TBYTE;

	*iop3xx_adap->biu->CR = cr;
	rc = iop3xx_adap_wait_tx_done(iop3xx_adap, &status);
	/* this assert fires every time, contrary to IOP manual	
	PASSERT((status&IOP321_ISR_UNITBUSY)!=0);
	*/
	PASSERT((status&IOP321_ISR_RXREAD)==0);
	     
	return rc;
}

static int iop3xx_adap_write_byte(struct i2c_algo_iop3xx_data *iop3xx_adap, char byte, int stop)
{
	unsigned cr = *iop3xx_adap->biu->CR;
	int status;
	int rc = 0;

	*iop3xx_adap->biu->DBR = byte;
	cr &= ~IOP321_ICR_MSTART;
	if (stop) {
		cr |= IOP321_ICR_MSTOP;
	} else {
		cr &= ~IOP321_ICR_MSTOP;
	}
	*iop3xx_adap->biu->CR = cr |= IOP321_ICR_TBYTE;
	rc = iop3xx_adap_wait_tx_done(iop3xx_adap, &status);

	return rc;
} 

static int iop3xx_adap_read_byte(struct i2c_algo_iop3xx_data *iop3xx_adap,
				 char* byte, int stop)
{
	unsigned cr = *iop3xx_adap->biu->CR;
	int status;
	int rc = 0;

	cr &= ~IOP321_ICR_MSTART;

	if (stop) {
		cr |= IOP321_ICR_MSTOP|IOP321_ICR_NACK;
	} else {
		cr &= ~(IOP321_ICR_MSTOP|IOP321_ICR_NACK);
	}
	*iop3xx_adap->biu->CR = cr |= IOP321_ICR_TBYTE;

	rc = iop3xx_adap_wait_rx_done(iop3xx_adap, &status);

	*byte = *iop3xx_adap->biu->DBR;

	return rc;
}

static int iop3xx_i2c_writebytes(struct i2c_adapter *i2c_adap, 
				 const char *buf, int count)
{
	struct i2c_algo_iop3xx_data *iop3xx_adap = i2c_adap->algo_data;
	int ii;
	int rc = 0;

	for (ii = 0; rc == 0 && ii != count; ++ii) {
		rc = iop3xx_adap_write_byte(iop3xx_adap, buf[ii], ii==count-1);
	}
	return rc;
}

static int iop3xx_i2c_readbytes(struct i2c_adapter *i2c_adap, 
				char *buf, int count)
{
	struct i2c_algo_iop3xx_data *iop3xx_adap = i2c_adap->algo_data;
	int ii;
	int rc = 0;

	for (ii = 0; rc == 0 && ii != count; ++ii) {
		rc = iop3xx_adap_read_byte(iop3xx_adap, &buf[ii], ii==count-1);
	}
	return rc;
}

/*
 * Description:  This function implements combined transactions.  Combined
 * transactions consist of combinations of reading and writing blocks of data.
 * FROM THE SAME ADDRESS
 * Each transfer (i.e. a read or a write) is separated by a repeated start
 * condition.
 */
static int iop3xx_handle_msg(struct i2c_adapter *i2c_adap, struct i2c_msg* pmsg) 
{
	struct i2c_algo_iop3xx_data *iop3xx_adap = i2c_adap->algo_data;
	int rc;

	rc = iop3xx_adap_send_target_slave_addr(iop3xx_adap, pmsg);
	if (rc < 0) {
		return rc;
	}

	if ((pmsg->flags&I2C_M_RD)) {
		return iop3xx_i2c_readbytes(i2c_adap, pmsg->buf, pmsg->len);
	} else {
		return iop3xx_i2c_writebytes(i2c_adap, pmsg->buf, pmsg->len);
	}
}

/*
 * master_xfer() - main read/write entry
 */
static int iop3xx_master_xfer(struct i2c_adapter *i2c_adap, struct i2c_msg msgs[], int num)
{
	struct i2c_algo_iop3xx_data *iop3xx_adap = i2c_adap->algo_data;
	int im = 0;
	int ret = 0;
	int status;

	iop3xx_adap_wait_idle(iop3xx_adap, &status);
	iop3xx_adap_reset(iop3xx_adap);
	iop3xx_adap_enable(iop3xx_adap);

	for (im = 0; ret == 0 && im != num; im++) {
		ret = iop3xx_handle_msg(i2c_adap, &msgs[im]);
	}

	iop3xx_adap_transaction_cleanup(iop3xx_adap);
	
	if(ret)
		return ret;

	return im;   
}

static int algo_control(struct i2c_adapter *adapter, unsigned int cmd,
			unsigned long arg)
{
	return 0;
}

static u32 iic_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}


/* -----exported algorithm data: -------------------------------------	*/

static struct i2c_algorithm iic_algo = {
	.name		= "IOP3xx I2C algorithm",
	.id		= I2C_ALGO_OCP_IOP3XX,
	.master_xfer	= iop3xx_master_xfer,
	.algo_control	= algo_control,
	.functionality	= iic_func,
};

/* 
 * registering functions to load algorithms at runtime 
 */
static int i2c_iop3xx_add_bus(struct i2c_adapter *iic_adap)
{
	struct i2c_algo_iop3xx_data *iop3xx_adap = iic_adap->algo_data;

	if (!request_region( REGION_START(iop3xx_adap), 
			      REGION_LENGTH(iop3xx_adap),
			      iic_adap->name)) {
		return -ENODEV;
	}

	init_waitqueue_head(&iop3xx_adap->waitq);
	spin_lock_init(&iop3xx_adap->lock);

	if (request_irq( 
		     iop3xx_adap->biu->irq,
		     iop3xx_i2c_handler,
		     /* SA_SAMPLE_RANDOM */ 0,
		     iic_adap->name,
		     iop3xx_adap)) {
		return -ENODEV;
	}			  

	/* register new iic_adapter to i2c module... */
	iic_adap->id |= iic_algo.id;
	iic_adap->algo = &iic_algo;

	iic_adap->timeout = 100;	/* default values, should */
	iic_adap->retries = 3;		/* be replaced by defines */

	iop3xx_adap_init(iic_adap->algo_data);
	i2c_add_adapter(iic_adap);
	return 0;
}

static int i2c_iop3xx_del_bus(struct i2c_adapter *iic_adap)
{
	struct i2c_algo_iop3xx_data *iop3xx_adap = iic_adap->algo_data;

	iop3xx_adap_final_cleanup(iop3xx_adap);
	free_irq(iop3xx_adap->biu->irq, iop3xx_adap);

	release_region(REGION_START(iop3xx_adap), REGION_LENGTH(iop3xx_adap));

	return i2c_del_adapter(iic_adap);
}

#ifdef CONFIG_ARCH_IOP321

static struct iop3xx_biu biu0 = {
	.CR	= IOP321_ICR0,
	.SR	= IOP321_ISR0,
	.SAR	= IOP321_ISAR0,
	.DBR	= IOP321_IDBR0,
	.BMR	= IOP321_IBMR0,
	.irq	= IRQ_IOP321_I2C_0,
};

static struct iop3xx_biu biu1 = {
	.CR	= IOP321_ICR1,
	.SR	= IOP321_ISR1,
	.SAR	= IOP321_ISAR1,
	.DBR	= IOP321_IDBR1,
	.BMR	= IOP321_IBMR1,
	.irq	= IRQ_IOP321_I2C_1,
};

#define ADAPTER_NAME_ROOT "IOP321 i2c biu adapter "
#else
#error Please define the BIU struct iop3xx_biu for your processor arch
#endif

static struct i2c_algo_iop3xx_data algo_iop3xx_data0 = {
	.channel		= 0,
	.biu			= &biu0,
	.timeout		= 1*HZ,
};
static struct i2c_algo_iop3xx_data algo_iop3xx_data1 = {
	.channel		= 1,
	.biu			= &biu1,
	.timeout		= 1*HZ,
};

static struct i2c_adapter iop3xx_ops0 = {
	.owner			= THIS_MODULE,
	.name			= ADAPTER_NAME_ROOT "0",
	.id			= I2C_HW_IOP321,
	.algo_data		= &algo_iop3xx_data0,
};
static struct i2c_adapter iop3xx_ops1 = {
	.owner			= THIS_MODULE,
	.name			= ADAPTER_NAME_ROOT "1",
	.id			= I2C_HW_IOP321,
	.algo_data		= &algo_iop3xx_data1,
};

static int __init i2c_iop3xx_init (void)
{
	return i2c_iop3xx_add_bus(&iop3xx_ops0) ||
		i2c_iop3xx_add_bus(&iop3xx_ops1);
}

static void __exit i2c_iop3xx_exit (void)
{
	i2c_iop3xx_del_bus(&iop3xx_ops0);
	i2c_iop3xx_del_bus(&iop3xx_ops1);
}

module_init (i2c_iop3xx_init);
module_exit (i2c_iop3xx_exit);

MODULE_AUTHOR("D-TACQ Solutions Ltd <www.d-tacq.com>");
MODULE_DESCRIPTION("IOP3xx iic algorithm and driver");
MODULE_LICENSE("GPL");
