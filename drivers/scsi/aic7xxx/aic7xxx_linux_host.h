/*
 * Adaptec AIC7xxx device driver host template for Linux.
 *
 * Copyright (c) 2000 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: //depot/src/linux/drivers/scsi/aic7xxx/aic7xxx_linux_host.h#2 $
 */

#ifndef _AIC7XXX_LINUX_HOST_H_
#define _AIC7XXX_LINUX_HOST_H_

int		 aic7xxx_proc_info(char *, char **, off_t, int, int, int);
int		 aic7xxx_queue(Scsi_Cmnd *, void (*)(Scsi_Cmnd *));
int		 aic7xxx_detect(Scsi_Host_Template *);
int		 aic7xxx_release(struct Scsi_Host *);
const char	*aic7xxx_info(struct Scsi_Host *);
int		 aic7xxx_biosparam(Disk *, kdev_t, int[]);
int		 aic7xxx_bus_reset(Scsi_Cmnd *);
int		 aic7xxx_dev_reset(Scsi_Cmnd *);
int		 aic7xxx_abort(Scsi_Cmnd *);

#if defined(__i386__)
#  define AIC7XXX_BIOSPARAM aic7xxx_biosparam
#else
#  define AIC7XXX_BIOSPARAM NULL
#endif

/*
 * Scsi_Host_Template (see hosts.h) for AIC-7xxx - some fields
 * to do with card config are filled in after the card is detected.
 */
#define AIC7XXX	{						\
	next: NULL,						\
	module: NULL,						\
	proc_dir: NULL,						\
	proc_info: aic7xxx_proc_info,				\
	name: NULL,						\
	detect: aic7xxx_detect,					\
	release: aic7xxx_release,				\
	info: aic7xxx_info,					\
	command: NULL,						\
	queuecommand: aic7xxx_queue,				\
	eh_strategy_handler: NULL,				\
	eh_abort_handler: aic7xxx_abort,			\
	eh_device_reset_handler: aic7xxx_dev_reset,		\
	eh_bus_reset_handler: aic7xxx_bus_reset,		\
	eh_host_reset_handler: NULL,				\
	abort: NULL,						\
	reset: NULL,						\
	slave_attach: NULL,					\
	bios_param: AIC7XXX_BIOSPARAM,				\
	can_queue: 254,		/* max simultaneous cmds      */\
	this_id: -1,		/* scsi id of host adapter    */\
	sg_tablesize: 0,	/* max scatter-gather cmds    */\
	cmd_per_lun: 2,		/* cmds per lun		      */\
	present: 0,		/* number of 7xxx's present   */\
	unchecked_isa_dma: 0,	/* no memory DMA restrictions */\
	use_clustering: ENABLE_CLUSTERING,			\
	use_new_eh_code: 1					\
}

#endif /* _AIC7XXX_LINUX_HOST_H_ */
