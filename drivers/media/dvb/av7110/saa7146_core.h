#ifndef __SAA7146_CORE__
#define __SAA7146_CORE__

#include <asm/io.h>
#include <asm/semaphore.h>

#include "dvbdev.h"


/* maximum number of capture frames we support */
#define SAA7146_MAX_BUF		5
/* maximum number of extensions we support */
#define SAA7146_MAX_EXTENSIONS	4

/* stuff for writing to saa7146 */
#define saa7146_write(mem,adr,dat)    writel((dat),(mem+(adr)))
#define saa7146_read(mem,adr)         readl(mem+(adr))


#define DVB_CARD_TT_SIEMENS   0
#define DVB_CARD_TT_BUDGET    1
#define DVB_CARD_TT_BUDGET_CI 2
#define DVB_CARD_KNC1         3


/* this struct contains some constants needed for horizontal and vertical scaling. 
   currently we only support PAL (mode=0)and NTSC (mode=1). */

struct saa7146 {

        char			name[32];	/* give it a nice name */

	struct list_head	list_head;
	struct pci_dev		*device;
	int 			card_type;

	struct dvb_adapter	*dvb_adapter;
	struct dvb_i2c_bus	*i2c_bus;
	struct semaphore	i2c_sem;

	void*			  data[SAA7146_MAX_EXTENSIONS];	/* data hooks for extensions */

	int (*command) (struct dvb_i2c_bus *i, unsigned int cmd, void *arg);

	unsigned char*		mem;		/* pointer to mapped IO memory */
	int			revision;	/* chip revision; needed for bug-workarounds*/

	int			interlace;
	int			mode;
	
	u32*	i2c;				/* i2c memory */
	u32*	grabbing;			/* grabbing memory */
	u32*	clipping;			/* clipping memory for mask or rectangle clipping*/
	u32*	rps0;				/* memory for rps0-program */
	u32*	rps1;				/* memory for rps1-program */
	u32*	debi;				/* memory for debi-transfers */
	
	int	buffers;			/* number of grabbing-buffers */
		
	u32*	page_table[SAA7146_MAX_BUF];	/* page_tables for buffers*/
	int	frame_stat[SAA7146_MAX_BUF];	/* status of grabbing buffers */

	int grab_width[SAA7146_MAX_BUF];	/* pixel width of grabs */
	int grab_height[SAA7146_MAX_BUF];	/* pixel height of grabs */
	int grab_format[SAA7146_MAX_BUF];	/* video format of grabs */
	int grab_port[SAA7146_MAX_BUF];		/* video port for grab */

        wait_queue_head_t rps0_wq;     /* rps0 interrupt queue (=> capture) */
        wait_queue_head_t rps1_wq;     /* rps1 interrupt queue (=> i2c, ...) */
};

#define	SAA7146_IRQ_RPS0  
#define	SAA7146_IRQ_RPS1 

struct saa7146_extension {
	char	name[32];
	u32 	handles_irqs;

	void (*irq_handler)(struct saa7146*, u32, void*);

	int (*command)(struct saa7146*, void*, unsigned int cmd, void *arg);

	int (*attach)(struct saa7146*, void**);
	int (*detach)(struct saa7146*, void**);

	void (*inc_use)(struct saa7146*);
	void (*dec_use)(struct saa7146*);
};

extern int saa7146_add_extension(struct saa7146_extension* ext);
extern int saa7146_del_extension(struct saa7146_extension* ext);


/* external grabbing states */
#define GBUFFER_UNUSED         0x000
#define GBUFFER_GRABBING       0x001
#define GBUFFER_DONE           0x002

#define SAA7146_CORE_BASE	200

#define	SAA7146_DO_MMAP		_IOW('d', (SAA7146_CORE_BASE+11), struct vm_area_struct *)
#define SAA7146_SET_DD1		_IOW('d', (SAA7146_CORE_BASE+12), u32)
#define SAA7146_DUMP_REGISTERS	_IOW('d', (SAA7146_CORE_BASE+13), u32)
#define SAA7146_DEBI_TRANSFER	_IOW('d', (SAA7146_CORE_BASE+14), struct saa7146_debi_transfer)


#define SAA7146_SUSPEND	_IOW('d', (SAA7146_CORE_BASE+32), u32)
#define SAA7146_RESUME	_IOW('d', (SAA7146_CORE_BASE+33), u32)

#endif

