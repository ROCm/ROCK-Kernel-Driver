/*
 *  linux/drivers/message/fusion/mptscsih.h
 *      High performance SCSI / Fibre Channel SCSI Host device driver.
 *      For use with PCI chip/adapter(s):
 *          LSIFC9xx/LSI409xx Fibre Channel
 *      running LSI Logic Fusion MPT (Message Passing Technology) firmware.
 *
 *  Credits:
 *      This driver would not exist if not for Alan Cox's development
 *      of the linux i2o driver.
 *
 *      A huge debt of gratitude is owed to David S. Miller (DaveM)
 *      for fixing much of the stupid and broken stuff in the early
 *      driver while porting to sparc64 platform.  THANK YOU!
 *
 *      (see also mptbase.c)
 *
 *  Copyright (c) 1999-2001 LSI Logic Corporation
 *  Originally By: Steven J. Ralston
 *  (mailto:Steve.Ralston@lsil.com)
 *
 *  $Id: mptscsih.h,v 1.7 2001/01/11 16:56:43 sralston Exp $
 */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; version 2 of the License.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    NO WARRANTY
    THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
    CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT
    LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,
    MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is
    solely responsible for determining the appropriateness of using and
    distributing the Program and assumes all risks associated with its
    exercise of rights under this Agreement, including but not limited to
    the risks and costs of program errors, damage to or loss of data,
    programs or equipment, and unavailability or interruption of operations.

    DISCLAIMER OF LIABILITY
    NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
    TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
    USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
    HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef SCSIHOST_H_INCLUDED
#define SCSIHOST_H_INCLUDED
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

#include "linux/version.h"

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	SCSI Public stuff...
 */

#ifdef __sparc__
#define MPT_SCSI_CAN_QUEUE	63
#define MPT_SCSI_CMD_PER_LUN	63
	/* FIXME!  Still investigating qd=64 hang on sparc64... */
#else
#define MPT_SCSI_CAN_QUEUE	64
#define MPT_SCSI_CMD_PER_LUN	64
#endif

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	Various bits and pieces broke within the lk-2.4.0-testN series:-(
 *	So here are various HACKS to work around them.
 */

/*
 *	Conditionalizing with "#ifdef MODULE/#endif" around:
 *		static Scsi_Host_Template driver_template = XX;
 *		#include <../../scsi/scsi_module.c>
 *	lines was REMOVED @ lk-2.4.0-test9
 *	Issue discovered 20001213 by: sshirron
 */
#define MPT_SCSIHOST_NEED_ENTRY_EXIT_HOOKUPS			1
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,4,0)
#	if LINUX_VERSION_CODE == KERNEL_VERSION(2,4,0)
		/*
		 *	Super HACK!  -by sralston:-(
		 *	(good grief; heaven help me!)
		 */
#		include <linux/capability.h>
#		if !defined(CAP_LEASE) && !defined(MODULE)
#			undef MPT_SCSIHOST_NEED_ENTRY_EXIT_HOOKUPS
#		endif
#	else
#		ifndef MODULE
#			undef MPT_SCSIHOST_NEED_ENTRY_EXIT_HOOKUPS
#		endif
#	endif
#endif

/*
 *	tq_scheduler disappeared @ lk-2.4.0-test12
 *	(right when <linux/sched.h> newly defined TQ_ACTIVE)
 */
#define HAVE_TQ_SCHED	1
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
#	include <linux/sched.h>
#	ifdef TQ_ACTIVE
#		undef HAVE_TQ_SCHED
#	endif
#endif
#ifdef HAVE_TQ_SCHED
#define SCHEDULE_TASK(x)		\
	/*MOD_INC_USE_COUNT*/;		\
	(x)->next = NULL;		\
	queue_task(x, &tq_scheduler)
#else
#define SCHEDULE_TASK(x)		\
	/*MOD_INC_USE_COUNT*/;		\
	if (schedule_task(x) == 0) {	\
		/*MOD_DEC_USE_COUNT*/;	\
	}
#endif

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

#define x_scsi_detect		mptscsih_detect
#define x_scsi_release		mptscsih_release
#define x_scsi_info		mptscsih_info
#define x_scsi_queuecommand	mptscsih_qcmd
#define x_scsi_abort		mptscsih_abort
#define x_scsi_bus_reset	mptscsih_bus_reset
#define x_scsi_dev_reset	mptscsih_dev_reset
#define x_scsi_host_reset	mptscsih_host_reset
#define x_scsi_bios_param	mptscsih_bios_param

#define x_scsi_taskmgmt_bh	mptscsih_taskmgmt_bh
#define x_scsi_old_abort	mptscsih_old_abort
#define x_scsi_old_reset	mptscsih_old_reset

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	MPT SCSI Host / Initiator decls...
 */
extern	int		 x_scsi_detect(Scsi_Host_Template *);
extern	int		 x_scsi_release(struct Scsi_Host *host);
extern	const char	*x_scsi_info(struct Scsi_Host *);
/*extern	int		 x_scsi_command(Scsi_Cmnd *);*/
extern	int		 x_scsi_queuecommand(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
#ifdef MPT_SCSI_USE_NEW_EH
extern	int		 x_scsi_abort(Scsi_Cmnd *);
extern	int		 x_scsi_bus_reset(Scsi_Cmnd *);
extern	int		 x_scsi_dev_reset(Scsi_Cmnd *);
/*extern	int		 x_scsi_host_reset(Scsi_Cmnd *);*/
#else
extern	int		 x_scsi_old_abort(Scsi_Cmnd *);
extern	int		 x_scsi_old_reset(Scsi_Cmnd *, unsigned int);
#endif
extern	int		 x_scsi_bios_param(Disk *, kdev_t, int *);
extern	void		 x_scsi_taskmgmt_bh(void *);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0)
#define PROC_SCSI_DECL
#else
#define PROC_SCSI_DECL  proc_name: "mptscsih",
#endif

#ifdef MPT_SCSI_USE_NEW_EH

#define MPT_SCSIHOST {						\
	next:				NULL,			\
	PROC_SCSI_DECL						\
	name:				"MPT SCSI Host",	\
	detect:				x_scsi_detect,		\
	release:			x_scsi_release,		\
	info:				x_scsi_info,		\
	command:			NULL,			\
	queuecommand:			x_scsi_queuecommand,	\
	eh_strategy_handler:		NULL,			\
	eh_abort_handler:		x_scsi_abort,		\
	eh_device_reset_handler:	x_scsi_dev_reset,	\
	eh_bus_reset_handler:		x_scsi_bus_reset,	\
	eh_host_reset_handler:		NULL,			\
	bios_param:			x_scsi_bios_param,	\
	can_queue:			MPT_SCSI_CAN_QUEUE,	\
	this_id:			-1,			\
	sg_tablesize:			25,			\
	cmd_per_lun:			MPT_SCSI_CMD_PER_LUN,	\
	unchecked_isa_dma:		0,			\
	use_clustering:			ENABLE_CLUSTERING,	\
	use_new_eh_code:		1			\
}

#else

#define MPT_SCSIHOST {						\
	next:				NULL,			\
	PROC_SCSI_DECL						\
	name:				"MPT SCSI Host",	\
	detect:				x_scsi_detect,		\
	release:			x_scsi_release,		\
	info:				x_scsi_info,		\
	command:			NULL,			\
	queuecommand:			x_scsi_queuecommand,	\
	abort:				x_scsi_old_abort,	\
	reset:				x_scsi_old_reset,	\
	bios_param:			x_scsi_bios_param,	\
	can_queue:			MPT_SCSI_CAN_QUEUE,	\
	this_id:			-1,			\
	sg_tablesize:			25,			\
	cmd_per_lun:			MPT_SCSI_CMD_PER_LUN,	\
	unchecked_isa_dma:		0,			\
	use_clustering:			ENABLE_CLUSTERING	\
}
#endif


/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

/*  include/scsi/scsi.h may not be quite complete...  */
#ifndef RESERVE_10
#define RESERVE_10		0x56
#endif
#ifndef RELEASE_10
#define RELEASE_10		0x57
#endif
#ifndef PERSISTENT_RESERVE_IN
#define PERSISTENT_RESERVE_IN	0x5e
#endif
#ifndef PERSISTENT_RESERVE_OUT
#define PERSISTENT_RESERVE_OUT	0x5f
#endif

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

#endif

