/********************************************************
* Header file for eata_pio.c Linux EATA-PIO SCSI driver *
* (c) 1993-96 Michael Neuffer  	                        *
*********************************************************
* last change: 2002/11/02				*
********************************************************/


#ifndef _EATA_PIO_H
#define _EATA_PIO_H

#include <linux/blk.h>
#include "scsi.h"
#include "hosts.h"
#include <scsi/scsicam.h>

#include "eata_generic.h"

#define VER_MAJOR 0
#define VER_MINOR 0
#define VER_SUB	  "1b"

/************************************************************************
 * Here you can switch parts of the code on and of			*
 ************************************************************************/

#define VERBOSE_SETUP		/* show startup screen of 2001 */
#define ALLOW_DMA_BOARDS 1

/************************************************************************
 * Debug options.							* 
 * Enable DEBUG and whichever options you require.			*
 ************************************************************************/
#define DEBUG_EATA	1	/* Enable debug code.                       */
#define DPT_DEBUG	0	/* Bobs special                             */
#define DBG_DELAY	0	/* Build in delays so debug messages can be
				 * be read before they vanish of the top of
				 * the screen!
				 */
#define DBG_PROBE	0	/* Debug probe routines.                    */
#define DBG_ISA		0	/* Trace ISA routines                       */
#define DBG_EISA	0	/* Trace EISA routines                      */
#define DBG_PCI		0	/* Trace PCI routines                       */
#define DBG_PIO		0	/* Trace get_config_PIO                     */
#define DBG_COM		0	/* Trace command call                       */
#define DBG_QUEUE	0	/* Trace command queueing.                  */
#define DBG_INTR	0	/* Trace interrupt service routine.         */
#define DBG_INTR2	0	/* Trace interrupt service routine.         */
#define DBG_PROC	0	/* Debug proc-fs related statistics         */
#define DBG_PROC_WRITE	0
#define DBG_REGISTER	0	/* */
#define DBG_ABNORM	1	/* Debug abnormal actions (reset, abort)    */

#if DEBUG_EATA
#define DBG(x, y)   if ((x)) {y;}
#else
#define DBG(x, y)
#endif

static int eata_pio_detect(Scsi_Host_Template *);
static int eata_pio_queue(Scsi_Cmnd *, void (*done) (Scsi_Cmnd *));
static int eata_pio_abort(Scsi_Cmnd *);
static int eata_pio_host_reset(Scsi_Cmnd *);
static int eata_pio_proc_info(char *, char **, off_t, int, int, int);
static int eata_pio_release(struct Scsi_Host *);

#define EATA_PIO {							\
	proc_info:         	eata_pio_proc_info, /* procinfo	  */	\
	name:              	"EATA (Extended Attachment) PIO driver",\
	detect:            	eata_pio_detect,			\
	release:           	eata_pio_release,			\
	queuecommand:      	eata_pio_queue,				\
	eh_abort_handler:  	eata_pio_abort,				\
	eh_host_reset_handler:	eata_pio_host_reset,			\
	use_clustering:    	ENABLE_CLUSTERING 			\
}

#endif				/* _EATA_PIO_H */
