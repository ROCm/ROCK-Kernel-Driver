#ifndef __BUDGET_DVB__
#define __BUDGET_DVB__

#include <media/saa7146.h>

#include "dvb_i2c.h"
#include "dvb_frontend.h"
#include "dvbdev.h"
#include "demux.h"
#include "dvb_demux.h"
#include "dmxdev.h"
#include "dvb_filter.h"
#include "dvb_net.h"

extern int budget_debug;

struct budget_info {
	char *name;
	int type;
};

/* place to store all the necessary device information */
struct budget {

        /* devices */
        struct dvb_device       dvb_dev;
        struct dvb_net               dvb_net;

        struct saa7146_dev	*dev;

	struct dvb_i2c_bus	*i2c_bus;	
	struct budget_info	*card;

	unsigned char		*grabbing;
	struct saa7146_pgtable	pt;

	struct tasklet_struct   fidb_tasklet;
	struct tasklet_struct   vpe_tasklet;

        struct dmxdev                dmxdev;
        struct dvb_demux	demux;

        struct dmx_frontend          hw_frontend;
        struct dmx_frontend          mem_frontend;

        int                     fe_synced; 
        struct semaphore        pid_mutex;

	int                     ci_present;
        int                     video_port;

        u8 tsf;
        u32 ttbp;
        int feeding;

	spinlock_t feedlock;

        struct dvb_adapter       *dvb_adapter;
	void			 *priv;
};



#define MAKE_BUDGET_INFO(x_var,x_name,x_type) \
static struct budget_info x_var ## _info = { \
	.name=x_name,	\
	.type=x_type };	\
static struct saa7146_pci_extension_data x_var = { \
	.ext_priv = &x_var ## _info, \
	.ext = &budget_extension };

#define TS_WIDTH  (376)
#define TS_HEIGHT (512)
#define TS_BUFLEN (TS_WIDTH*TS_HEIGHT)
#define TS_MAX_PACKETS (TS_BUFLEN/TS_SIZE)

#define BUDGET_TT		   0
#define BUDGET_TT_HW_DISEQC	   1
#define BUDGET_KNC1		   2
#define BUDGET_PATCH		   3
#define BUDGET_FS_ACTIVY	   4

#define BUDGET_VIDEO_PORTA         0
#define BUDGET_VIDEO_PORTB         1

extern int ttpci_budget_init (struct budget *budget,
			      struct saa7146_dev* dev,
			      struct saa7146_pci_extension_data *info);
extern int ttpci_budget_deinit (struct budget *budget);
extern void ttpci_budget_irq10_handler (struct saa7146_dev* dev, u32 *isr);
extern void ttpci_budget_set_video_port(struct saa7146_dev* dev, int video_port);

#endif

