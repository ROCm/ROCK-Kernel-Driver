/*  
 * Simple synchronous serial port driver for ETRAX 100LX.
 *
 * Synchronous serial ports are used for continous streamed data like audio.
 * The default setting for this driver is compatible with the STA 013 MP3
 * decoder. The driver can easily be tuned to fit other audio encoder/decoders
 * and SPI
 *
 * Copyright (c) 2001 Axis Communications AB
 * 
 * Author: Mikael Starvik 
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/svinto.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/sync_serial.h>

/* The receiver is a bit tricky beacuse of the continous stream of data. */
/*                                                                       */
/* Two DMA descriptors are linked together. Each DMA descriptor is       */
/* responsible for one half of a common buffer.                          */
/*                                                                       */
/* ------------------------------                                        */
/* |   ----------   ----------	|                                        */
/* --> | Descr1 |-->| Descr2 |---                                        */
/*     ----------   ----------                                           */
/*         |            |                                                */
/*         v            v                                                */
/*   -----------------------------                                       */
/*   |        BUFFER             |                                       */
/*   -----------------------------                                       */
/*      |             |                                                  */
/*    readp          writep                                              */
/*                                                                       */
/* If the application keeps up the pace readp will be right after writep.*/
/* If the application can't keep the pace we have to throw away data.    */ 
/* The idea is that readp should be ready with the data pointed out by	 */
/* Descr1 when the DMA has filled in Descr2. Otherwise we will discard	 */
/* the rest of the data pointed out by Descr1 and set readp to the start */
/* of Descr2                                                             */

#define SYNC_SERIAL_MAJOR 125

/* IN_BUFFER_SIZE should be a multiple of 6 to make sure that 24 bit */
/* words can be handled */

#define IN_BUFFER_SIZE 12288
#define OUT_BUFFER_SIZE 4096

#define DEFAULT_FRAME_RATE 0
#define DEFAULT_WORD_RATE 7

#define DEBUG(x) 

/* Define some macros to access ETRAX 100 registers */
#define SETF(var, reg, field, val) var = (var & ~IO_MASK(##reg##, field)) | \
					  IO_FIELD(##reg##, field, val)
#define SETS(var, reg, field, val) var = (var & ~IO_MASK(##reg##, field)) | \
					  IO_STATE(##reg##, field, val)

typedef struct sync_port
{
	/* Etrax registers and bits*/
	volatile unsigned * const status;
	volatile unsigned * const ctrl_data;
	volatile unsigned * const output_dma_first;
	volatile unsigned char * const output_dma_cmd;
	volatile unsigned char * const output_dma_clr_irq;
	volatile unsigned * const input_dma_first;
	volatile unsigned char * const input_dma_cmd;
	volatile unsigned char * const input_dma_clr_irq;
	volatile unsigned * const data_out;
	volatile unsigned * const data_in;
	char data_avail_bit; /* In R_IRQ_MASK1_RD */
	char transmitter_ready_bit; /* In R_IRQ_MASK1_RD */
	char ready_irq_bit; /* In R_IRQ_MASK1_SET and R_IRQ_MASK1_CLR */
	char input_dma_descr_bit; /* In R_IRQ_MASK2_RD */
	char output_dma_bit; /* In R_IRQ_MASK2_RD */

	int enabled;  /* 1 if port is enabled */
	int use_dma;  /* 1 if port uses dma */
	int port_nbr; /* Port 0 or 1 */
	unsigned ctrl_data_shadow; /* Register shadow */
	char busy; /* 1 if port is busy */
	wait_queue_head_t out_wait_q;
	wait_queue_head_t in_wait_q;
	struct etrax_dma_descr out_descr;
	struct etrax_dma_descr in_descr1;
	struct etrax_dma_descr in_descr2;
	char out_buffer[OUT_BUFFER_SIZE];
	int out_count; /* Remaining bytes for current transfer */
	char* outp; /* Current position in out_buffer */
	char in_buffer[IN_BUFFER_SIZE];
	volatile char* readp;  /* Next byte to be read by application */
	volatile char* writep; /* Next byte to be written by etrax */
	int odd_output; /* 1 if writing odd nible in 12 bit mode */
	int odd_input;  /* 1 if reading odd nible in 12 bit mode */
} sync_port;


static int etrax_sync_serial_init(void);
static void initialize_port(int portnbr);
static int sync_serial_open(struct inode *, struct file*);
static int sync_serial_release(struct inode*, struct file*);
static int sync_serial_ioctl(struct inode*, struct file*,
			     unsigned int cmd, unsigned long arg);
static ssize_t sync_serial_write(struct file * file, const char * buf, 
				 size_t count, loff_t *ppos);
static ssize_t sync_serial_manual_write(struct file * file, const char * buf, 
					size_t count, loff_t *ppos);
static ssize_t sync_serial_read(struct file *file, char *buf, 
				size_t count, loff_t *ppos);
static void send_word(sync_port* port);
static void start_dma(struct sync_port *port, const char* data, int count);
static void start_dma_in(sync_port* port);
static void tr_interrupt(int irq, void *dev_id, struct pt_regs * regs);
static void rx_interrupt(int irq, void *dev_id, struct pt_regs * regs);
static void manual_interrupt(int irq, void *dev_id, struct pt_regs * regs);

/* The ports */
static struct sync_port ports[]=
{
	{
		R_SYNC_SERIAL1_STATUS,  /* status */
		R_SYNC_SERIAL1_CTRL,    /* ctrl_data */
		R_DMA_CH8_FIRST,        /* output_dma_first */
		R_DMA_CH8_CMD,          /* output_dma_cmd */
		R_DMA_CH8_CLR_INTR,     /* output_dma_clr_irq */
		R_DMA_CH9_FIRST,        /* input_dma_first */
		R_DMA_CH9_CMD,          /* input_dma_cmd */
		R_DMA_CH9_CLR_INTR,     /* input_dma_clr_irq */
		R_SYNC_SERIAL1_TR_DATA, /* data_out */
		R_SYNC_SERIAL1_REC_DATA,/* data in */
		IO_BITNR(R_IRQ_MASK1_RD, ser1_data),   /* data_avail_bit */
		IO_BITNR(R_IRQ_MASK1_RD, ser1_ready),  /* transmitter_ready_bit */
		IO_BITNR(R_IRQ_MASK1_SET, ser1_ready), /* ready_irq_bit */
		IO_BITNR(R_IRQ_MASK2_RD, dma9_descr),  /* input_dma_descr_bit */
		IO_BITNR(R_IRQ_MASK2_RD, dma8_eop),    /* output_dma_bit */
	},
	{
		R_SYNC_SERIAL3_STATUS,  /* status */
		R_SYNC_SERIAL3_CTRL,    /* ctrl_data */
		R_DMA_CH4_FIRST,        /* output_dma_first */
		R_DMA_CH4_CMD,          /* output_dma_cmd */
		R_DMA_CH4_CLR_INTR,     /* output_dma_clr_irq */
		R_DMA_CH5_FIRST,        /* input_dma_first */
		R_DMA_CH5_CMD,          /* input_dma_cmd */
		R_DMA_CH5_CLR_INTR,     /* input_dma_clr_irq */
		R_SYNC_SERIAL3_TR_DATA, /* data_out */
		R_SYNC_SERIAL3_REC_DATA,/* data in */
		IO_BITNR(R_IRQ_MASK1_RD, ser3_data),   /* data_avail_bit */
		IO_BITNR(R_IRQ_MASK1_RD, ser3_ready),  /* transmitter_ready_bit */
		IO_BITNR(R_IRQ_MASK1_SET, ser3_ready), /* ready_irq_bit */
		IO_BITNR(R_IRQ_MASK2_RD, dma5_descr),  /* input_dma_descr_bit */
		IO_BITNR(R_IRQ_MASK2_RD, dma4_eop),    /* output_dma_bit */
	}
};

/* Register shadows */
static unsigned sync_serial_prescale_shadow = 0;
static unsigned gen_config_ii_shadow = 0;

#define NUMBER_OF_PORTS (sizeof(ports)/sizeof(sync_port))

static struct file_operations sync_serial_fops = {
       owner:	THIS_MODULE,
       write:	sync_serial_write,
       read:	sync_serial_read,
       ioctl:	sync_serial_ioctl,
       open:	sync_serial_open,
       release: sync_serial_release
};

static int __init etrax_sync_serial_init(void)
{
	ports[0].enabled = 0;
	ports[1].enabled = 0;

	if (register_chrdev(SYNC_SERIAL_MAJOR,"sync serial", &sync_serial_fops) <0 ) 
	{
		printk("unable to get major for synchronous serial port\n");
		return -EBUSY;
	}

	/* Deselect synchronous serial ports */
	SETS(gen_config_ii_shadow, R_GEN_CONFIG_II, sermode1, async);
	SETS(gen_config_ii_shadow, R_GEN_CONFIG_II, sermode3, async);
	SETS(gen_config_ii_shadow, R_GEN_CONFIG_II, ser3, select);
	*R_GEN_CONFIG_II = gen_config_ii_shadow;
  
	/* Initialize Ports */
#if defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL_PORT0)
	ports[0].enabled = 1;
	SETS(port_pb_i2c_shadow, R_PORT_PB_I2C, syncser1, ss1extra); 
	SETS(gen_config_ii_shadow, R_GEN_CONFIG_II, sermode1, sync);
#if defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL0_DMA)
	ports[0].use_dma = 1;
	initialize_port(0);
	if(request_irq(24, tr_interrupt, 0, "synchronous serial 1 dma tr", &ports[0]))
		 panic("Can't allocate sync serial port 1 IRQ");
	if(request_irq(25, rx_interrupt, 0, "synchronous serial 1 dma rx", &ports[0]))
		panic("Can't allocate sync serial port 1 IRQ");
	RESET_DMA(8); WAIT_DMA(8);
	RESET_DMA(9); WAIT_DMA(9);
	*R_DMA_CH8_CLR_INTR = IO_STATE(R_DMA_CH8_CLR_INTR, clr_eop, do) |
	  IO_STATE(R_DMA_CH8_CLR_INTR, clr_descr, do); 
	*R_DMA_CH9_CLR_INTR = IO_STATE(R_DMA_CH9_CLR_INTR, clr_eop, do) |
	  IO_STATE(R_DMA_CH9_CLR_INTR, clr_descr, do); 
	*R_IRQ_MASK2_SET =
	  IO_STATE(R_IRQ_MASK2_SET, dma8_eop, set) |
	  IO_STATE(R_IRQ_MASK2_SET, dma8_descr, set) |
          IO_STATE(R_IRQ_MASK2_SET, dma9_descr, set);
	start_dma_in(&ports[0]);
#else
	ports[0].use_dma = 0;
	initialize_port(0);
	if (request_irq(8, manual_interrupt, SA_SHIRQ, "synchronous serial manual irq", &ports[0]))
		panic("Can't allocate sync serial manual irq");
	*R_IRQ_MASK1_SET = IO_STATE(R_IRQ_MASK1_SET, ser1_data, set);	 
#endif
#endif

#if defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL_PORT1)
	ports[1].enabled = 1;
	SETS(port_pb_i2c_shadow, R_PORT_PB_I2C, syncser3, ss3extra);
	SETS(gen_config_ii_shadow, R_GEN_CONFIG_II, sermode3, sync);
#if defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL1_DMA)
	ports[1].use_dma = 1;
	initialize_port(1);
	if(request_irq(20, tr_interrupt, 0, "synchronous serial 3 dma tr", &ports[1]))
		panic("Can't allocate sync serial port 1 IRQ");
	if(request_irq(21, rx_interrupt, 0, "synchronous serial 3 dma rx", &ports[1]))
		panic("Can't allocate sync serial port 1 IRQ");
	RESET_DMA(4); WAIT_DMA(4);
	RESET_DMA(5); WAIT_DMA(5);
	*R_DMA_CH4_CLR_INTR = IO_STATE(R_DMA_CH4_CLR_INTR, clr_eop, do) |
	  IO_STATE(R_DMA_CH4_CLR_INTR, clr_descr, do); 
	*R_DMA_CH5_CLR_INTR = IO_STATE(R_DMA_CH5_CLR_INTR, clr_eop, do) |
	  IO_STATE(R_DMA_CH5_CLR_INTR, clr_descr, do); 
	*R_IRQ_MASK2_SET =
	  IO_STATE(R_IRQ_MASK2_SET, dma4_eop, set) |
	  IO_STATE(R_IRQ_MASK2_SET, dma4_descr, set) |
	  IO_STATE(R_IRQ_MASK2_SET, dma5_descr, set);
	start_dma_in(&ports[1]);
#else
	ports[1].use_dma = 0;	
	initialize_port(1);
	if (port[0].use_dma) /* Port 0 uses dma, we must manual allocate IRQ */
	{
		if (request_irq(8, manual_interrupt, SA_SHIRQ, "synchronous serial manual irq", &ports[1]))
			panic("Can't allocate sync serial manual irq");
	}
	*R_IRQ_MASK1_SET = IO_STATE(R_IRQ_MASK1_SET, ser3_data, set);	 
#endif
#endif

	*R_PORT_PB_I2C = port_pb_i2c_shadow; /* Use PB4/PB7 */

	/* Set up timing */
	*R_SYNC_SERIAL_PRESCALE = sync_serial_prescale_shadow = (
	  IO_STATE(R_SYNC_SERIAL_PRESCALE, clk_sel_u1, codec) | 
	  IO_STATE(R_SYNC_SERIAL_PRESCALE, word_stb_sel_u1, external) | 
	  IO_STATE(R_SYNC_SERIAL_PRESCALE, clk_sel_u3, codec) | 
	  IO_STATE(R_SYNC_SERIAL_PRESCALE, word_stb_sel_u3, external) | 
	  IO_STATE(R_SYNC_SERIAL_PRESCALE, prescaler, div4) | 
	  IO_FIELD(R_SYNC_SERIAL_PRESCALE, frame_rate, DEFAULT_FRAME_RATE) | 
	  IO_FIELD(R_SYNC_SERIAL_PRESCALE, word_rate, DEFAULT_WORD_RATE) | 
	  IO_STATE(R_SYNC_SERIAL_PRESCALE, warp_mode, normal));

	/* Select synchronous ports */
	*R_GEN_CONFIG_II = gen_config_ii_shadow;

	printk("ETRAX 100LX synchronous serial port driver\n");
	return 0;
}

static void initialize_port(int portnbr)
{
	struct sync_port* port = &ports[portnbr];

	DEBUG(printk("Init sync serial port %d\n", portnbr));
    
	port->port_nbr = portnbr;	
	port->busy = 0;		
	port->readp = port->in_buffer;
	port->writep = port->in_buffer + IN_BUFFER_SIZE/2;
	port->odd_input = 0;

	init_waitqueue_head(&port->out_wait_q);
	init_waitqueue_head(&port->in_wait_q);

	port->ctrl_data_shadow =
	  IO_STATE(R_SYNC_SERIAL1_CTRL, tr_baud, c115k2Hz)   | 
	  IO_STATE(R_SYNC_SERIAL1_CTRL, mode, master_output) | 
	  IO_STATE(R_SYNC_SERIAL1_CTRL, error, ignore)       |
	  IO_STATE(R_SYNC_SERIAL1_CTRL, rec_enable, disable) |
	  IO_STATE(R_SYNC_SERIAL1_CTRL, f_synctype, normal)  |
	  IO_STATE(R_SYNC_SERIAL1_CTRL, f_syncsize, word)    |
	  IO_STATE(R_SYNC_SERIAL1_CTRL, f_sync, on)	     |
	  IO_STATE(R_SYNC_SERIAL1_CTRL, clk_mode, normal)    |
	  IO_STATE(R_SYNC_SERIAL1_CTRL, clk_halt, stopped)   |
	  IO_STATE(R_SYNC_SERIAL1_CTRL, bitorder, msb)	     |
	  IO_STATE(R_SYNC_SERIAL1_CTRL, tr_enable, disable)  |
	  IO_STATE(R_SYNC_SERIAL1_CTRL, wordsize, size8bit)  |
	  IO_STATE(R_SYNC_SERIAL1_CTRL, buf_empty, lmt_8)    |
	  IO_STATE(R_SYNC_SERIAL1_CTRL, buf_full, lmt_8)     |
	  IO_STATE(R_SYNC_SERIAL1_CTRL, flow_ctrl, enabled)  |
	  IO_STATE(R_SYNC_SERIAL1_CTRL, clk_polarity, neg)   |
	  IO_STATE(R_SYNC_SERIAL1_CTRL, frame_polarity, normal)|
	  IO_STATE(R_SYNC_SERIAL1_CTRL, status_polarity, inverted)|
	  IO_STATE(R_SYNC_SERIAL1_CTRL, clk_driver, normal)   |
	  IO_STATE(R_SYNC_SERIAL1_CTRL, frame_driver, normal) |
	  IO_STATE(R_SYNC_SERIAL1_CTRL, status_driver, normal)|
	  IO_STATE(R_SYNC_SERIAL1_CTRL, def_out0, high);
  
	if (port->use_dma)
		port->ctrl_data_shadow |= IO_STATE(R_SYNC_SERIAL1_CTRL, dma_enable, on);
	else
		port->ctrl_data_shadow |= IO_STATE(R_SYNC_SERIAL1_CTRL, dma_enable, off);
  
	*port->ctrl_data = port->ctrl_data_shadow;
}

static int sync_serial_open(struct inode *inode, struct file *file)
{
	int dev = MINOR(inode->i_rdev);
	DEBUG(printk("Open sync serial port %d\n", dev)); 
  
	if (dev < 0 || dev >= NUMBER_OF_PORTS || !ports[dev].enabled)
	{
		DEBUG(printk("Invalid minor %d\n", dev));
		return -ENODEV;
	}
	if (ports[dev].busy)
	{
		DEBUG(printk("Device is busy.. \n"));
		return -EBUSY;
	}
	ports[dev].busy = 1;
	return 0;
}

static int sync_serial_release(struct inode *inode, struct file *file)
{
	int dev = MINOR(inode->i_rdev);
	if (dev < 0 || dev >= NUMBER_OF_PORTS || !ports[dev].enabled)
	{
		DEBUG(printk("Invalid minor %d\n", dev));
		return -ENODEV;
	}
	ports[dev].busy = 0;
	return 0;
}

static int sync_serial_ioctl(struct inode *inode, struct file *file,
		  unsigned int cmd, unsigned long arg)
{
	int return_val = 0;
	int dev = MINOR(file->f_dentry->d_inode->i_rdev);
  sync_port* port;
        
	if (dev < 0 || dev >= NUMBER_OF_PORTS || !ports[dev].enabled)
	{
		DEBUG(printk("Invalid minor %d\n", dev));
		return -1;
	}
        port = &ports[dev];

	/* Disable port while changing config */
	if (dev)
	{
		RESET_DMA(4); WAIT_DMA(4);
		*R_DMA_CH4_CLR_INTR = IO_STATE(R_DMA_CH4_CLR_INTR, clr_eop, do) |
                  IO_STATE(R_DMA_CH4_CLR_INTR, clr_descr, do); 
		SETS(gen_config_ii_shadow, R_GEN_CONFIG_II, sermode3, async);
	}
	else
	{
		RESET_DMA(8); WAIT_DMA(8);
		*R_DMA_CH8_CLR_INTR = IO_STATE(R_DMA_CH8_CLR_INTR, clr_eop, do) |
                  IO_STATE(R_DMA_CH8_CLR_INTR, clr_descr, do);  
		SETS(gen_config_ii_shadow, R_GEN_CONFIG_II, sermode1, async);
	}
	*R_GEN_CONFIG_II = gen_config_ii_shadow;

	switch(cmd)
	{
		case SSP_SPEED:
			if (GET_SPEED(arg) == CODEC)
			{
				if (dev)
					SETS(sync_serial_prescale_shadow, R_SYNC_SERIAL_PRESCALE, clk_sel_u3, codec);
				else
					SETS(sync_serial_prescale_shadow, R_SYNC_SERIAL_PRESCALE, clk_sel_u1, codec);

				SETF(sync_serial_prescale_shadow, R_SYNC_SERIAL_PRESCALE, prescaler, GET_FREQ(arg));
				SETF(sync_serial_prescale_shadow, R_SYNC_SERIAL_PRESCALE, frame_rate, GET_FRAME_RATE(arg));
				SETF(sync_serial_prescale_shadow, R_SYNC_SERIAL_PRESCALE, word_rate, GET_WORD_RATE(arg));
			}
			else
			{
				SETF(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, tr_baud, GET_SPEED(arg));
				if (dev)
					SETS(sync_serial_prescale_shadow, R_SYNC_SERIAL_PRESCALE, clk_sel_u3, baudrate);
				else
					SETS(sync_serial_prescale_shadow, R_SYNC_SERIAL_PRESCALE, clk_sel_u1, baudrate);
			}
			break;
		case SSP_MODE:
			if (arg > 5)
				return -EINVAL;
			SETF(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, mode, arg);
			break;
		case SSP_FRAME_SYNC:
			if (arg & NORMAL_SYNC)
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, f_synctype, normal);
			else if (arg & EARLY_SYNC)
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, f_synctype, early);
    
			if (arg & BIT_SYNC)
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, f_syncsize, bit);
			else if (arg & WORD_SYNC)
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, f_syncsize, word);
			else if (arg & EXTENDED_SYNC)
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, f_syncsize, extended);
    
			if (arg & SYNC_ON)
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, f_sync, on);
			else if (arg & SYNC_OFF)
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, f_sync, off);
    
			if (arg & WORD_SIZE_8)
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, wordsize, size8bit);
			else if (arg & WORD_SIZE_12)
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, wordsize, size12bit);
			else if (arg & WORD_SIZE_16)
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, wordsize, size16bit);
			else if (arg & WORD_SIZE_24)
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, wordsize, size24bit);
			else if (arg & WORD_SIZE_32)
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, wordsize, size32bit);
    
			if (arg & BIT_ORDER_MSB)
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, bitorder, msb);
			else if (arg & BIT_ORDER_LSB)
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, bitorder, lsb);

			if (arg & FLOW_CONTROL_ENABLE)
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, flow_ctrl, enabled);
			else if (arg & FLOW_CONTROL_DISABLE)
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, flow_ctrl, disabled);

			if (arg & CLOCK_NOT_GATED)
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, clk_mode, normal);
			else if (arg & CLOCK_GATED)
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, clk_mode, gated);
    
			break;
		case SSP_IPOLARITY:
			if (arg & CLOCK_NORMAL)
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, clk_polarity, neg);
			else if (arg & CLOCK_INVERT)
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, clk_polarity, pos);

			if (arg & FRAME_NORMAL)
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, frame_polarity, normal);
			else if (arg & FRAME_INVERT)
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, frame_polarity, inverted);

			if (arg & STATUS_NORMAL)
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, status_polarity, normal);
			else if (arg & STATUS_INVERT)
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, status_polarity, inverted);
			break;
		case SSP_OPOLARITY:
			if (arg & CLOCK_NORMAL)
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, clk_driver, normal);
			else if (arg & CLOCK_INVERT)
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, clk_driver, inverted);
    
			if (arg & FRAME_NORMAL)
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, frame_driver, normal);
			else if (arg & FRAME_INVERT)
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, frame_driver, inverted);

			if (arg & STATUS_NORMAL)
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, status_driver, normal);
			else if (arg & STATUS_INVERT)
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, status_driver, inverted);
			break;
		case SSP_SPI:
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, flow_ctrl, disabled);
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, bitorder, msb);
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, wordsize, size8bit);
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, f_sync, on);
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, f_syncsize, word);
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, f_synctype, normal);
			if (arg & SPI_SLAVE)
			{
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, frame_polarity, inverted);
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, clk_polarity, neg);
				SETF(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, mode, SLAVE_INPUT);
			}
			else
			{
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, frame_driver, inverted);
				SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, clk_driver, inverted);
				SETF(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, mode, MASTER_OUTPUT);
			}
			break;
		default:
			return_val = -1;
	}
	/* Set config and enable port */
	*port->ctrl_data = port->ctrl_data_shadow;
	*R_SYNC_SERIAL_PRESCALE = sync_serial_prescale_shadow;
	if (dev)
		SETS(gen_config_ii_shadow, R_GEN_CONFIG_II, sermode3, sync);
	else
		SETS(gen_config_ii_shadow, R_GEN_CONFIG_II, sermode1, sync);

	*R_GEN_CONFIG_II = gen_config_ii_shadow;
	return return_val;
}

static ssize_t sync_serial_manual_write(struct file * file, const char * buf, 
                                        size_t count, loff_t *ppos)
{
	int dev = MINOR(file->f_dentry->d_inode->i_rdev);
	DECLARE_WAITQUEUE(wait, current);
	sync_port* port;

	if (dev < 0 || dev >= NUMBER_OF_PORTS || !ports[dev].enabled)
	{
		DEBUG(printk("Invalid minor %d\n", dev));
		return -ENODEV;
	}

	port = &ports[dev];
	copy_from_user(port->out_buffer, buf, count);
	port->outp = port->out_buffer;
	port->out_count = count;
	port->odd_output = 1;
	add_wait_queue(&port->out_wait_q, &wait);
	set_current_state(TASK_INTERRUPTIBLE);
	*R_IRQ_MASK1_SET = 1 << port->ready_irq_bit; /* transmitter ready IRQ on */
	send_word(port); /* Start sender by sending first word */
	schedule();
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&port->out_wait_q, &wait);
	if (signal_pending(current))
	{
		return -EINTR;
	}
	return count;
}

static ssize_t sync_serial_write(struct file * file, const char * buf, 
                                 size_t count, loff_t *ppos)
{
	int dev = MINOR(file->f_dentry->d_inode->i_rdev);
	DECLARE_WAITQUEUE(wait, current);	
	sync_port *port;

	if (dev < 0 || dev >= NUMBER_OF_PORTS || !ports[dev].enabled)
	{
		DEBUG(printk("Invalid minor %d\n", dev));
		return -ENODEV;
	}
	port = &ports[dev];

	DEBUG(printk("Write dev %d count %d\n", port->port_nbr, count));

	count = count > OUT_BUFFER_SIZE ? OUT_BUFFER_SIZE : count;

	/* Make sure transmitter is running */
	SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, clk_halt, running);
	SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, tr_enable, enable);
	*port->ctrl_data = port->ctrl_data_shadow;

	if (!port->use_dma)
	{
		return sync_serial_manual_write(file, buf, count, ppos); 
	}
  
	copy_from_user(port->out_buffer, buf, count);
	add_wait_queue(&port->out_wait_q, &wait);
	set_current_state(TASK_INTERRUPTIBLE);
	start_dma(port, buf, count);
	schedule();
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&port->out_wait_q, &wait);
	if (signal_pending(current))
	{
		return -EINTR;
	}
	return count;
}

static ssize_t sync_serial_read(struct file * file, char * buf, 
				size_t count, loff_t *ppos)
{
	int dev = MINOR(file->f_dentry->d_inode->i_rdev);
	int avail;
	sync_port *port;
	char* start; 
	char* end;
	unsigned long flags;	

	if (dev < 0 || dev >= NUMBER_OF_PORTS || !ports[dev].enabled)
	{
		DEBUG(printk("Invalid minor %d\n", dev));
		return -ENODEV;
	}
	port = &ports[dev];

	DEBUG(printk("Read dev %d count %d\n", dev, count));

	/* Make sure receiver is running */
	SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, clk_halt, running);
	SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, rec_enable, enable);
	*port->ctrl_data = port->ctrl_data_shadow;

	/* Calculate number of available bytes */
	while (port->readp == port->writep) /* No data */
	{
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		interruptible_sleep_on(&port->in_wait_q);
		if (signal_pending(current))
		{
			return -EINTR;
		}
	}
	
	/* Save pointers to avoid that they are modified by interrupt */
	start = port->readp;
	end = port->writep;

	/* Lazy read, never return wrapped data. */
	if (end > start)
		avail = end - start;
	else 
		avail = port->in_buffer + IN_BUFFER_SIZE - start;
  
	count = count > avail ? avail : count;
	copy_to_user(buf, start, count);

	/* Disable interrupts while updating readp */
	save_flags(flags);
	cli();	
	port->readp += count;
	if (port->readp == port->in_buffer + IN_BUFFER_SIZE) /* Wrap? */
		port->readp = port->in_buffer;
	restore_flags(flags);

	DEBUG(printk("%d bytes read\n", count));
	return count;
}

static void send_word(sync_port* port)
{
	switch(port->ctrl_data_shadow & IO_MASK(R_SYNC_SERIAL1_CTRL, wordsize))
	{
		case IO_STATE(R_SYNC_SERIAL1_CTRL, wordsize, size8bit):
			port->out_count--;
			*port->data_out = *port->outp++;
			break;
		case IO_STATE(R_SYNC_SERIAL1_CTRL, wordsize, size12bit):
			port->out_count--;
			if (port->odd_output)
				*port->data_out = ((*port->outp) << 16) | (*(unsigned short *)(port->outp + 1));
			else
				*port->data_out = ((*(unsigned short *)port->outp) << 8) | (*(port->outp + 1));
			port->odd_output = !port->odd_output;
			port->outp++;
			break;
		case IO_STATE(R_SYNC_SERIAL1_CTRL, wordsize, size16bit):
			port->out_count-=2;
			*port->data_out = *(unsigned short *)port->outp;
			port->outp+=2;
			break;
		case IO_STATE(R_SYNC_SERIAL1_CTRL, wordsize, size24bit):
			port->out_count-=3;
			*port->data_out = *(unsigned int *)port->outp;
			port->outp+=3;
			break;
		case IO_STATE(R_SYNC_SERIAL1_CTRL, wordsize, size32bit):
			port->out_count-=4;
			*port->data_out = *(unsigned int *)port->outp;
			port->outp+=4;
			break;
	}
}

static void start_dma(struct sync_port* port, const char* data, int count)
{
	port->out_descr.hw_len = 0;
	port->out_descr.next = 0;
	port->out_descr.ctrl = d_int | d_eol | d_eop | d_wait;
	port->out_descr.sw_len = count;
	port->out_descr.buf = virt_to_phys(port->out_buffer);
	port->out_descr.status = 0;

	*port->output_dma_first = virt_to_phys(&port->out_descr);
	*port->output_dma_cmd = IO_STATE(R_DMA_CH0_CMD, cmd, start);
}

static void start_dma_in(sync_port* port)
{
	if (port->writep > port->in_buffer + IN_BUFFER_SIZE)
	{
		panic("Offset too large in sync serial driver\n");
		return;
	}
	port->in_descr1.hw_len = 0;
	port->in_descr1.ctrl = d_int;
	port->in_descr1.status = 0;
	port->in_descr1.next = virt_to_phys(&port->in_descr2);
	port->in_descr2.hw_len = 0;
	port->in_descr2.next = virt_to_phys(&port->in_descr1);
	port->in_descr2.ctrl = d_int;
	port->in_descr2.status = 0;

	/* Find out which descriptor to start */
	if (port->writep >= port->in_buffer + IN_BUFFER_SIZE/2)
	{
		/* Start descriptor 2 */
		port->in_descr1.sw_len = IN_BUFFER_SIZE/2; /* All data available in 1 */
		port->in_descr1.buf = virt_to_phys(port->in_buffer);
		port->in_descr2.sw_len = port->in_buffer + IN_BUFFER_SIZE - port->writep;
		port->in_descr2.buf = virt_to_phys(port->writep);
		*port->input_dma_first = virt_to_phys(&port->in_descr2);
	}
	else
	{
		/* Start descriptor 1 */
		port->in_descr1.sw_len = port->in_buffer + IN_BUFFER_SIZE/2 - port->writep;
		port->in_descr1.buf = virt_to_phys(port->writep);
		port->in_descr2.sw_len = IN_BUFFER_SIZE/2;
		port->in_descr2.buf = virt_to_phys(port->in_buffer + IN_BUFFER_SIZE / 2);
		*port->input_dma_first = virt_to_phys(&port->in_descr1);
	}
	*port->input_dma_cmd = IO_STATE(R_DMA_CH0_CMD, cmd, start);
}

static void tr_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
	unsigned long ireg = *R_IRQ_MASK2_RD;
	int i;

	for (i = 0; i < NUMBER_OF_PORTS; i++) 
	{
		sync_port *port = &ports[i];
		if (ireg & (1 << port->output_dma_bit)) /* IRQ active for the port? */
		{
			/* Clear IRQ */
			*port->output_dma_clr_irq = 
			  IO_STATE(R_DMA_CH0_CLR_INTR, clr_eop, do) |
			  IO_STATE(R_DMA_CH0_CLR_INTR, clr_descr, do);
			wake_up_interruptible(&port->out_wait_q); /* wake up the waiting process */
		} 
	}
}

static void rx_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
	unsigned long ireg = *R_IRQ_MASK2_RD;
	int i;	

	for (i = 0; i < NUMBER_OF_PORTS; i++) 
	{
		int update = 0;
		sync_port *port = &ports[i];

		if (!port->enabled)
		{
			continue;
		}    

		if (ireg & (1 << port->input_dma_descr_bit)) /* Descriptor interrupt */
		{
			/* DMA has reached end of descriptor */
			*port->input_dma_clr_irq = 
			  IO_STATE(R_DMA_CH0_CLR_INTR, clr_eop, do) | 
			  IO_STATE(R_DMA_CH0_CLR_INTR, clr_descr, do);

			/* Find out which descriptor that is ready */
			if (port->writep >= port->in_buffer + IN_BUFFER_SIZE/2) 
			{
				/* Descr 2 was ready. Restart DMA at descriptor 1 */
				port->writep = port->in_buffer;
	
				/* Throw away data? */
				if (port->readp < port->in_buffer + IN_BUFFER_SIZE/2)
					port->readp = port->in_buffer + IN_BUFFER_SIZE/2;
			}
			else
			{
				/* Descr 1 was ready. Restart DMA at descriptor 2 */
				port->writep = port->in_buffer + IN_BUFFER_SIZE/2;

				/* Throw away data? */
				if (port->readp >= port->in_buffer + IN_BUFFER_SIZE/2)
					port->readp = port->in_buffer;
			}
			start_dma_in(port);
			wake_up_interruptible(&port->in_wait_q); /* wake up the waiting process */
		}
	}
}

static void manual_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
	int i;

	for (i = 0; i < NUMBER_OF_PORTS; i++)
	{
		sync_port* port = &ports[i];

		if (!port->enabled)
		{
			continue;
		}

		if (*R_IRQ_MASK1_RD & (1 << port->data_avail_bit))	/* Data received? */
		{
			/* Read data */
			switch(port->ctrl_data_shadow & IO_MASK(R_SYNC_SERIAL1_CTRL, wordsize))
			{
				case IO_STATE(R_SYNC_SERIAL1_CTRL, wordsize, size8bit):
					*port->writep++ = *(volatile char *)port->data_in;
					break;
				case IO_STATE(R_SYNC_SERIAL1_CTRL, wordsize, size12bit):
				{
					int data = *(unsigned short *)port->data_in;
					if (port->odd_input)
					{
						*port->writep |= (data & 0x0f00) >> 8;
						*(port->writep + 1) = data & 0xff;
					}
					else
					{
						*port->writep = (data & 0x0ff0) >> 4;
						*(port->writep + 1) = (data & 0x0f) << 4;
					}
					port->odd_input = !port->odd_input;
					port->writep+=1;
				}
				break;
				case IO_STATE(R_SYNC_SERIAL1_CTRL, wordsize, size16bit):
					*(unsigned short*)port->writep = *(volatile unsigned short *)port->data_in;
					port->writep+=2;
					break;
				case IO_STATE(R_SYNC_SERIAL1_CTRL, wordsize, size24bit):
					*(unsigned int*)port->writep = *port->data_in;
					port->writep+=3;
					break;
				case IO_STATE(R_SYNC_SERIAL1_CTRL, wordsize, size32bit):
					*(unsigned int*)port->writep = *port->data_in;
					port->writep+=4;
					break;
			}

			if (port->writep > port->in_buffer + IN_BUFFER_SIZE) /* Wrap? */
				port->writep = port->in_buffer;
			wake_up_interruptible(&port->in_wait_q); /* Wake up application */
		}

		if (*R_IRQ_MASK1_RD & (1 << port->transmitter_ready_bit)) /* Transmitter ready? */
		{
			if (port->out_count) /* More data to send */
				send_word(port);
			else /* transmission finished */
			{
				*R_IRQ_MASK1_CLR = 1 << port->ready_irq_bit; /* Turn off IRQ */
				wake_up_interruptible(&port->out_wait_q); /* Wake up application */
			}
		}
	}
}

module_init(etrax_sync_serial_init);
