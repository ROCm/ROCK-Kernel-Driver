/* 
   3w-xxxx.c -- 3ware Storage Controller device driver for Linux.

   Written By: Adam Radford <linux@3ware.com>
   Modifications By: Joel Jacobson <linux@3ware.com>
   		     Arnaldo Carvalho de Melo <acme@conectiva.com.br>
                     Brad Strand <linux@3ware.com>

   Copyright (C) 1999-2001 3ware Inc.

   Kernel compatablity By: 	Andre Hedrick <andre@suse.com>
   Non-Copyright (C) 2000	Andre Hedrick <andre@suse.com>
   
   Further tiny build fixes and trivial hoovering    Alan Cox

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

   Bugs/Comments/Suggestions should be mailed to:                            
   linux@3ware.com

   For more information, goto:
   http://www.3ware.com

   History
   -------
   0.1.000 -     Initial release.
   0.4.000 -     Added support for Asynchronous Event Notification through
                 ioctls for 3DM.
   1.0.000 -     Added DPO & FUA bit support for WRITE_10 & WRITE_6 cdb
                 to disable drive write-cache before writes.
   1.1.000 -     Fixed performance bug with DPO & FUA not existing for WRITE_6.
   1.2.000 -     Added support for clean shutdown notification/feature table.
   1.02.00.001 - Added support for full command packet posts through ioctls
                 for 3DM.
                 Bug fix so hot spare drives don't show up.
   1.02.00.002 - Fix bug with tw_setfeature() call that caused oops on some
                 systems.
   08/21/00    - release previously allocated resources on failure at
                 tw_allocate_memory (acme)
   1.02.00.003 - Fix tw_interrupt() to report error to scsi layer when
                 controller status is non-zero.
                 Added handling of request_sense opcode.
                 Fix possible null pointer dereference in 
                 tw_reset_device_extension()
   1.02.00.004 - Add support for device id of 3ware 7000 series controllers.
                 Make tw_setfeature() call with interrupts disabled.
                 Register interrupt handler before enabling interrupts.
                 Clear attention interrupt before draining aen queue.
   1.02.00.005 - Allocate bounce buffers and custom queue depth for raid5 for
                 6000 and 5000 series controllers.
                 Reduce polling mdelays causing problems on some systems.
                 Fix use_sg = 1 calculation bug.
                 Check for scsi_register returning NULL.
                 Add aen count to /proc/scsi/3w-xxxx.
                 Remove aen code unit masking in tw_aen_complete().
   1.02.00.006 - Remove unit from printk in tw_scsi_eh_abort(), causing
                 possible oops.
                 Fix possible null pointer dereference in tw_scsi_queue()
                 if done function pointer was invalid.
   1.02.00.007 - Fix possible null pointer dereferences in tw_ioctl().
                 Remove check for invalid done function pointer from
                 tw_scsi_queue().
   1.02.00.008 - Set max sectors per io to TW_MAX_SECTORS in tw_findcards().
                 Add tw_decode_error() for printing readable error messages.
                 Print some useful information on certain aen codes.
                 Add tw_decode_bits() for interpreting status register output.
                 Make scsi_set_pci_device() for kernels >= 2.4.4
                 Fix bug where aen's could be lost before a reset.
                 Re-add spinlocks in tw_scsi_detect().
                 Fix possible null pointer dereference in tw_aen_drain_queue()
                 during initialization.
                 Clear pci parity errors during initialization and during io.
   1.02.00.009 - Remove redundant increment in tw_state_request_start().
                 Add ioctl support for direct ATA command passthru.
                 Add entire aen code string list.
   1.02.00.010 - Cleanup queueing code, fix jbod thoughput.
                 Fix get_param for specific units.
*/

#include <linux/module.h>

MODULE_AUTHOR ("3ware Inc.");
MODULE_DESCRIPTION ("3ware Storage Controller Linux Driver");
MODULE_LICENSE("GPL");


#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/time.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/blk.h>
#include <linux/hdreg.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/smp.h>
#include <linux/reboot.h>
#include <linux/spinlock.h>

#include <asm/errno.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

#define __3W_C			/* let 3w-xxxx.h know it is use */

#include "sd.h"
#include "scsi.h"
#include "hosts.h"

#include "3w-xxxx.h"

static int tw_copy_info(TW_Info *info, char *fmt, ...);
static void tw_copy_mem_info(TW_Info *info, char *data, int len);
static void tw_interrupt(int irq, void *dev_instance, struct pt_regs *regs);
static int tw_halt(struct notifier_block *nb, ulong event, void *buf);

/* Notifier block to get a notify on system shutdown/halt/reboot */
static struct notifier_block tw_notifier = {
  tw_halt, NULL, 0
};

/* Globals */
char *tw_driver_version="1.02.00.010";
TW_Device_Extension *tw_device_extension_list[TW_MAX_SLOT];
int tw_device_extension_count = 0;

/* Functions */

/* This function will complete an aen request from the isr */
int tw_aen_complete(TW_Device_Extension *tw_dev, int request_id) 
{
	TW_Param *param;
	unsigned short aen;

	dprintk(KERN_WARNING "3w-xxxx: tw_aen_complete()\n");
	if (tw_dev->alignment_virtual_address[request_id] == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_aen_complete(): Bad alignment virtual address.\n");
		return 1;
	}
	param = (TW_Param *)tw_dev->alignment_virtual_address[request_id];
	aen = *(unsigned short *)(param->data);
	dprintk(KERN_NOTICE "3w-xxxx: tw_aen_complete(): Queue'd code 0x%x\n", aen);

	/* Print some useful info when certain aen codes come out */
	if (aen == 0x0ff) {
		printk(KERN_WARNING "3w-xxxx: scsi%d: AEN: AEN queue overflow.\n", tw_dev->host->host_no);
	} else {
		if ((aen & 0x0ff) < TW_AEN_STRING_MAX) {
			if ((tw_aen_string[aen & 0xff][strlen(tw_aen_string[aen & 0xff])-1]) == '#') {
				printk(KERN_WARNING "3w-xxxx: scsi%d: AEN: %s%d.\n", tw_dev->host->host_no, tw_aen_string[aen & 0xff], aen >> 8);
			} else {
				printk(KERN_WARNING "3w-xxxx: scsi%d: AEN: %s.\n", tw_dev->host->host_no, tw_aen_string[aen & 0xff]);
			}
		} else
			printk(KERN_WARNING "3w-xxxx: scsi%d: Received AEN %d.\n", tw_dev->host->host_no, aen);
	}
	tw_dev->aen_count++;

	/* Now queue the code */
	tw_dev->aen_queue[tw_dev->aen_tail] = aen;
	if (tw_dev->aen_tail == TW_Q_LENGTH - 1) {
		tw_dev->aen_tail = TW_Q_START;
	} else {
		tw_dev->aen_tail = tw_dev->aen_tail + 1;
	}
	if (tw_dev->aen_head == tw_dev->aen_tail) {
		if (tw_dev->aen_head == TW_Q_LENGTH - 1) {
			tw_dev->aen_head = TW_Q_START;
		} else {
			tw_dev->aen_head = tw_dev->aen_head + 1;
		}
	}
	tw_dev->state[request_id] = TW_S_COMPLETED;
	tw_state_request_finish(tw_dev, request_id);

	return 0;
} /* End tw_aen_complete() */

/* This function will drain the aen queue after a soft reset */
int tw_aen_drain_queue(TW_Device_Extension *tw_dev)
{
	TW_Command *command_packet;
	TW_Param *param;
	int tries = 0;
	int request_id = 0;
	u32 command_que_value = 0, command_que_addr;
	u32 status_reg_value = 0, status_reg_addr;
	u32 param_value;
	TW_Response_Queue response_queue;
	u32 response_que_addr;
	unsigned short aen;
	unsigned short aen_code;
	int finished = 0;
	int first_reset = 0;
	int queue = 0;
	int imax, i;
	int found = 0;

	dprintk(KERN_NOTICE "3w-xxxx: tw_aen_drain_queue()\n");

	command_que_addr = tw_dev->registers.command_que_addr;
	status_reg_addr = tw_dev->registers.status_reg_addr;
	response_que_addr = tw_dev->registers.response_que_addr;

	if (tw_poll_status(tw_dev, TW_STATUS_ATTENTION_INTERRUPT, 15)) {
		dprintk(KERN_WARNING "3w-xxxx: tw_aen_drain_queue(): No attention interrupt for card %d.\n", tw_device_extension_count);
		return 1;
	}

	/* Initialize command packet */
	if (tw_dev->command_packet_virtual_address[request_id] == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_aen_drain_queue(): Bad command packet virtual address.\n");
		return 1;
	}
	command_packet = (TW_Command *)tw_dev->command_packet_virtual_address[request_id];
	memset(command_packet, 0, sizeof(TW_Sector));
	command_packet->byte0.opcode = TW_OP_GET_PARAM;
	command_packet->byte0.sgl_offset = 2;
	command_packet->size = 4;
	command_packet->request_id = request_id;
	command_packet->byte3.unit = 0;
	command_packet->byte3.host_id = 0;
	command_packet->status = 0;
	command_packet->flags = 0;
	command_packet->byte6.parameter_count = 1;
	command_que_value = tw_dev->command_packet_physical_address[request_id];
	if (command_que_value == 0) {
		printk(KERN_WARNING "3w-xxxx: tw_aen_drain_queue(): Bad command packet physical address.\n");
		return 1;
	}

	/* Now setup the param */
	if (tw_dev->alignment_virtual_address[request_id] == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_aen_drain_queue(): Bad alignment virtual address.\n");
		return 1;
	}
	param = (TW_Param *)tw_dev->alignment_virtual_address[request_id];
	memset(param, 0, sizeof(TW_Sector));
	param->table_id = 0x401; /* AEN table */
	param->parameter_id = 2; /* Unit code */
	param->parameter_size_bytes = 2;
	param_value = tw_dev->alignment_physical_address[request_id];
	if (param_value == 0) {
		printk(KERN_WARNING "3w-xxxx: tw_aen_drain_queue(): Bad alignment physical address.\n");
		return 1;
	}
	command_packet->byte8.param.sgl[0].address = param_value;
	command_packet->byte8.param.sgl[0].length = sizeof(TW_Sector);

	imax = TW_POLL_MAX_RETRIES;

	/* Now drain the controller's aen queue */
	do {
		/* Post command packet */
		outl(command_que_value, command_que_addr);
    
		/* Now poll for completion */
		for (i=0;i<imax;i++) {
			mdelay(5);
			status_reg_value = inl(status_reg_addr);
			if (tw_check_bits(status_reg_value)) {
				dprintk(KERN_WARNING "3w-xxxx: tw_aen_drain_queue(): Unexpected bits.\n");
				tw_decode_bits(tw_dev, status_reg_value);
				return 1;
			}
			if ((status_reg_value & TW_STATUS_RESPONSE_QUEUE_EMPTY) == 0) {
				response_queue.value = inl(response_que_addr);
				request_id = (unsigned char)response_queue.u.response_id;
    
				if (request_id != 0) {
					/* Unexpected request id */
					printk(KERN_WARNING "3w-xxxx: tw_aen_drain_queue(): Unexpected request id.\n");
					return 1;
				}
	
				if (command_packet->status != 0) {
					if (command_packet->flags != TW_AEN_TABLE_UNDEFINED) {
						/* Bad response */
						dprintk(KERN_WARNING "3w-xxxx: tw_aen_drain_queue(): Bad response, status = 0x%x, flags = 0x%x.\n", command_packet->status, command_packet->flags);
						tw_decode_error(tw_dev, command_packet->status, command_packet->flags, command_packet->byte3.unit);
						return 1;
					} else {
						/* We know this is a 3w-1x00, and doesn't support aen's */
						return 0;
					}
				}

				/* Now check the aen */
				aen = *(unsigned short *)(param->data);
				aen_code = (aen & 0x0ff);
				queue = 0;
				switch (aen_code) {
					case TW_AEN_QUEUE_EMPTY:
						dprintk(KERN_NOTICE "3w-xxxx: tw_aen_drain_queue(): Found TW_AEN_QUEUE_EMPTY.\n");
						if (first_reset != 1) {
							continue;
						} else {
							finished = 1;
						}
						break;
					case TW_AEN_SOFT_RESET:
						dprintk(KERN_NOTICE "3w-xxxx: tw_aen_drain_queue(): Found TW_AEN_SOFT_RESET.\n");
						if (first_reset == 0) {
							first_reset = 1;
						} else {
							queue = 1;
						}
						break;
					case TW_AEN_DEGRADED_MIRROR:
						dprintk(KERN_NOTICE "3w-xxxx: tw_aen_drain_queue(): Found TW_AEN_DEGRADED_MIRROR.\n");
						queue = 1;
						break;
					case TW_AEN_CONTROLLER_ERROR:
						dprintk(KERN_NOTICE "3w-xxxx: tw_aen_drain_queue(): Found TW_AEN_CONTROLLER_ERROR.\n");
						queue = 1;
						break;
					case TW_AEN_REBUILD_FAIL:
						dprintk(KERN_NOTICE "3w-xxxx: tw_aen_drain_queue(): Found TW_AEN_REBUILD_FAIL.\n");
						queue = 1;
						break;
					case TW_AEN_REBUILD_DONE:
						dprintk(KERN_NOTICE "3w-xxxx: tw_aen_drain_queue(): Found TW_AEN_REBUILD_DONE.\n");
						queue = 1;
						break;
					case TW_AEN_QUEUE_FULL:
						dprintk(KERN_NOTICE "3w-xxxx: tw_aen_drain_queue(): Found TW_AEN_QUEUE_FULL.\n");
						queue = 1;
						break;
					case TW_AEN_APORT_TIMEOUT:
						printk(KERN_WARNING "3w-xxxx: Received drive timeout AEN on port %d, check drive and drive cables.\n", aen >> 8);
						queue = 1;
						break;
					case TW_AEN_DRIVE_ERROR:
						printk(KERN_WARNING "3w-xxxx: Received drive error AEN on port %d, check/replace cabling, or possible bad drive.\n", aen >> 8);
						queue = 1;
						break;
					case TW_AEN_SMART_FAIL:
						printk(KERN_WARNING "3w-xxxx: Received S.M.A.R.T. threshold AEN on port %d, check drive/cooling, or possible bad drive.\n", aen >> 8);
						queue = 1;
						break;
					case TW_AEN_SBUF_FAIL:
						printk(KERN_WARNING "3w-xxxx: Received SBUF integrity check failure AEN, reseat card or bad card.\n");
						queue = 1;
						break;
					default:
						dprintk(KERN_WARNING "3w-xxxx: tw_aen_drain_queue(): Unknown AEN code 0x%x.\n", aen_code);
						queue = 1;
				}

				/* Now put the aen on the aen_queue */
				if (queue == 1) {
					tw_dev->aen_queue[tw_dev->aen_tail] = aen_code;
					if (tw_dev->aen_tail == TW_Q_LENGTH - 1) {
						tw_dev->aen_tail = TW_Q_START;
					} else {
						tw_dev->aen_tail = tw_dev->aen_tail + 1;
					}
					if (tw_dev->aen_head == tw_dev->aen_tail) {
						if (tw_dev->aen_head == TW_Q_LENGTH - 1) {
							tw_dev->aen_head = TW_Q_START;
						} else {
							tw_dev->aen_head = tw_dev->aen_head + 1;
						}
					}
				}
				found = 1;
				break;
			}
		}
		if (found == 0) {
			printk(KERN_WARNING "3w-xxxx: tw_aen_drain_queue(): Response never received.\n");
			return 1;
		}
		tries++;
	} while ((tries < TW_MAX_AEN_TRIES) && (finished == 0));

	if (tries >=TW_MAX_AEN_TRIES) {
		printk(KERN_WARNING "3w-xxxx: tw_aen_drain_queue(): Aen queue error.\n");
		return 1;
	}

	return 0;
} /* End tw_aen_drain_queue() */

/* This function will read the aen queue from the isr */
int tw_aen_read_queue(TW_Device_Extension *tw_dev, int request_id) 
{
	TW_Command *command_packet;
	TW_Param *param;
	u32 command_que_value = 0, command_que_addr;
	u32 status_reg_value = 0, status_reg_addr;
	u32 param_value = 0;

	dprintk(KERN_NOTICE "3w-xxxx: tw_aen_read_queue()\n");
	command_que_addr = tw_dev->registers.command_que_addr;
	status_reg_addr = tw_dev->registers.status_reg_addr;

	status_reg_value = inl(status_reg_addr);
	if (tw_check_bits(status_reg_value)) {
		dprintk(KERN_WARNING "3w-xxxx: tw_aen_read_queue(): Unexpected bits.\n");
		tw_decode_bits(tw_dev, status_reg_value);
		return 1;
	}
	if (tw_dev->command_packet_virtual_address[request_id] == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_aen_read_queue(): Bad command packet virtual address.\n");
		return 1;
	}
	command_packet = (TW_Command *)tw_dev->command_packet_virtual_address[request_id];
	memset(command_packet, 0, sizeof(TW_Sector));
	command_packet->byte0.opcode = TW_OP_GET_PARAM;
	command_packet->byte0.sgl_offset = 2;
	command_packet->size = 4;
	command_packet->request_id = request_id;
	command_packet->byte3.unit = 0;
	command_packet->byte3.host_id = 0;
	command_packet->status = 0;
	command_packet->flags = 0;
	command_packet->byte6.parameter_count = 1;
	command_que_value = tw_dev->command_packet_physical_address[request_id];
	if (command_que_value == 0) {
		printk(KERN_WARNING "3w-xxxx: tw_aen_read_queue(): Bad command packet physical address.\n");
		return 1;
	}
	/* Now setup the param */
	if (tw_dev->alignment_virtual_address[request_id] == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_aen_read_queue(): Bad alignment virtual address.\n");
		return 1;
	}
	param = (TW_Param *)tw_dev->alignment_virtual_address[request_id];
	memset(param, 0, sizeof(TW_Sector));
	param->table_id = 0x401; /* AEN table */
	param->parameter_id = 2; /* Unit code */
	param->parameter_size_bytes = 2;
	param_value = tw_dev->alignment_physical_address[request_id];
	if (param_value == 0) {
		printk(KERN_WARNING "3w-xxxx: tw_aen_read_queue(): Bad alignment physical address.\n");
		return 1;
	}
	command_packet->byte8.param.sgl[0].address = param_value;
	command_packet->byte8.param.sgl[0].length = sizeof(TW_Sector);

	/* Now post the command packet */
	if ((status_reg_value & TW_STATUS_COMMAND_QUEUE_FULL) == 0) {
		dprintk(KERN_WARNING "3w-xxxx: tw_aen_read_queue(): Post succeeded.\n");
		tw_dev->srb[request_id] = 0; /* Flag internal command */
		tw_dev->state[request_id] = TW_S_POSTED;
		outl(command_que_value, command_que_addr);
	} else {
		printk(KERN_WARNING "3w-xxxx: tw_aen_read_queue(): Post failed, will retry.\n");
		return 1;
	}

	return 0;
} /* End tw_aen_read_queue() */

/* This function will allocate memory and check if it is 16 d-word aligned */
int tw_allocate_memory(TW_Device_Extension *tw_dev, int request_id, int size, int which)
{
	u32 *virt_addr = kmalloc(size, GFP_ATOMIC);

	dprintk(KERN_NOTICE "3w-xxxx: tw_allocate_memory()\n");

	if (!virt_addr) {
		printk(KERN_WARNING "3w-xxxx: tw_allocate_memory(): kmalloc() failed.\n");
		return 1;
	}

	if ((u32)virt_addr % TW_ALIGNMENT) {
		kfree(virt_addr);
		printk(KERN_WARNING "3w-xxxx: tw_allocate_memory(): Found unaligned address.\n");
		return 1;
	}

	switch(which) {
	case 0:
		tw_dev->command_packet_virtual_address[request_id] = virt_addr;
		tw_dev->command_packet_physical_address[request_id] = virt_to_bus(virt_addr);
		break;
	case 1:
		tw_dev->alignment_virtual_address[request_id] = virt_addr;
		tw_dev->alignment_physical_address[request_id] = virt_to_bus(virt_addr);
		break;
	case 2:
		tw_dev->bounce_buffer[request_id] = virt_addr;
		break;
	default:
		printk(KERN_WARNING "3w-xxxx: tw_allocate_memory(): case slip in tw_allocate_memory()\n");
		return 1;
	}
	return 0;
} /* End tw_allocate_memory() */

/* This function will check the status register for unexpected bits */
int tw_check_bits(u32 status_reg_value)
{
	if ((status_reg_value & TW_STATUS_EXPECTED_BITS) != TW_STATUS_EXPECTED_BITS) {  
		dprintk(KERN_WARNING "3w-xxxx: tw_check_bits(): No expected bits (0x%x).\n", status_reg_value);
		return 1;
	}
	if ((status_reg_value & TW_STATUS_UNEXPECTED_BITS) != 0) {
		dprintk(KERN_WARNING "3w-xxxx: tw_check_bits(): Found unexpected bits (0x%x).\n", status_reg_value);
		return 1;
	}

	return 0;
} /* End tw_check_bits() */

/* This function will report controller error status */
int tw_check_errors(TW_Device_Extension *tw_dev) 
{
	u32 status_reg_addr, status_reg_value;
  
	status_reg_addr = tw_dev->registers.status_reg_addr;
	status_reg_value = inl(status_reg_addr);

	if (TW_STATUS_ERRORS(status_reg_value) || tw_check_bits(status_reg_value))
		return 1;

	return 0;
} /* End tw_check_errors() */

/* This function will clear the attention interrupt */
void tw_clear_attention_interrupt(TW_Device_Extension *tw_dev)
{
	u32 control_reg_addr, control_reg_value;
  
	control_reg_addr = tw_dev->registers.control_reg_addr;
	control_reg_value = TW_CONTROL_CLEAR_ATTENTION_INTERRUPT;
	outl(control_reg_value, control_reg_addr);
} /* End tw_clear_attention_interrupt() */

/* This function will clear the host interrupt */
void tw_clear_host_interrupt(TW_Device_Extension *tw_dev)
{
	u32 control_reg_addr, control_reg_value;

	control_reg_addr = tw_dev->registers.control_reg_addr;
	control_reg_value = TW_CONTROL_CLEAR_HOST_INTERRUPT;
	outl(control_reg_value, control_reg_addr);
} /* End tw_clear_host_interrupt() */

/* This function is called by tw_scsi_proc_info */
static int tw_copy_info(TW_Info *info, char *fmt, ...) 
{
	va_list args;
	char buf[81];
	int len;
  
	va_start(args, fmt);
	len = vsprintf(buf, fmt, args);
	va_end(args);
	tw_copy_mem_info(info, buf, len);
	return len;
} /* End tw_copy_info() */

/* This function is called by tw_scsi_proc_info */
static void tw_copy_mem_info(TW_Info *info, char *data, int len)
{
	if (info->position + len > info->length)
		len = info->length - info->position;

	if (info->position + len < info->offset) {
		info->position += len;
		return;
	}
	if (info->position < info->offset) {
		data += (info->offset - info->position);
		len  -= (info->offset - info->position);
	}
	if (len > 0) {
		memcpy(info->buffer + info->position, data, len);
		info->position += len;
	}
} /* End tw_copy_mem_info() */

/* This function will print readable messages from statsu register errors */
void tw_decode_bits(TW_Device_Extension *tw_dev, u32 status_reg_value)
{
	dprintk(KERN_WARNING "3w-xxxx: tw_decode_bits()\n");
	switch (status_reg_value & TW_STATUS_UNEXPECTED_BITS) {
	case TW_STATUS_PCI_PARITY_ERROR:
		printk(KERN_WARNING "3w-xxxx: PCI Parity Error: Reseat card, move card, or buggy device on the bus.\n");
		outl(TW_CONTROL_CLEAR_PARITY_ERROR, tw_dev->registers.control_reg_addr);
		pci_write_config_word(tw_dev->tw_pci_dev, PCI_STATUS, TW_PCI_CLEAR_PARITY_ERRORS);
		break;
	case TW_STATUS_MICROCONTROLLER_ERROR:
		printk(KERN_WARNING "3w-xxxx: Microcontroller Error.\n");
		break;
  }
} /* End tw_decode_bits() */

/* This function will print readable messages from flags and status values */
void tw_decode_error(TW_Device_Extension *tw_dev, unsigned char status, unsigned char flags, unsigned char unit)
{
	dprintk(KERN_WARNING "3w-xxxx: tw_decode_error()\n");
	switch (status) {
	case 0xc7:
		switch (flags) {
		case 0x1b: 
			printk(KERN_WARNING "3w-xxxx: scsi%d: Drive timeout on unit %d, check drive and drive cables.\n", tw_dev->host->host_no, unit);
			break;
		case 0x51:
			printk(KERN_WARNING "3w-xxxx: scsi%d: Unrecoverable drive error on unit %d, check/replace cabling, or possible bad drive.\n", tw_dev->host->host_no, unit);
			break;
		default:
			printk(KERN_WARNING "3w-xxxx: scsi%d: Controller error: status = 0x%x, flags = 0x%x, unit #%d.\n", tw_dev->host->host_no, status, flags, unit);
		}
		break;
	default:
		printk(KERN_WARNING "3w-xxxx: scsi%d: Controller error: status = 0x%x, flags = 0x%x, unit #%d.\n", tw_dev->host->host_no, status, flags, unit);
	}
} /* End tw_decode_error() */

/* This function will disable interrupts on the controller */  
void tw_disable_interrupts(TW_Device_Extension *tw_dev) 
{
	u32 control_reg_value, control_reg_addr;

	control_reg_addr = tw_dev->registers.control_reg_addr;
	control_reg_value = TW_CONTROL_DISABLE_INTERRUPTS;
	outl(control_reg_value, control_reg_addr);
} /* End tw_disable_interrupts() */

/* This function will empty the response que */
int tw_empty_response_que(TW_Device_Extension *tw_dev) 
{
	u32 status_reg_addr, status_reg_value;
	u32 response_que_addr, response_que_value;

	status_reg_addr = tw_dev->registers.status_reg_addr;
	response_que_addr = tw_dev->registers.response_que_addr;
  
	status_reg_value = inl(status_reg_addr);

	if (tw_check_bits(status_reg_value)) {
		dprintk(KERN_WARNING "3w-xxxx: tw_empty_response_queue(): Unexpected bits 1.\n");
		tw_decode_bits(tw_dev, status_reg_value);
		return 1;
	}
  
	while ((status_reg_value & TW_STATUS_RESPONSE_QUEUE_EMPTY) == 0) {
		response_que_value = inl(response_que_addr);
		status_reg_value = inl(status_reg_addr);
		if (tw_check_bits(status_reg_value)) {
			dprintk(KERN_WARNING "3w-xxxx: tw_empty_response_queue(): Unexpected bits 2.\n");
			tw_decode_bits(tw_dev, status_reg_value);
			return 1;
		}
	}
	return 0;
} /* End tw_empty_response_que() */

/* This function will enable interrupts on the controller */
void tw_enable_interrupts(TW_Device_Extension *tw_dev)
{
	u32 control_reg_value, control_reg_addr;

	control_reg_addr = tw_dev->registers.control_reg_addr;
	control_reg_value = (TW_CONTROL_CLEAR_ATTENTION_INTERRUPT |
			     TW_CONTROL_UNMASK_RESPONSE_INTERRUPT |
			     TW_CONTROL_ENABLE_INTERRUPTS);
	outl(control_reg_value, control_reg_addr);
} /* End tw_enable_interrupts() */

/* This function will find and initialize all cards */
int tw_findcards(Scsi_Host_Template *tw_host) 
{
	int numcards = 0, tries = 0, error = 0;
	struct Scsi_Host *host;
	TW_Device_Extension *tw_dev;
	TW_Device_Extension *tw_dev2;
	struct pci_dev *tw_pci_dev = NULL;
	u32 status_reg_value;
	unsigned char c = 1;
	int i;
	u16 device[TW_NUMDEVICES] = { TW_DEVICE_ID, TW_DEVICE_ID2 };

	dprintk(KERN_NOTICE "3w-xxxx: tw_findcards()\n");

	for (i=0;i<TW_NUMDEVICES;i++) {
		while ((tw_pci_dev = pci_find_device(TW_VENDOR_ID, device[i], tw_pci_dev))) {
			if (pci_enable_device(tw_pci_dev))
				continue;
			/* Prepare temporary device extension */
			tw_dev=(TW_Device_Extension *)kmalloc(sizeof(TW_Device_Extension), GFP_ATOMIC);
			if (tw_dev == NULL) {
				printk(KERN_WARNING "3w-xxxx: tw_findcards(): kmalloc() failed for card %d.\n", numcards);
				continue;
			}
			memset(tw_dev, 0, sizeof(TW_Device_Extension));

			error = tw_initialize_device_extension(tw_dev);
			if (error) {
				printk(KERN_WARNING "3w-xxxx: tw_findcards(): Couldn't initialize device extension for card %d.\n", numcards);
				tw_free_device_extension(tw_dev);
				kfree(tw_dev);
				continue;
			}

			/* Calculate the cards register addresses */
			tw_dev->registers.base_addr = pci_resource_start(tw_pci_dev, 0);
			tw_dev->registers.control_reg_addr = pci_resource_start(tw_pci_dev, 0);
			tw_dev->registers.status_reg_addr = pci_resource_start(tw_pci_dev, 0) + 0x4;
			tw_dev->registers.command_que_addr = pci_resource_start(tw_pci_dev, 0) + 0x8;
			tw_dev->registers.response_que_addr = pci_resource_start(tw_pci_dev, 0) + 0xC;
			/* Save pci_dev struct to device extension */
			tw_dev->tw_pci_dev = tw_pci_dev;

			/* Check for errors and clear them */
			status_reg_value = inl(tw_dev->registers.status_reg_addr);
			if (TW_STATUS_ERRORS(status_reg_value)) 
			  tw_decode_bits(tw_dev, status_reg_value);
			
			/* Poll status register for 60 secs for 'Controller Ready' flag */
			if (tw_poll_status(tw_dev, TW_STATUS_MICROCONTROLLER_READY, 60)) {
				printk(KERN_WARNING "3w-xxxx: tw_findcards(): Microcontroller not ready for card %d.\n", numcards);
				tw_free_device_extension(tw_dev);
				kfree(tw_dev);
				continue;
			}

			/* Disable interrupts on the card */
			tw_disable_interrupts(tw_dev);
			
			while (tries < TW_MAX_RESET_TRIES) {
				/* Do soft reset */
				tw_soft_reset(tw_dev);
			  
				error = tw_aen_drain_queue(tw_dev);
				if (error) {
					printk(KERN_WARNING "3w-xxxx: tw_findcards(): No attention interrupt for card %d.\n", numcards);
					tries++;
					continue;
				}

				/* Check for controller errors */
				if (tw_check_errors(tw_dev)) {
					printk(KERN_WARNING "3w-xxxx: tw_findcards(): Controller errors found, soft resetting card %d.\n", numcards);
					tries++;
					continue;
				}

				/* Empty the response queue */
				error = tw_empty_response_que(tw_dev);
				if (error) {
					printk(KERN_WARNING "3w-xxxx: tw_findcards(): Couldn't empty response queue for card %d.\n", numcards);
					tries++;
					continue;
				}

				/* Now the controller is in a good state */
				break;
			}

			if (tries >= TW_MAX_RESET_TRIES) {
				printk(KERN_WARNING "3w-xxxx: tw_findcards(): Controller error or no attention interrupt: giving up for card %d.\n", numcards);
				tw_free_device_extension(tw_dev);
				kfree(tw_dev);
				continue;
			}

			/* Make sure that io region isn't already taken */
			if (check_region((tw_dev->tw_pci_dev->resource[0].start), TW_IO_ADDRESS_RANGE)) {
				printk(KERN_WARNING "3w-xxxx: tw_findcards(): Couldn't get io range 0x%lx-0x%lx for card %d.\n", 
				       (tw_dev->tw_pci_dev->resource[0].start), 
				       (tw_dev->tw_pci_dev->resource[0].start) + 
				       TW_IO_ADDRESS_RANGE, numcards);
				tw_free_device_extension(tw_dev);
				kfree(tw_dev);
				continue;
			}
    
			/* Reserve the io address space */
			request_region((tw_dev->tw_pci_dev->resource[0].start), TW_IO_ADDRESS_RANGE, TW_DEVICE_NAME);
			error = tw_initialize_units(tw_dev);
			if (error) {
				printk(KERN_WARNING "3w-xxxx: tw_findcards(): Couldn't initialize units for card %d.\n", numcards);
				release_region((tw_dev->tw_pci_dev->resource[0].start), TW_IO_ADDRESS_RANGE);
				tw_free_device_extension(tw_dev);
				kfree(tw_dev);
				continue;
			}

			error = tw_initconnection(tw_dev, TW_INIT_MESSAGE_CREDITS);
			if (error) {
				printk(KERN_WARNING "3w-xxxx: tw_findcards(): Couldn't initconnection for card %d.\n", numcards);
				release_region((tw_dev->tw_pci_dev->resource[0].start), TW_IO_ADDRESS_RANGE);
				tw_free_device_extension(tw_dev);
				kfree(tw_dev);
				continue;
			}

			/* Calculate max cmds per lun, and setup queues */
			if (tw_dev->num_units > 0) {
				if ((tw_dev->num_raid_five > 0) && (tw_dev->tw_pci_dev->device == TW_DEVICE_ID)) {
					tw_host->cmd_per_lun = (TW_MAX_BOUNCEBUF-1)/tw_dev->num_units;
					tw_dev->free_head = TW_Q_START;
					tw_dev->free_tail = TW_MAX_BOUNCEBUF - 1;
					tw_dev->free_wrap = TW_MAX_BOUNCEBUF - 1;
				} else {
					tw_host->cmd_per_lun = (TW_Q_LENGTH-1)/tw_dev->num_units;
					tw_dev->free_head = TW_Q_START;
					tw_dev->free_tail = TW_Q_LENGTH - 1;
					tw_dev->free_wrap = TW_Q_LENGTH - 1;
				}
			}

		/* Register the card with the kernel SCSI layer */
			host = scsi_register(tw_host, sizeof(TW_Device_Extension));
			if (host == NULL) {
				printk(KERN_WARNING "3w-xxxx: tw_findcards(): scsi_register() failed for card %d.\n", numcards-1);
				release_region((tw_dev->tw_pci_dev->resource[0].start), TW_IO_ADDRESS_RANGE);
				tw_free_device_extension(tw_dev);
				kfree(tw_dev);
				continue;
			}

			/* Set max sectors per io */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,7)
			host->max_sectors = TW_MAX_SECTORS;
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,4)
			scsi_set_pci_device(host, tw_pci_dev);
#endif

			status_reg_value = inl(tw_dev->registers.status_reg_addr);

			printk(KERN_NOTICE "scsi%d : Found a 3ware Storage Controller at 0x%x, IRQ: %d, P-chip: %d.%d\n", host->host_no,
				(u32)(tw_pci_dev->resource[0].start), tw_pci_dev->irq, 
				(status_reg_value & TW_STATUS_MAJOR_VERSION_MASK) >> 28, 
				(status_reg_value & TW_STATUS_MINOR_VERSION_MASK) >> 24);

			if (host->hostdata) {
				tw_dev2 = (TW_Device_Extension *)host->hostdata;
				memcpy(tw_dev2, tw_dev, sizeof(TW_Device_Extension));
				tw_device_extension_list[tw_device_extension_count] = tw_dev2;
				numcards++;
				tw_device_extension_count = numcards;
				tw_dev2->host = host;
			} else { 
				printk(KERN_WARNING "3w-xxxx: tw_findcards(): Bad scsi host data for card %d.\n", numcards-1);
				scsi_unregister(host);
				release_region((tw_dev->tw_pci_dev->resource[0].start), TW_IO_ADDRESS_RANGE);
				tw_free_device_extension(tw_dev);
				kfree(tw_dev);
				continue;
			}

			/* Tell the firmware we support shutdown notification*/
			tw_setfeature(tw_dev2, 2, 1, &c);

			/* Now setup the interrupt handler */
			error = tw_setup_irq(tw_dev2);
			if (error) {
				printk(KERN_WARNING "3w-xxxx: tw_findcards(): Error requesting irq for card %d.\n", numcards-1);
				scsi_unregister(host);
				release_region((tw_dev->tw_pci_dev->resource[0].start), TW_IO_ADDRESS_RANGE);

				tw_free_device_extension(tw_dev);
				kfree(tw_dev);
				numcards--;
				continue;
			}

			/* Re-enable interrupts on the card */
			tw_enable_interrupts(tw_dev2);

			/* Free the temporary device extension */
			if (tw_dev)
				kfree(tw_dev);
		}
	}

	if (numcards == 0) 
		printk(KERN_WARNING "3w-xxxx: No cards with valid units found.\n");
	else
	  register_reboot_notifier(&tw_notifier);

	return numcards;
} /* End tw_findcards() */

/* This function will free up device extension resources */
void tw_free_device_extension(TW_Device_Extension *tw_dev)
{
	int i;

	dprintk(KERN_NOTICE "3w-xxxx: tw_free_device_extension()\n");
	/* Free command packet and generic buffer memory */
	for (i=0;i<TW_Q_LENGTH;i++) {
		if (tw_dev->command_packet_virtual_address[i]) 
			kfree(tw_dev->command_packet_virtual_address[i]);

		if (tw_dev->alignment_virtual_address[i])
			kfree(tw_dev->alignment_virtual_address[i]);

	}
	for (i=0;i<TW_MAX_BOUNCEBUF;i++) {
		if (tw_dev->bounce_buffer[i])
			kfree(tw_dev->bounce_buffer[i]);
	}
} /* End tw_free_device_extension() */

/* Clean shutdown routine */
static int tw_halt(struct notifier_block *nb, ulong event, void *buf)
{
	int i;

	for (i=0;i<tw_device_extension_count;i++) {
		printk(KERN_NOTICE "3w-xxxx: Shutting down card %d.\n", i);
		tw_shutdown_device(tw_device_extension_list[i]);
	}
	unregister_reboot_notifier(&tw_notifier);

	return NOTIFY_OK;
} /* End tw_halt() */

/* This function will send an initconnection command to controller */
int tw_initconnection(TW_Device_Extension *tw_dev, int message_credits) 
{
	u32 command_que_addr, command_que_value;
	u32 status_reg_addr, status_reg_value;
	u32 response_que_addr;
	TW_Command  *command_packet;
	TW_Response_Queue response_queue;
	int request_id = 0;
	int i = 0;
	int imax = 0;

	dprintk(KERN_NOTICE "3w-xxxx: tw_initconnection()\n");
	command_que_addr = tw_dev->registers.command_que_addr;
	status_reg_addr = tw_dev->registers.status_reg_addr;
	response_que_addr = tw_dev->registers.response_que_addr;

	/* Initialize InitConnection command packet */
	if (tw_dev->command_packet_virtual_address[request_id] == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_initconnection(): Bad command packet virtual address.\n");
		return 1;
	}

	command_packet = (TW_Command *)tw_dev->command_packet_virtual_address[request_id];
	memset(command_packet, 0, sizeof(TW_Sector));
	command_packet->byte0.opcode = TW_OP_INIT_CONNECTION;
	command_packet->byte0.sgl_offset = 0x0;
	command_packet->size = TW_INIT_COMMAND_PACKET_SIZE;
	command_packet->request_id = request_id;
	command_packet->byte3.unit = 0x0;
	command_packet->byte3.host_id = 0x0;
	command_packet->status = 0x0;
	command_packet->flags = 0x0;
	command_packet->byte6.message_credits = message_credits; 
	command_packet->byte8.init_connection.response_queue_pointer = 0x0;
	command_que_value = tw_dev->command_packet_physical_address[request_id];

	if (command_que_value == 0) {
		printk(KERN_WARNING "3w-xxxx: tw_initconnection(): Bad command packet physical address.\n");
		return 1;
	}
  
	/* Send command packet to the board */
	outl(command_que_value, command_que_addr);
    
	/* Poll for completion */
	imax = TW_POLL_MAX_RETRIES;
	for (i=0;i<imax;i++) {
		mdelay(5);
		status_reg_value = inl(status_reg_addr);
		if (tw_check_bits(status_reg_value)) {
			dprintk(KERN_WARNING "3w-xxxx: tw_initconnection(): Unexpected bits.\n");
			tw_decode_bits(tw_dev, status_reg_value);
			return 1;
		}
		if ((status_reg_value & TW_STATUS_RESPONSE_QUEUE_EMPTY) == 0) {
			response_queue.value = inl(response_que_addr);
			request_id = (unsigned char)response_queue.u.response_id;
			if (request_id != 0) {
				/* unexpected request id */
				printk(KERN_WARNING "3w-xxxx: tw_initconnection(): Unexpected request id.\n");
				return 1;
			}
			if (command_packet->status != 0) {
				/* bad response */
				dprintk(KERN_WARNING "3w-xxxx: tw_initconnection(): Bad response, status = 0x%x, flags = 0x%x.\n", command_packet->status, command_packet->flags);
				tw_decode_error(tw_dev, command_packet->status, command_packet->flags, command_packet->byte3.unit);
				return 1;
			}
			break;	/* Response was okay, so we exit */
		}
	}
	return 0;
} /* End tw_initconnection() */

/* This function will initialize the fields of a device extension */
int tw_initialize_device_extension(TW_Device_Extension *tw_dev)
{
	int i, imax;

	dprintk(KERN_NOTICE "3w-xxxx: tw_initialize_device_extension()\n");
	imax = TW_Q_LENGTH;

	for (i=0; i<imax; i++) {
		/* Initialize command packet buffers */
		tw_allocate_memory(tw_dev, i, sizeof(TW_Sector), 0);
		if (tw_dev->command_packet_virtual_address[i] == NULL) {
			printk(KERN_WARNING "3w-xxxx: tw_initialize_device_extension(): Bad command packet virtual address.\n");
			return 1;
		}
		memset(tw_dev->command_packet_virtual_address[i], 0, sizeof(TW_Sector));
    
		/* Initialize generic buffer */
		tw_allocate_memory(tw_dev, i, sizeof(TW_Sector), 1);
		if (tw_dev->alignment_virtual_address[i] == NULL) {
			printk(KERN_WARNING "3w-xxxx: tw_initialize_device_extension(): Bad alignment virtual address.\n");
			return 1;
		}
		memset(tw_dev->alignment_virtual_address[i], 0, sizeof(TW_Sector));

		tw_dev->free_queue[i] = i;
		tw_dev->state[i] = TW_S_INITIAL;
		tw_dev->ioctl_size[i] = 0;
		tw_dev->aen_queue[i] = 0;
	}

	for (i=0;i<TW_MAX_UNITS;i++) {
		tw_dev->is_unit_present[i] = 0;
		tw_dev->is_raid_five[i] = 0;
	}

	tw_dev->num_units = 0;
	tw_dev->num_aborts = 0;
	tw_dev->num_resets = 0;
	tw_dev->posted_request_count = 0;
	tw_dev->max_posted_request_count = 0;
	tw_dev->max_sgl_entries = 0;
	tw_dev->sgl_entries = 0;
	tw_dev->host = NULL;
	tw_dev->pending_head = TW_Q_START;
	tw_dev->pending_tail = TW_Q_START;
	tw_dev->aen_head = 0;
	tw_dev->aen_tail = 0;
	tw_dev->sector_count = 0;
	tw_dev->max_sector_count = 0;
	tw_dev->aen_count = 0;
	tw_dev->num_raid_five = 0;
	spin_lock_init(&tw_dev->tw_lock);
	tw_dev->flags = 0;
	return 0;
} /* End tw_initialize_device_extension() */

/* This function will get unit info from the controller */
int tw_initialize_units(TW_Device_Extension *tw_dev) 
{
	int found = 0;
	unsigned char request_id = 0;
	TW_Command *command_packet;
	TW_Param *param;
	int i, j, imax, num_units = 0, num_raid_five = 0;
	u32 status_reg_addr, status_reg_value;
	u32 command_que_addr, command_que_value;
	u32 response_que_addr;
	TW_Response_Queue response_queue;
	u32 param_value;
	unsigned char *is_unit_present;
	unsigned char *raid_level;

	dprintk(KERN_NOTICE "3w-xxxx: tw_initialize_units()\n");

	status_reg_addr = tw_dev->registers.status_reg_addr;
	command_que_addr = tw_dev->registers.command_que_addr;
	response_que_addr = tw_dev->registers.response_que_addr;
  
	/* Setup the command packet */
	command_packet = (TW_Command *)tw_dev->command_packet_virtual_address[request_id];
	if (command_packet == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_initialize_units(): Bad command packet virtual address.\n");
		return 1;
	}
	memset(command_packet, 0, sizeof(TW_Sector));
	command_packet->byte0.opcode      = TW_OP_GET_PARAM;
	command_packet->byte0.sgl_offset  = 2;
	command_packet->size              = 4;
	command_packet->request_id        = request_id;
	command_packet->byte3.unit        = 0;
	command_packet->byte3.host_id     = 0;
	command_packet->status            = 0;
	command_packet->flags             = 0;
	command_packet->byte6.block_count = 1;

	/* Now setup the param */
	if (tw_dev->alignment_virtual_address[request_id] == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_initialize_units(): Bad alignment virtual address.\n");
		return 1;
	}
	param = (TW_Param *)tw_dev->alignment_virtual_address[request_id];
	memset(param, 0, sizeof(TW_Sector));
	param->table_id = 3;       /* unit summary table */
	param->parameter_id = 3;   /* unitstatus parameter */
	param->parameter_size_bytes = TW_MAX_UNITS;
	param_value = tw_dev->alignment_physical_address[request_id];
	if (param_value == 0) {
		printk(KERN_WARNING "3w-xxxx: tw_initialize_units(): Bad alignment physical address.\n");
		return 1;
	}

	command_packet->byte8.param.sgl[0].address = param_value;
	command_packet->byte8.param.sgl[0].length = sizeof(TW_Sector);

	/* Post the command packet to the board */
	command_que_value = tw_dev->command_packet_physical_address[request_id];
	if (command_que_value == 0) {
		printk(KERN_WARNING "3w-xxxx: tw_initialize_units(): Bad command packet physical address.\n");
		return 1;
	}
	outl(command_que_value, command_que_addr);

	/* Poll for completion */
	imax = TW_POLL_MAX_RETRIES;
	for(i=0; i<imax; i++) {
		mdelay(5);
		status_reg_value = inl(status_reg_addr);
		if (tw_check_bits(status_reg_value)) {
			dprintk(KERN_WARNING "3w-xxxx: tw_initialize_units(): Unexpected bits.\n");
			tw_decode_bits(tw_dev, status_reg_value);
			return 1;
		}
		if ((status_reg_value & TW_STATUS_RESPONSE_QUEUE_EMPTY) == 0) {
			response_queue.value = inl(response_que_addr);
			request_id = (unsigned char)response_queue.u.response_id;
			if (request_id != 0) {
				/* unexpected request id */
				printk(KERN_WARNING "3w-xxxx: tw_initialize_units(): Unexpected request id.\n");
				return 1;
			}
			if (command_packet->status != 0) {
				/* bad response */
				dprintk(KERN_WARNING "3w-xxxx: tw_initialize_units(): Bad response, status = 0x%x, flags = 0x%x.\n", command_packet->status, command_packet->flags);
				tw_decode_error(tw_dev, command_packet->status, command_packet->flags, command_packet->byte3.unit);
				return 1;
			}
			found = 1;
			break;
		}
	}
	if (found == 0) {
		/* response never received */
		printk(KERN_WARNING "3w-xxxx: tw_initialize_units(): No response.\n");
		return 1;
	}

	param = (TW_Param *)tw_dev->alignment_virtual_address[request_id];
	is_unit_present = (unsigned char *)&(param->data[0]);
  
	/* Show all units present */
	imax = TW_MAX_UNITS;
	for(i=0; i<imax; i++) {
		if (is_unit_present[i] == 0) {
			tw_dev->is_unit_present[i] = FALSE;
		} else {
		  if (is_unit_present[i] & TW_UNIT_ONLINE) {
			dprintk(KERN_NOTICE "3w-xxxx: tw_initialize_units(): Unit %d found.\n", i);
			tw_dev->is_unit_present[i] = TRUE;
			num_units++;
		  }
		}
	}
	tw_dev->num_units = num_units;

	if (num_units == 0) {
		dprintk(KERN_NOTICE "3w-xxxx: tw_initialize_units(): No units found.\n");
		return 1;
	}

	/* Find raid 5 arrays */
	for (j=0;j<TW_MAX_UNITS;j++) {
		if (tw_dev->is_unit_present[j] == 0)
			continue;
		command_packet = (TW_Command *)tw_dev->command_packet_virtual_address[request_id];
		if (command_packet == NULL) {
			printk(KERN_WARNING "3w-xxxx: tw_initialize_units(): Bad command packet virtual address.\n");
			return 1;
		}
		memset(command_packet, 0, sizeof(TW_Sector));
		command_packet->byte0.opcode      = TW_OP_GET_PARAM;
		command_packet->byte0.sgl_offset  = 2;
		command_packet->size              = 4;
		command_packet->request_id        = request_id;
		command_packet->byte3.unit        = 0;
		command_packet->byte3.host_id     = 0;
		command_packet->status            = 0;
		command_packet->flags             = 0;
		command_packet->byte6.block_count = 1;

		/* Now setup the param */
		if (tw_dev->alignment_virtual_address[request_id] == NULL) {
			printk(KERN_WARNING "3w-xxxx: tw_initialize_units(): Bad alignment virtual address.\n");
			return 1;
		}
		param = (TW_Param *)tw_dev->alignment_virtual_address[request_id];
		memset(param, 0, sizeof(TW_Sector));
		param->table_id = 0x300+j; /* unit summary table */
		param->parameter_id = 0x6; /* unit descriptor */
		param->parameter_size_bytes = 0xc;
		param_value = tw_dev->alignment_physical_address[request_id];
		if (param_value == 0) {
			printk(KERN_WARNING "3w-xxxx: tw_initialize_units(): Bad alignment physical address.\n");
			return 1;
		}

		command_packet->byte8.param.sgl[0].address = param_value;
		command_packet->byte8.param.sgl[0].length = sizeof(TW_Sector);

		/* Post the command packet to the board */
		command_que_value = tw_dev->command_packet_physical_address[request_id];
		if (command_que_value == 0) {
			printk(KERN_WARNING "3w-xxxx: tw_initialize_units(): Bad command packet physical address.\n");
			return 1;
		}
		outl(command_que_value, command_que_addr);

		/* Poll for completion */
		imax = TW_POLL_MAX_RETRIES;
		for(i=0; i<imax; i++) {
			mdelay(5);
			status_reg_value = inl(status_reg_addr);
			if (tw_check_bits(status_reg_value)) {
				dprintk(KERN_WARNING "3w-xxxx: tw_initialize_units(): Unexpected bits.\n");
				tw_decode_bits(tw_dev, status_reg_value);
				return 1;
			}
			if ((status_reg_value & TW_STATUS_RESPONSE_QUEUE_EMPTY) == 0) {
				response_queue.value = inl(response_que_addr);
				request_id = (unsigned char)response_queue.u.response_id;
				if (request_id != 0) {
					/* unexpected request id */
					printk(KERN_WARNING "3w-xxxx: tw_initialize_units(): Unexpected request id.\n");
					return 1;
				}
				if (command_packet->status != 0) {
					/* bad response */
					dprintk(KERN_WARNING "3w-xxxx: tw_initialize_units(): Bad response, status = 0x%x, flags = 0x%x.\n", command_packet->status, command_packet->flags);
					tw_decode_error(tw_dev, command_packet->status, command_packet->flags, command_packet->byte3.unit);
					return 1;
				}
				found = 1;
				break;
			}
		}
		if (found == 0) {
			/* response never received */
			printk(KERN_WARNING "3w-xxxx: tw_initialize_units(): No response.\n");
			return 1;
		}

		param = (TW_Param *)tw_dev->alignment_virtual_address[request_id];
		raid_level = (unsigned char *)&(param->data[1]);
		if (*raid_level == 5) {
			dprintk(KERN_WARNING "3w-xxxx: Found unit %d to be a raid5 unit.\n", j);
			tw_dev->is_raid_five[j] = 1;
			num_raid_five++;
		}
	}
	tw_dev->num_raid_five = num_raid_five;

	/* Now allocate raid5 bounce buffers */
	if ((num_raid_five != 0) && (tw_dev->tw_pci_dev->device == TW_DEVICE_ID)) {
		for (i=0;i<TW_MAX_BOUNCEBUF;i++) {
			tw_allocate_memory(tw_dev, i, sizeof(TW_Sector)*TW_MAX_SECTORS, 2);
			if (tw_dev->bounce_buffer[i] == NULL) {
				printk(KERN_WARNING "3w-xxxx: Bounce buffer allocation failed.\n");
				return 1;
			}
			memset(tw_dev->bounce_buffer[i], 0, sizeof(TW_Sector)*TW_MAX_SECTORS);
		}
	}
  
	return 0;
} /* End tw_initialize_units() */

/* This function is the interrupt service routine */
static void tw_interrupt(int irq, void *dev_instance, struct pt_regs *regs) 
{
	int request_id;
	u32 status_reg_addr, status_reg_value;
	u32 response_que_addr;
	TW_Device_Extension *tw_dev = (TW_Device_Extension *)dev_instance;
	TW_Response_Queue response_que;
	int error = 0;
	int do_response_interrupt=0;
	int do_attention_interrupt=0;
	int do_host_interrupt=0;
	int do_command_interrupt=0;
	unsigned long flags = 0;
	TW_Command *command_packet;
	if (test_and_set_bit(TW_IN_INTR, &tw_dev->flags))
		return;
	spin_lock_irqsave(&io_request_lock, flags);

	if (tw_dev->tw_pci_dev->irq == irq) {
		spin_lock(&tw_dev->tw_lock);
		dprintk(KERN_NOTICE "3w-xxxx: tw_interrupt()\n");

		/* Read the registers */
		status_reg_addr = tw_dev->registers.status_reg_addr;
		response_que_addr = tw_dev->registers.response_que_addr;
		status_reg_value = inl(status_reg_addr);

		/* Check which interrupt */
		if (status_reg_value & TW_STATUS_HOST_INTERRUPT)
			do_host_interrupt=1;
		if (status_reg_value & TW_STATUS_ATTENTION_INTERRUPT)
			do_attention_interrupt=1;
		if (status_reg_value & TW_STATUS_COMMAND_INTERRUPT)
			do_command_interrupt=1;
		if (status_reg_value & TW_STATUS_RESPONSE_INTERRUPT)
			do_response_interrupt=1;

		/* Handle host interrupt */
		if (do_host_interrupt) {
			dprintk(KERN_NOTICE "3w-xxxx: tw_interrupt(): Received host interrupt.\n");
			tw_clear_host_interrupt(tw_dev);
		}

		/* Handle attention interrupt */
		if (do_attention_interrupt) {
			dprintk(KERN_NOTICE "3w-xxxx: tw_interrupt(): Received attention interrupt.\n");
			dprintk(KERN_WARNING "3w-xxxx: tw_interrupt(): Clearing attention interrupt.\n");
			tw_clear_attention_interrupt(tw_dev);
			tw_state_request_start(tw_dev, &request_id);
			error = tw_aen_read_queue(tw_dev, request_id);
			if (error) {
				printk(KERN_WARNING "3w-xxxx: scsi%d: Error reading aen queue.\n", tw_dev->host->host_no);
				tw_dev->state[request_id] = TW_S_COMPLETED;
				tw_state_request_finish(tw_dev, request_id);
			}
		}

		/* Handle command interrupt */
		if (do_command_interrupt) {
			/* Drain as many pending commands as we can */
			while (tw_dev->pending_request_count > 0) {
				request_id = tw_dev->pending_queue[tw_dev->pending_head];
				if (tw_dev->state[request_id] != TW_S_PENDING) {
					printk(KERN_WARNING "3w-xxxx: scsi%d: Found request id that wasn't pending.\n", tw_dev->host->host_no);
					break;
				}
				if (tw_post_command_packet(tw_dev, request_id)==0) {
					if (tw_dev->pending_head == TW_Q_LENGTH-1) {
						tw_dev->pending_head = TW_Q_START;
					} else {
						tw_dev->pending_head = tw_dev->pending_head + 1;
					}
					tw_dev->pending_request_count--;
				} else {
					break;
				}
			}
			/* If there are no more pending requests, we mask command interrupt */
			if (tw_dev->pending_request_count == 0) 
				tw_mask_command_interrupt(tw_dev);
		}

		/* Handle response interrupt */
		if (do_response_interrupt) {
			/* Drain the response queue from the board */
			while ((status_reg_value & TW_STATUS_RESPONSE_QUEUE_EMPTY) == 0) {
				response_que.value = inl(response_que_addr);
				request_id = response_que.u.response_id;
				command_packet = (TW_Command *)tw_dev->command_packet_virtual_address[request_id];
				error = 0;
				if (command_packet->status != 0) {
					dprintk(KERN_WARNING "3w-xxxx: tw_interrupt(): Bad response, status = 0x%x, flags = 0x%x, unit = 0x%x.\n", command_packet->status, command_packet->flags, command_packet->byte3.unit);
					tw_decode_error(tw_dev, command_packet->status, command_packet->flags, command_packet->byte3.unit);
					error = 1;
				}
				if (tw_dev->state[request_id] != TW_S_POSTED) {
					printk(KERN_WARNING "3w-xxxx: scsi%d: Received a request id (%d) (opcode = 0x%x) that wasn't posted.\n", tw_dev->host->host_no, request_id, command_packet->byte0.opcode);
					error = 1;
				}
				if (TW_STATUS_ERRORS(status_reg_value)) {
				  tw_decode_bits(tw_dev, status_reg_value);
				  error = 1;
				}
				dprintk(KERN_NOTICE "3w-xxxx: tw_interrupt(): Response queue request id: %d.\n", request_id);
				/* Check for internal command */
				if (tw_dev->srb[request_id] == 0) {
					dprintk(KERN_WARNING "3w-xxxx: tw_interrupt(): Found internally posted command.\n");
					error = tw_aen_complete(tw_dev, request_id);
					if (error) {
						printk(KERN_WARNING "3w-xxxx: scsi%d: Error completing aen.\n", tw_dev->host->host_no);
					}
					status_reg_value = inl(status_reg_addr);
					if (tw_check_bits(status_reg_value)) {
						dprintk(KERN_WARNING "3w-xxxx: tw_interrupt(): Unexpected bits.\n");
						tw_decode_bits(tw_dev, status_reg_value);
					}
		} else {
				switch (tw_dev->srb[request_id]->cmnd[0]) {
					case READ_10:
					case READ_6:
						dprintk(KERN_NOTICE "3w-xxxx: tw_interrupt(): caught READ_10/READ_6\n");
						break;
					case WRITE_10:
					case WRITE_6:
						dprintk(KERN_NOTICE "3w-xxxx: tw_interrupt(): caught WRITE_10/WRITE_6\n");
						break;
					case INQUIRY:
						dprintk(KERN_NOTICE "3w-xxxx: tw_interrupt(): caught INQUIRY\n");
						error = tw_scsiop_inquiry_complete(tw_dev, request_id);
						break;
					case READ_CAPACITY:
						dprintk(KERN_NOTICE "3w-xxxx: tw_interrupt(): caught READ_CAPACITY\n");
						error = tw_scsiop_read_capacity_complete(tw_dev, request_id);
						break;
					case TW_IOCTL:
						dprintk(KERN_NOTICE "3w-xxxx: tw_interrupt(): caught TW_IOCTL\n");
						error = tw_ioctl_complete(tw_dev, request_id);
						break;
					default:
						printk(KERN_WARNING "3w-xxxx: scsi%d: Unknown scsi opcode: 0x%x.\n", tw_dev->host->host_no, tw_dev->srb[request_id]->cmnd[0]);
						tw_dev->srb[request_id]->result = (DID_BAD_TARGET << 16);
						tw_dev->srb[request_id]->scsi_done(tw_dev->srb[request_id]);
					}
					if (error) {
						/* Tell scsi layer there was an error */
						dprintk(KERN_WARNING "3w-xxxx: tw_interrupt(): Scsi Error.\n");
						tw_dev->srb[request_id]->result = (DID_RESET << 16);
					} else {
						/* Tell scsi layer command was a success */
						tw_dev->srb[request_id]->result = (DID_OK << 16);
					}
					tw_dev->state[request_id] = TW_S_COMPLETED;
					tw_state_request_finish(tw_dev, request_id);
					tw_dev->posted_request_count--;
					tw_dev->srb[request_id]->scsi_done(tw_dev->srb[request_id]);
					status_reg_value = inl(status_reg_addr);
					if (tw_check_bits(status_reg_value)) {
						dprintk(KERN_WARNING "3w-xxxx: tw_interrupt(): Unexpected bits.\n");
						tw_decode_bits(tw_dev, status_reg_value);
					}
				}
			}
		}
		spin_unlock(&tw_dev->tw_lock);
	}
	spin_unlock_irqrestore(&io_request_lock, flags);
	clear_bit(TW_IN_INTR, &tw_dev->flags);
}	/* End tw_interrupt() */

/* This function handles ioctls from userspace to the driver */
int tw_ioctl(TW_Device_Extension *tw_dev, int request_id)
{
	unsigned char opcode;
	int bufflen;
	TW_Param *param;
	TW_Command *command_packet;
	u32 param_value;
	TW_Ioctl *ioctl = NULL;
	TW_Passthru *passthru = NULL;
	int tw_aen_code;

	ioctl = (TW_Ioctl *)tw_dev->srb[request_id]->request_buffer;
	if (ioctl == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_ioctl(): Request buffer NULL.\n");
		tw_dev->state[request_id] = TW_S_COMPLETED;
		tw_state_request_finish(tw_dev, request_id);
		tw_dev->srb[request_id]->result = (DID_OK << 16);
		tw_dev->srb[request_id]->scsi_done(tw_dev->srb[request_id]);
		return 0;
	}
	bufflen = tw_dev->srb[request_id]->request_bufflen;

	/* Initialize command packet */
	command_packet = (TW_Command *)tw_dev->command_packet_virtual_address[request_id];
	if (command_packet == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_ioctl(): Bad command packet virtual address.\n");
		tw_dev->state[request_id] = TW_S_COMPLETED;
		tw_state_request_finish(tw_dev, request_id);
		tw_dev->srb[request_id]->result = (DID_OK << 16);
		tw_dev->srb[request_id]->scsi_done(tw_dev->srb[request_id]);
		return 0;
	}
	memset(command_packet, 0, sizeof(TW_Sector));

	/* Initialize param */
	if (tw_dev->alignment_virtual_address[request_id] == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_ioctl(): Bad alignment virtual address.\n");
		tw_dev->state[request_id] = TW_S_COMPLETED;
		tw_state_request_finish(tw_dev, request_id);
		tw_dev->srb[request_id]->result = (DID_OK << 16);
		tw_dev->srb[request_id]->scsi_done(tw_dev->srb[request_id]);
		return 0;
	}
	param = (TW_Param *)tw_dev->alignment_virtual_address[request_id];
	memset(param, 0, sizeof(TW_Sector));

	dprintk(KERN_NOTICE "opcode = %d table_id = %d parameter_id = %d parameter_size_bytes = %d\n", ioctl->opcode, ioctl->table_id, ioctl->parameter_id, ioctl->parameter_size_bytes);
	opcode = ioctl->opcode;

	switch (opcode) {
		case TW_OP_NOP:
			dprintk(KERN_NOTICE "3w-xxxx: tw_ioctl(): caught TW_OP_NOP.\n");
			command_packet->byte0.opcode = TW_OP_NOP;
			break;
		case TW_OP_GET_PARAM:
			dprintk(KERN_NOTICE "3w-xxxx: tw_ioctl(): caught TW_OP_GET_PARAM.\n");
			command_packet->byte0.opcode = TW_OP_GET_PARAM;
			command_packet->byte3.unit = ioctl->unit_index;
			param->table_id = ioctl->table_id;
			param->parameter_id = ioctl->parameter_id;
			param->parameter_size_bytes = ioctl->parameter_size_bytes;
			tw_dev->ioctl_size[request_id] = ioctl->parameter_size_bytes;
			dprintk(KERN_NOTICE "table_id = %d parameter_id = %d parameter_size_bytes %d\n", param->table_id, param->parameter_id, param->parameter_size_bytes);
			break;
		case TW_OP_SET_PARAM:
			dprintk(KERN_NOTICE "3w-xxxx: tw_ioctl(): caught TW_OP_SET_PARAM: table_id = %d, parameter_id = %d, parameter_size_bytes = %d.\n",
			ioctl->table_id, ioctl->parameter_id, ioctl->parameter_size_bytes);
			if (ioctl->data != NULL) {
				command_packet->byte0.opcode = TW_OP_SET_PARAM;
				param->table_id = ioctl->table_id;
				param->parameter_id = ioctl->parameter_id;
				param->parameter_size_bytes = ioctl->parameter_size_bytes;
				memcpy(param->data, ioctl->data, ioctl->parameter_size_bytes);
				break;
			} else {
				printk(KERN_WARNING "3w-xxxx: tw_ioctl(): ioctl->data NULL.\n");
				return 1;
			}
		case TW_OP_AEN_LISTEN:
			dprintk(KERN_NOTICE "3w-xxxx: tw_ioctl(): caught TW_OP_AEN_LISTEN.\n");
			if (tw_dev->aen_head == tw_dev->aen_tail) {
				/* aen queue empty */
				dprintk(KERN_NOTICE "3w-xxxx: tw_ioctl(): Aen queue empty.\n");
				tw_aen_code = TW_AEN_QUEUE_EMPTY;
				memcpy(tw_dev->srb[request_id]->request_buffer, &tw_aen_code, ioctl->parameter_size_bytes);
			} else {
				/* Copy aen queue entry to request buffer */
				dprintk(KERN_NOTICE "3w-xxxx: tw_ioctl(): Returning aen 0x%x\n", tw_dev->aen_queue[tw_dev->aen_head]);
				tw_aen_code = tw_dev->aen_queue[tw_dev->aen_head];
				memcpy(tw_dev->srb[request_id]->request_buffer, &tw_aen_code, ioctl->parameter_size_bytes);
				if (tw_dev->aen_head == TW_Q_LENGTH - 1) {
					tw_dev->aen_head = TW_Q_START;
				} else {
					tw_dev->aen_head = tw_dev->aen_head + 1;
				}
			}
			tw_dev->state[request_id] = TW_S_COMPLETED;
			tw_state_request_finish(tw_dev, request_id);
			tw_dev->srb[request_id]->result = (DID_OK << 16);
			tw_dev->srb[request_id]->scsi_done(tw_dev->srb[request_id]);
			return 0;
		case TW_ATA_PASSTHRU:
			if (ioctl->data != NULL) {
				memcpy(command_packet, ioctl->data, sizeof(TW_Command));
				command_packet->request_id = request_id;
			} else {
				printk(KERN_WARNING "3w-xxxx: tw_ioctl(): ioctl->data NULL.\n");
				return 1;
			}

			passthru = (TW_Passthru *)tw_dev->command_packet_virtual_address[request_id];
			passthru->sg_list[0].length = passthru->sector_count*512;
			if (passthru->sg_list[0].length > TW_MAX_PASSTHRU_BYTES) {
				printk(KERN_WARNING "3w-xxxx: tw_ioctl(): Passthru size (%ld) too big.\n", passthru->sg_list[0].length);
				return 1;
			}
			passthru->sg_list[0].address = virt_to_bus(tw_dev->alignment_virtual_address[request_id]);
			tw_post_command_packet(tw_dev, request_id);
			return 0;
		case TW_CMD_PACKET:
			dprintk(KERN_WARNING "3w-xxxx: tw_ioctl(): caught TW_CMD_PACKET.\n");
			if (ioctl->data != NULL) {
				memcpy(command_packet, ioctl->data, sizeof(TW_Command));
				command_packet->request_id = request_id;
				tw_post_command_packet(tw_dev, request_id);
				return 0;
			} else {
				printk(KERN_WARNING "3w-xxxx: tw_ioctl(): ioctl->data NULL.\n");
				return 1;
			}
		default:
			printk(KERN_WARNING "3w-xxxx: Unknown ioctl 0x%x.\n", opcode);
			tw_dev->state[request_id] = TW_S_COMPLETED;
			tw_state_request_finish(tw_dev, request_id);
			tw_dev->srb[request_id]->result = (DID_OK << 16);
			tw_dev->srb[request_id]->scsi_done(tw_dev->srb[request_id]);
			return 0;
	}

	param_value = tw_dev->alignment_physical_address[request_id];
	if (param_value == 0) {
		printk(KERN_WARNING "3w-xxxx: tw_ioctl(): Bad alignment physical address.\n");
		tw_dev->state[request_id] = TW_S_COMPLETED;
		tw_state_request_finish(tw_dev, request_id);
		tw_dev->srb[request_id]->result = (DID_OK << 16);
		tw_dev->srb[request_id]->scsi_done(tw_dev->srb[request_id]);
	}

	command_packet->byte8.param.sgl[0].address = param_value;
	command_packet->byte8.param.sgl[0].length = sizeof(TW_Sector);

	command_packet->byte0.sgl_offset = 2;
	command_packet->size = 4;
	command_packet->request_id = request_id;
	command_packet->byte3.host_id = 0;
	command_packet->status = 0;
	command_packet->flags = 0;
	command_packet->byte6.parameter_count = 1;

	/* Now try to post the command to the board */
	tw_post_command_packet(tw_dev, request_id);

	return 0;
} /* End tw_ioctl() */

/* This function is called by the isr to complete ioctl requests */
int tw_ioctl_complete(TW_Device_Extension *tw_dev, int request_id)
{
	unsigned char *param_data;
	unsigned char *buff;
	TW_Param *param;
	TW_Ioctl *ioctl = NULL;
	TW_Passthru *passthru = NULL;

	ioctl = (TW_Ioctl *)tw_dev->srb[request_id]->request_buffer;
	dprintk(KERN_NOTICE "3w-xxxx: tw_ioctl_complete()\n");
	buff = tw_dev->srb[request_id]->request_buffer;
	if (buff == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_ioctl_complete(): Request buffer NULL.\n");
		return 1;
	}
	dprintk(KERN_NOTICE "3w-xxxx: tw_ioctl_complete(): Request_bufflen = %d\n", tw_dev->srb[request_id]->request_bufflen);

	ioctl = (TW_Ioctl *)buff;
	switch (ioctl->opcode) {
		case TW_ATA_PASSTHRU:
			passthru = (TW_Passthru *)ioctl->data;
			memcpy(buff, tw_dev->alignment_virtual_address[request_id], passthru->sector_count * 512);
			break;
		default:
			memset(buff, 0, tw_dev->srb[request_id]->request_bufflen);
			param = (TW_Param *)tw_dev->alignment_virtual_address[request_id];
			if (param == NULL) {
				printk(KERN_WARNING "3w-xxxx: tw_ioctl_complete(): Bad alignment virtual address.\n");
				return 1;
			}
			param_data = &(param->data[0]);
			memcpy(buff, param_data, tw_dev->ioctl_size[request_id]);
	}
	return 0;
} /* End tw_ioctl_complete() */

/* This function will mask the command interrupt */
void tw_mask_command_interrupt(TW_Device_Extension *tw_dev)
{
	u32 control_reg_addr, control_reg_value;
	
	control_reg_addr = tw_dev->registers.control_reg_addr;
	control_reg_value = TW_CONTROL_MASK_COMMAND_INTERRUPT;
	outl(control_reg_value, control_reg_addr);
} /* End tw_mask_command_interrupt() */

/* This function will poll the status register for a flag */
int tw_poll_status(TW_Device_Extension *tw_dev, u32 flag, int seconds)
{
	u32 status_reg_addr, status_reg_value;
	struct timeval before, timeout;

	status_reg_addr = tw_dev->registers.status_reg_addr;
	do_gettimeofday(&before);
	status_reg_value = inl(status_reg_addr);

	while ((status_reg_value & flag) != flag) {
		status_reg_value = inl(status_reg_addr);
		do_gettimeofday(&timeout);
		if (before.tv_sec + seconds < timeout.tv_sec) { 
			dprintk(KERN_WARNING "3w-xxxx: tw_poll_status(): Flag 0x%x not found.\n", flag);
			return 1;
		}
		mdelay(1);
	}
	return 0;
} /* End tw_poll_status() */

/* This function will attempt to post a command packet to the board */
int tw_post_command_packet(TW_Device_Extension *tw_dev, int request_id)
{
	u32 status_reg_addr, status_reg_value;
	u32 command_que_addr, command_que_value;

	dprintk(KERN_NOTICE "3w-xxxx: tw_post_command_packet()\n");
	command_que_addr = tw_dev->registers.command_que_addr;
	command_que_value = tw_dev->command_packet_physical_address[request_id];
	status_reg_addr = tw_dev->registers.status_reg_addr;
	status_reg_value = inl(status_reg_addr);

	if (tw_check_bits(status_reg_value)) {
		dprintk(KERN_WARNING "3w-xxxx: tw_post_command_packet(): Unexpected bits.\n");
		tw_decode_bits(tw_dev, status_reg_value);
	}

	if ((status_reg_value & TW_STATUS_COMMAND_QUEUE_FULL) == 0) {
		/* We successfully posted the command packet */
		outl(command_que_value, command_que_addr);
		tw_dev->state[request_id] = TW_S_POSTED;
		tw_dev->posted_request_count++;
		if (tw_dev->posted_request_count > tw_dev->max_posted_request_count) {
			tw_dev->max_posted_request_count = tw_dev->posted_request_count;
		}
	} else {
		/* Couldn't post the command packet, so we do it in the isr */
		if (tw_dev->state[request_id] != TW_S_PENDING) {
			tw_dev->state[request_id] = TW_S_PENDING;
			tw_dev->pending_request_count++;
			if (tw_dev->pending_request_count > tw_dev->max_pending_request_count) {
				tw_dev->max_pending_request_count = tw_dev->pending_request_count;
			}
			tw_dev->pending_queue[tw_dev->pending_tail] = request_id;
			if (tw_dev->pending_tail == TW_Q_LENGTH-1) {
				tw_dev->pending_tail = TW_Q_START;
			} else {
				tw_dev->pending_tail = tw_dev->pending_tail + 1;
			}
		} 
		tw_unmask_command_interrupt(tw_dev);
		return 1;
	}
	return 0;
} /* End tw_post_command_packet() */

/* This function will reset a device extension */
int tw_reset_device_extension(TW_Device_Extension *tw_dev) 
{
	int imax = 0;
	int i = 0;
	Scsi_Cmnd *srb;

	dprintk(KERN_NOTICE "3w-xxxx: tw_reset_device_extension()\n");
	imax = TW_Q_LENGTH;

	if (tw_reset_sequence(tw_dev)) {
		printk(KERN_WARNING "3w-xxxx: scsi%d: Reset sequence failed.\n", tw_dev->host->host_no);
		return 1;
	}

	/* Abort all requests that are in progress */
	for (i=0;i<imax;i++) {
		if ((tw_dev->state[i] != TW_S_FINISHED) && 
		    (tw_dev->state[i] != TW_S_INITIAL) &&
		    (tw_dev->state[i] != TW_S_COMPLETED)) {
			srb = tw_dev->srb[i];
			if (srb != NULL) {
				srb = tw_dev->srb[i];
				srb->result = (DID_RESET << 16);
				tw_dev->srb[i]->scsi_done(tw_dev->srb[i]);
			}
		}
	}

	/* Reset queues and counts */
	for (i=0;i<imax;i++) {
		tw_dev->free_queue[i] = i;
		tw_dev->state[i] = TW_S_INITIAL;
	}
	tw_dev->free_head = TW_Q_START;
	tw_dev->free_tail = TW_Q_LENGTH - 1;
	tw_dev->posted_request_count = 0;
	tw_dev->pending_request_count = 0;
	tw_dev->pending_head = TW_Q_START;
	tw_dev->pending_tail = TW_Q_START;

	return 0;
} /* End tw_reset_device_extension() */

/* This function will reset a controller */
int tw_reset_sequence(TW_Device_Extension *tw_dev) 
{
	int error = 0;
	int tries = 0;

	/* Disable interrupts */
	tw_disable_interrupts(tw_dev);

	/* Reset the board */
	while (tries < TW_MAX_RESET_TRIES) {
		tw_soft_reset(tw_dev);

		error = tw_aen_drain_queue(tw_dev);
		if (error) {
			printk(KERN_WARNING "3w-xxxx: scsi%d: Card not responding, retrying.\n", tw_dev->host->host_no);
			tries++;
			continue;
		}

		/* Check for controller errors */
		if (tw_check_errors(tw_dev)) {
			printk(KERN_WARNING "3w-xxxx: scsi%d: Controller errors found, retrying.\n", tw_dev->host->host_no);
			tries++;
			continue;
		}

		/* Empty the response queue again */
		error = tw_empty_response_que(tw_dev);
		if (error) {
			printk(KERN_WARNING "3w-xxxx: scsi%d: Couldn't empty response queue, retrying.\n", tw_dev->host->host_no);
			tries++;
			continue;
		}

		/* Now the controller is in a good state */
		break;
	}

	if (tries >= TW_MAX_RESET_TRIES) {
		printk(KERN_WARNING "3w-xxxx: scsi%d: Controller errors, card not responding, check all cabling.\n", tw_dev->host->host_no);
		return 1;
	}

	error = tw_initconnection(tw_dev, TW_INIT_MESSAGE_CREDITS);
	if (error) {
		printk(KERN_WARNING "3w-xxxx: scsi%d: Connection initialization failed.\n", tw_dev->host->host_no);
		return 1;
	}

	/* Re-enable interrupts */
	tw_enable_interrupts(tw_dev);

	return 0;
} /* End tw_reset_sequence() */

/* This funciton returns unit geometry in cylinders/heads/sectors */
int tw_scsi_biosparam(Disk *disk, kdev_t dev, int geom[]) 
{
	int heads, sectors, cylinders;
	TW_Device_Extension *tw_dev;
	
	dprintk(KERN_NOTICE "3w-xxxx: tw_scsi_biosparam()\n");
	tw_dev = (TW_Device_Extension *)disk->device->host->hostdata;

	heads = 64;
	sectors = 32;
	cylinders = disk->capacity / (heads * sectors);

	if (disk->capacity >= 0x200000) {
		heads = 255;
		sectors = 63;
		cylinders = disk->capacity / (heads * sectors);
	}

	dprintk(KERN_NOTICE "3w-xxxx: tw_scsi_biosparam(): heads = %d, sectors = %d, cylinders = %d\n", heads, sectors, cylinders);
	geom[0] = heads;			 
	geom[1] = sectors;
	geom[2] = cylinders;

	return 0;
} /* End tw_scsi_biosparam() */

/* This function will find and initialize any cards */
int tw_scsi_detect(Scsi_Host_Template *tw_host)
{
	int ret;
	
	dprintk(KERN_NOTICE "3w-xxxx: tw_scsi_detect()\n");

	printk(KERN_WARNING "3ware Storage Controller device driver for Linux v%s.\n", tw_driver_version);

	/* Check if the kernel has PCI interface compiled in */
	if (!pci_present()) {
		printk(KERN_WARNING "3w-xxxx: tw_scsi_detect(): No pci interface present.\n");
		return 0;
	}

	spin_unlock_irq(&io_request_lock);
	ret = tw_findcards(tw_host);
	spin_lock_irq(&io_request_lock);

	return ret;
} /* End tw_scsi_detect() */

/* This is the new scsi eh abort function */
int tw_scsi_eh_abort(Scsi_Cmnd *SCpnt) 
{
	TW_Device_Extension *tw_dev=NULL;
	int i = 0;

	dprintk(KERN_NOTICE "3w-xxxx: tw_scsi_eh_abort()\n");

	if (!SCpnt) {
		printk(KERN_WARNING "3w-xxxx: tw_scsi_eh_abort(): Invalid Scsi_Cmnd.\n");
		return (FAILED);
	}

	tw_dev = (TW_Device_Extension *)SCpnt->host->hostdata;
	if (tw_dev == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_scsi_eh_abort(): Invalid device extension.\n");
		return (FAILED);
	}

	/* We have to let AEN requests through before the reset */
	spin_unlock_irq(&io_request_lock);
	mdelay(TW_AEN_WAIT_TIME);
	spin_lock_irq(&io_request_lock);

	spin_lock(&tw_dev->tw_lock);
	tw_dev->num_aborts++;

	/* If the command hasn't been posted yet, we can do the abort */
	for (i=0;i<TW_Q_LENGTH;i++) {
		if (tw_dev->srb[i] == SCpnt) {
			if (tw_dev->state[i] == TW_S_STARTED) {
				printk(KERN_WARNING "3w-xxxx: scsi%d: Command (0x%x) timed out.\n", tw_dev->host->host_no, (u32)SCpnt);
				tw_dev->state[i] = TW_S_COMPLETED;
				tw_state_request_finish(tw_dev, i);
				spin_unlock(&tw_dev->tw_lock);
				return (SUCCESS);
			}
			if (tw_dev->state[i] == TW_S_PENDING) {
				printk(KERN_WARNING "3w-xxxx: scsi%d: Command (0x%x) timed out.\n", tw_dev->host->host_no, (u32)SCpnt);
				if (tw_dev->pending_head == TW_Q_LENGTH-1) {
					tw_dev->pending_head = TW_Q_START;
				} else {
					tw_dev->pending_head = tw_dev->pending_head + 1;
				}
				tw_dev->pending_request_count--;
				tw_dev->state[i] = TW_S_COMPLETED;
				tw_state_request_finish(tw_dev, i);
				spin_unlock(&tw_dev->tw_lock);
				return (SUCCESS);
			}
		}
	}

	/* If the command has already been posted, we have to reset the card */
	printk(KERN_WARNING "3w-xxxx: scsi%d: Command (0x%x) timed out, resetting card.\n", tw_dev->host->host_no, (u32)SCpnt);
	if (tw_reset_device_extension(tw_dev)) {
		dprintk(KERN_WARNING "3w-xxxx: tw_scsi_eh_abort(): Reset failed for card %d.\n", tw_dev->host->host_no);
		spin_unlock(&tw_dev->tw_lock);
		return (FAILED);
	}
	spin_unlock(&tw_dev->tw_lock);

	return (SUCCESS);
} /* End tw_scsi_eh_abort() */

/* This is the new scsi eh reset function */
int tw_scsi_eh_reset(Scsi_Cmnd *SCpnt) 
{
	TW_Device_Extension *tw_dev=NULL;

	dprintk(KERN_NOTICE "3w-xxxx: tw_scsi_eh_reset()\n");

	if (!SCpnt) {
		printk(KERN_WARNING "3w-xxxx: tw_scsi_eh_reset(): Invalid Scsi_Cmnd.\n");
		return (FAILED);
	}

	tw_dev = (TW_Device_Extension *)SCpnt->host->hostdata;
	if (tw_dev == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_scsi_eh_reset(): Invalid device extension.\n");
		return (FAILED);
	}

	/* We have to let AEN requests through before the reset */
	spin_unlock_irq(&io_request_lock);
	mdelay(TW_AEN_WAIT_TIME);
	spin_lock_irq(&io_request_lock);

	spin_lock(&tw_dev->tw_lock);
	tw_dev->num_resets++;

	/* Now reset the card and some of the device extension data */
	if (tw_reset_device_extension(tw_dev)) {
		printk(KERN_WARNING "3w-xxxx: scsi%d: Reset failed.\n", tw_dev->host->host_no);
		spin_unlock(&tw_dev->tw_lock);
		return (FAILED);
	}
	printk(KERN_WARNING "3w-xxxx: scsi%d: Reset succeeded.\n", tw_dev->host->host_no);
	spin_unlock(&tw_dev->tw_lock);

	return (SUCCESS);
} /* End tw_scsi_eh_reset() */

/* This function handles input and output from /proc/scsi/3w-xxxx/x */
int tw_scsi_proc_info(char *buffer, char **start, off_t offset, int length, int hostno, int inout) 
{
	TW_Device_Extension *tw_dev = NULL;
	TW_Info info;
	int i;
	int j;

	dprintk(KERN_NOTICE "3w-xxxx: tw_scsi_proc_info()\n");

	/* Find the correct device extension */
	for (i=0;i<tw_device_extension_count;i++) 
		if (tw_device_extension_list[i]->host->host_no == hostno) 
			tw_dev = tw_device_extension_list[i];
	if (tw_dev == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_scsi_proc_info(): Couldn't locate device extension.\n");
		return (-EINVAL);
	}

	info.buffer = buffer;
	info.length = length;
	info.offset = offset;
	info.position = 0;
	
	if (inout) {
		/* Write */
		if (strncmp(buffer, "debug", 5) == 0) {
			printk(KERN_INFO "3w-xxxx: Posted commands:\n");
			for (j=0;j<TW_Q_LENGTH;j++) {
				if (tw_dev->state[j] == TW_S_POSTED) {
					TW_Command *command = (TW_Command *)tw_dev->command_packet_virtual_address[j];
					printk(KERN_INFO "3w-xxxx: Request_id: %d\n", j);
					printk(KERN_INFO "Opcode: 0x%x\n", command->byte0.opcode);
					printk(KERN_INFO "Block_count: 0x%x\n", command->byte6.block_count);
					printk(KERN_INFO "LBA: 0x%x\n", (u32)command->byte8.io.lba);
					printk(KERN_INFO "Physical command packet addr: 0x%x\n", tw_dev->command_packet_physical_address[j]);
					printk(KERN_INFO "Scsi_Cmnd: 0x%x\n", (u32)tw_dev->srb[j]);
				}
			}
			printk(KERN_INFO "3w-xxxx: Free_head: %3d\n", tw_dev->free_head);
			printk(KERN_INFO "3w-xxxx: Free_tail: %3d\n", tw_dev->free_tail);
		} 
		return length;
	} else {
		/* Read */
		if (start) {
			*start = buffer;
		}
		tw_copy_info(&info, "scsi%d: 3ware Storage Controller\n", hostno);
		tw_copy_info(&info, "Driver version: %s\n", tw_driver_version);
		tw_copy_info(&info, "Current commands posted:       %3d\n", tw_dev->posted_request_count);
		tw_copy_info(&info, "Max commands posted:           %3d\n", tw_dev->max_posted_request_count);
		tw_copy_info(&info, "Current pending commands:      %3d\n", tw_dev->pending_request_count);
		tw_copy_info(&info, "Max pending commands:          %3d\n", tw_dev->max_pending_request_count);
		tw_copy_info(&info, "Last sgl length:               %3d\n", tw_dev->sgl_entries);
		tw_copy_info(&info, "Max sgl length:                %3d\n", tw_dev->max_sgl_entries);
		tw_copy_info(&info, "Last sector count:             %3d\n", tw_dev->sector_count);
		tw_copy_info(&info, "Max sector count:              %3d\n", tw_dev->max_sector_count);
		tw_copy_info(&info, "Resets:                        %3d\n", tw_dev->num_resets);
		tw_copy_info(&info, "Aborts:                        %3d\n", tw_dev->num_aborts);
		tw_copy_info(&info, "AEN's:                         %3d\n", tw_dev->aen_count);
	}
	if (info.position > info.offset) {
		return (info.position - info.offset);
	} else { 
		return 0;
	}
} /* End tw_scsi_proc_info() */

/* This is the main scsi queue function to handle scsi opcodes */
int tw_scsi_queue(Scsi_Cmnd *SCpnt, void (*done)(Scsi_Cmnd *)) 
{
	unsigned char *command = SCpnt->cmnd;
	int request_id = 0;
	int error = 0;
	unsigned long flags = 0;
	TW_Device_Extension *tw_dev = (TW_Device_Extension *)SCpnt->host->hostdata;

	if (tw_dev == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_scsi_queue(): Invalid device extension.\n");
		SCpnt->result = (DID_ERROR << 16);
		done(SCpnt);
		return 0;
	}

	spin_lock_irqsave(&tw_dev->tw_lock, flags);
	dprintk(KERN_NOTICE "3w-xxxx: tw_scsi_queue()\n");

	/* Skip scsi command if it isn't for us */
	if ((tw_dev->is_unit_present[SCpnt->target] == FALSE) || (SCpnt->lun != 0)) {
		SCpnt->result = (DID_BAD_TARGET << 16);
		done(SCpnt);
		spin_unlock_irqrestore(&tw_dev->tw_lock, flags);
		return 0;
	}
	
	/* Save done function into Scsi_Cmnd struct */
	SCpnt->scsi_done = done;
		 
	/* Queue the command and get a request id */
	tw_state_request_start(tw_dev, &request_id);

	/* Save the scsi command for use by the ISR */
	tw_dev->srb[request_id] = SCpnt;

	switch (*command) {
		case READ_10:
		case READ_6:
		case WRITE_10:
		case WRITE_6:
			dprintk(KERN_NOTICE "3w-xxxx: tw_scsi_queue(): caught READ/WRITE.\n");
			error = tw_scsiop_read_write(tw_dev, request_id);
			break;
		case TEST_UNIT_READY:
			dprintk(KERN_NOTICE "3w-xxxx: tw_scsi_queue(): caught TEST_UNIT_READY.\n");
			error = tw_scsiop_test_unit_ready(tw_dev, request_id);
			break;
		case INQUIRY:
			dprintk(KERN_NOTICE "3w-xxxx: tw_scsi_queue(): caught INQUIRY.\n");
			error = tw_scsiop_inquiry(tw_dev, request_id);
			break;
		case READ_CAPACITY:
			dprintk(KERN_NOTICE "3w-xxxx: tw_scsi_queue(): caught READ_CAPACITY.\n");
			error = tw_scsiop_read_capacity(tw_dev, request_id);
			break;
	        case REQUEST_SENSE:
		        dprintk(KERN_NOTICE "3w-xxxx: tw_scsi_queue(): caught REQUEST_SENSE.\n");
		        error = tw_scsiop_request_sense(tw_dev, request_id);
		        break;
		case TW_IOCTL:
			dprintk(KERN_NOTICE "3w-xxxx: tw_scsi_queue(): caught TW_SCSI_IOCTL.\n");
			error = tw_ioctl(tw_dev, request_id);
			break;
		default:
			printk(KERN_NOTICE "3w-xxxx: scsi%d: Unknown scsi opcode: 0x%x\n", tw_dev->host->host_no, *command);
			tw_dev->state[request_id] = TW_S_COMPLETED;
			tw_state_request_finish(tw_dev, request_id);
			SCpnt->result = (DID_BAD_TARGET << 16);
			done(SCpnt);
	}
	if (error) {
		tw_dev->state[request_id] = TW_S_COMPLETED;
		tw_state_request_finish(tw_dev, request_id);
		SCpnt->result = (DID_ERROR << 16);
		done(SCpnt);
	}
	spin_unlock_irqrestore(&tw_dev->tw_lock, flags);

	return 0;
} /* End tw_scsi_queue() */

/* This function will release the resources on an rmmod call */
int tw_scsi_release(struct Scsi_Host *tw_host) 
{
	TW_Device_Extension *tw_dev;
	tw_dev = (TW_Device_Extension *)tw_host->hostdata;

	dprintk(KERN_NOTICE "3w-xxxx: tw_scsi_release()\n");

	/* Free up the IO region */
	release_region((tw_dev->tw_pci_dev->resource[0].start), TW_IO_ADDRESS_RANGE);

	/* Free up the IRQ */
	free_irq(tw_dev->tw_pci_dev->irq, tw_dev);

	/* Free up device extension resources */
	tw_free_device_extension(tw_dev);

	/* Tell kernel scsi-layer we are gone */
	scsi_unregister(tw_host);
	
	/* Fake like we just shut down, so notify the card that
	 * we "shut down cleanly".
	 */
	tw_halt(0, 0, 0);  // parameters aren't actually used

	return 0;
} /* End tw_scsi_release() */

/* This function handles scsi inquiry commands */
int tw_scsiop_inquiry(TW_Device_Extension *tw_dev, int request_id)
{
	TW_Param *param;
	TW_Command *command_packet;
	u32 command_que_value, command_que_addr;
	u32 param_value;

	dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_inquiry()\n");

	/* Initialize command packet */
	command_que_addr = tw_dev->registers.command_que_addr;
	command_packet = (TW_Command *)tw_dev->command_packet_virtual_address[request_id];
	if (command_packet == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_scsiop_inquiry(): Bad command packet virtual address.\n");
		return 1;
	}
	memset(command_packet, 0, sizeof(TW_Sector));
	command_packet->byte0.opcode = TW_OP_GET_PARAM;
	command_packet->byte0.sgl_offset = 2;
	command_packet->size = 4;
	command_packet->request_id = request_id;
	command_packet->byte3.unit = 0;
	command_packet->byte3.host_id = 0;
	command_packet->status = 0;
	command_packet->flags = 0;
	command_packet->byte6.parameter_count = 1;

	/* Now setup the param */
	if (tw_dev->alignment_virtual_address[request_id] == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_scsiop_inquiry(): Bad alignment virtual address.\n");
		return 1;
	}
	param = (TW_Param *)tw_dev->alignment_virtual_address[request_id];
	memset(param, 0, sizeof(TW_Sector));
	param->table_id = 3;	 /* unit summary table */
	param->parameter_id = 3; /* unitsstatus parameter */
	param->parameter_size_bytes = TW_MAX_UNITS;
	param_value = tw_dev->alignment_physical_address[request_id];
	if (param_value == 0) {
		printk(KERN_WARNING "3w-xxxx: tw_scsiop_inquiry(): Bad alignment physical address.\n");
		return 1;
	}

	command_packet->byte8.param.sgl[0].address = param_value;
	command_packet->byte8.param.sgl[0].length = sizeof(TW_Sector);
	command_que_value = tw_dev->command_packet_physical_address[request_id];
	if (command_que_value == 0) {
		printk(KERN_WARNING "3w-xxxx: tw_scsiop_inquiry(): Bad command packet physical address.\n");
		return 1;
	}

	/* Now try to post the command packet */
	tw_post_command_packet(tw_dev, request_id);

	return 0;
} /* End tw_scsiop_inquiry() */

/* This function is called by the isr to complete an inquiry command */
int tw_scsiop_inquiry_complete(TW_Device_Extension *tw_dev, int request_id)
{
	unsigned char *is_unit_present;
	unsigned char *request_buffer;
	int i;
	TW_Param *param;

	dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_inquiry_complete()\n");

	/* Fill request buffer */
	if (tw_dev->srb[request_id]->request_buffer == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_scsiop_inquiry_complete(): Request buffer NULL.\n");
		return 1;
	}
	request_buffer = tw_dev->srb[request_id]->request_buffer;
	memset(request_buffer, 0, tw_dev->srb[request_id]->request_bufflen);
	request_buffer[0] = TYPE_DISK;									 /* Peripheral device type */
	request_buffer[1] = 0;													 /* Device type modifier */
	request_buffer[2] = 0;													 /* No ansi/iso compliance */
	request_buffer[4] = 31;													/* Additional length */
	memcpy(&request_buffer[8], "3ware   ", 8);	 /* Vendor ID */
	memcpy(&request_buffer[16], "3w-xxxx         ", 16); /* Product ID */
	memcpy(&request_buffer[32], tw_driver_version, 3);

	param = (TW_Param *)tw_dev->alignment_virtual_address[request_id];
	if (param == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_scsiop_inquiry_complete(): Bad alignment virtual address.\n");
		return 1;
	}
	is_unit_present = &(param->data[0]);

	for (i=0 ; i<TW_MAX_UNITS; i++) {
		if (is_unit_present[i] == 0) {
			tw_dev->is_unit_present[i] = FALSE;
		} else {
		  if (is_unit_present[i] & TW_UNIT_ONLINE) {
			tw_dev->is_unit_present[i] = TRUE;
			dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_inquiry_complete: Unit %d found.\n", i);
		  }
		}
	}

	return 0;
} /* End tw_scsiop_inquiry_complete() */

/* This function handles scsi read_capacity commands */
int tw_scsiop_read_capacity(TW_Device_Extension *tw_dev, int request_id) 
{
	TW_Param *param;
	TW_Command *command_packet;
	u32 command_que_addr, command_que_value;
	u32 param_value;

	dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_read_capacity()\n");

	/* Initialize command packet */
	command_que_addr = tw_dev->registers.command_que_addr;
	command_packet = (TW_Command *)tw_dev->command_packet_virtual_address[request_id];

	if (command_packet == NULL) {
		dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_read_capacity(): Bad command packet virtual address.\n");
		return 1;
	}
	memset(command_packet, 0, sizeof(TW_Sector));
	command_packet->byte0.opcode = TW_OP_GET_PARAM;
	command_packet->byte0.sgl_offset = 2;
	command_packet->size = 4;
	command_packet->request_id = request_id;
	command_packet->byte3.unit = tw_dev->srb[request_id]->target;
	command_packet->byte3.host_id = 0;
	command_packet->status = 0;
	command_packet->flags = 0;
	command_packet->byte6.block_count = 1;

	/* Now setup the param */
	if (tw_dev->alignment_virtual_address[request_id] == NULL) {
		dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_read_capacity(): Bad alignment virtual address.\n");
		return 1;
	}
	param = (TW_Param *)tw_dev->alignment_virtual_address[request_id];
	memset(param, 0, sizeof(TW_Sector));
	param->table_id = TW_UNIT_INFORMATION_TABLE_BASE + 
	tw_dev->srb[request_id]->target;
	param->parameter_id = 4;	/* unitcapacity parameter */
	param->parameter_size_bytes = 4;
	param_value = tw_dev->alignment_physical_address[request_id];
	if (param_value == 0) {
		dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_read_capacity(): Bad alignment physical address.\n");
		return 1;
	}
  
	command_packet->byte8.param.sgl[0].address = param_value;
	command_packet->byte8.param.sgl[0].length = sizeof(TW_Sector);
	command_que_value = tw_dev->command_packet_physical_address[request_id];
	if (command_que_value == 0) {
		dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_read_capacity(): Bad command packet physical address.\n");
		return 1;
	}

	/* Now try to post the command to the board */
	tw_post_command_packet(tw_dev, request_id);
  
	return 0;
} /* End tw_scsiop_read_capacity() */

/* This function is called by the isr to complete a readcapacity command */
int tw_scsiop_read_capacity_complete(TW_Device_Extension *tw_dev, int request_id)
{
	unsigned char *param_data;
	u32 capacity;
	char *buff;
	TW_Param *param;

	dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_read_capacity_complete()\n");

	buff = tw_dev->srb[request_id]->request_buffer;
	if (buff == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_scsiop_read_capacity_complete(): Request buffer NULL.\n");
		return 1;
	}
	memset(buff, 0, tw_dev->srb[request_id]->request_bufflen);
	param = (TW_Param *)tw_dev->alignment_virtual_address[request_id];
	if (param == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_scsiop_read_capacity_complete(): Bad alignment virtual address.\n");
		return 1;
	}
	param_data = &(param->data[0]);

	capacity = (param_data[3] << 24) | (param_data[2] << 16) | 
		   (param_data[1] << 8) | param_data[0];

	dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_read_capacity_complete(): Capacity = 0x%x.\n", capacity);

	/* Number of LBA's */
	buff[0] = (capacity >> 24);
	buff[1] = (capacity >> 16) & 0xff;
	buff[2] = (capacity >> 8) & 0xff;
	buff[3] = capacity & 0xff;

	/* Block size in bytes (512) */
	buff[4] = (TW_BLOCK_SIZE >> 24);
	buff[5] = (TW_BLOCK_SIZE >> 16) & 0xff;
	buff[6] = (TW_BLOCK_SIZE >> 8) & 0xff;
	buff[7] = TW_BLOCK_SIZE & 0xff;

	return 0;
} /* End tw_scsiop_read_capacity_complete() */

/* This function handles scsi read or write commands */
int tw_scsiop_read_write(TW_Device_Extension *tw_dev, int request_id) 
{
	TW_Command *command_packet;
	u32 command_que_addr, command_que_value = 0;
	u32 lba = 0x0, num_sectors = 0x0;
	int i, count = 0;
	Scsi_Cmnd *srb;
	struct scatterlist *sglist;

	dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_read_write()\n");

	if (tw_dev->srb[request_id]->request_buffer == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_scsiop_read_write(): Request buffer NULL.\n");
		return 1;
	}
	sglist = (struct scatterlist *)tw_dev->srb[request_id]->request_buffer;
	srb = tw_dev->srb[request_id];

	/* Initialize command packet */
	command_que_addr = tw_dev->registers.command_que_addr;
	command_packet = (TW_Command *)tw_dev->command_packet_virtual_address[request_id];
	if (command_packet == NULL) {
		dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_read_write(): Bad command packet virtual address.\n");
		return 1;
	}

	if (srb->cmnd[0] == READ_6 || srb->cmnd[0] == READ_10) {
		command_packet->byte0.opcode = TW_OP_READ;
	} else {
		command_packet->byte0.opcode = TW_OP_WRITE;
	}

	command_packet->byte0.sgl_offset = 3;
	command_packet->size = 5;
	command_packet->request_id = request_id;
	command_packet->byte3.unit = srb->target;
	command_packet->byte3.host_id = 0;
	command_packet->status = 0;
	command_packet->flags = 0;

	if (srb->cmnd[0] == WRITE_10) {
		if ((srb->cmnd[1] & 0x8) || (srb->cmnd[1] & 0x10))
			command_packet->flags = 1;
	}

	if (srb->cmnd[0] == READ_6 || srb->cmnd[0] == WRITE_6) {
		lba = ((u32)srb->cmnd[1] << 16) | ((u32)srb->cmnd[2] << 8) | (u32)srb->cmnd[3];
		num_sectors = (u32)srb->cmnd[4];
	} else {
		lba = ((u32)srb->cmnd[2] << 24) | ((u32)srb->cmnd[3] << 16) | ((u32)srb->cmnd[4] << 8) | (u32)srb->cmnd[5];
		num_sectors = (u32)srb->cmnd[8] | ((u32)srb->cmnd[7] << 8);
	}
  
	/* Update sector statistic */
	tw_dev->sector_count = num_sectors;
	if (tw_dev->sector_count > tw_dev->max_sector_count)
		tw_dev->max_sector_count = tw_dev->sector_count;
  
	dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_read_write(): lba = 0x%x num_sectors = 0x%x\n", lba, num_sectors);
	command_packet->byte8.io.lba = lba;
	command_packet->byte6.block_count = num_sectors;

	if ((tw_dev->is_raid_five[tw_dev->srb[request_id]->target] == 0) || (srb->cmnd[0] == READ_6) || (srb->cmnd[0] == READ_10) || (tw_dev->tw_pci_dev->device == TW_DEVICE_ID2)) {
		/* Do this if there are no sg list entries */
		if (tw_dev->srb[request_id]->use_sg == 0) {    
			dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_read_write(): SG = 0\n");
			command_packet->byte8.io.sgl[0].address = virt_to_bus(tw_dev->srb[request_id]->request_buffer);
			command_packet->byte8.io.sgl[0].length = tw_dev->srb[request_id]->request_bufflen;
		}

		/* Do this if we have multiple sg list entries */
		if (tw_dev->srb[request_id]->use_sg > 0) {
			for (i=0;i<tw_dev->srb[request_id]->use_sg; i++) {
				command_packet->byte8.io.sgl[i].address = virt_to_bus(sglist[i].address);
				command_packet->byte8.io.sgl[i].length = sglist[i].length;
				command_packet->size+=2;
			}
			if (tw_dev->srb[request_id]->use_sg >= 1)
				command_packet->size-=2;
		}
	} else {
                /* Do this if there are no sg list entries for raid 5 */
                if (tw_dev->srb[request_id]->use_sg == 0) {
			dprintk(KERN_WARNING "doing raid 5 write use_sg = 0, bounce_buffer[%d] = 0x%p\n", request_id, tw_dev->bounce_buffer[request_id]);
			memcpy(tw_dev->bounce_buffer[request_id], tw_dev->srb[request_id]->request_buffer, tw_dev->srb[request_id]->request_bufflen);
			command_packet->byte8.io.sgl[0].address = virt_to_bus(tw_dev->bounce_buffer[request_id]);
			command_packet->byte8.io.sgl[0].length = tw_dev->srb[request_id]->request_bufflen;
                }

                /* Do this if we have multiple sg list entries for raid 5 */
                if (tw_dev->srb[request_id]->use_sg > 0) {
                        dprintk(KERN_WARNING "doing raid 5 write use_sg = %d, sglist[0].length = %d\n", tw_dev->srb[request_id]->use_sg, sglist[0].length);
                        for (i=0;i<tw_dev->srb[request_id]->use_sg; i++) {
                                memcpy((char *)(tw_dev->bounce_buffer[request_id])+count, sglist[i].address, sglist[i].length);
				count+=sglist[i].length;
                        }
                        command_packet->byte8.io.sgl[0].address = virt_to_bus(tw_dev->bounce_buffer[request_id]);
                        command_packet->byte8.io.sgl[0].length = count;
                        command_packet->size = 5; /* single sgl */
                }
        }

	/* Update SG statistics */
	tw_dev->sgl_entries = tw_dev->srb[request_id]->use_sg;
	if (tw_dev->sgl_entries > tw_dev->max_sgl_entries)
		tw_dev->max_sgl_entries = tw_dev->sgl_entries;

	command_que_value = tw_dev->command_packet_physical_address[request_id];
	if (command_que_value == 0) {
		dprintk(KERN_WARNING "3w-xxxx: tw_scsiop_read_write(): Bad command packet physical address.\n");
		return 1;
	}
      
	/* Now try to post the command to the board */
	tw_post_command_packet(tw_dev, request_id);

	return 0;
} /* End tw_scsiop_read_write() */

/* This function will handle the request sense scsi command */
int tw_scsiop_request_sense(TW_Device_Extension *tw_dev, int request_id)
{
       dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_request_sense()\n");
  
       /* For now we just zero the sense buffer */
       memset(tw_dev->srb[request_id]->request_buffer, 0, tw_dev->srb[request_id]->request_bufflen);
       tw_dev->state[request_id] = TW_S_COMPLETED;
       tw_state_request_finish(tw_dev, request_id);
  
       /* If we got a request_sense, we probably want a reset, return error */
       tw_dev->srb[request_id]->result = (DID_ERROR << 16);
       tw_dev->srb[request_id]->scsi_done(tw_dev->srb[request_id]);
  
       return 0;
} /* End tw_scsiop_request_sense() */

/* This function will handle test unit ready scsi command */
int tw_scsiop_test_unit_ready(TW_Device_Extension *tw_dev, int request_id)
{
	dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_test_unit_ready()\n");

	/* Tell the scsi layer were done */
	tw_dev->state[request_id] = TW_S_COMPLETED;
	tw_state_request_finish(tw_dev, request_id);
	tw_dev->srb[request_id]->result = (DID_OK << 16);
	tw_dev->srb[request_id]->scsi_done(tw_dev->srb[request_id]);

	return 0;
} /* End tw_scsiop_test_unit_ready() */

/* Set a value in the features table */
int tw_setfeature(TW_Device_Extension *tw_dev, int parm, int param_size,
                  unsigned char *val)
{
	TW_Param *param;
	TW_Command  *command_packet;
	TW_Response_Queue response_queue;
	int request_id = 0;
	u32 command_que_value, command_que_addr;
	u32 status_reg_addr, status_reg_value;
	u32 response_que_addr;
	u32 param_value;
	int imax, i;

  	/* Initialize SetParam command packet */
	if (tw_dev->command_packet_virtual_address[request_id] == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_setfeature(): Bad command packet virtual address.\n");
		return 1;
	}
	command_packet = (TW_Command *)tw_dev->command_packet_virtual_address[request_id];
	memset(command_packet, 0, sizeof(TW_Sector));
	param = (TW_Param *)tw_dev->alignment_virtual_address[request_id];

	command_packet->byte0.opcode = TW_OP_SET_PARAM;
	command_packet->byte0.sgl_offset  = 2;
	param->table_id = 0x404;  /* Features table */
	param->parameter_id = parm;
	param->parameter_size_bytes = param_size;
	memcpy(param->data, val, param_size);

	param_value = tw_dev->alignment_physical_address[request_id];
	if (param_value == 0) {
		printk(KERN_WARNING "3w-xxxx: tw_ioctl(): Bad alignment physical address.\n");
		tw_dev->state[request_id] = TW_S_COMPLETED;
		tw_state_request_finish(tw_dev, request_id);
		tw_dev->srb[request_id]->result = (DID_OK << 16);
		tw_dev->srb[request_id]->scsi_done(tw_dev->srb[request_id]);
	}
	command_packet->byte8.param.sgl[0].address = param_value;
	command_packet->byte8.param.sgl[0].length = sizeof(TW_Sector);

	command_packet->size = 4;
	command_packet->request_id = request_id;
	command_packet->byte6.parameter_count = 1;

  	command_que_value = tw_dev->command_packet_physical_address[request_id];
	if (command_que_value == 0) {
		printk(KERN_WARNING "3w-xxxx: tw_setfeature(): Bad command packet physical address.\n");
	return 1;
	}
	command_que_addr = tw_dev->registers.command_que_addr;
	status_reg_addr = tw_dev->registers.status_reg_addr;
	response_que_addr = tw_dev->registers.response_que_addr;

	/* Send command packet to the board */
	outl(command_que_value, command_que_addr);

	/* Poll for completion */
	imax = TW_POLL_MAX_RETRIES;
	for (i=0;i<imax;i++) {
		mdelay(5);
		status_reg_value = inl(status_reg_addr);
		if (tw_check_bits(status_reg_value)) {
			dprintk(KERN_WARNING "3w-xxxx: tw_setfeature(): Unexpected bits.\n");
			tw_decode_bits(tw_dev, status_reg_value);
			return 1;
		}
		if ((status_reg_value & TW_STATUS_RESPONSE_QUEUE_EMPTY) == 0) {
			response_queue.value = inl(response_que_addr);
			request_id = (unsigned char)response_queue.u.response_id;
			if (request_id != 0) {
				/* unexpected request id */
				printk(KERN_WARNING "3w-xxxx: tw_setfeature(): Unexpected request id.\n");
				return 1;
			}
			if (command_packet->status != 0) {
				/* bad response */
				dprintk(KERN_WARNING "3w-xxxx: tw_setfeature(): Bad response, status = 0x%x, flags = 0x%x.\n", command_packet->status, command_packet->flags);
				tw_decode_error(tw_dev, command_packet->status, command_packet->flags, command_packet->byte3.unit);
				return 1;
			}
			break; /* Response was okay, so we exit */
		}
	}

  return 0;
} /* End tw_setfeature() */

/* This function will setup the interrupt handler */
int tw_setup_irq(TW_Device_Extension *tw_dev)
{
	char *device = TW_DEVICE_NAME;
	int error;

	dprintk(KERN_NOTICE "3w-xxxx: tw_setup_irq()\n");
	error = request_irq(tw_dev->tw_pci_dev->irq, tw_interrupt, SA_SHIRQ, device, tw_dev);

	if (error < 0) {
		printk(KERN_WARNING "3w-xxxx: scsi%d: Error requesting IRQ: %d.\n", tw_dev->host->host_no, tw_dev->tw_pci_dev->irq);
		return 1;
	}
	return 0;
} /* End tw_setup_irq() */

/* This function will tell the controller we're shutting down by sending
   initconnection with a 1 */
int tw_shutdown_device(TW_Device_Extension *tw_dev)
{
	int error;

	/* Disable interrupts */
	tw_disable_interrupts(tw_dev);

	/* poke the board */
	error = tw_initconnection(tw_dev, 1);
	if (error) {
		printk(KERN_WARNING "3w-xxxx: scsi%d: Connection shutdown failed.\n", tw_dev->host->host_no);
	} else {
		printk(KERN_NOTICE "3w-xxxx: Shutdown complete.\n");
	}

	/* Re-enable interrupts */
	tw_enable_interrupts(tw_dev);

	return 0;
} /* End tw_shutdown_device() */

/* This function will soft reset the controller */
void tw_soft_reset(TW_Device_Extension *tw_dev) 
{
	u32 control_reg_addr, control_reg_value;

	control_reg_addr = tw_dev->registers.control_reg_addr;
	control_reg_value = (	TW_CONTROL_ISSUE_SOFT_RESET |
				TW_CONTROL_CLEAR_HOST_INTERRUPT |
				TW_CONTROL_CLEAR_ATTENTION_INTERRUPT |
				TW_CONTROL_MASK_COMMAND_INTERRUPT |
				TW_CONTROL_MASK_RESPONSE_INTERRUPT |
				TW_CONTROL_CLEAR_ERROR_STATUS | 
				TW_CONTROL_DISABLE_INTERRUPTS);
	outl(control_reg_value, control_reg_addr);
} /* End tw_soft_reset() */

/* This function will free a request_id */
int tw_state_request_finish(TW_Device_Extension *tw_dev, int request_id)
{
	dprintk(KERN_NOTICE "3w-xxxx: tw_state_request_finish()\n");
  
	do {    
		if (tw_dev->free_tail == tw_dev->free_wrap) {
			tw_dev->free_tail = TW_Q_START;
		} else {
			tw_dev->free_tail = tw_dev->free_tail + 1;
		}
	} while ((tw_dev->state[tw_dev->free_queue[tw_dev->free_tail]] != TW_S_COMPLETED));

	tw_dev->free_queue[tw_dev->free_tail] = request_id;

	tw_dev->state[request_id] = TW_S_FINISHED;
	dprintk(KERN_NOTICE "3w-xxxx: tw_state_request_finish(): Freeing request_id %d\n", request_id);

	return 0;
} /* End tw_state_request_finish() */

/* This function will assign an available request_id */
int tw_state_request_start(TW_Device_Extension *tw_dev, int *request_id)
{
	int id = 0;

	dprintk(KERN_NOTICE "3w-xxxx: tw_state_request_start()\n");

	/* Obtain next free request_id */
	do {
		if (tw_dev->free_head == tw_dev->free_wrap) {
			tw_dev->free_head = TW_Q_START;
		} else {
			tw_dev->free_head = tw_dev->free_head + 1;
		}
	} while (tw_dev->state[tw_dev->free_queue[tw_dev->free_head]] & TW_START_MASK);

	id = tw_dev->free_queue[tw_dev->free_head];

	dprintk(KERN_NOTICE "3w-xxxx: tw_state_request_start(): id = %d.\n", id);
	*request_id = id;
	tw_dev->state[id] = TW_S_STARTED;

	return 0;
} /* End tw_state_request_start() */

/* This function will unmask the command interrupt on the controller */
void tw_unmask_command_interrupt(TW_Device_Extension *tw_dev)
{
	u32 control_reg_addr, control_reg_value;

	control_reg_addr = tw_dev->registers.control_reg_addr;
	control_reg_value = TW_CONTROL_UNMASK_COMMAND_INTERRUPT;
	outl(control_reg_value, control_reg_addr);
} /* End tw_unmask_command_interrupt() */

/* Now get things going */

static Scsi_Host_Template driver_template = TWXXXX;
#include "scsi_module.c"

