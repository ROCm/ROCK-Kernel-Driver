/******************************************************************************
 *     Copyright (C)  2003 -2005 QLogic Corporation
 * QLogic ISP4xxx Device Driver
 *
 * This program includes a device driver for Linux 2.6.x that may be
 * distributed with QLogic hardware specific firmware binary file.
 * You may modify and redistribute the device driver code under the
 * GNU General Public License as published by the Free Software Foundation
 * (version 2 or a later version) and/or under the following terms,
 * as applicable:
 *
 * 	1. Redistribution of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 * 	2. Redistribution in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in
 *         the documentation and/or other materials provided with the
 *         distribution.
 * 	3. The name of QLogic Corporation may not be used to endorse or
 *         promote products derived from this software without specific
 *         prior written permission
 * 	
 * You may redistribute the hardware specific firmware binary file under
 * the following terms:
 * 	1. Redistribution of source code (only if applicable), must
 *         retain the above copyright notice, this list of conditions and
 *         the following disclaimer.
 * 	2. Redistribution in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials provided
 *         with the distribution.
 * 	3. The name of QLogic Corporation may not be used to endorse or
 *         promote products derived from this software without specific
 *         prior written permission
 *
 * REGARDLESS OF WHAT LICENSING MECHANISM IS USED OR APPLICABLE,
 * THIS PROGRAM IS PROVIDED BY QLOGIC CORPORATION "AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * USER ACKNOWLEDGES AND AGREES THAT USE OF THIS PROGRAM WILL NOT CREATE
 * OR GIVE GROUNDS FOR A LICENSE BY IMPLICATION, ESTOPPEL, OR OTHERWISE
 * IN ANY INTELLECTUAL PROPERTY RIGHTS (PATENT, COPYRIGHT, TRADE SECRET,
 * MASK WORK, OR OTHER PROPRIETARY RIGHT) EMBODIED IN ANY OTHER QLOGIC
 * HARDWARE OR SOFTWARE EITHER SOLELY OR IN COMBINATION WITH THIS PROGRAM
 *
 ******************************************************************************
 *             Please see release.txt for revision history.                   *
 *                                                                            *
 ******************************************************************************
 * Function Table of Contents:
 *      qla4xxx_get_hba_count
 *      pci_set_dma_mask
 *      qla4xxx_config_dma_addressing
 *      qla4xxx_detect
 *      qla4xxx_display_config
 *      qla4xxx_alloc_srb_pool
 *      qla4xxx_free_srb_pool
 *      qla4xxx_mem_alloc
 *      qla4xxx_mem_free
 *      qla4xxx_register_resources
 *      qla4xxx_set_info
 *      copy_mem_info
 *      copy_info
 *      qla4xxx_proc_dump_srb_info
 *      qla4xxx_proc_dump_discovered_devices
 *      qla4xxx_proc_dump_scanned_devices
 *      qla4xxx_proc_info
 *      qla4xxx_get_adapter_handle
 *      qla4xxx_release
 *      del_from_active_array
 *      qla4xxx_normalize_dma_addr
 *      qla4xxx_alloc_cont_entry
 *      qla4xxx_send_command_to_isp
 *      qla4xxx_complete_request
 *      qla4xxx_queuecommand
 *      qla4xxx_extend_timeout
 *      qla4xxx_start_io
 *      qla4xxx_os_cmd_timeout
 *      qla4xxx_add_timer_to_cmd
 *      qla4xxx_delete_timer_from_cmd
 *      qla4xxx_timer
 *      qla4xxx_ioctl_error_recovery
 *      qla4xxx_do_dpc
 *      qla4xxx_panic
 *      qla4xxx_eh_wait_on_command
 *      qla4xxx_wait_for_hba_online
 *      qla4xxx_eh_abort
 *      qla4010_soft_reset
 *      qla4xxx_topcat_reset
 *      qla4xxx_soft_reset
 *      qla4xxx_hard_reset
 *      qla4xxx_cmd_wait
 *      qla4xxx_recover_adapter
 *      qla4xxx_eh_wait_for_active_target_commands
 *      qla4xxx_eh_device_reset
 *      qla4xxx_eh_bus_reset
 *      qla4xxx_reset_target
 *      qla4xxx_flush_active_srbs
 *      qla4xxx_eh_host_reset
 *      apidev_open
 *      apidev_close
 *      apidev_ioctl
 *      apidev_init
 *      apidev_cleanup
 ****************************************************************************/

#include "ql4_def.h"

#include <linux/version.h>
#include <linux/moduleparam.h>
#include <linux/vmalloc.h>
#include <linux/smp_lock.h>
#include <linux/delay.h>

#include <scsi/scsi_tcq.h>
#include <scsi/scsicam.h>

/*
 * List of host adapters
 *---------------------------------------------------------------------------*/
/*
 * True list of host adapters.  Available for use after qla4xxx_detect has completed
 */
LIST_HEAD(qla4xxx_hostlist);
rwlock_t qla4xxx_hostlist_lock = RW_LOCK_UNLOCKED;

struct list_head *qla4xxx_hostlist_ptr = &qla4xxx_hostlist;
rwlock_t *qla4xxx_hostlist_lock_ptr = &qla4xxx_hostlist_lock;
EXPORT_SYMBOL_GPL(qla4xxx_hostlist_ptr);
EXPORT_SYMBOL_GPL(qla4xxx_hostlist_lock_ptr);

int qla4xxx_hba_count = 0;
int qla4xxx_hba_going_away = 0;

/*
 * Command line options
 *---------------------------------------------------------------------------*/
/*
 * Just in case someone uses commas to separate items on the insmod
 * command line, we define a dummy buffer here to avoid having insmod
 * write wild stuff into our code segment
 */
int ql4xdiscoverywait=60;
module_param(ql4xdiscoverywait, int, S_IRUGO|S_IRUSR);
MODULE_PARM_DESC(ql4xdiscoverywait,
		 "Discovery wait time");
int ql4xdontresethba=0;
module_param(ql4xdontresethba, int, S_IRUGO|S_IRUSR);
MODULE_PARM_DESC(ql4xdontresethba,
		 "Dont reset the HBA when the driver gets 0x8002 AEN "
		 " default it will reset hba :0"
		 " set to 1 to avoid resetting HBA");

int ql4xcmdretrycount = 20;
module_param(ql4xcmdretrycount, int, S_IRUGO|S_IRUSR);
MODULE_PARM_DESC(ql4xcmdretrycount,
		 "Maximum number of mid-layer retries allowed for a command.  "
		 "Default value is 20");

int ql4xmaxqdepth = 0;
module_param(ql4xmaxqdepth, int, S_IRUGO|S_IRUSR);
MODULE_PARM_DESC(ql4xmaxqdepth,
		 "Maximum queue depth to report for target devices.");

int extended_error_logging = 0;	/* 0 = off, 1 = log errors, 2 = debug logging */
module_param(extended_error_logging, int, S_IRUGO|S_IRUSR);
MODULE_PARM_DESC(extended_error_logging,
		 "Option to enable extended error logging, "
		 "Default is 0 - no logging. 1 - log errors. 2 - debug "
		 "logging");

int displayConfig = 0;
module_param(displayConfig, int, S_IRUGO|S_IRUSR);
MODULE_PARM_DESC(displayConfig,
		 "If 1 then display the configuration used in "
		 "/etc/modules.conf.");

char *ql4xdevconf = NULL;

MODULE_AUTHOR("QLogic Corporation");
MODULE_DESCRIPTION("QLogic ISP4XXX iSCSI Host Bus Adapter driver");
MODULE_LICENSE("GPL");

/*
 * Proc info processing
 *---------------------------------------------------------------------------*/
struct info_str {
	char    *buffer;
	int     length;
	off_t   offset;
	int     pos;
};

/*
 * String messages for various state values (used for print statements)
 *---------------------------------------------------------------------------*/
const char *ddb_state_msg[] = DDB_STATE_TBL();
const char *srb_state_msg[] = SRB_STATE_TBL();



static uint8_t qla4xxx_mem_alloc(scsi_qla_host_t *ha);
static void qla4xxx_mem_free(scsi_qla_host_t *ha);
void qla4xxx_timer(unsigned long p);
static int qla4xxx_do_dpc(void *data);
void qla4xxx_display_config(void);
void qla4xxx_add_timer_to_cmd(srb_t *srb, int timeout);
static void qla4xxx_flush_active_srbs(scsi_qla_host_t *ha);
uint8_t qla4xxx_reset_target(scsi_qla_host_t *ha, ddb_entry_t *ddb_entry);
uint8_t qla4xxx_recover_adapter(scsi_qla_host_t *ha, uint8_t renew_ddb_list);
void qla4xxx_config_dma_addressing(scsi_qla_host_t *ha);

CONTINUE_ENTRY *qla4xxx_alloc_cont_entry(scsi_qla_host_t *ha);

static void qla4xxx_free_other_mem(scsi_qla_host_t *ha);
static int qla4xxx_iospace_config(scsi_qla_host_t *ha);
extern fc_lun_t * qla4xxx_add_fclun(fc_port_t *fcport, uint16_t lun);


/*
 * PCI driver interface definitions
 *---------------------------------------------------------------------------*/
static struct pci_device_id qla4xxx_pci_tbl[] __devinitdata =
{
	{
		.vendor		= PCI_VENDOR_ID_QLOGIC,
		.device		= PCI_DEVICE_ID_QLOGIC_ISP4010,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
	},
	{
		.vendor		= PCI_VENDOR_ID_QLOGIC,
		.device		= PCI_DEVICE_ID_QLOGIC_ISP4022,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
	},
	{0, 0},
};
MODULE_DEVICE_TABLE(pci, qla4xxx_pci_tbl);

static int __devinit qla4xxx_probe_adapter(struct pci_dev *, const struct pci_device_id *);
static void __devexit qla4xxx_remove_adapter(struct pci_dev *);
static void qla4xxx_free_adapter(scsi_qla_host_t *ha);

struct pci_driver qla4xxx_pci_driver = {
	.name           = DRIVER_NAME,
	.id_table       = qla4xxx_pci_tbl,
	.probe          = qla4xxx_probe_adapter,
	.remove         = qla4xxx_remove_adapter,
};

int qla4xxx_proc_info(struct Scsi_Host *, char *, char **, off_t, int, int);
int qla4xxx_queuecommand(struct scsi_cmnd *cmd, void (*done_fn)(struct scsi_cmnd *));
int qla4xxx_eh_abort(struct scsi_cmnd *cmd);
int qla4xxx_eh_bus_reset(struct scsi_cmnd *cmd);
int qla4xxx_eh_device_reset(struct scsi_cmnd *cmd);
int qla4xxx_eh_host_reset(struct scsi_cmnd *cmd);
int qla4xxx_slave_configure(struct scsi_device * device);

static struct scsi_host_template qla4xxx_driver_template = {
	.module			= THIS_MODULE,
	.name			= "qla4xxx",
	.proc_name		= "qla4xxx",
	.proc_info		= qla4xxx_proc_info,
	.queuecommand		= qla4xxx_queuecommand,

	.eh_abort_handler	= qla4xxx_eh_abort,
	.eh_device_reset_handler = qla4xxx_eh_device_reset,
	.eh_bus_reset_handler	= qla4xxx_eh_bus_reset,
	.eh_host_reset_handler	= qla4xxx_eh_host_reset,

	.slave_configure	= qla4xxx_slave_configure,

	.this_id		= -1,
	.cmd_per_lun		= 3,
	.use_clustering		= ENABLE_CLUSTERING,
	.sg_tablesize		= SG_ALL,
		
	/* 2^32-1 size limit */
	.max_sectors 		= 0xffff,
};

/**************************************************************************
 * qla4xxx_set_info
 *      This routine set parameters for the driver from the /proc filesystem.
 *
 * Input:
 *      Unused
 *
 * Returns:
 *      -ENOSYS - no-op
 *
 * Context:
 *      Kernel context.
 **************************************************************************/
int
qla4xxx_set_info(char *buffer, int length, struct Scsi_Host *host)
{
	return(-ENOSYS);  /* Currently this is a no-op */
}


/**************************************************************************
 * qla4xxx_module_init
 *    Module initialization.
 **************************************************************************/
static int __init
qla4xxx_module_init(void)
{
	int status;

	printk(KERN_INFO
	    "QLogic iSCSI HBA Driver (%p)\n", qla4xxx_set_info);

#if ISP_RESET_TEST
	printk(KERN_INFO "qla4xxx: Adapter Reset Test Enabled!  "
	       "Adapter Resets will be issued every 3 minutes!\n");
#endif

	status = pci_module_init(&qla4xxx_pci_driver);

	return status;
}

/**************************************************************************
 * qla4xxx_module_exit
 *    Module cleanup.
 **************************************************************************/
static void __exit
qla4xxx_module_exit(void)
{
	pci_unregister_driver(&qla4xxx_pci_driver);
}
module_init(qla4xxx_module_init);
module_exit(qla4xxx_module_exit);


/**************************************************************************
 * qla4xxx_probe_adapter
 *    This routine will probe for Qlogic 4010 iSCSI host adapters.
 *    It returns the number of host adapters of a particular
 *    type that were found.  It also initializes all data necessary for
 *    the driver.  It is passed-in the host number, so that it
 *    knows where its first entry is in the scsi_hosts[] array.
 *
 * Input:
 *
 * Returns:
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int __devinit
qla4xxx_probe_adapter(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct Scsi_Host *host;
	scsi_qla_host_t *ha;
	uint8_t status;
	uint8_t init_retry_count = 0;

	ENTER(__func__);

	if (pci_enable_device(pdev))
		return -1;

	host = scsi_host_alloc(&qla4xxx_driver_template,
	    sizeof(scsi_qla_host_t));
	if (host == NULL) {
		printk(KERN_WARNING
		    "qla4xxx: Couldn't allocate host from scsi layer!\n");
		goto probe_disable_device;
	}


	switch (extended_error_logging) {
	case 2:
		qla4xxx_set_debug_level(QLP1|QLP2|QLP7|QLP20|QLP19);
		break;
	case 1:
		qla4xxx_set_debug_level(QLP1|QLP2);
		break;
	}

	/* Clear our data area */
	ha = (scsi_qla_host_t *)host->hostdata;
	memset(ha, 0, sizeof(scsi_qla_host_t));

	/* Save the information from PCI BIOS.  */
	ha->pdev = pdev;
	ha->host = host;
	ha->host_no = host->host_no;
	ha->instance = qla4xxx_hba_count;

	/* Configure PCI I/O space. */
	if (qla4xxx_iospace_config(ha) != QLA_SUCCESS)
		goto probe_failed;

	host->irq = pdev->irq;

        ql4_printk(KERN_INFO, ha,
	    "Found an ISP%04x, irq %d, iobase 0x%p\n", pdev->device, host->irq,
	    ha->reg);

	/* Configure OS DMA addressing method. */
	qla4xxx_config_dma_addressing(ha);

	/* Initialize lists and spinlocks. */
	INIT_LIST_HEAD(&ha->ddb_list);
	INIT_LIST_HEAD(&ha->free_srb_q);
	INIT_LIST_HEAD(&ha->fcports);
	INIT_LIST_HEAD(&ha->done_srb_q);
	INIT_LIST_HEAD(&ha->retry_srb_q);

	init_MUTEX(&ha->mbox_sem);
	init_waitqueue_head(&ha->mailbox_wait_queue);

	spin_lock_init(&ha->hardware_lock);
	spin_lock_init(&ha->list_lock);

	ha->dpc_pid = -1;
	init_completion(&ha->dpc_inited);
	init_completion(&ha->dpc_exited);

	/* Verify iSCSI PCI Funcion Number */
	if (IS_QLA4010(ha)) {
		ha->function_number = ISP4010_ISCSI_FUNCTION;
	} else if (IS_QLA4022(ha)) {
		spin_lock_irq(&ha->hardware_lock);
		ha->function_number = (RD_REG_DWORD(&ha->reg->ctrl_status) &
		    CSR_PCI_FUNC_NUM_MASK) >> 8;
		spin_unlock_irq(&ha->hardware_lock);
	}
	if (PCI_FUNC(pdev->devfn) != ha->function_number) {
		ql4_printk(KERN_WARNING, ha, "HA function number (0x%x) does "
		    "not match PCI function number (0x%x)\n",
		    ha->function_number, PCI_FUNC(pdev->devfn));

		goto probe_disable_device;
	}

	/*
	 * Allocate memory for dma buffers
	 */
	if (qla4xxx_mem_alloc(ha) == QLA_ERROR) {
		ql4_printk(KERN_WARNING, ha,
		    "[ERROR] Failed to allocate memory for adapter\n");

		goto probe_disable_device;
	}

	/*
	 * Initialize the Host adapter request/response queues and
	 * firmware
	 * NOTE: interrupts enabled upon successful completion
	 */
	status = qla4xxx_initialize_adapter(ha, REBUILD_DDB_LIST);
	while ((status == QLA_ERROR) &&
	       (init_retry_count++ < MAX_INIT_RETRIES)) {
		DEBUG2(printk("scsi: %s: retrying adapter "
		    "initialization (%d)\n", __func__, init_retry_count));

		qla4xxx_soft_reset(ha);
		status = qla4xxx_initialize_adapter(ha, REBUILD_DDB_LIST);
	}

	if (status == QLA_ERROR) {
		ql4_printk(KERN_WARNING, ha,"Failed to initialize adapter\n");

		DEBUG2(printk("scsi: Failed to initialize adapter\n"));

		goto probe_failed;
	}

	host->cmd_per_lun = 3;
	host->io_port = ha->io_addr;
	host->max_channel =  0;
	host->max_lun = MAX_LUNS-1;
	host->max_id = MAX_TARGETS;
	host->unique_id = ha->instance;
	host->max_cmd_len = IOCB_MAX_CDB_LEN;
	host->can_queue = REQUEST_QUEUE_DEPTH + 128;

	/* Startup the kernel thread for this host adapter. */
	QL4PRINT(QLP7, printk("scsi: %s: Starting kernel thread for "
	    "qla4xxx_dpc\n", __func__));
	ha->dpc_should_die = 0;
	ha->dpc_pid = kernel_thread(qla4xxx_do_dpc, ha, 0);
	init_retry_count = 0;
	while ((ha->dpc_pid < 0) &&
	       (init_retry_count++ < MAX_INIT_RETRIES)) {
		ha->dpc_pid = kernel_thread(qla4xxx_do_dpc, ha, 0);
		if (ha->dpc_pid < 0) {
			printk(KERN_WARNING
				" host_no =%d Unable to start DPC thread!"
				"init_retry_count =%d\n", ha->host_no,
				init_retry_count);
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(1 * HZ);
		} else {
		 	break;	
		}	
	}
	if (ha->dpc_pid < 0) {
		ql4_printk(KERN_WARNING, ha, "Unable to start DPC thread!\n");

		goto probe_failed;
	}
	wait_for_completion(&ha->dpc_inited);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,12))
	#if ENABLE_MSI
	if (IS_QLA4022(ha)) {
		if (pci_enable_msi(pdev)) {
			ql4_printk(KERN_WARNING, ha,
			    "Failed to Enable MSI!!!.\n");
			goto probe_failed;
		}
	}
	ql4_printk(KERN_INFO, ha, "MSI Enabled...\n");
	set_bit(AF_MSI_ENABLED, &ha->flags);
	#endif
#endif

	/* Install the interrupt handler with the new ha */
	if (request_irq(ha->pdev->irq, qla4xxx_intr_handler,
	    SA_INTERRUPT|SA_SHIRQ, "qla4xxx", ha)) {
		ql4_printk(KERN_WARNING, ha,
		    "Failed to reserve interrupt %d already in use.\n",
		    host->irq);

		goto probe_failed;
	}
	set_bit(AF_IRQ_ATTACHED, &ha->flags);
	QL4PRINT(QLP7, printk("scsi%d: irq %d attached\n", ha->host_no,
	    ha->pdev->irq));
	qla4xxx_enable_intrs(ha);

	/* Start timer thread. */
	QL4PRINT(QLP7, printk("scsi: %s: Starting timer thread for adapter "
	    "%d\n", __func__, ha->instance));
	init_timer(&ha->timer);
	ha->timer.expires = jiffies + HZ;
	ha->timer.data = (unsigned long)ha;
	ha->timer.function = (void (*)(unsigned long))qla4xxx_timer;
	add_timer(&ha->timer);
	ha->timer_active = 1;

	strcpy(ha->driver_verstr, QLA4XXX_DRIVER_VERSION);
	ha->driver_version[0] = QL4_DRIVER_MAJOR_VER;
	ha->driver_version[1] = QL4_DRIVER_MINOR_VER;
	ha->driver_version[2] = QL4_DRIVER_PATCH_VER;
	ha->driver_version[3] = QL4_DRIVER_BETA_VER;

	/* Insert new entry into the list of adapters. */
	write_lock(&qla4xxx_hostlist_lock);
	list_add_tail(&ha->list, &qla4xxx_hostlist);
	write_unlock(&qla4xxx_hostlist_lock);

	DEBUG(printk("qla4xxx: lock=%p listhead=%p, done adding ha list=%p.\n",
	    &qla4xxx_hostlist_lock, &qla4xxx_hostlist, &ha->list);)

	qla4xxx_display_config();

	set_bit(AF_INIT_DONE, &ha->flags);
	qla4xxx_hba_count++;

	pci_set_drvdata(pdev, ha);

	if (scsi_add_host(host, &pdev->dev))
		goto probe_failed;

	printk(KERN_INFO
	    " QLogic iSCSI HBA Driver version: %s\n"
	    "  QLogic ISP%04x @ %s hdma%c, host#=%d, fw=%02d.%02d.%02d.%02d\n",
	    QLA4XXX_DRIVER_VERSION,
	    ha->pdev->device, pci_name(ha->pdev),
	    test_bit(AF_64BIT_PCI_ADDR, &ha->flags) ? '+': '-', ha->host_no,
	    ha->firmware_version[0], ha->firmware_version[1],
	    ha->patch_number, ha->build_number);
	
	scsi_scan_host(host);

	return 0;

probe_failed:
	qla4xxx_free_adapter(ha);

probe_disable_device:
	pci_disable_device(pdev);

	return -1;
}

/**************************************************************************
 * qla4xxx_remove_adapter
 *
 * Input:
 *	pci_dev - PCI device pointer
 *
 * Returns:
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static void __devexit
qla4xxx_remove_adapter(struct pci_dev *pdev)
{
	scsi_qla_host_t *ha;

	ha = pci_get_drvdata(pdev);

	write_lock(&qla4xxx_hostlist_lock);
	list_del_init(&ha->list);
	write_unlock(&qla4xxx_hostlist_lock);

	scsi_remove_host(ha->host);

	qla4xxx_free_adapter(ha);

	scsi_host_put(ha->host);

	pci_set_drvdata(pdev, NULL);
}

static void
qla4xxx_free_adapter(scsi_qla_host_t *ha)
{
	int ret;

	ENTER(__func__);

	qla4xxx_hba_going_away++;

#if 0
	/* Deregister with the iSNS Server */
	if (test_bit(ISNS_FLAG_ISNS_SRV_REGISTERED, &ha->isns_flags)) {
		u_long wait_cnt;

		QL4PRINT(QLP7, printk("scsi%d: %s: deregister iSNS\n",
				      ha->host_no, __func__));
		qla4xxx_isns_scn_dereg(ha);
		qla4xxx_isns_dev_dereg(ha);

		wait_cnt = jiffies + ISNS_DEREG_TOV * HZ;
		while (wait_cnt > jiffies) {
			if (test_bit(ISNS_FLAG_ISNS_SRV_REGISTERED,
				     &ha->isns_flags) == 0)
				break;
			QL4PRINT(QLP7, printk("."));
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(1 * HZ);
		}
	}
#endif

	if (test_bit(ISNS_FLAG_ISNS_ENABLED_IN_ISP, &ha->isns_flags)) {
		QL4PRINT(QLP7, printk("scsi%d: %s: Stop iSNS service\n",
		    ha->host_no, __func__));
		qla4xxx_isns_disable(ha);
	}

	if (test_bit(AF_INTERRUPTS_ON, &ha->flags)) {
		/* Turn-off interrupts on the card. */
		qla4xxx_disable_intrs(ha);
	}

	/* Kill the kernel thread for this host */
	if (ha->dpc_pid >= 0) {
		ha->dpc_should_die = 1;
		wmb();
		ret = kill_proc(ha->dpc_pid, SIGHUP, 1);
		if (ret) {
			ql4_printk(KERN_ERR, ha,
			    "Unable to signal DPC thread -- (%d)\n", ret);
			
			/* TODO: SOMETHING MORE??? */
		} else
			wait_for_completion(&ha->dpc_exited);
	}

	/* Issue Soft Reset to put firmware in unknown state */
	QL4PRINT(QLP7, printk("scsi%d: %s: Soft Reset\n",ha->host_no,__func__));
	qla4xxx_soft_reset(ha);

	/* Remove timer thread, if present */
	if (ha->timer_active) {
		QL4PRINT(QLP7, printk("scsi%d: %s: Removing timer thread for "
		    "adapter %d\n", ha->host_no, __func__, ha->instance));

		del_timer_sync(&ha->timer);
		ha->timer_active = 0;
	}

	/* free extra memory */
	qla4xxx_mem_free(ha);

	/* Detach interrupts */
	if (test_and_clear_bit(AF_IRQ_ATTACHED, &ha->flags))
		free_irq(ha->pdev->irq, ha);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,10))
        #if ENABLE_MSI
	if (test_and_clear_bit(AF_MSI_ENABLED, &ha->flags))
		pci_disable_msi(ha->pdev);
	#endif
#endif	

	/* Free I/O Region */
	if (ha->io_addr) {
		release_region(ha->io_addr, ha->io_len);
		ha->io_addr = 0;
	}

	pci_disable_device(ha->pdev);

	qla4xxx_hba_going_away--;

	LEAVE(__func__);
}

/**************************************************************************
 * qla4xxx_iospace_config
 *    This routine
 *
 * Input:
 *
 * Returns:
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4xxx_iospace_config(scsi_qla_host_t *ha)
{
	int bar;

	/* Search for I/O register. */
	for (bar = 0; bar <= 5; bar++) {
		unsigned long pci_base_address;

		pci_base_address = pci_resource_start(ha->pdev, bar);
		ha->pci_resource_flags = pci_resource_flags(ha->pdev, bar);

#if MEMORY_MAPPED_IO
		if (ha->pci_resource_flags & IORESOURCE_MEM) {
			QL4PRINT(QLP7, printk("scsi%d: Assigned to Memory I/O "
			    "0x%lx in PCI BAR%d\n", ha->host_no,
			    pci_base_address, bar));

			ha->mem_addr = pci_base_address;
			ha->mem_len = pci_resource_len(ha->pdev, bar);
			break;
		}
#else
		if (ha->pci_resource_flags IORESOURCE_IO) {
			QL4PRINT(QLP7, printk("scsi%d: Assigned to I/O Port "
			    "0x%lx in PCI BAR%d\n", ha->host_no,
			    pci_base_address, bar));

			ha->io_addr = pci_base_address;
			ha->io_len = pci_resource_len(ha->pdev, bar);
			break;
		}
#endif
	}

	/* Map the Memory I/O register. */
	if (ha->mem_addr) {
		unsigned long  page_offset, base;

		if (!request_mem_region(ha->mem_addr, ha->mem_len,
		    DRIVER_NAME)) {
			printk(KERN_WARNING
			    "Could not allocate IO Memory space %lx len %ld.\n",
			    ha->mem_addr, ha->mem_len);
			return -1;
		}

		QL4PRINT(QLP7, printk("scsi%d: %s: base memory address = "
		    "0x%lx\n", ha->host_no, __func__, ha->mem_addr));

		/* Find proper memory chunk for memory map I/O reg. */
		base = ha->mem_addr & PAGE_MASK;
		page_offset = ha->mem_addr - base;

		/* Get virtual address for I/O registers. */
		ha->virt_mmapbase = ioremap(base, page_offset +
		    sizeof(*ha->reg));
		if (ha->virt_mmapbase == NULL) {
			QL4PRINT(QLP2, printk("scsi%d: %s: I/O Remap Failed\n",
			    ha->host_no, __func__));
			return -1;
		}

		QL4PRINT(QLP7, printk("scsi%d: %s: virt memory_mapped_address "
		    "= 0x%p\n", ha->host_no, __func__, ha->virt_mmapbase));

		ha->reg = (isp_reg_t *)(ha->virt_mmapbase + page_offset);
		QL4PRINT(QLP7, printk("scsi%d: %s: registers = 0x%p\n",
		    ha->host_no, __func__, ha->reg));
	}

	if (ha->io_addr) {
		if (!request_region(ha->io_addr, ha->io_len, DRIVER_NAME)) {
			printk(KERN_WARNING
			    "Could not allocate IO space %lx len %ld.\n",
			    ha->io_addr, ha->io_len);

			return -1;
		}
	}

	return QLA_SUCCESS;
}

/**************************************************************************
 * qla4xxx_display_config
 *      This routine  displays the configuration information to be used in
 *      modules.conf.
 *
 * Input:
 *      ha - Pointer to host adapter structure
 *
 * Output:
 *      None
 *
 * Returns:
 *      None
 *
 * Context:
 *      Kernel context.
 **************************************************************************/
void
qla4xxx_display_config(void)
{
	scsi_qla_host_t *ha, *htemp;

	read_lock(&qla4xxx_hostlist_lock);
	list_for_each_entry_safe(ha, htemp, &qla4xxx_hostlist, list) {
		/* Display the M.A.C. Address for adapter */
		printk(KERN_INFO
		    "scsi-qla%d-mac=%02x%02x%02x%02x%02x%02x\\;\n",
		    ha->instance,
		    ha->my_mac[0], ha->my_mac[1], ha->my_mac[2],
		    ha->my_mac[3], ha->my_mac[4], ha->my_mac[5]);
	}
	read_unlock(&qla4xxx_hostlist_lock);

}

/**************************************************************************
 * qla4xxx_get_hba_count
 *      This routine returns the number of host adapters present.
 *
 * Input:
 *      None
 *
 * Returns:
 *    qla4xxx_hba_count - Number of host adapters present.
 *
 * Context:
 *      Kernel context.
 **************************************************************************/
uint32_t
qla4xxx_get_hba_count(void)
{
	return(qla4xxx_hba_count);
}



/****************************************************************************/
/*  LINUX -  Loadable Module Functions.                                     */
/****************************************************************************/

/**
 * qla4xxx_config_dma_addressing() - Configure OS DMA addressing method.
 * @ha: HA context
 *
 * At exit, the @ha's flags.enable_64bit_addressing set to indicated
 * supported addressing method.
 */
void
qla4xxx_config_dma_addressing(scsi_qla_host_t *ha)
{
	int retval;

	/* Assume 32bit DMA address. */
	clear_bit(AF_64BIT_PCI_ADDR, &ha->flags);

	/*
	 * Given the two variants pci_set_dma_mask(), allow the compiler to
	 * assist in setting the proper dma mask.
	 */
	if (sizeof(dma_addr_t) > 4) {
		/* Update our PCI device dma_mask for full 64 bit mask */
		if (pci_set_dma_mask(ha->pdev, DMA_64BIT_MASK) == 0) {
			set_bit(AF_64BIT_PCI_ADDR, &ha->flags);

			if (pci_set_consistent_dma_mask(ha->pdev,
			    DMA_64BIT_MASK)) {
				ql4_printk(KERN_DEBUG, ha,
				    "Failed to set 64 bit PCI consistent mask; "
				    "using 32 bit.\n");

				retval = pci_set_consistent_dma_mask(ha->pdev,
				    DMA_32BIT_MASK);
			}
		} else {
			ql4_printk(KERN_DEBUG, ha,
			    "Failed to set 64 bit PCI DMA mask, falling back "
			    "to 32 bit MASK.\n");

			retval = pci_set_dma_mask(ha->pdev, DMA_32BIT_MASK);
		}
	} else {
		pci_set_dma_mask(ha->pdev, DMA_32BIT_MASK);
	}
}

/**************************************************************************
 * qla4xxx_alloc_srb_pool
 *      This routine is called during driver initialization to allocate
 *      memory for the local srb pool.
 *
 * Input:
 *      ha - Pointer to host adapter structure
 *
 * Returns:
 *      QLA_SUCCESS - Successfully allocated srbs
 *      QLA_ERROR   - Failed to allocate any srbs
 *
 * Context:
 *      Kernel context.
 **************************************************************************/
static uint8_t
qla4xxx_alloc_srb_pool(scsi_qla_host_t *ha)
{
	srb_t *srb;
	int i;
	uint8_t status = QLA_ERROR;

	ENTER("qla4xxx_alloc_srb_pool");

	ha->num_srbs_allocated = 0;
	ha->free_srb_q_count = 0; /* incremented in add_to_free_srb_q routine */

	/*
	 * NOTE: Need to allocate each SRB separately, as Kernel 2.4.4 seems to
	 * have an error when allocating a large amount of memory.
	 */
	for (i=0; i < MAX_SRBS; i++) {
		srb = (srb_t *) kmalloc(sizeof(srb_t), GFP_KERNEL);
		if (srb == NULL) {
			QL4PRINT(QLP2, printk(
			    "scsi%d: %s: failed to allocate memory, count = "
			    "%d\n", ha->host_no, __func__, i));
		} else {
			ha->num_srbs_allocated++;
			memset(srb, 0, sizeof(srb_t));
			atomic_set(&srb->ref_count, 1);
			__add_to_free_srb_q(ha, srb);
		}
	}

	if (ha->free_srb_q_count)
		status = QLA_SUCCESS;

	DEBUG2(printk("scsi%d: %s: Allocated %d SRB(s)\n",
	    ha->host_no, __func__, ha->free_srb_q_count));

	LEAVE("qla4xxx_alloc_srb_pool");

	return (status);
}

/**************************************************************************
 * qla4xxx_free_srb_pool
 *      This routine is called during driver unload to deallocate the srb
 *      pool.
 *
 * Input:
 *      ha - Pointer to host adapter structure
 *
 * Returns:
 *      None
 *
 * Context:
 *      Kernel context.
 **************************************************************************/
static void
qla4xxx_free_srb_pool(scsi_qla_host_t *ha)
{
	srb_t *srb, *stemp;
	int cnt_free_srbs = 0;
	unsigned long flags;

	ENTER("qla4xxx_free_srb_pool");
	spin_lock_irqsave(&ha->list_lock, flags);
	list_for_each_entry_safe(srb, stemp, &ha->free_srb_q, list_entry) {
		__del_from_free_srb_q(ha, srb);
		kfree(srb);
		cnt_free_srbs++;
	}
	spin_unlock_irqrestore(&ha->list_lock, flags);

	if (cnt_free_srbs != ha->num_srbs_allocated) {
		QL4PRINT(QLP2, printk(KERN_WARNING
		    "scsi%d: Did not free all srbs, Free'd srb count = %d, "
		    "Alloc'd srb count %d\n", ha->host_no, cnt_free_srbs,
		    ha->num_srbs_allocated));
	}

	LEAVE("qla4xxx_free_srb_pool");
}

/**************************************************************************
 * qla4xxx_mem_alloc
 *      This routine allocates memory use by the adapter.
 *
 * Input:
 *      ha - Pointer to host adapter structure
 *
 * Returns:
 *      QLA_SUCCESS - Successfully allocated adapter memory
 *      QLA_ERROR   - Failed to allocate adapter memory
 *
 * Context:
 *      Kernel context.
 **************************************************************************/
static uint8_t
qla4xxx_mem_alloc(scsi_qla_host_t *ha)
{
	unsigned long	align;

	ENTER("qla4xxx_mem_alloc");

	/* Allocate contiguous block of DMA memory for queues. */
	ha->queues_len = ((REQUEST_QUEUE_DEPTH * QUEUE_SIZE) +
	    (RESPONSE_QUEUE_DEPTH * QUEUE_SIZE) + sizeof(shadow_regs_t) +
	    MEM_ALIGN_VALUE + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1);
	ha->queues = pci_alloc_consistent(ha->pdev, ha->queues_len,
	    &ha->queues_dma);
	if (ha->queues == NULL) {
		ql4_printk(KERN_WARNING, ha,
		    "Memory Allocation failed - queues.\n");

		goto mem_alloc_error_exit;
	}
	memset(ha->queues, 0, ha->queues_len);

	/*
	 * As per RISC alignment requirements -- the bus-address must be a
	 * multiple of the request-ring size (in bytes).
	 */
	align = 0;
	if ((unsigned long)ha->queues_dma & (MEM_ALIGN_VALUE - 1)) {
		align = MEM_ALIGN_VALUE -
		    ((unsigned long)ha->queues_dma & (MEM_ALIGN_VALUE - 1));
	}

	/* Update request and response queue pointers. */
	ha->request_dma = ha->queues_dma + align;
	ha->request_ring = (QUEUE_ENTRY *)(ha->queues + align);
	ha->response_dma = ha->queues_dma + align +
	    (REQUEST_QUEUE_DEPTH * QUEUE_SIZE);
	ha->response_ring = (QUEUE_ENTRY *)(ha->queues + align +
	    (REQUEST_QUEUE_DEPTH * QUEUE_SIZE));
	ha->shadow_regs_dma = ha->queues_dma + align +
	    (REQUEST_QUEUE_DEPTH * QUEUE_SIZE) +
	    (RESPONSE_QUEUE_DEPTH * QUEUE_SIZE);
	ha->shadow_regs = (shadow_regs_t *)(ha->queues + align +
	    (REQUEST_QUEUE_DEPTH * QUEUE_SIZE) +
	    (RESPONSE_QUEUE_DEPTH * QUEUE_SIZE));

	QL4PRINT(QLP7, printk("scsi%d: %s: queues 0x%lx (%p) %lx\n",
	    ha->host_no, __func__, (unsigned long)ha->queues_dma,
	    ha->queues, ha->queues_len));
	QL4PRINT(QLP7, printk("scsi%d: %s: request ring 0x%lx (%p)\n",
	    ha->host_no, __func__, (unsigned long)ha->request_dma,
	    ha->request_ring));
	QL4PRINT(QLP7, printk("scsi%d: %s: response ring 0x%lx (%p)\n",
	    ha->host_no, __func__, (unsigned long)ha->response_dma,
	    ha->response_ring));
	QL4PRINT(QLP7, printk("scsi%d: %s: shadow regs 0x%lx (%p)\n",
	    ha->host_no, __func__, (unsigned long)ha->shadow_regs_dma,
	    ha->shadow_regs));

	/* Allocate iSNS Discovered Target Database
	 * ---------------------------------------- */
	ha->isns_disc_tgt_database_size = sizeof(ISNS_DISCOVERED_TARGET) *
	    MAX_ISNS_DISCOVERED_TARGETS;
	ha->isns_disc_tgt_databasev = pci_alloc_consistent(ha->pdev,
	    ha->isns_disc_tgt_database_size, &ha->isns_disc_tgt_databasep);
	if (ha->isns_disc_tgt_databasev == NULL) {
		ql4_printk(KERN_WARNING, ha,
		    "Memory Allocation failed - iSNS DB.\n");

		goto mem_alloc_error_exit;
	}
	memset(ha->isns_disc_tgt_databasev, 0, ha->isns_disc_tgt_database_size);

	QL4PRINT(QLP7, printk("scsi%d: %s: iSNS DB 0x%ld (%p)\n", ha->host_no,
	    __func__, (unsigned long)ha->isns_disc_tgt_databasep,
	    ha->isns_disc_tgt_databasev));

	/*
	 * Allocate memory for srb pool
	 *-----------------------------*/
	if (qla4xxx_alloc_srb_pool(ha) == QLA_ERROR)
		goto mem_alloc_error_exit;

	LEAVE("qla4xxx_mem_alloc");

	return (QLA_SUCCESS);

mem_alloc_error_exit:
	qla4xxx_mem_free(ha);
	LEAVE("qla4xxx_mem_alloc");
	return (QLA_ERROR);
}

/**************************************************************************
 * qla4xxx_mem_free
 *      This routine frees adapter allocated memory
 *
 * Input:
 *      ha - Pointer to host adapter structure.
 *
 * Returns:
 *      None
 *
 * Context:
 *      Kernel context.
 **************************************************************************/
static void
qla4xxx_mem_free(scsi_qla_host_t *ha)
{
	ENTER("qla4xxx_mem_free");

	if (ha->queues) {
		QL4PRINT(QLP7, printk("scsi%d: %s: free queues.\n", ha->host_no,
		    __func__));

		pci_free_consistent(ha->pdev, ha->queues_len, ha->queues,
		    ha->queues_dma);
	}
	ha->queues_len = 0;
	ha->queues = NULL;
	ha->queues_dma = 0;
	ha->request_ring = NULL;
	ha->request_dma = 0;
	ha->response_ring = NULL;
	ha->response_dma = 0;
	ha->shadow_regs = NULL;
	ha->shadow_regs_dma = 0;

	if (ha->isns_disc_tgt_databasev) {
		QL4PRINT(QLP7, printk("scsi%d: %s: free iSNS DB.\n",
		    ha->host_no, __func__));

		pci_free_consistent(ha->pdev, ha->isns_disc_tgt_database_size,
		    ha->isns_disc_tgt_databasev, ha->isns_disc_tgt_databasep);
	}
	ha->isns_disc_tgt_database_size = 0;
	ha->isns_disc_tgt_databasev = 0;
	ha->isns_disc_tgt_databasep = 0;

	/* Free srb pool */
	if (ha->num_srbs_allocated) {
		QL4PRINT(QLP7, printk("scsi%d: %s: free srb pool\n",
		    ha->host_no, __func__));

		qla4xxx_free_srb_pool(ha);
	}

	/* Free ddb list */
	if (!list_empty(&ha->ddb_list)) {
		QL4PRINT(QLP7, printk("scsi%d: %s: free ddb list\n",
		    ha->host_no, __func__));

		qla4xxx_free_ddb_list(ha);
	}

	/* Unmap Memory Mapped I/O region */
	if (ha->virt_mmapbase) {
		QL4PRINT(QLP7, printk("scsi%d: %s: unmap mem io region\n",
		    ha->host_no, __func__));

		iounmap(ha->virt_mmapbase);
		ha->virt_mmapbase = NULL;
	}

	if (ha->mem_addr)
		release_mem_region(ha->mem_addr, ha->mem_len);
	ha->mem_addr = 0;

	qla4xxx_free_other_mem(ha);

	LEAVE("qla4xxx_mem_free");
}


/**************************************************************************
* qla2xxx_slave_configure
*
* Description:
**************************************************************************/
int
qla4xxx_slave_configure(struct scsi_device *sdev)
{
	scsi_qla_host_t *ha = to_qla_host(sdev->host);
	int queue_depth;
	os_tgt_t     *tgt_entry;
	os_lun_t     *lun_entry;

	queue_depth = 32;

	/* Enable TCQ. */
	if (sdev->tagged_supported) {
		if (ql4xmaxqdepth != 0 && ql4xmaxqdepth <= 0xffffU)
			queue_depth = ql4xmaxqdepth;

		ql4xmaxqdepth = queue_depth;

		scsi_activate_tcq(sdev, queue_depth);

		ql4_printk(KERN_INFO, ha,
		    "scsi(%d:%d:%d:%d): Enabled tagged queuing, queue "
		    "depth %d.\n", sdev->host->host_no, sdev->channel,
		    sdev->id, sdev->lun, sdev->queue_depth);
	} else {
		 scsi_adjust_queue_depth(sdev, 0 /* TCQ off */,
		     sdev->host->hostt->cmd_per_lun /* 3 */);
	}

	/* Save misc. information. */
	tgt_entry = qla4xxx_lookup_target_by_SCSIID(ha, sdev->channel,
	    sdev->id);
	if (tgt_entry != NULL) {
		lun_entry = qla4xxx_lookup_lun_handle(ha, tgt_entry,
		    sdev->lun);
	        if (lun_entry != NULL) {
			lun_entry->sdev = sdev;
			if (sdev->type == TYPE_TAPE) {
				tgt_entry->fcport->flags |= FCF_TAPE_PRESENT;
			}
		}
	}

	return (0);
}


/*
 * The following support functions are adopted to handle
 * the re-entrant qla4xxx_proc_info correctly.
 */
static void
copy_mem_info(struct info_str *info, char *data, int len)
{
	if (info->pos + len > info->offset + info->length)
		len = info->offset + info->length - info->pos;

	if (info->pos + len < info->offset) {
		info->pos += len;
		return;
	}

	if (info->pos < info->offset) {
		off_t partial;

		partial = info->offset - info->pos;
		data += partial;
		info->pos += partial;
		len  -= partial;
	}

	if (len > 0) {
		memcpy(info->buffer, data, len);
		info->pos += len;
		info->buffer += len;
	}
}

static int
copy_info(struct info_str *info, char *fmt, ...)
{
	va_list args;
	static char buf[256];
	int len;

	va_start(args, fmt);
	len = vsprintf(buf, fmt, args);
	va_end(args);

	copy_mem_info(info, buf, len);

	return(len);
}

/**************************************************************************
 * qla4xxx_proc_dump_srb_info
 *      This routine displays srb information in the proc buffer.
 *
 * Input:
 *      len - length of proc buffer prior to this function's execution.
 *      srb - Pointer to srb to display.
 *
 * Remarks:
 *      This routine is dependent on the DISPLAY_SRBS_IN_PROC #define being
 *      set to 1.
 *
 * Returns:
 *      len - length of proc buffer after this function's execution.
 *
 * Context:
 *      Kernel context.
 **************************************************************************/
inline void
qla4xxx_proc_dump_srb_info(scsi_qla_host_t *ha, struct info_str *info, srb_t *srb)
{
	ddb_entry_t *ddb_entry;
	os_lun_t *lun_entry;

	ddb_entry = qla4xxx_lookup_ddb_by_fw_index(ha, srb->fw_ddb_index);
	lun_entry = srb->lun_queue;

	copy_info(info, "srb %p", srb);

	if (ddb_entry && lun_entry && srb->cmd) {
		struct scsi_cmnd *cmd = srb->cmd;
		//int i;

		copy_info(info, ", b%d,t%d,l%d, SS=%d, DS=%d, LS=%d, "
			  "r_start=%ld, u_start=%ld",
			  cmd->device->channel, cmd->device->id,
			  cmd->device->lun,
			  srb->state,
			  atomic_read(&ddb_entry->state),
			  lun_entry->lun_state,
			  srb->r_start,srb->u_start);

		//copy_info(info, ", cdb=");
		//for (i=0; i<cmd->cmd_len; i++)
		//        copy_info(info, "%02X ", cmd->cmnd[i]);
	}

	copy_info(info, "\n");
}

/**************************************************************************
 * qla4xxx_proc_dump_discovered_devices
 *      This routine displays information for discovered devices in the proc
 *      buffer.
 *
 * Input:
 *      info - length of proc buffer prior to this function's execution.
 *
 * Remarks:
 *      This routine is dependent on the DISPLAY_SRBS_IN_PROC #define being
 *      set to 1.
 *
 * Returns:
 *      info - length of proc buffer after this function's execution.
 *
 * Context:
 *      Kernel context.
 **************************************************************************/
inline void
qla4xxx_proc_dump_discovered_devices(scsi_qla_host_t *ha, struct info_str *info)
{
	int i,j;

	ENTER(__func__);

	copy_info(info, "SCSI discovered device Information:\n");
	copy_info(info, "Index: DID: NameString: Alias:\n");

	for (i=0; i < ha->isns_num_discovered_targets; i++) {
		ISNS_DISCOVERED_TARGET *isns_tgt =
		&ha->isns_disc_tgt_databasev[i];

		copy_info(info, "%2d: %4d:   %s: %s\n",
			  i,
			  isns_tgt->DDID,
			  isns_tgt->NameString,
			  isns_tgt->Alias);

		for (j = 0; j < isns_tgt->NumPortals; j++) {
			ISNS_DISCOVERED_TARGET_PORTAL *isns_portal =
			&isns_tgt->Portal[j];

			copy_info(info, "            Port %d: IP %d.%d.%d.%d\n",
				  isns_portal->PortNumber,
				  isns_portal->IPAddr[0],
				  isns_portal->IPAddr[1],
				  isns_portal->IPAddr[2],
				  isns_portal->IPAddr[3]);
		}
	}
	LEAVE(__func__);
}

/**************************************************************************
 * qla4xxx_proc_dump_scanned_devices
 *      This routine displays information for scanned devices in the proc
 *      buffer.
 *
 * Input:
 *      info - length of proc buffer prior to this function's execution.
 *
 * Remarks:
 *      This routine is dependent on the DISPLAY_SRBS_IN_PROC #define being
 *      set to 1.
 *
 * Returns:
 *      info - length of proc buffer after this function's execution.
 *
 * Context:
 *      Kernel context.
 **************************************************************************/
inline void
qla4xxx_proc_dump_scanned_devices(scsi_qla_host_t *ha, struct info_str *info)
{
	os_lun_t        *up;
	ddb_entry_t     *ddb_entry;
	fc_port_t	*fcport;
	int             t, l;

	ENTER(__func__);
	/* 2.25 node/port display to proc */
	/* Display the node name for adapter */
	copy_info(info, "\nSCSI Device Information:\n");
	copy_info(info,
                  "scsi-qla%d-adapter-port=\"%s\";\n",
                  (int)ha->instance, ha->name_string);
	copy_info(info,
                  "scsi-qla%d-adapter-address=%d.%d.%d.%d: %d.%d.%d.%d;\n",
                  (int)ha->instance, ha->ip_address[0], ha->ip_address[1],
		  ha->ip_address[2], ha->ip_address[3], ha->subnet_mask[0],
		  ha->subnet_mask[1], ha->subnet_mask[2], ha->subnet_mask[3]);
	copy_info(info,
                  "scsi-qla%d-adapter-gateway=%d.%d.%d.%d;\n",
                  (int)ha->instance, ha->gateway[0], ha->gateway[1],
		  ha->gateway[2], ha->gateway[3]);

	list_for_each_entry(fcport, &ha->fcports, list) {
		if(fcport->port_type != FCT_TARGET)
			continue;

		ddb_entry = fcport->ddbptr;
			
		copy_info(info,
	    	"scsi-qla%d-target-%d=ddb 0x%04x: \"%s\": %d.%d.%d.%d;\n",
	    	(int)ha->instance, ddb_entry->target, ddb_entry->fw_ddb_index,
			fcport->iscsi_name,
			ddb_entry->ip_addr[0],
			ddb_entry->ip_addr[1],
			ddb_entry->ip_addr[2],
			ddb_entry->ip_addr[3]);
	}

	
	//copy_info(info, "SCSI scanned device Information:\n");
	copy_info(info, "\nSCSI LUN Information:\n");
	copy_info(info, " (H: B: T: L) * - indicates lun is not registered with the OS.\n");

	/* scan for all equipment stats */
	for (t = 0; t < ha->host->max_id; t++) {
		/* scan all luns */
		for (l = 0; l < ha->host->max_lun; l++) {
			up = (os_lun_t *) GET_LU_Q(ha, t, l);

			if (up == NULL) {
				continue;
			}
			if (up->fclun == NULL) {
				continue;
			}

			if (up->fclun->fcport == NULL) {
				continue;
			}

    			/* don't display luns if OS didn't probe */
			if (up->tot_io_count < 4)
				continue;

			ddb_entry = up->fclun->fcport->ddbptr;
			copy_info(info,
				  "(%2d:%2d:%2d:%2d): Total reqs %ld,",
				  ha->host_no, ddb_entry->bus,
				  t,l,up->tot_io_count);

			copy_info(info,
				  " Active reqs %ld,",
				  up->out_count);

			copy_info(info, "states= %d:%d:%d ",
				  atomic_read(&ddb_entry->state),
				  up->lun_state,
				  ddb_entry->fw_ddb_device_state);

			if (up->tot_io_count < 4) {
				copy_info(info,
					  " flags 0x%lx*,",
					  ddb_entry->flags);
			}
			else {
				copy_info(info,
					  " flags 0x%lx,",
					  ddb_entry->flags);
			}

			copy_info(info,
				  " %d:%d:%02x %02x",
				  up->fclun->fcport->ha->instance,
				  up->fclun->fcport->cur_path,
				  ddb_entry->fw_ddb_index,
				  up->fclun->device_type);

			copy_info(info, "\n");

			if (info->pos >= info->offset + info->length) {
				/* No need to continue */
				return;
			}
		}

		if (info->pos >= info->offset + info->length) {
			/* No need to continue */
			break;
		}
	}
	LEAVE(__func__);
}

/**************************************************************************
 * qla4xxx_proc_info
 *      This routine return information to handle /proc support for the driver
 *
 * Input:
 * Output:
 *      inout  - Decides on the direction of the dataflow and the meaning of
 *               the variables.
 *      buffer - If inout==0 data is being written to it else read from
 *               it (ptrs to a page buffer).
 *      *start - If inout==0 start of the valid data in the buffer.
 *      offset - If inout==0 offset from the beginning of the imaginary
 *               file from which we start writing into the buffer.
 *      length - If inout==0 max number of bytes to be written into the
 *               buffer else number of bytes in the buffer.
 *      hostno - Host number
 *
 * Remarks:
 *      None
 *
 * Returns:
 *      Size of proc buffer.
 *
 * Context:
 *      Kernel context.
 **************************************************************************/
int
qla4xxx_proc_info(struct Scsi_Host *shost, char *buffer, char **start,
    off_t offset, int length, int inout)
{
	int             retval = -EINVAL;
	scsi_qla_host_t *ha = NULL;
	struct info_str info;
	unsigned long   flags;
	srb_t *srb, *stemp;

	QL4PRINT(QLP16, printk("scsi%d: Entering %s: buff_in=%p, "
			       "offset=0x%lx, length=0x%x\n",
			       shost->host_no, __func__, buffer, offset,
			       length));

	ha = (scsi_qla_host_t *) shost->hostdata;

	if (inout) {
		/* Has data been written to the file? */
		QL4PRINT(QLP3, printk("scsi%d: %s: has data been written "
				      "to the file. \n",
				      ha->host_no, __func__));
		return(qla4xxx_set_info(buffer, length, ha->host));
	}

	if (start) {
		*start = buffer;
	}

	info.buffer = buffer;
	info.length = length;
	info.offset = offset;
	info.pos    = 0;

	/* start building the print buffer */
	copy_info(&info, "QLogic iSCSI Adapter for ISP %x:\n",
	    ha->pdev->device);
	copy_info(&info, "Driver version %s\n", QLA4XXX_DRIVER_VERSION);
	copy_info(&info, "Firmware version %2d.%02d.%02d.%02d\n",
	    ha->firmware_version[0], ha->firmware_version[1],
	    ha->patch_number, ha->build_number);
	copy_info(&info, "Code starts at address = %p\n", qla4xxx_set_info);

	if (ha->mem_addr)
		copy_info(&info, "Memory I/O = 0x%lx\n", ha->mem_addr);
	else
		copy_info(&info, "I/O Port = 0x%lx\n", ha->io_addr);

	copy_info(&info, "IP Address = %d.%d.%d.%d\n",
		  ha->ip_address[0], ha->ip_address[1],
		  ha->ip_address[2], ha->ip_address[3]);

	if (ha->tcp_options & TOPT_ISNS_ENABLE) {
		copy_info(&info, "iSNS IP Address = %d.%d.%d.%d\n",
			  ha->isns_ip_address[0], ha->isns_ip_address[1],
			  ha->isns_ip_address[2], ha->isns_ip_address[3]);
		copy_info(&info, "iSNS Server Port# = %d\n",
			  ha->isns_server_port_number);
	}
#if 0
	copy_info(&info, "ReqQ DMA= 0x%lx, virt= 0x%p, depth= 0x%x\n",
		  (unsigned long)ha->request_dma, ha->request_ring, REQUEST_QUEUE_DEPTH);
	copy_info(&info, "ComplQ DMA= 0x%lx, virt= 0x%p, depth= 0x%x\n",
		  (unsigned long)ha->response_dma, ha->response_ring, RESPONSE_QUEUE_DEPTH);
	copy_info(&info, "Shadow Regs DMA= 0x%lx, virt= 0x%p, size (bytes) = 0x%x\n",
		  (unsigned long)ha->shadow_regs_dma, ha->shadow_regs, sizeof(shadow_regs_t));
	copy_info(&info, "PDU Buffer Addr= 0x%x, size (bytes) = 0x%x\n",
		  ha->pdu_buffsv, ha->pdu_buff_size);
					
	copy_info(&info, "Discovered Target Database Addr = 0x%x, size (bytes) = 0x%x\n",
		  ha->isns_disc_tgt_databasev,
		  sizeof(ha->isns_disc_tgt_databasev));
#endif
	copy_info(&info, "Number of free request entries  = %d of %d\n",
		  ha->req_q_count, REQUEST_QUEUE_DEPTH);
	//copy_info(&info, "Number of free aen entries    = %d of %d\n",
	//	  ha->aen_q_count, MAX_AEN_ENTRIES);
	copy_info(&info, "Number of Mailbox Timeouts = %d\n",
		  ha->mailbox_timeout_count);

	spin_lock_irqsave(&ha->hardware_lock, flags);
	copy_info(&info, "Interrupt Status = %d\n",
		  RD_REG_DWORD(&ha->reg->ctrl_status));
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	
	copy_info(&info, "ReqQptr=%p, ReqIn=%d, ReqOut=%d\n",
		  ha->request_ptr, ha->request_in, ha->request_out);
	copy_info(&info, "Device queue depth = 0x%x\n",
		  (ql4xmaxqdepth == 0) ? 16 : ql4xmaxqdepth);
	copy_info(&info, "Adapter flags = 0x%x, DPC flags = 0x%x\n",
		  ha->flags, ha->dpc_flags);

	copy_info(&info, "Number of commands in retry_srb_q = %d\n",
		  ha->retry_srb_q_count);

	if (((ql_dbg_level & QLP16) != 0) && (ha->retry_srb_q_count)) {
		copy_info(&info, "\nDump retry_srb_q:\n");
		spin_lock_irqsave(&ha->list_lock, flags);
		list_for_each_entry_safe(srb, stemp, &ha->retry_srb_q,
		    list_entry)
			qla4xxx_proc_dump_srb_info(ha, &info, srb);
		spin_unlock_irqrestore(&ha->list_lock, flags);
		copy_info(&info, "\n");
	}

	copy_info(&info, "Number of commands in done_srb_q = %d\n",
		  ha->done_srb_q_count);

	if (((ql_dbg_level & QLP16) != 0) && (ha->done_srb_q_count)) {
		copy_info(&info, "\nDump done_srb_q:\n");
		spin_lock_irqsave(&ha->list_lock, flags);
		list_for_each_entry_safe(srb, stemp, &ha->done_srb_q,
		    list_entry)
			qla4xxx_proc_dump_srb_info(ha, &info, srb);
		spin_unlock_irqrestore(&ha->list_lock, flags);
		copy_info(&info, "\n");
	}
	copy_info(&info, "Keep Alive Timeout = %d\n", ha->port_down_retry_count);
	
	copy_info(&info, "Number of active commands = %d\n",
		  ha->active_srb_count);

	if (((ql_dbg_level & QLP16) != 0) && (ha->active_srb_count)) {
		int i;

		spin_lock_irqsave(&ha->hardware_lock, flags);
		copy_info(&info, "\nDump active commands:\n");
		for (i = 1; i < MAX_SRBS; i++) {
			srb_t *srb = ha->active_srb_array[i];
			if (srb)
				qla4xxx_proc_dump_srb_info(ha, &info, srb);
		}
		copy_info(&info, "\n");
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
	}

	copy_info(&info, "Total number of IOCBs (used/max) "
		  "= (%d/%d)\n", ha->iocb_cnt, ha->iocb_hiwat);
	copy_info(&info, "Number of free srbs    = %d of %d\n",
		  ha->free_srb_q_count, ha->num_srbs_allocated);
	copy_info(&info, "\n");

	qla4xxx_proc_dump_scanned_devices(ha, &info);
	copy_info(&info, "\n");

	if (test_bit(ISNS_FLAG_ISNS_ENABLED_IN_ISP, &ha->isns_flags))
		qla4xxx_proc_dump_discovered_devices(ha, &info);

	copy_info(&info, "\0");

	retval = info.pos > info.offset ? info.pos - info.offset : 0;

	QL4PRINT(QLP16, printk("scsi%d: Exiting %s: info.pos=%d, "
			       "offset=0x%lx, length=0x%x\n",
			       ha->host_no, __func__, info.pos, offset, length));

	return(retval);
}


/**************************************************************************
 * del_from_active_array
 *      This routine removes and returns the srb at the specified index
 *
 * Input:
 *      ha - Pointer to host adapter structure.
 *      index - index into to the active_array
 *
 * Returns:
 *      Pointer to corresponding SCSI Request Block
 *
 * Context:
 *      Kernel/Interrupt context.
 **************************************************************************/
srb_t *
del_from_active_array(scsi_qla_host_t *ha, uint32_t index)
{
	srb_t *srb = NULL;

	/* validate handle and remove from active array */
	if (index < MAX_SRBS) {
		srb = ha->active_srb_array[index];
		ha->active_srb_array[index] = 0;

		if (srb) {
			os_lun_t *lun_entry = srb->lun_queue;

			/* update counters */
			if ((srb->flags & SRB_DMA_VALID) != 0) {
				ha->req_q_count += srb->entry_count;
				ha->iocb_cnt -= srb->entry_count;
				if (ha->active_srb_count)
					ha->active_srb_count--;
				if (lun_entry)
					lun_entry->out_count--;
				if (srb->cmd)
					srb->cmd->host_scribble = NULL;
			}
		}
		else
			QL4PRINT(QLP2,
				 printk("scsi%d: %s: array_index=%d "
					"already completed.\n",
					ha->host_no, __func__, index));
	}
	else
		QL4PRINT(QLP2, printk("scsi%d: %s: array_index=%d "
				      "exceeded max index of %d\n",
				      ha->host_no, __func__, index, MAX_SRBS));

	return(srb);
}

uint16_t
qla4xxx_calc_request_entries(uint16_t dsds)
{
	uint16_t iocbs;/* number of request queue entries */
				/* (commamd + continue) */
	iocbs = 1;
	if (dsds > COMMAND_SEG) {
		iocbs += (dsds - COMMAND_SEG) / CONTINUE_SEG;
		if ((dsds - COMMAND_SEG) % CONTINUE_SEG)
			iocbs++;
	}
	return (iocbs);
}

void
qla4xxx_build_scsi_iocbs(srb_t *srb, COMMAND_ENTRY *cmd_entry, uint16_t tot_dsds)
{
	scsi_qla_host_t	*ha;
	uint16_t	avail_dsds;
	DATA_SEG_A64 *cur_dsd;
	struct scsi_cmnd *cmd;

	cmd = srb->cmd;
	ha = srb->ha;

	if (cmd->request_bufflen == 0 ||
	    cmd->sc_data_direction == DMA_NONE) {
		/* No data being transferred */
		QL4PRINT(QLP5, printk("scsi%d:%d:%d:%d: %s: No data xfer\n",
		    ha->host_no, cmd->device->channel, cmd->device->id,
		    cmd->device->lun, __func__));

		cmd_entry->ttlByteCnt = __constant_cpu_to_le32(0);
		return;
	}

	avail_dsds = COMMAND_SEG;
	cur_dsd = (DATA_SEG_A64 *) &(cmd_entry->dataseg[0]);

	/* Load data segments */
	if (cmd->use_sg) {
		struct  scatterlist *cur_seg;
		struct  scatterlist *end_seg;

		/* Data transfer with Scatter/Gather
		 *
		 * We must build an SG list in adapter format, as the kernel's
		 * SG list cannot be used directly because of data field size
		 * (__alpha__) differences and the kernel SG list uses virtual
		 * addresses where we need physical addresses.
		 */
		cur_seg = (struct scatterlist *) cmd->request_buffer;
		end_seg = cur_seg + tot_dsds;

		while (cur_seg < end_seg) {
			dma_addr_t sle_dma;

			/* Allocate additional continuation packets? */
			if (avail_dsds == 0) {
				CONTINUE_ENTRY *cont_entry;

				cont_entry = qla4xxx_alloc_cont_entry(ha);
				cur_dsd = (DATA_SEG_A64 *) &cont_entry->dataseg[0];
				avail_dsds = CONTINUE_SEG;
			}

			sle_dma = sg_dma_address(cur_seg);
			cur_dsd->base.addrLow = cpu_to_le32(LSDW(sle_dma));
			cur_dsd->base.addrHigh = cpu_to_le32(MSDW(sle_dma));
			cur_dsd->count = cpu_to_le32(sg_dma_len(cur_seg));
			avail_dsds--;

			DEBUG(printk("scsi%d:%d:%d:%d: %s: S/G "
			    "DSD %p phys_addr=%x:%08x, len=0x%x, tot_dsd=0x%x, "
			    "avail_dsd=0x%x\n", ha->host_no,
			    cmd->device->channel, cmd->device->id,
			    cmd->device->lun, __func__, cur_dsd,
			    cur_dsd->base.addrHigh, cur_dsd->base.addrLow,
			    cur_dsd->count, tot_dsds, avail_dsds));

			cur_dsd++;
			cur_seg++;
	}
	} else {
		/* Data transfer without SG entries. */
		dma_addr_t	req_dma;
		struct page	*page;
		unsigned long	offset;

		page = virt_to_page(cmd->request_buffer);
		offset = ((unsigned long) cmd->request_buffer & ~PAGE_MASK);
		req_dma = pci_map_page(ha->pdev, page, offset,
		    cmd->request_bufflen, cmd->sc_data_direction);
		srb->saved_dma_handle = req_dma;

		cur_dsd->base.addrLow = cpu_to_le32(LSDW(req_dma));
		cur_dsd->base.addrHigh = cpu_to_le32(MSDW(req_dma));
		cur_dsd->count = cpu_to_le32(cmd->request_bufflen);

		QL4PRINT(QLP5, printk("scsi%d:%d:%d:%d: %s: No S/G transfer, "
		    "DSD=%p cmd=%p dma_addr=%x:%08x, len=%x, tot_dsd=0x%x, "
		    "avail_dsd=0x%x\n", ha->host_no, cmd->device->channel,
		    cmd->device->id, cmd->device->lun, __func__, cur_dsd, cmd,
		    cur_dsd->base.addrHigh, cur_dsd->base.addrLow,
		    cur_dsd->count, tot_dsds, avail_dsds));

		cur_dsd++;
	}
}

CONTINUE_ENTRY *
qla4xxx_alloc_cont_entry(scsi_qla_host_t *ha)
{
        CONTINUE_ENTRY *cont_entry;
        ENTER("qla4xxx_alloc_cont_entry");

        cont_entry = (CONTINUE_ENTRY *)ha->request_ptr;

        /* Advance request queue pointer */
        if (ha->request_in == (REQUEST_QUEUE_DEPTH - 1)) {
                ha->request_in = 0;
                ha->request_ptr = ha->request_ring;
                QL4PRINT(QLP10, printk("scsi%d: %s: wraparound -- new "
                    "request_in = %04x, new request_ptr = %p\n", ha->host_no,
                    __func__, ha->request_in, ha->request_ptr));
        } else {
		ha->request_in++;
                ha->request_ptr++;
                QL4PRINT(QLP10, printk("scsi%d: %s: new request_in = %04x, new "
                    "request_ptr = %p\n", ha->host_no, __func__, ha->request_in,
                    ha->request_ptr));
        }

        /* Load packet defaults */
        cont_entry->hdr.entryType = ET_CONTINUE;
        cont_entry->hdr.entryCount = 1;
        cont_entry->hdr.systemDefined =
        (uint8_t) cpu_to_le16(ha->request_in);

        LEAVE("qla4xxx_alloc_cont_entry");
        return(cont_entry);
}

/**************************************************************************
 * qla4xxx_send_command_to_isp
 *      This routine is called by qla4xxx_queuecommand to build an ISP
 *      command and pass it to the ISP for execution.
 *
 * Input:
 *      ha - Pointer to host adapter structure.
 *      srb - pointer to SCSI Request Block to be sent to ISP
 *
 * Output:
 *      None
 *
 * Remarks:
 *      None
 *
 * Returns:
 *      QLA_SUCCESS - Successfully sent command to ISP
 *      QLA_ERROR   - Failed to send command to ISP
 *
 * Context:
 *      Kernel context.
 **************************************************************************/
uint8_t
qla4xxx_send_command_to_isp(scsi_qla_host_t *os_ha, srb_t *srb)
{
	struct scsi_cmnd     *cmd = srb->cmd;
	ddb_entry_t   *ddb_entry;
	os_lun_t   *lun_entry;
	COMMAND_ENTRY *cmd_entry;
	struct scatterlist *sg = NULL;

	uint16_t      tot_dsds;	/* number of data segments */
				/* (sg entries, if sg request) */
	uint16_t	req_cnt;	/* number of request queue entries */

	unsigned long flags;
	uint16_t	cnt;
	uint16_t      i;
	uint32_t      index;
	fc_lun_t                *fclun;
	scsi_qla_host_t *ha;
	char		tag[2];
	uint32_t	timeout;

	ENTER("qla4xxx_send_command_to_isp");

	/* Get real lun and adapter */
	fclun = srb->lun_queue->fclun;
	ha = fclun->fcport->ha;

	cmd = srb->cmd;
	ddb_entry = fclun->fcport->ddbptr;
	lun_entry = srb->lun_queue;

	/* Send marker(s) if needed. */
	if (ha->marker_needed == 1) {
		if (qla4xxx_send_marker_iocb(ha, ddb_entry, fclun) !=
		    QLA_SUCCESS) {
			return(QLA_ERROR);
		}
		ha->marker_needed = 0;
	}

	/* Acquire hardware specific lock */
	spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Check for room in active srb array */
	index = ha->current_active_index;
	for (i = 0; i < MAX_SRBS; i++) {
		index++;
		if (index == MAX_SRBS)
			index = 1;
		if (ha->active_srb_array[index] == 0) {
			ha->current_active_index = index;
			break;
		}
	}
	if (i >= MAX_SRBS) {
		printk(KERN_INFO "scsi%d: %s: NO more SRB entries"
		    "used iocbs=%d, \n reqs remaining=%d\n", ha->host_no,
		    __func__, ha->iocb_cnt, ha->req_q_count);
		goto queuing_error;
	}


	/* Calculate the number of request entries needed. */
	tot_dsds = 0;
	if (cmd->use_sg) {
		sg = (struct scatterlist *) cmd->request_buffer;
		tot_dsds = pci_map_sg(ha->pdev, sg, cmd->use_sg,
		    cmd->sc_data_direction);
		if (tot_dsds == 0)
			goto queuing_error;
	} else if (cmd->request_bufflen) {
	    tot_dsds++;
	}
	req_cnt = qla4xxx_calc_request_entries(tot_dsds);

	if (ha->req_q_count < (req_cnt + 2)) {
		// cnt = RD_REG_WORD_RELAXED(ISP_REQ_Q_OUT(ha));
	    	cnt = (uint16_t) le32_to_cpu(ha->shadow_regs->req_q_out);
		if (ha->request_in < cnt) {
			ha->req_q_count = cnt - ha->request_in;
		}
		else {
			ha->req_q_count = REQUEST_QUEUE_DEPTH - (ha->request_in - cnt);
		}

		DEBUG(printk(KERN_INFO "scsi%d: %s: new request queue count -"
		    "calc req_q_count=%d, reqs needed=%d, shadow reqs=%d, req_in =%d\n", ha->host_no,
		    __func__, ha->req_q_count, req_cnt, cnt, ha->request_in));
	}

	if (ha->req_q_count < (req_cnt + 2)) {
		/* free the resources */
		if (cmd->use_sg)
			pci_unmap_sg(ha->pdev, sg, cmd->use_sg, cmd->sc_data_direction);

		DEBUG(printk(KERN_INFO "scsi%d: %s: No more request queues, "
		    "used iocbs=%d, \n reqs remaining=%d, reqs needed=%d\n",
	            ha->host_no,
		    __func__, ha->iocb_cnt, ha->req_q_count, req_cnt));
		goto queuing_error;
	}

	/* total iocbs active */
	if ((ha->iocb_cnt + req_cnt)  >= REQUEST_QUEUE_DEPTH ) {
		if (cmd->use_sg)
			pci_unmap_sg(ha->pdev, sg, cmd->use_sg, cmd->sc_data_direction);
		goto queuing_error;
	}

	/* Build command packet */
	cmd_entry = (COMMAND_ENTRY *) ha->request_ptr;
	memset(cmd_entry, 0, sizeof(COMMAND_ENTRY));
	cmd_entry->hdr.entryType = ET_COMMAND;
	cmd_entry->handle = cpu_to_le32(index);
	cmd_entry->target = cpu_to_le16(ddb_entry->fw_ddb_index);
	cmd_entry->connection_id = cpu_to_le16(ddb_entry->connection_id);
	
	cmd_entry->lun[1] = LSB(cmd->device->lun);	/* SAMII compliant. */
	cmd_entry->lun[2] = MSB(cmd->device->lun);
	cmd_entry->cmdSeqNum = cpu_to_le32(ddb_entry->CmdSn);
	cmd_entry->ttlByteCnt = cpu_to_le32(cmd->request_bufflen);
	memcpy(cmd_entry->cdb, cmd->cmnd, MIN(MAX_COMMAND_SIZE, cmd->cmd_len));
	cmd_entry->dataSegCnt = cpu_to_le16(tot_dsds);
	cmd_entry->hdr.entryCount = req_cnt;

	/* We want the firmware to time out the command first.*/
	timeout = (uint32_t)(cmd->timeout_per_command / HZ);
	if (timeout > 65535)
                cmd_entry->timeout = __constant_cpu_to_le16(0);
	else if (timeout > MIN_CMD_TOV)
		cmd_entry->timeout = cpu_to_le16((uint16_t) timeout -
					 (QLA_CMD_TIMER_DELTA+1));
	else
		cmd_entry->timeout = cpu_to_le16((uint16_t)timeout);

	srb->iocb_tov = cmd_entry->timeout;
	srb->os_tov = timeout;

	/* Set data transfer direction control flags
	 * NOTE: Look at data_direction bits iff there is data to be
	 *       transferred, as the data direction bit is sometimed filled
	 *       in when there is no data to be transferred */
	cmd_entry->control_flags = CF_NO_DATA;
	if (cmd->request_bufflen) {
		if (cmd->sc_data_direction == DMA_TO_DEVICE)
			cmd_entry->control_flags = CF_WRITE;
		else if (cmd->sc_data_direction == DMA_FROM_DEVICE)
			cmd_entry->control_flags = CF_READ;
	}

	/* Set tagged queueing control flags */
	cmd_entry->control_flags |= CF_SIMPLE_TAG;
	if (scsi_populate_tag_msg(cmd, tag)) {
		switch (tag[0]) {
		case MSG_HEAD_TAG:
			cmd_entry->control_flags |= CF_HEAD_TAG;
			break;
		case MSG_ORDERED_TAG:
			cmd_entry->control_flags |= CF_ORDERED_TAG;
			break;
		}
	}

	/* Advance request queue pointer */
	ha->request_in++;
        if (ha->request_in == REQUEST_QUEUE_DEPTH ) {
                ha->request_in = 0;
                ha->request_ptr = ha->request_ring;
        } else {
                ha->request_ptr++;
        }

	qla4xxx_build_scsi_iocbs(srb, cmd_entry, tot_dsds);

	wmb();

	/*
	 * Check to see if adapter is online before placing request on
	 * request queue.  If a reset occurs and a request is in the queue,
	 * the firmware will still attempt to process the request, retrieving
	 * garbage for pointers.
	 */
	if (!test_bit(AF_ONLINE, &ha->flags)) {
		QL4PRINT(QLP2, printk("scsi%d: %s: Adapter OFFLINE! "
				      "Do not issue command.\n",
                                      ha->host_no, __func__));
		goto queuing_error;
	}

	/* put command in active array */
	ha->active_srb_array[index] = srb;
	srb->cmd->host_scribble = (unsigned char *)(unsigned long)index;
	//srb->active_array_index = index;

	/* update counters */
	ha->active_srb_count++;
	lun_entry->out_count++;
	lun_entry->tot_io_count++;
	srb->state = SRB_ACTIVE_STATE;
	srb->flags |= SRB_DMA_VALID;

	/* Track IOCB used */
	ha->iocb_cnt += req_cnt;
	srb->entry_count = req_cnt;
	ha->req_q_count -= req_cnt;

	/* Debug print statements */
#ifdef QL_DEBUG_LEVEL_3
	printk("scsi%d:%d:%d:%d: %s: CDB = ", ha->host_no,
	    cmd->device->channel, cmd->device->target, cmd->device->lun,
	    __func__);
	for (i = 0; i < cmd->cmd_len; i++)
		printk("%02x ", cmd->cmnd[i]);
	printk("\n");
#endif
	srb->u_start = jiffies;
	WRT_REG_DWORD(&ha->reg->req_q_in, ha->request_in);
	PCI_POSTING(&ha->reg->req_q_in);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	LEAVE("qla4xxx_send_command_to_isp");

	return(QLA_SUCCESS);

queuing_error:
	/* Release hardware specific lock */
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	LEAVE("qla4xxx_send_command_to_isp");

	return(QLA_ERROR);
}

/**************************************************************************
* qla4xxx_done
*      Process completed commands.
*
* Input:
*      old_ha           = adapter block pointer.
*
* Returns:
* int
**************************************************************************/
int
qla4xxx_done(scsi_qla_host_t *old_ha)
{
	os_lun_t        *lq;
	struct scsi_cmnd       *cmd;
	unsigned long   flags = 0;
	scsi_qla_host_t *ha;
	scsi_qla_host_t *vis_ha;
	int             cnt;
	srb_t           *srb, *stemp;
	struct  list_head local_sp_list;

	ENTER(__func__);

	cnt = 0;

	INIT_LIST_HEAD(&local_sp_list);

	/*
	 * Get into local queue such that we do not wind up calling done queue
	 * takslet for the same IOs from DPC or any other place.
	 */
	spin_lock_irqsave(&old_ha->list_lock,flags);
	list_splice_init(&old_ha->done_srb_q, &local_sp_list);
	spin_unlock_irqrestore(&old_ha->list_lock, flags);

	list_for_each_entry_safe(srb, stemp, &local_sp_list, list_entry) {
		old_ha->done_srb_q_count--;
		srb->state = SRB_NO_QUEUE_STATE;
		list_del_init(&srb->list_entry);

		cnt++;

		cmd = srb->cmd;
		if (cmd == NULL) {
#if 0
			panic("%s: SP %p already freed - %s %d.\n",
			      __func__, srb, __FILE__,__LINE__);
#else
			continue;
#endif
		}

		if (cmd->device == NULL) {
#if 0
			panic("%s: SP %p already freed - %s %d.\n",
			      __func__, srb, __FILE__,__LINE__);
#else
			//DEBUG2(printk("%s: sp %p already freed\n", __func__, srb);)
			//DEBUG2(__dump_dwords(srb, sizeof(*srb)));
			continue;
#endif
		}

		vis_ha = (scsi_qla_host_t *)cmd->device->host->hostdata;
		lq = srb->lun_queue;
		if( lq == NULL ) {
			DEBUG2(printk("%s: lq == NULL  , sp= %p, %s %d \n",
			      __func__, srb, __FILE__,__LINE__);)
			continue;
		}
		if( lq->fclun == NULL ) {
			DEBUG2(printk("%s: lq->fclun == NULL  , sp=%p %s %d \n",
			       __func__, srb,__FILE__,__LINE__);)
			continue;
		}
		if( lq->fclun->fcport == NULL ) {
			DEBUG2(printk("%s: lq->fclun->fcport == NULL  , sp=%p %s %d \n",
			       __func__, srb,__FILE__,__LINE__);)
			continue;
		}
		ha = srb->ha;
		/* Release memory used for this I/O */
		if ((srb->flags & SRB_DMA_VALID) != 0) {
			srb->flags &= ~SRB_DMA_VALID;
	
			/* Release memory used for this I/O */
			if (cmd->use_sg) {
				pci_unmap_sg(ha->pdev,
					     cmd->request_buffer,
					     cmd->use_sg,
					     cmd->sc_data_direction);
			} else if (cmd->request_bufflen) {
				pci_unmap_page(ha->pdev,
					       srb->saved_dma_handle,
					       cmd->request_bufflen,
					       cmd->sc_data_direction);
			}
	
			ha->total_mbytes_xferred += cmd->request_bufflen / 1024;
			}

		sp_put(vis_ha, srb);

	} /* end of while */

	LEAVE(__func__);

	return(cnt);
}

/**************************************************************************
 * qla4xxx_request_cleanup
 *      This routine frees resources for a command that
 *      didn't get completed.
 *
 * Input:
 *      ha - Pointer to host adapter structure.
 *      srb - Pointer to SCSI Request Block
 *
 * Remarks:
 *    The srb pointer should be guaranteed to be nonzero before calling
 *    this function.  The caller should also ensure that the list_lock is
 *    released before calling this function.
 *
 * Returns:
 *
 * Context:
 *      Kernel/Interrupt context.
 **************************************************************************/
void
qla4xxx_request_cleanup(scsi_qla_host_t *ha, srb_t *srb)
{
	struct scsi_cmnd *cmd;

	qla4xxx_delete_timer_from_cmd(srb);

	cmd = srb->cmd;
	/*  Let abort handler know we are completing the command */
	CMD_SP(cmd) = NULL;

	/* Release memory used for this I/O */
	if ( (srb->flags & SRB_DMA_VALID) ) {
		srb->flags &= ~SRB_DMA_VALID;

		/* Release memory used for this I/O */
		if (cmd->use_sg) {
			pci_unmap_sg(ha->pdev,
				     cmd->request_buffer,
				     cmd->use_sg,
				     cmd->sc_data_direction);
		}
		else if (cmd->request_bufflen) {
			pci_unmap_page(ha->pdev,
				       srb->saved_dma_handle,
				       cmd->request_bufflen,
				       srb->cmd->sc_data_direction);
		}
	}

	srb->cmd = NULL;
	add_to_free_srb_q(ha, srb);

}

/**************************************************************************
 * qla4xxx_complete_request
 *      This routine returns a command to the caller via the done_fn
 *      specified in the cmd structure.
 *
 * Input:
 *      ha - Pointer to host adapter structure.
 *      srb - Pointer to SCSI Request Block
 *
 * Remarks:
 *    The srb pointer should be guaranteed to be nonzero before calling
 *    this function.  The caller should also ensure that the list_lock is
 *    released before calling this function.
 *
 * Returns:
 *      QLA_SUCCESS - Successfully completed request
 *      QLA_ERROR   - Failed to complete request
 *
 * Context:
 *      Kernel/Interrupt context.
 **************************************************************************/
uint8_t
qla4xxx_complete_request(scsi_qla_host_t *ha, srb_t *srb)
{
	uint8_t status = QLA_ERROR;
	struct scsi_cmnd *cmd;

	//ENTER("qla4xxx_complete_request");

	/* Make sure the cmd pointer is valid */
	if (srb == NULL) {
		QL4PRINT(QLP2, printk("scsi%d: %s: ERROR: NULL srb \n",
				      ha->host_no, __func__));
		goto exit_complete_request;
	}
	if ((srb->flags & SRB_FREE_STATE) == 0)
		qla4xxx_delete_timer_from_cmd(srb);

	cmd = srb->cmd;
	if (cmd == NULL) {
		QL4PRINT(QLP2,
			 printk("scsi%d: %s: ERROR: NULL cmd pointer in "
				"srb=%p\n", ha->host_no, __func__, srb));

		goto exit_complete_request;
	}

	/*  Let abort handler know we are completing the command */
	CMD_SP(cmd) = NULL;


	/* Release memory used for this I/O */
	if ((srb->flags & SRB_DMA_VALID) != 0) {
		srb->flags &= ~SRB_DMA_VALID;

		/* Release memory used for this I/O */
		if (cmd->use_sg) {
			QL4PRINT(QLP5,
				 printk("scsi%d: %s: S/G unmap_sg cmd=%p\n",
					ha->host_no, __func__, cmd));

			pci_unmap_sg(ha->pdev,
				     cmd->request_buffer,
				     cmd->use_sg,
				     cmd->sc_data_direction);
		}
		else if (cmd->request_bufflen) {
			QL4PRINT(QLP5,
				 printk("scsi%d: %s: No S/G unmap_single "
					"cmd=%p saved_dma_handle=%x\n",
					ha->host_no, __func__, cmd,
					(uint32_t) srb->saved_dma_handle));

			pci_unmap_page(ha->pdev,
				       srb->saved_dma_handle,
				       cmd->request_bufflen,
				       srb->cmd->sc_data_direction);
		}

		ha->total_mbytes_xferred += cmd->request_bufflen / 1024;
	}

	if (host_byte(cmd->result) == DID_OK) {
		if (!(srb->flags & SRB_GOT_SENSE)) {
			os_lun_t *lun_entry = srb->lun_queue;
			unsigned long flags;

			if (lun_entry) {
				/*
				 * If lun was not ready (suspended or timeout)
				 * then change state to "READY".
				 */
				spin_lock_irqsave(&lun_entry->lun_lock, flags);
				if (lun_entry->lun_state != LS_LUN_READY) {
					lun_entry->lun_state = LS_LUN_READY;
				}
				spin_unlock_irqrestore(&lun_entry->lun_lock, flags);
			}
		}
	}

	#ifdef DEBUG
	/* debug prints */
	// qla4xxx_dump_command(ha, cmd);

	#endif
#if 0
	if (host_byte(cmd->result) != DID_OK ) {
			    DEBUG2(printk("scsi%d:%d:%d:%d: %s: "
				"did_error=%d, cmd=%p cbd[0]=%02X, pid=%ld\n",
				ha->host_no, cmd->device->channel, cmd->device->id,
				cmd->device->lun,
				__func__,
				host_byte(cmd->result),
				cmd, cmd->data_cmnd[0],
    				cmd->serial_number));
	}
#endif
	/*
	 * WORKAROUND
	 * A backdoor device-reset (via eh_resets) requires different
	 * error handling.  This code differentiates between normal
	 * error handling and the backdoor method
	 */
	if (host_byte(cmd->result) == DID_RESET) {
		if (qla4xxx_is_eh_active(ha->host))
			// srb->cmd->result = DID_IMM_RETRY << 16;
			srb->cmd->result = DID_BUS_BUSY << 16;
	}

#ifdef QL_DEBUG_LEVEL_3
	if (cmd->result & 0xff) {
		QL4PRINT(QLP13,
			 printk("REQUEST_SENSE data:  "
				"(MAX 0x20 bytes displayed)\n"));

		qla4xxx_dump_bytes(QLP13, cmd->sense_buffer,
				   MIN(0x20, sizeof(cmd->sense_buffer)));
	}
#endif

	/* Call the mid-level driver interrupt handler */
	srb->cmd = NULL;
	add_to_free_srb_q(ha, srb);


	// CMD_SP(cmd) = NULL;
	(*(cmd)->scsi_done)(cmd);

	exit_complete_request:
	//LEAVE("qla4xxx_complete_request");

	return(status);
}

/**************************************************************************
 * qla4xxx_queuecommand
 *      This routine is invoked by Linux to send a SCSI command to the driver.
 *
 * Input:
 *      cmd - Pointer to Linux's SCSI command structure
 *      done_fn - Function that the driver calls to notify the SCSI mid-layer
 *                that the command has been processed.
 *
 * Remarks:
 *    The mid-level driver tries to ensure that queuecommand never gets
 *    invoked concurrently with itself or the interrupt handler (although
 *    the interrupt handler may call this routine as part of request-
 *    completion handling).   Unfortunely, it sometimes calls the scheduler
 *    in interrupt context which is a big NO! NO!.
 *
 * Returns:
 *      None
 *
 * Context:
 *      Kernel context.
 **************************************************************************/
int
qla4xxx_queuecommand(struct scsi_cmnd *cmd, void (*done_fn)(struct scsi_cmnd *))
{
	scsi_qla_host_t *ha;
	ddb_entry_t     *ddb_entry;
	os_lun_t     *lun_entry;
	os_tgt_t     *tgt_entry;
	uint32_t        b, t, l;
	int             return_status = 0;
	srb_t           *srb;
	fc_port_t       *fcport;
	fc_lun_t       *fclun;

	b = cmd->device->channel;
	t = cmd->device->id;
	l = cmd->device->lun;
	ha = (scsi_qla_host_t *) cmd->device->host->hostdata;

	cmd->scsi_done = done_fn;
	spin_unlock_irq(ha->host->host_lock);

	/*
	 * Allocate a comand packet
	 */
	srb = del_from_free_srb_q_head(ha);
	if (srb == NULL) {
		DEBUG2(printk("scsi%d: %s: srb not available\n"
		    , ha->host_no, __func__);)
		DEBUG2(printk("Number of free srbs   = %d of %d\n",
		  ha->free_srb_q_count, ha->num_srbs_allocated);)

		spin_lock_irq(ha->host->host_lock);

		return (1);
	}

	/* Link the srb with cmd */
	srb->cmd = cmd;
	CMD_SP(cmd) = (void *)srb;
	srb->r_start = jiffies;
	srb->flags = 0;
	srb->err_id = 0;
	srb->ha = ha;
	srb->iocb_cnt = 0;

	/* Start command timer. */
	if ((cmd->timeout_per_command/HZ) > MIN_CMD_TOV)
		qla4xxx_add_timer_to_cmd(srb, (cmd->timeout_per_command / HZ) -
		    QLA_CMD_TIMER_DELTA);
	else
		qla4xxx_add_timer_to_cmd(srb, (cmd->timeout_per_command / HZ));

	/* retrieve device and lun handles */
	tgt_entry = qla4xxx_lookup_target_by_SCSIID(ha, b, t);
	if (tgt_entry ==  NULL) {
		cmd->result = DID_NO_CONNECT << 16;
		goto qc_complete;
	}
	
	lun_entry = qla4xxx_lookup_lun_handle(ha, tgt_entry, l);
	if (lun_entry == NULL) {

		/* If no target, don't create lun */
		if (tgt_entry->fcport == NULL) {
			cmd->result = DID_NO_CONNECT << 16;
			goto qc_complete;
		}

		/* Only allocate luns for ddb device state SESSION_ACTIVE */
		if (tgt_entry->fcport->ddbptr->fw_ddb_device_state != DDB_DS_SESSION_ACTIVE) {
			cmd->result = DID_NO_CONNECT << 16;
			goto qc_complete;
		}

		/*
		 * Allocate a LUN queue for this request if we haven't
		 * already did it on a previous command.
		 */
		fcport = tgt_entry->fcport;
		fclun = qla4xxx_add_fclun(fcport, l);
		if (fclun == NULL) {
			DEBUG2(printk("%s: Can't get FCLUN queue.\n",
			    __func__);)
			cmd->result = DID_ERROR << 16;
			goto qc_complete;
		}
		
		/* Assume this type right now and fixup after command completes */
		fclun->device_type = TYPE_DISK;
		lun_entry = qla4xxx_fclun_bind(ha, fcport, fclun);
		if( lun_entry  == NULL ) {
			DEBUG2(printk("%s: Can't Bind or allocate LUN queue.\n",
		    __func__);)
			cmd->result = DID_ERROR << 16;
			goto qc_complete;
		}
	}

	srb->tgt_queue = tgt_entry;
	srb->lun_queue = lun_entry;
	srb->fclun = lun_entry->fclun;
	if (lun_entry->fclun == NULL) {
		cmd->result = DID_NO_CONNECT << 16;
		DEBUG2(printk(
		    "scsi%d: (lq->fclun == NULL) pid=%ld,lq=%p\n",
    		ha->host_no, srb->cmd->serial_number, lun_entry));
		goto qc_complete;
	}
	fcport = lun_entry->fclun->fcport;
	if (fcport ==  NULL) {
		cmd->result = DID_NO_CONNECT << 16;
		DEBUG2(printk(
		 "scsi%d: (lq->fclun->fcport == NULL) pid=%ld, lq=%p,"
		 "lq->fclun=%p\n",
    		ha->host_no, srb->cmd->serial_number,
		lun_entry, lun_entry->fclun));
		goto qc_complete;
	}

	ddb_entry = fcport->ddbptr;
	if (ddb_entry ==  NULL) {
		cmd->result = DID_NO_CONNECT << 16;
		DEBUG2(printk("scsi%d: (ddbptr == NULL) pid=%ld, ddb entry=%p\n",
    		ha->host_no, srb->cmd->serial_number, ddb_entry));
		goto qc_complete;
	}
	srb->ha = fcport->ha;

	/* Only modify the allowed count if the target is a *non* tape device */
	if ( !(fcport->flags & FCF_TAPE_PRESENT) &&
	    cmd->allowed < ql4xcmdretrycount)
		cmd->allowed = ql4xcmdretrycount;
	
	if ( (fcport->flags & FCF_TAPE_PRESENT) ||
		(fcport->flags & FCF_NONFO_DEVICE) )
		srb->flags |= SRB_TAPE;
	
	if (atomic_read(&fcport->state) == FCS_DEVICE_DEAD) {
		cmd->result = DID_NO_CONNECT << 16;
		if (!test_bit(AF_LINK_UP, &fcport->ha->flags))
			srb->err_id = SRB_ERR_LOOP;
		else
			srb->err_id = SRB_ERR_PORT;
		DEBUG2(printk(
		    "scsi%d: PORT DEAD cmd=%p cdb[0]=%02X, cmd_err_flag=0x%x "
		    "errid=%d, fcport=%p, did_error=%x\n",
    		ha->host_no, srb->cmd, srb->cmd->data_cmnd[0], srb->cmd->eh_eflags,
		srb->err_id, fcport, host_byte(cmd->result)));
		add_to_done_srb_q(ha, srb);
		qla4xxx_done(ha);
		spin_lock_irq(ha->host->host_lock);
		return 0;
	}

	/*
	 * If the device is missing or the adapter is OFFLINE,
	 * put the request on the retry queue.
	 */
	if (atomic_read(&ddb_entry->state) == DEV_STATE_MISSING ||
	    !ADAPTER_UP(fcport->ha)) {
		DEBUG2(printk("scsi%d: PORT missing or HBA link-down"
		    "-ddb state=0x%x, hba flags=0x%lx, pid=%ld"
		    "\n", fcport->ha->host_no,
		    atomic_read(&ddb_entry->state),
		    fcport->ha->flags, srb->cmd->serial_number));

		qla4xxx_device_suspend(ha, lun_entry, srb);
		spin_lock_irq(ha->host->host_lock);
		return 0;
	}

	/*
	 * If this request's lun is suspended then put the request on
	 * the  scsi_retry queue.
	 */
	if (lun_entry->lun_state == LS_LUN_SUSPENDED) {
		DEBUG2(printk("scsi%d: Lun suspended - pid=%ld - "
		    "retry_q\n", fcport->ha->host_no,
		    srb->cmd->serial_number));

		qla4xxx_device_suspend(ha, lun_entry, srb);
		spin_lock_irq(ha->host->host_lock);
		return 0;
	}

	DEBUG(printk(
		    "scsi%d: %s pid=%ld, errid=%d, sp->flags=0x%x fcport=%p\n",
    		ha->host_no, __func__, srb->cmd->serial_number, srb->err_id, srb->flags, fcport));

	/* If target suspended put incoming I/O in retry_q. */
	if (test_bit(TQF_SUSPENDED, &tgt_entry->flags) &&
	    (srb->flags & SRB_TAPE) == 0) {
		qla4xxx_device_suspend(ha, lun_entry, srb);
		spin_lock_irq(ha->host->host_lock);
		return 0;
	}

	if (qla4xxx_send_command_to_isp(ha, srb) != QLA_SUCCESS) {
		/*
		 * Unable to send command to the ISP at this time.
		 * Notify the OS to queue commands.
		 */
		DEBUG(printk("scsi%d: %s: unable to send cmd "
		    "to ISP, retry later\n", ha->host_no, __func__));
		qla4xxx_request_cleanup(ha, srb);
		return_status = SCSI_MLQUEUE_HOST_BUSY;	

	}
	spin_lock_irq(ha->host->host_lock);
	return(return_status);

qc_complete:
	qla4xxx_complete_request(ha, srb);

	spin_lock_irq(ha->host->host_lock);
	return(return_status);
}

/**************************************************************************
 * qla4xxx_device_suspend
 *      This routine is invoked by driver to stall the request queue
 *
 * Input:
 *
 * Remarks:
 *	This routine calls the scsi_device_quiesce which may go to sleep.
 * Returns:
 *      None
 *
 * Context:
 *      Kernel context.
 **************************************************************************/
int
qla4xxx_device_suspend( scsi_qla_host_t *ha, os_lun_t *lun_entry, srb_t *srb )
{
	qla4xxx_extend_timeout(srb->cmd, EXTEND_CMD_TOV);
	add_to_retry_srb_q(ha, srb);
	return 0;
}

/**************************************************************************
 * qla4xxx_extend_timeout
 *      This routine will extend the timeout to the specified value.
 *
 * Input:
 *      cmd - Pointer to Linux's SCSI command structure
 *      timeout - Amount of time to extend the OS timeout
 *
 * Returns:
 *      None
 *
 * Context:
 *      Kernel/Interrupt context.
 **************************************************************************/
void
qla4xxx_extend_timeout(struct scsi_cmnd *cmd, int timeout)
{
	srb_t *srb = (srb_t *) CMD_SP(cmd);
	u_long our_jiffies = (timeout * HZ) + jiffies;

	if (timer_pending(&cmd->eh_timeout)) {
		mod_timer(&cmd->eh_timeout,our_jiffies);
	}
	if (timer_pending(&srb->timer)) {
		/*
		 * Our internal timer should timeout before the midlayer has a
		 * chance begin the abort process
		 */
		mod_timer(&srb->timer,
			  our_jiffies - (QLA_CMD_TIMER_DELTA * HZ));
	}
}


/**************************************************************************
 * qla4xxx_os_cmd_timeout
 *
 * Description:
 *       Handles the command if it times out in any state.
 *
 * Input:
 *     sp - pointer to validate
 *
 * Returns:
 * None.
 **************************************************************************/
void
qla4xxx_os_cmd_timeout(srb_t *sp)
{
	int t, l;
	int processed;
	scsi_qla_host_t *vis_ha, *dest_ha;
	struct scsi_cmnd *cmd;
	ulong      flags;
	ulong      cpu_flags;
	fc_port_t       *fcport;

	cmd = sp->cmd;
	vis_ha = (scsi_qla_host_t *) cmd->device->host->hostdata;

	DEBUG2(printk("scsi%d: %s: sp->state = %x\n",
		      vis_ha->host_no, __func__, sp->state);)

	t = cmd->device->id;
	l = cmd->device->lun;
	fcport = sp->fclun->fcport;
	dest_ha = sp->ha;

	/*
	 * If IO is found either in retry Queue
	 *    OR in Lun Queue
	 * Return this IO back to host
	 */
	processed = 0;
	spin_lock_irqsave(&dest_ha->list_lock, flags);
	if ((sp->state == SRB_RETRY_STATE)
	    ) {

		DEBUG2(printk("scsi%d: Found in (Scsi) Retry queue "
			      "pid %ld, State = %x., "
			      "fcport state=%d jiffies=%lx retried=%d\n",
			      dest_ha->host_no,
			      sp->cmd->serial_number, sp->state,
			      atomic_read(&fcport->state),
			      jiffies, sp->cmd->retries);)

		if ((sp->state == SRB_RETRY_STATE)) {
			__del_from_retry_srb_q(dest_ha, sp);
		}

		/*
		 * If FC_DEVICE is marked as dead return the cmd with
		 * DID_NO_CONNECT status.  Otherwise set the host_byte to
		 * DID_IMM_RETRY to let the OS  retry this cmd.
		 */
			if (atomic_read(&fcport->state) == FCS_DEVICE_DEAD) {
				qla4xxx_extend_timeout(cmd, EXTEND_CMD_TOV);
				cmd->result = DID_NO_CONNECT << 16;
				if (!test_bit(AF_LINK_UP, &fcport->ha->flags))
					sp->err_id = SRB_ERR_LOOP;
				else
					sp->err_id = SRB_ERR_PORT;
			}
			else {
				cmd->result = DID_BUS_BUSY << 16;
			}

		__add_to_done_srb_q(dest_ha, sp);
		processed++;
	}
	spin_unlock_irqrestore(&dest_ha->list_lock, flags);
	if (processed) {
		qla4xxx_done(dest_ha);
		DEBUG2(printk("scsi%d: %s: Leaving 1\n", dest_ha->host_no, __func__);)
		return;
	}

	spin_lock_irqsave(&dest_ha->list_lock, cpu_flags);
	if (sp->state == SRB_DONE_STATE) {
		/* IO in done_q  -- leave it */
		DEBUG2(printk("scsi%d: %s: Found in Done queue pid %ld sp=%p.\n",
			      dest_ha->host_no, __func__, sp->cmd->serial_number, sp);)
	}
	else if (sp->state == SRB_SUSPENDED_STATE) {
		DEBUG2(printk("scsi%d: %s: Found SP %p in suspended state  "
			      "- pid %ld:\n",
			      dest_ha->host_no,__func__, sp,
			      sp->cmd->serial_number);)
	}
	else if (sp->state == SRB_ACTIVE_STATE) {
		/*
		 * IO is with ISP find the command in our active list.
		 */
		spin_unlock_irqrestore(&dest_ha->list_lock, cpu_flags);	/* 01/03 */
		spin_lock_irqsave(&dest_ha->hardware_lock, flags);
		if (sp == dest_ha->active_srb_array
		    [(unsigned long)sp->cmd->host_scribble]) {

			if (sp->flags & SRB_TAPE) {
				/*
				 * We cannot allow the midlayer error handler
				 * to wakeup and begin the abort process.
				 * Extend the timer so that the firmware can
				 * properly return the IOCB.
				 */
				DEBUG2(printk("scsi%d: %s: Extending timeout "
					      "of FCP2 tape command!\n",
					      dest_ha->host_no, __func__));
				qla4xxx_extend_timeout(sp->cmd,
						       EXTEND_CMD_TOV);
			}

			sp->state = SRB_ACTIVE_TIMEOUT_STATE;
			spin_unlock_irqrestore(&dest_ha->hardware_lock, flags);
		}
		else {
			spin_unlock_irqrestore(&dest_ha->hardware_lock, flags);
			DEBUG2(printk(
			       "scsi%d: %s: State indicates it is with "
			       "ISP, But not in active array\n",
			       dest_ha->host_no, __func__));
		}
		spin_lock_irqsave(&dest_ha->list_lock, cpu_flags);
	}
	else if (sp->state == SRB_ACTIVE_TIMEOUT_STATE) {
		/* double timeout */
	}
	else {
		/* EMPTY */
		DEBUG3(printk("scsi%d: %s: LOST command state = "
			      "0x%x, sp=%p\n",
			      vis_ha->host_no, __func__, sp->state, sp);)

		printk(KERN_INFO
		       "scsi%d: %s: LOST command state = 0x%x\n",
			dest_ha->host_no, __func__, sp->state);
	}
	spin_unlock_irqrestore(&dest_ha->list_lock, cpu_flags);
}


/**************************************************************************
 * qla4xxx_add_timer_to_cmd
 *      This routine creates a timer for the specified command. The timeout
 *      is usually the command time from kernel minus 2 secs.
 *
 * Input:
 *      srb - Pointer to SCSI Request Block
 *      timeout - Number of seconds to extend command timeout.
 *
 * Returns:
 *      None
 *
 * Context:
 *      Kernel context.
 **************************************************************************/
void
qla4xxx_add_timer_to_cmd(srb_t *srb, int timeout)
{
	init_timer(&srb->timer);
	srb->timer.expires = jiffies + timeout * HZ;
	srb->timer.data = (unsigned long) srb;
	srb->timer.function = (void (*) (unsigned long))qla4xxx_os_cmd_timeout;
	add_timer(&srb->timer);
	QL4PRINT(QLP3, printk("%s: srb %p, timeout %d\n",
			      __func__, srb, timeout));
}

/**************************************************************************
 * qla4xxx_delete_timer_from_cmd
 *      This routine deletes the timer for the specified command.
 *
 * Input:
 *      srb - Pointer to SCSI Request Block
 *
 * Returns:
 *      None
 *
 * Context:
 *      Kernel/Interrupt context.
 **************************************************************************/
void
qla4xxx_delete_timer_from_cmd(srb_t *srb )
{
	if (timer_pending(&srb->timer)) {
		del_timer(&srb->timer);
		srb->timer.function =  NULL;
		srb->timer.data = (unsigned long) NULL;
	}
}


/****************************************************************************/
/*                        Interrupt Service Routine.                        */
/****************************************************************************/

/**************************************************************************
 * qla4xxx_timer
 *      This routine is scheduled to be invoked every second to search for
 *      work to do.
 *
 * Input:
 *      p - Pointer to host adapter structure.
 *
 * Returns:
 *      None
 *
 * Context:
 *      Interrupt context.
 **************************************************************************/
void
qla4xxx_timer(unsigned long p)
{
	scsi_qla_host_t *ha = (scsi_qla_host_t *) p;
	ddb_entry_t *ddb_entry, *dtemp;
	int             start_dpc = 0;
	os_lun_t *lun_entry;
	unsigned long cpu_flags;
	int             t, l;

#if ISP_RESET_TEST
	if (ha->isp_reset_timer++ == (60 *3)) {
		printk("scsi%d: %s going to schedule BIG HAMMER\n",
		       ha->host_no, __func__);

		set_bit(DPC_RESET_HA, &ha->dpc_flags);
		ha->isp_reset_timer = 0;
	}
#endif
#if NIC_RESET_TEST
	if (ha->nic_reset_timer++ == (60 *3)) {

		printk("scsi%d: %s ********** simulated NIC Reset ***********\n",
		       ha->host_no, __func__);

		set_bit(DPC_RESET_NIC_TEST, &ha->dpc_flags);
		ha->nic_reset_timer = 0;
	}
#endif

	DEBUG3(printk("scsi%d: %s: Host%d=%d/%d flags=[%lx,%lx,%lx] <%d,%d> "
	    "AEN in/out={%d/%d}, counters={%d,%d} %d\n", ha->host_no, __func__, ha->instance,
	    ha->spurious_int_count, (uint32_t)ha->isr_count, ha->flags,
	    ha->dpc_flags, ha->isns_flags, ha->aborted_io_count,
	    ha->mailbox_timeout_count, ha->aen_in, ha->aen_out,
	    ha->retry_srb_q_count,
	    ha->active_srb_count, ha->seconds_since_last_intr));

	/* Do we need to process the retry queue? */
	if (!list_empty(&ha->retry_srb_q)) {
		start_dpc++;
	}

	/* LUN suspension */
	for (t = 0; t < MAX_TARGETS; t++) {
		for (l = 0; l < MAX_LUNS ; l++) {
			lun_entry = GET_LU_Q(ha, t, l);
			if (lun_entry == NULL)
				continue;

			spin_lock_irqsave(&lun_entry->lun_lock, cpu_flags);
			if (lun_entry->lun_state != LS_LUN_SUSPENDED ||
			    !atomic_read(&lun_entry->suspend_timer)) {
				spin_unlock_irqrestore(&lun_entry->lun_lock,
				    cpu_flags);
				continue;
			}

			DEBUG2(printk("scsi%d: %s:"
			    "suspended lun_q - lun=%d, timer=%d "
			    "retry_count=%d\n", ha->host_no, __func__,
			    lun_entry->lun,
			    atomic_read(&lun_entry->suspend_timer),
			    lun_entry->retry_count));

			if (!atomic_dec_and_test(&lun_entry->suspend_timer)) {
				spin_unlock_irqrestore(&lun_entry->lun_lock,
				    cpu_flags);
				continue;
			}

			
			if (test_and_clear_bit(LF_LUN_DELAYED,
			    &lun_entry->flags)) {
				lun_entry->lun_state = LS_LUN_READY;
			} else {
				lun_entry->retry_count++;
				if (lun_entry->retry_count ==
				    lun_entry->max_retry_count) {
					DEBUG2(printk("scsi%d: %s: LUN "
					    "%d TIMEOUT RETRY_CNT:%d\n",
					    ha->host_no, __func__,
					    lun_entry->lun,
					    lun_entry->retry_count));

					lun_entry->lun_state = LS_LUN_TIMEOUT;
				} else {
					DEBUG2(printk("scsi%d: %s: LUN "
					    "%d RETRY\n", ha->host_no, __func__,
					    lun_entry->lun));

					lun_entry->lun_state = LS_LUN_RETRY;
				}
			}
			spin_unlock_irqrestore(&lun_entry->lun_lock, cpu_flags);
		}
	}

	/*
	 * Search for relogin's to time-out and port down retry.
	 */
	list_for_each_entry_safe(ddb_entry, dtemp, &ha->ddb_list, list_entry) {
		/* First check to see if the device has exhausted the
		 * port down retry count */
		if (atomic_read(&ddb_entry->state) == DEV_STATE_MISSING) {
			if (atomic_read(&ddb_entry->port_down_timer) == 0)
				continue;

			if (atomic_dec_and_test(&ddb_entry->port_down_timer)) {
				DEBUG2(printk("scsi%d: %s: index [%d] "
				    "port down retry count of (%d) secs "
				    "exhausted, marking device DEAD.\n",
				    ha->host_no, __func__,
				    ddb_entry->fw_ddb_index,
				    ha->port_down_retry_count);)

				atomic_set(&ddb_entry->state, DEV_STATE_DEAD);
				if (ddb_entry->fcport)
					atomic_set(&ddb_entry->fcport->state,
					    FCS_DEVICE_DEAD);

				DEBUG2(printk("scsi%d:%d:%d: "
				    "%s: index [%d] marked DEAD\n", ha->host_no,
				    ddb_entry->bus, ddb_entry->target, __func__,
				    ddb_entry->fw_ddb_index);)
				start_dpc++;
			}
		}


		/* Count down time between sending relogins */
		if (ADAPTER_UP(ha) && (!test_bit(DF_RELOGIN, &ddb_entry->flags) &&
		    (atomic_read(&ddb_entry->state) != DEV_STATE_ONLINE))) {
			if (atomic_read(&ddb_entry->retry_relogin_timer) !=
			    INVALID_ENTRY) {
				if (atomic_read(&ddb_entry->retry_relogin_timer) == 0) {
					atomic_set(&ddb_entry->retry_relogin_timer, INVALID_ENTRY);
					set_bit(DPC_RELOGIN_DEVICE,
					    &ha->dpc_flags);
					set_bit(DF_RELOGIN,
					    &ddb_entry->flags);
					DEBUG2(printk("scsi%d:%d:%d: "
				    "%s: index [%d] login device\n", ha->host_no,
				    ddb_entry->bus, ddb_entry->target, __func__,
				    ddb_entry->fw_ddb_index);)
				} else
					atomic_dec(&ddb_entry->retry_relogin_timer);
			}
		}

		/* Wait for relogin to timeout */
		if (atomic_read(&ddb_entry->relogin_timer)  &&
		    (atomic_dec_and_test(&ddb_entry->relogin_timer) != 0)) {
			/*
			 * If the relogin times out and the device is
			 * still NOT ONLINE then try and relogin again.
			 */
			if (atomic_read(&ddb_entry->state) !=
			    DEV_STATE_ONLINE &&
			    ddb_entry->fw_ddb_device_state ==
			    DDB_DS_SESSION_FAILED) {
				/* Reset retry relogin timer */
				atomic_inc(&ddb_entry->relogin_retry_count);
				QL4PRINT(QLP2, printk(
				    "scsi%d:%d:%d: index[%d] relogin timed "
				    "out-retrying relogin (%d)\n", ha->host_no,
				    ddb_entry->bus, ddb_entry->target,
				    ddb_entry->fw_ddb_index,
				    atomic_read(&ddb_entry->relogin_retry_count)));
				start_dpc++;
				QL4PRINT(QLP3, printk(
				    "scsi%d:%d:%d: index [%d] initate relogin "
				    "after %d seconds\n", ha->host_no,
				    ddb_entry->bus, ddb_entry->target,
				    ddb_entry->fw_ddb_index,
				    ddb_entry->default_time2wait+4));

				atomic_set(&ddb_entry->retry_relogin_timer,
				    ddb_entry->default_time2wait + 4);
			}
		}
	}

	if (!list_empty(&ha->done_srb_q)) {
		DEBUG2(printk("scsi%d: %s:"
			   "Pending done_q requests, "
			    "=%d\n", ha->host_no, __func__,
			    ha->done_srb_q_count));
		start_dpc++;
	}

	/*
	 * Check for heartbeat interval
	 */
	if ((ha->firmware_options & FWOPT_HEARTBEAT_ENABLE) &&
	    (ha->heartbeat_interval != 0)) {
		ha->seconds_since_last_heartbeat ++;

		if (ha->seconds_since_last_heartbeat >
		    ha->heartbeat_interval+2) {
			QL4PRINT(QLP2, printk(
			    "scsi%d: Heartbeat not received for %d seconds. "
			    "HeartbeatInterval = %d seconds. Scheduling SOFT "
			    "RESET.\n", ha->host_no,
			    ha->seconds_since_last_heartbeat,
			    ha->heartbeat_interval));

			set_bit(DPC_RESET_HA, &ha->dpc_flags);
		}
	}

	/*
	 * Check for iSNS actions
	 */
	if (test_bit(ISNS_FLAG_RESTART_SERVICE, &ha->isns_flags)) {
		if (atomic_read(&ha->isns_restart_timer)) {
			if (!atomic_dec_and_test(&ha->isns_restart_timer) &&
			    test_bit(ISNS_FLAG_ISNS_SRV_ENABLED,
				    &ha->isns_flags) &&
			    !IPAddrIsZero(ha->isns_ip_address) &&
			    ha->isns_server_port_number) {
				set_bit(DPC_ISNS_RESTART_COMPLETION,
				    &ha->dpc_flags);
			}
		} else
			clear_bit(ISNS_FLAG_RESTART_SERVICE, &ha->isns_flags);
	}

	/* Wakeup the dpc routine for this adapter, if needed */
	if ((start_dpc ||
	     test_bit(DPC_RESET_HA, &ha->dpc_flags) ||
	     test_bit(DPC_RETRY_RESET_HA, &ha->dpc_flags) ||
	     test_bit(DPC_RESET_NIC_TEST, &ha->dpc_flags) ||
	     test_bit(DPC_RELOGIN_DEVICE, &ha->dpc_flags) ||
	     test_bit(DPC_RESET_HA_DESTROY_DDB_LIST, &ha->dpc_flags) ||
	     test_bit(DPC_RESET_HA_INTR, &ha->dpc_flags) ||
	     test_bit(DPC_IOCTL_ERROR_RECOVERY, &ha->dpc_flags) ||
	     test_bit(DPC_ISNS_RESTART, &ha->dpc_flags) ||
	     test_bit(DPC_ISNS_RESTART_COMPLETION, &ha->dpc_flags) ||
	     test_bit(DPC_GET_DHCP_IP_ADDR, &ha->dpc_flags) ||
	     test_bit(DPC_AEN, &ha->dpc_flags)) &&
	    !test_bit(AF_DPC_SCHEDULED, &ha->flags) &&
	    !ha->dpc_active && ha->dpc_wait) {
		DEBUG2(printk("scsi%d: %s: scheduling dpc routine - dpc flags = 0x%lx\n",
		    ha->host_no, __func__, ha->dpc_flags));
		set_bit(AF_DPC_SCHEDULED, &ha->flags);
		up(ha->dpc_wait);
	}

	/* Reschedule timer thread to call us back in one second */
	mod_timer(&ha->timer, jiffies + HZ);

	DEBUG2(ha->seconds_since_last_intr++;)
}


/**************************************************************************
 * qla4xxx_do_dpc
 *      This routine is a task that is schedule by the interrupt handler
 *      to perform the background processing for interrupts.  We put it
 *      on a task queue that is consumed whenever the scheduler runs; that's
 *      so you can do anything (i.e. put the process to sleep etc).  In fact,
 *      the mid-level tries to sleep when it reaches the driver threshold
 *      "host->can_queue". This can cause a panic if we were in our interrupt
 *      code.
 *
 * Input:
 *      p - Pointer to host adapter structure.
 *
 * Returns:
 *      None
 *
 * Context:
 *      Kernel context.
 **************************************************************************/
static int
qla4xxx_do_dpc(void *data)
{
	DECLARE_MUTEX_LOCKED(sem);
	scsi_qla_host_t *ha = (scsi_qla_host_t *) data;
	ddb_entry_t *ddb_entry, *dtemp;
	fc_port_t       *fcport;

	ENTER("qla4xxx_do_dpc");

	lock_kernel();

	daemonize("qla4xxx_%d_dpc", ha->host_no);
	allow_signal(SIGHUP);

	ha->dpc_wait = &sem;

	set_user_nice(current, -20);

	unlock_kernel();

	complete(&ha->dpc_inited);

	while (1) {
		DEBUG2(printk("scsi%d: %s: DPC handler sleeping "
		    "*****************\n", ha->host_no, __func__));

		if (down_interruptible(&sem))
			break;

		if (ha->dpc_should_die)
			break;

		DEBUG2(printk("scsi%d: %s: DPC handler waking up "
		    "****************\n", ha->host_no, __func__));

		DEBUG2(printk("scsi%d: %s: ha->flags = 0x%08lx\n",
		    ha->host_no, __func__, ha->flags));
		DEBUG2(printk("scsi%d: %s: ha->dpc_flags = 0x%08lx\n",
		    ha->host_no, __func__, ha->dpc_flags));

		/* Initialization not yet finished. Don't do anything yet. */
		if (!test_bit(AF_INIT_DONE, &ha->flags) || ha->dpc_active)
			continue;
			
		ha->dpc_active = 1;
		clear_bit(AF_DPC_SCHEDULED, &ha->flags);

		if (!list_empty(&ha->done_srb_q))
			qla4xxx_done(ha);

		/* ---- return cmds on retry_q? --- */
		if (!list_empty(&ha->retry_srb_q)) {
			srb_t    *srb, *stemp;
			unsigned long flags;

			spin_lock_irqsave(&ha->list_lock, flags);

			DEBUG2(printk("scsi%d: %s: found %d srbs in "
			    "retry_srb_q \n", ha->host_no, __func__,
			    ha->retry_srb_q_count));

			list_for_each_entry_safe(srb, stemp, &ha->retry_srb_q,
			    list_entry) {
				ddb_entry_t *ddb_entry;
				os_lun_t *lun_entry;

				lun_entry = srb->lun_queue;

				if (lun_entry == NULL)
					continue;

				if (lun_entry->lun_state ==
				    LS_LUN_SUSPENDED)
					continue;
				fcport = lun_entry->fclun->fcport;
				ddb_entry = fcport->ddbptr;

				if (ddb_entry == NULL)
					continue;

				DEBUG2(printk("scsi%d: %s: found srb %p cmd %p lun=%d srb_state=%d\n",
						ha->host_no, __func__,
						srb, srb->cmd, srb->cmd->device->lun, srb->state);)

				if (atomic_read(&ddb_entry->state) ==
				    DEV_STATE_DEAD) {
					DEBUG2(printk("scsi%d: %s: found srb %p, "
						      "cmd %p, "
							"in retry_srb_q, "
							"Device DEAD, returning\n",
							ha->host_no, __func__,
							srb, srb->cmd));

					__del_from_retry_srb_q(ha, srb);
					srb->cmd->result = DID_NO_CONNECT << 16;
					__add_to_done_srb_q(ha,srb);
				}

				/*
				 * Send requests to OS when device goes ONLINE
				 * so that the OS will retry them via I/O thread.
				 * We don't want to issue I/O via recovery thread.
				 */
				if (ADAPTER_UP(ha) &&
				    (atomic_read(&ddb_entry->state)
				     == DEV_STATE_ONLINE)) {
					DEBUG2(printk("scsi%d: %s: found srb %p cmd %p lun=%d target=%d"
							"in retry_srb_q, "
							"Device ONLINE, returning\n",
							ha->host_no, __func__,
							srb, srb->cmd, srb->cmd->device->lun, srb->cmd->device->id));

					__del_from_retry_srb_q(ha, srb);
					// srb->cmd->result = DID_IMM_RETRY << 16;
					srb->cmd->result = DID_BUS_BUSY << 16;
					__add_to_done_srb_q(ha,srb);
				}
			}
			spin_unlock_irqrestore(&ha->list_lock, flags);

			if (!list_empty(&ha->done_srb_q))
				qla4xxx_done(ha);

		}



		/*
		 * Determine what action is necessary
		 */

		/* ---- recover adapter? --- */
		if (ADAPTER_UP(ha) ||
		    test_bit(DPC_RESET_HA, &ha->dpc_flags) ||
		    test_bit(DPC_RESET_HA_INTR, &ha->dpc_flags) ||
		    test_bit(DPC_RESET_HA_DESTROY_DDB_LIST, &ha->dpc_flags)) {

#if	DISABLE_HBA_RESETS
				QL4PRINT(QLP2, printk("scsi: %s: ignoring RESET_HA, "
				    "rebootdisable=1 \n", __func__));
				clear_bit(DPC_RESET_HA, &ha->dpc_flags);
				clear_bit(DPC_RESET_HA_INTR, &ha->dpc_flags);
				clear_bit(DPC_RESET_HA_DESTROY_DDB_LIST, &ha->dpc_flags);
#else
			if (test_bit(DPC_RESET_HA_DESTROY_DDB_LIST,
			    &ha->dpc_flags))
				/* dg 09/23 Never initialize ddb list once we up and running
					qla4xxx_recover_adapter(ha, REBUILD_DDB_LIST); */
				qla4xxx_recover_adapter(ha, PRESERVE_DDB_LIST);
			
			if (test_bit(DPC_RESET_HA, &ha->dpc_flags))
				qla4xxx_recover_adapter(ha, PRESERVE_DDB_LIST);
			
			if (test_and_clear_bit(DPC_RESET_HA_INTR, &ha->dpc_flags)) {
				uint8_t wait_time = RESET_INTR_TOV;
				unsigned long flags = 0;

				qla4xxx_flush_active_srbs(ha);

				spin_lock_irqsave(&ha->hardware_lock, flags);
				while ((RD_REG_DWORD(&ha->reg->ctrl_status) &
				    (CSR_SOFT_RESET|CSR_FORCE_SOFT_RESET)) != 0) {
					if (--wait_time == 0)
						break;

					spin_unlock_irqrestore(
					    &ha->hardware_lock, flags);

					set_current_state(TASK_UNINTERRUPTIBLE);
					schedule_timeout(1 * HZ);

					spin_lock_irqsave(&ha->hardware_lock,
					    flags);
				}
				spin_unlock_irqrestore(&ha->hardware_lock,
				    flags);

				if (wait_time == 0) {
					QL4PRINT(QLP2,
						 printk("scsi%d: %s: SR|FSR bit not cleared-- resetting\n",
							ha->host_no, __func__));

					set_bit(DPC_RESET_HA, &ha->dpc_flags);
				}
				else if (!qla4xxx_hba_going_away) {
					qla4xxx_initialize_adapter(
							  ha,
							  PRESERVE_DDB_LIST);
					qla4xxx_enable_intrs(ha);
				}
			}
#endif
		}

		/* ---- Reset NIC test? --- */
		if (test_and_clear_bit(DPC_RESET_NIC_TEST, &ha->dpc_flags)) {
			unsigned long flags;

			/* reset from NIC's register space */
			spin_lock_irqsave(&ha->hardware_lock, flags);
			//FIXME: First get access to NIC's address space
			//WRT_REG_DWORD(&ha->nic_reg->ctrl_status, SET_RMASK(CSR_SOFT_RESET));
			//PCI_POSTING(&ha->nic_reg->ctrl_status);
			spin_unlock_irqrestore(&ha->hardware_lock, flags);
		}

		/* ---- process AEN? --- */
		if (test_and_clear_bit(DPC_AEN, &ha->dpc_flags))
			qla4xxx_process_aen(ha, PROCESS_ALL_AENS);

		/* ---- Get DHCP IP Address? --- */
		if (test_and_clear_bit(DPC_GET_DHCP_IP_ADDR, &ha->dpc_flags))
			qla4xxx_get_dhcp_ip_address(ha);

		/* ---- relogin device? --- */
		if (ADAPTER_UP(ha) &&
		    test_and_clear_bit(DPC_RELOGIN_DEVICE, &ha->dpc_flags)) {
			list_for_each_entry_safe(ddb_entry, dtemp,
			    &ha->ddb_list, list_entry) {
				if (test_and_clear_bit(DF_RELOGIN,
				    &ddb_entry->flags) &&
				    atomic_read(&ddb_entry->state) != DEV_STATE_ONLINE) {
					qla4xxx_relogin_device(ha, ddb_entry);
				}
				    /* If mbx cmd times out there is no point
				     * in continuing further.
				     * With large no of targets this can hang
				     * the system.
				     */		
				    if (test_bit(DPC_RESET_HA, &ha->dpc_flags)) {
					QL4PRINT(QLP2, printk("scsi%d: %s: need to reset hba\n", ha->host_no, __func__));
					printk("scsi%d: %s: need to reset hba\n", ha->host_no, __func__);
					break;
				    }	
			}
		}

		/* ---- restart iSNS server? --- */
		if (ADAPTER_UP(ha) &&
		    test_and_clear_bit(DPC_ISNS_RESTART, &ha->dpc_flags)) {
			qla4xxx_isns_restart_service(ha);
		}

		if (ADAPTER_UP(ha) &&
		    test_and_clear_bit(DPC_ISNS_RESTART_COMPLETION,
			    &ha->dpc_flags)) {
			uint32_t ip_addr = 0;
			IPAddr2Uint32(ha->isns_ip_address, &ip_addr);

			if (qla4xxx_isns_restart_service_completion(ha,
							    ip_addr,
					    ha->isns_server_port_number)
			    != QLA_SUCCESS) {
				DEBUG2( printk(KERN_WARNING "scsi%d: %s: "
						"restart service failed\n",
						ha->host_no, __func__));
			}
		}

		ha->dpc_active = 0;
	}

	/*
	* Make sure that nobody tries to wake us up again.
	*/
	ha->dpc_wait = NULL;
	ha->dpc_active = 0;

	complete_and_exit(&ha->dpc_exited, 0);
}

/**************************************************************************
 * qla4xxx_eh_wait_on_command
 *      This routine waits for the command to be returned by the Firmware
 *      for some max time.
 *
 * Input:
 *    ha = actual ha whose done queue will contain the command
 *            returned by firmware.
 *    cmd = Scsi Command to wait on.
 *
 * Returns:
 *    Not Found : 0
 *    Found : 1
 *
 * Context:
 *      Kernel context.
 **************************************************************************/
static int
qla4xxx_eh_wait_on_command(scsi_qla_host_t *ha, struct scsi_cmnd *cmd)
{
#define ABORT_POLLING_PERIOD	HZ
	
	int		found = 0;
	int 		done = 0;
	srb_t 		*rp = NULL;
	struct list_head *list, *temp;
	uint32_t max_wait_time = EH_WAIT_CMD_TOV;

	DEBUG2(printk("%s: ENTER,  cmd=%p\n", __func__, cmd);)

	do {
		/* Check on done queue */
		spin_lock(&ha->list_lock);
		list_for_each_safe(list, temp, &ha->done_srb_q) {
			rp = list_entry(list, srb_t, list_entry);

			/*
			 * Found command. Just exit and wait for the cmd sent
			 * to OS.
			*/
			if (cmd == rp->cmd) {
				found++;
				DEBUG2(printk("%s: found in done queue.\n",
				    __func__);)
				break;
			}
		}
		spin_unlock(&ha->list_lock);

		/* Complete the cmd right away. */
		if (found) {
			del_from_done_srb_q(ha, rp);
			sp_put(ha, rp);
			done++;
			break;
		}

		/* Checking to see if its returned to OS */
		rp = (srb_t *) CMD_SP(cmd);
		if (rp == NULL ) {
			done++;
			break;
		}

		spin_unlock_irq(ha->host->host_lock);
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(ABORT_POLLING_PERIOD);
		spin_lock_irq(ha->host->host_lock);

	} while (max_wait_time--);

	DEBUG2(printk("%s: EXIT done=%d cmd=%p cmd_sp=%p\n",
		      __func__, done, cmd, CMD_SP(cmd));)
	return done;
}

/**************************************************************************
 * qla4xxx_wait_for_hba_online
 *      This routine
 *
 * Input:
 *      ha - Pointer to host adapter structure
 *
 * Remarks:
 *
 * Returns:
 *      SUCCESS - Adapter is ONLINE
 *      FAILED  - Adapter is DEAD
 *
 * Context:
 *      Kernel context.  Assume io_request_lock LOCKED upon entry
 **************************************************************************/
inline uint8_t
qla4xxx_wait_for_hba_online(scsi_qla_host_t *ha)
{
	unsigned long wait_online;

	wait_online = jiffies + (30 * HZ);
	while (time_before(jiffies, wait_online)) {
		if (ADAPTER_UP(ha))
			return QLA_SUCCESS;

		if (!ADAPTER_UP(ha) && (ha->retry_reset_ha_cnt == 0)) {
			QL4PRINT(QLP2, printk("scsi%d: %s: adapter down, "
			    "retry_reset_ha_cnt = %d\n", ha->host_no, __func__,
			    ha->retry_reset_ha_cnt));

			return QLA_ERROR;
		}

		QL4PRINT(QLP3, printk("scsi%d: %s: adapter down, "
		    "retry_reset_ha_cnt = %d, delay 2 sec.\n", ha->host_no,
		    __func__, ha->retry_reset_ha_cnt));

		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(2 * HZ);
	}

	return QLA_ERROR;
}

/**************************************************************************
 * qla4xxx_eh_abort
 *      This routine aborts commands that currently held in the adapter's
 *      internal queues.  Commands that are active are NOT aborted.
 *
 * Input:
 *      cmd - Pointer to Linux's SCSI command structure
 *
 * Remarks:
 *      Aborts get translated to "device resets" by the scsi switch
 *      which will return a RESET status and not ABORT. Since the
 *      mid-level is expecting an ABORT status during an abort(),
 *      we always elevate to device reset.
 *
 * Returns:
 *      SUCCESS - Successfully aborted non-active command
 *      FAILED  - Command not found, or command currently active
 *
 * Context:
 *      Kernel context.  io_request_lock LOCKED
 **************************************************************************/
int
qla4xxx_eh_abort(struct scsi_cmnd *cmd)
{
	int return_status = FAILED;
	scsi_qla_host_t *ha, *vis_ha;
	srb_t *srb;
	srb_t *stemp;

	srb = (srb_t *) CMD_SP(cmd);
	if (!srb) {
		/* Already returned to upper-layers. */
		ql4_printk(KERN_INFO, to_qla_host(cmd->device->host),
		    "Command already completed cmd=%p, pid=%ld.\n",
			   cmd, cmd->serial_number);
		DEBUG2(printk("%s: Command already completed cmd=%p, pid=%ld.\n",
			   __func__, cmd, cmd->serial_number);)
		return SUCCESS;
	}

	vis_ha = (scsi_qla_host_t *) cmd->device->host->hostdata;
		ha = vis_ha;

	ha->aborted_io_count++;

	/* Print statements
	 * ---------------- */
	QL4PRINT(QLP2, printk(
			      "scsi%d:%d:%d:%d: abort srb=%p, cmd=%p, state=%s, r_start=%ld , "
			      "u_start=%ld\n", ha->host_no, cmd->device->channel,
			      cmd->device->id, cmd->device->lun, srb, cmd,
			      srb_state_msg[srb->state],srb->r_start,srb->u_start));
	DEBUG2(printk("scsi%d: %s: os_tov = %d, iocb_tov = %d\n",
	       ha->host_no, __func__, srb->os_tov, srb->iocb_tov);)
	DEBUG2(printk("scsi%d: pid=%ld, errid=%d\n",
		     ha->host_no, srb->cmd->serial_number, srb->err_id));
	// qla4xxx_dump_dwords(QLP10, srb, sizeof(*srb));
	//__dump_registers(QLP2, ha);

#if 0
        spin_unlock_irq(ha->host->host_lock);
        if (qla4xxx_wait_for_hba_online(ha) != QLA_SUCCESS) {
                DEBUG2(printk("%s failed:board disabled\n", __func__);)
                spin_lock_irq(ha->host->host_lock);
                return FAILED;
        }
        spin_lock_irq(ha->host->host_lock);
#endif

	
	/* If srb found in done_q, return the cmd with ABORTED status */
	spin_lock(&ha->list_lock);
	list_for_each_entry_safe(srb, stemp, &ha->done_srb_q, list_entry) {
		if (srb->cmd != cmd)
			continue;

		QL4PRINT(QLP2, printk("scsi%d: %s: srb %p found on done "
		    "queue\n", ha->host_no, __func__, srb));

		__del_from_done_srb_q(ha, srb);
		cmd->result = DID_ABORT << 16;

		spin_unlock(&ha->list_lock);
		spin_unlock_irq(ha->host->host_lock);
		sp_put(ha, srb);
		spin_lock_irq(ha->host->host_lock);
		return SUCCESS;
	}
	spin_unlock(&ha->list_lock);
	
	/*
         * If srb found in retry_q, return the cmd with ABORTED status
         */
	spin_lock(&ha->list_lock);
	list_for_each_entry_safe(srb, stemp, &ha->retry_srb_q, list_entry) {
		if (srb->cmd != cmd)
			continue;

		QL4PRINT(QLP2,
			 printk("scsi%d: %s: srb %p found on retry queue\n",
				ha->host_no, __func__, srb));

		__del_from_retry_srb_q(ha, srb);
		cmd->result = DID_ABORT << 16;

		spin_unlock(&ha->list_lock);
		spin_unlock_irq(ha->host->host_lock);
		sp_put(ha, srb);
		spin_lock_irq(ha->host->host_lock);
		return SUCCESS;
	}
	spin_unlock(&ha->list_lock);

	if (qla4xxx_eh_wait_on_command(ha, cmd)) {
		QL4PRINT(QLP2, printk("scsi%d: %s: return with status = %x\n",
		    ha->host_no, __func__, SUCCESS));
		return SUCCESS;
	}

	/*
	 * Aborts get translated to "device resets" by the scsi switch which
	 * will return a RESET status and not ABORT. Since the mid-level is
	 * expecting an ABORT status during an abort(), we always elevate to
	 * device reset.
	 */
	sp_get(ha, srb);
	return_status = FAILED;

	QL4PRINT(QLP2, printk("scsi%d: %s: return with status = %x\n",
	    ha->host_no, __func__, return_status));
	QL4PRINT(QLP2, printk("scsi%d: %s: cmd=%p cmd_err_flags=0x%x\n",
			      ha->host_no, __func__, cmd, cmd->eh_eflags));
	return return_status;
}

/**************************************************************************
 * qla4010_soft_reset
 *      This routine performs a SOFT RESET.
 *
 * Input:
 *      ha - Pointer to host adapter structure.
 *
 * Returns:
 *      QLA_SUCCESS - Successfully reset the firmware
 *      QLA_ERROR   - Failed to reset the firmware
 *
 * Context:
 *      Kernel context.
 **************************************************************************/
uint8_t
qla4010_soft_reset(scsi_qla_host_t *ha){
	uint32_t  max_wait_time;
	unsigned long flags = 0;
	uint8_t status = QLA_ERROR;
	uint32_t ctrl_status;

	ENTER(__func__);

	spin_lock_irqsave(&ha->hardware_lock, flags);

	/*
	 * If the SCSI Reset Interrupt bit is set, clear it.
	 * Otherwise, the Soft Reset won't work.
	 */
	ctrl_status = RD_REG_DWORD(&ha->reg->ctrl_status);
	if ((ctrl_status & CSR_SCSI_RESET_INTR) != 0)
		WRT_REG_DWORD(&ha->reg->ctrl_status, SET_RMASK(CSR_SCSI_RESET_INTR));

	/* Issue Soft Reset */
	WRT_REG_DWORD(&ha->reg->ctrl_status, SET_RMASK(CSR_SOFT_RESET));
	PCI_POSTING(&ha->reg->ctrl_status);

	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	/* Wait until the Network Reset Intr bit is cleared */
	max_wait_time = RESET_INTR_TOV;
	do {
		spin_lock_irqsave(&ha->hardware_lock, flags);
		ctrl_status = RD_REG_DWORD(&ha->reg->ctrl_status);
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

		if ((ctrl_status & CSR_NET_RESET_INTR) == 0)
			break;

		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1 * HZ);
	} while ((--max_wait_time));

	if ((ctrl_status & CSR_NET_RESET_INTR) != 0) {
		QL4PRINT(QLP2,
			 printk(KERN_WARNING "scsi%d: Network Reset Intr not cleared "
				"by Network function, clearing it now!\n", ha->host_no));
		spin_lock_irqsave(&ha->hardware_lock, flags);
		WRT_REG_DWORD(&ha->reg->ctrl_status, SET_RMASK(CSR_NET_RESET_INTR));
		PCI_POSTING(&ha->reg->ctrl_status);
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
	}

	/* Wait until the firmware tells us the Soft Reset is done */
	max_wait_time = SOFT_RESET_TOV;
	do {
		spin_lock_irqsave(&ha->hardware_lock, flags);
		ctrl_status = RD_REG_DWORD(&ha->reg->ctrl_status);
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

		if ((ctrl_status & CSR_SOFT_RESET) == 0) {
			status = QLA_SUCCESS;
			break;
		}

		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1 * HZ);
	} while ((--max_wait_time));

	/*
	 * Also, make sure that the SCSI Reset Interrupt bit has been cleared
	 * after the soft reset has taken place.
	 */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	ctrl_status = RD_REG_DWORD(&ha->reg->ctrl_status);
	if ((ctrl_status & CSR_SCSI_RESET_INTR) != 0) {
		WRT_REG_DWORD(&ha->reg->ctrl_status, SET_RMASK(CSR_SCSI_RESET_INTR));
		PCI_POSTING(&ha->reg->ctrl_status);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	/* If soft reset fails then most probably the bios on other
	 * function is also enabled.
	 * Since the initialization is sequential the other fn
	 * wont be able to acknowledge the soft reset.
	 * Issue a force soft reset to workaround this scenario.
	 */
	if (max_wait_time == 0) {
		/* Issue Force Soft Reset */
		spin_lock_irqsave(&ha->hardware_lock, flags);
		WRT_REG_DWORD(&ha->reg->ctrl_status,
			SET_RMASK(CSR_FORCE_SOFT_RESET));
		PCI_POSTING(&ha->reg->ctrl_status);
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
		/* Wait until the firmware tells us the Soft Reset is done */
		max_wait_time = SOFT_RESET_TOV;
		do {
			spin_lock_irqsave(&ha->hardware_lock, flags);
			ctrl_status = RD_REG_DWORD(&ha->reg->ctrl_status);
			spin_unlock_irqrestore(&ha->hardware_lock, flags);

			if ((ctrl_status & CSR_FORCE_SOFT_RESET) == 0) {
				status = QLA_SUCCESS;
				break;
			}

			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(1 * HZ);
		} while ((--max_wait_time));
	}

	QL4PRINT(QLP2, printk("scsi%d: %s status=%d\n",
			      ha->host_no, __func__, status));
	LEAVE(__func__);
	return(status);
}

/**************************************************************************
 * qla4xxx_topcat_reset
 *      This routine performs a HARD RESET of the TopCat chip.
 *
 * Input:
 *      ha - Pointer to host adapter structure.
 *
 * Returns:
 *      QLA_SUCCESS - Successfully reset the firmware
 *      QLA_ERROR   - Failed to reset the firmware
 *
 * Context:
 *      Kernel context.
 **************************************************************************/
uint8_t
qla4xxx_topcat_reset(scsi_qla_host_t *ha){
	unsigned long flags;

	QL4PRINT(QLP2, printk(KERN_WARNING "scsi%d: %s: TopCat chip reset!\n",
			      ha->host_no, __func__));

	QL4XXX_LOCK_NVRAM(ha);

	spin_lock_irqsave(&ha->hardware_lock, flags);

	WRT_REG_DWORD(ISP_GP_OUT(ha), SET_RMASK(GPOR_TOPCAT_RESET));
	PCI_POSTING(ISP_GP_OUT(ha));
	TOPCAT_RESET_DELAY();
	WRT_REG_DWORD(ISP_GP_OUT(ha), CLR_RMASK(GPOR_TOPCAT_RESET));
	PCI_POSTING(ISP_GP_OUT(ha));

	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	TOPCAT_POST_RESET_DELAY();

	/* qla4xxx_clear_hw_semaphore(ha, SEM_NVRAM); */
	QL4XXX_UNLOCK_NVRAM(ha);
	return(QLA_SUCCESS);
}


/**************************************************************************
 * qla4xxx_soft_reset
 *      This routine performs a SOFT RESET.
 *
 * Input:
 *      ha - Pointer to host adapter structure.
 *
 * Returns:
 *      QLA_SUCCESS - Successfully reset the firmware
 *      QLA_ERROR   - Failed to reset the firmware
 *
 * Context:
 *      Kernel context.
 **************************************************************************/
uint8_t
qla4xxx_soft_reset(scsi_qla_host_t *ha){

	QL4PRINT(QLP2, printk(KERN_WARNING "scsi%d: %s: chip reset!\n",
			      ha->host_no, __func__));
	if (test_bit(AF_TOPCAT_CHIP_PRESENT, &ha->flags)) {
		uint8_t status = QLA_ERROR;

		if (qla4010_soft_reset(ha) == QLA_SUCCESS) {
			if (qla4xxx_topcat_reset(ha) == QLA_SUCCESS) {
				if (qla4010_soft_reset(ha) == QLA_SUCCESS) {
					status = QLA_SUCCESS;
				}
			}
		}
		return(status);
	}
	else
		return(qla4010_soft_reset(ha));
}

/**************************************************************************
 * qla4xxx_hard_reset
 *      This routine performs a HARD RESET.
 *
 * Input:
 *      ha - Pointer to host adapter structure.
 *
 * Returns:
 *      QLA_SUCCESS - Successfully reset the firmware
 *      QLA_ERROR   - Failed to reset the firmware
 *
 * Context:
 *      Kernel context.
 **************************************************************************/
inline uint8_t
qla4xxx_hard_reset(scsi_qla_host_t *ha){
	/* The QLA4010 really doesn't have an equivalent to a hard reset */
	qla4xxx_flush_active_srbs(ha);
	if (test_bit(AF_TOPCAT_CHIP_PRESENT, &ha->flags)) {
		uint8_t status = QLA_ERROR;

		if (qla4010_soft_reset(ha)  == QLA_SUCCESS) {
			if (qla4xxx_topcat_reset(ha)  == QLA_SUCCESS) {
				if (qla4010_soft_reset(ha) == QLA_SUCCESS) {
					status = QLA_SUCCESS;
				}
			}
		}
		return(status);
	}
	else
                return(qla4010_soft_reset(ha));
	}

/**************************************************************************
 * qla4xxx_cmd_wait
 *      This routine stalls the driver until all outstanding commands are
 *      returned.
 *
 * Input:
 *      ha - Pointer to host adapter structure.
 *
 * Remarks:
 *       Caller must release the Hardware Lock prior to calling this routine.
 *
 * Returns:
 *      QLA_SUCCESS - All outstanding commands completed
 *      QLA_ERROR   - All outstanding commands did not complete
 *
 * Context:
 *      Kernel context.
 **************************************************************************/
static uint8_t
qla4xxx_cmd_wait(scsi_qla_host_t *ha){
	uint32_t index = 0;
	uint8_t stat = QLA_SUCCESS;
	int wait_cnt = WAIT_CMD_TOV;			/* Initialized for 30 seconds as we expect all
						 commands to retuned ASAP.*/
	unsigned long flags;

	ENTER("qla4xxx_cmd_wait: started\n");

	while (wait_cnt) {
	spin_lock_irqsave(&ha->hardware_lock, flags);
		/* Find a command that hasn't completed. */
	for (index = 1; index < MAX_SRBS; index++) {
			if (ha->active_srb_array[index] != NULL)
				break;
		}
			spin_unlock_irqrestore(&ha->hardware_lock, flags);

		/* If No Commands are pending, wait is complete */
		if (index == MAX_SRBS) {
			break;
		}

		/* If we timed out on waiting for commands to come back
		 * return ERROR.
		 */
		wait_cnt--;
		if (wait_cnt == 0)
			stat =  QLA_ERROR;
		else {
			/* sleep a second */
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(1 * HZ);
		}
	}		     /* End of While (wait_cnt) */

	QL4PRINT(QLP2,printk("(%d): %s: Done waiting on commands - array_index=%d\n",
			     ha->host_no, __func__, index));

	LEAVE("qla4xxx_cmd_wait");

	return(stat);
}

/**************************************************************************
 * qla4xxx_recover_adapter
 *      This routine recovers that adapter from a fatal state.
 *
 * Input:
 *      ha - Pointer to host adapter structure.
 *      renew_ddb_list - Indicates what to do with the adapter's ddb list
 *                      after adapter recovery has completed.
 *                      0=preserve ddb list, 1=destroy and rebuild ddb list
 *
 * Returns:
 *      QLA_SUCCESS - Successfully recovered adapter
 *      QLA_ERROR   - Failed to recover adapter
 *
 * Context:
 *      Kernel context.
 **************************************************************************/
uint8_t
qla4xxx_recover_adapter(scsi_qla_host_t *ha, uint8_t renew_ddb_list){
	uint8_t status = QLA_SUCCESS;

	ENTER("qla4xxx_recover_adapter");

	QL4PRINT(QLP2,
		 printk("scsi%d: recover adapter (begin)\n",
			ha->host_no));

	/* Stall incoming I/O until we are done */
	clear_bit(AF_ONLINE, &ha->flags);
	DEBUG2(printk("scsi%d: %s calling qla4xxx_cmd_wait\n",
			      ha->host_no, __func__));

	/* Wait for outstanding commands to complete.
	 * Stalls the driver for max 30 secs
	 */
	status = qla4xxx_cmd_wait(ha);

	qla4xxx_disable_intrs(ha);

	/* Flush any pending ddb changed AENs */
	qla4xxx_process_aen(ha, FLUSH_DDB_CHANGED_AENS);

	/* Reset the firmware.  If successful, function
	 * returns with ISP interrupts enabled.
	 */
	if (status == QLA_SUCCESS) {
		DEBUG2(printk(
		    "scsi%d: %s - Performing soft reset..\n",
				ha->host_no,__func__));
		status = qla4xxx_soft_reset(ha);
	}

	/* If firmware (SOFT) reset failed, or if all outstanding
	 * commands have not returned, then do a HARD reset.
	 */
	if (status == QLA_ERROR) {
		DEBUG2(printk(
		    "scsi%d: %s - Performing hard reset..\n",
				ha->host_no,__func__));
		status = qla4xxx_hard_reset(ha);
	}

	/* Flush any pending ddb changed AENs */
	qla4xxx_process_aen(ha, FLUSH_DDB_CHANGED_AENS);

	/* Re-initialize firmware. If successful, function returns
	 * with ISP interrupts enabled */
	if (status == QLA_SUCCESS) {
		DEBUG2(printk(
		    "scsi%d: %s - Initializing adapter..\n",
				ha->host_no, __func__));

		/* If successful, AF_ONLINE flag set in
		 * qla4xxx_initialize_adapter */
		status = qla4xxx_initialize_adapter(ha, renew_ddb_list);
	}

	/* Failed adapter initialization?
	 * Retry reset_ha only if invoked via DPC (DPC_RESET_HA) */
	if ((test_bit(AF_ONLINE, &ha->flags) == 0) &&
	    (test_bit(DPC_RESET_HA, &ha->dpc_flags))) {
		/* Adapter initialization failed, see if we can retry
		 * resetting the ha */
		if (!test_bit(DPC_RETRY_RESET_HA, &ha->dpc_flags)) {
			ha->retry_reset_ha_cnt = MAX_RESET_HA_RETRIES;
			DEBUG2(printk("scsi%d: recover adapter - "
					"retrying (%d) more times\n",
					ha->host_no, ha->retry_reset_ha_cnt));
			set_bit(DPC_RETRY_RESET_HA, &ha->dpc_flags);
			status = QLA_ERROR;
		}
		else {
			if (ha->retry_reset_ha_cnt > 0) {
				/* Schedule another Reset HA -- DPC will retry */
				ha->retry_reset_ha_cnt--;
				DEBUG2(printk(
					"scsi%d: recover adapter - "
					"retry remaining %d\n", ha->host_no,
						ha->retry_reset_ha_cnt));
				status = QLA_ERROR;
			}

			if (ha->retry_reset_ha_cnt == 0) {
				/* Recover adapter retries have been exhausted.
				 * Adapter DEAD */
				DEBUG2( printk(
					"scsi%d: recover adapter failed - "
					"board disabled\n", ha->host_no));
				qla4xxx_flush_active_srbs(ha);
				clear_bit(DPC_RETRY_RESET_HA, &ha->dpc_flags);
				clear_bit(DPC_RESET_HA, &ha->dpc_flags);
				clear_bit(DPC_RESET_HA_DESTROY_DDB_LIST,
				    &ha->dpc_flags);
				status = QLA_ERROR;
			}
		}
	}
	else {
		clear_bit(DPC_RESET_HA, &ha->dpc_flags);
		clear_bit(DPC_RESET_HA_DESTROY_DDB_LIST, &ha->dpc_flags);
		clear_bit(DPC_RETRY_RESET_HA, &ha->dpc_flags);
	}

	ha->adapter_error_count++;

 	if (status == QLA_SUCCESS)
 		qla4xxx_enable_intrs(ha);

	DEBUG2(printk("scsi%d: recover adapter .. DONE\n", ha->host_no));
	LEAVE("qla4xxx_recover_adapter");
	return(status);
}

/**************************************************************************
 * qla4xxx_eh_wait_for_active_target_commands
 *      This routine
 *
 * Input:
 *      ha - Pointer to host adapter structure.
 *      target - SCSI target ID
 *
 * Returns:
 *      0 - All pending commands returned
 *      non-zero - All pending commands did not return
 *
 * Context:
 *      Kernel context.
 **************************************************************************/
int
qla4xxx_eh_wait_for_active_target_commands(scsi_qla_host_t *ha, int t, int l)
{
	int cnt;
	int status;
	srb_t *sp;
	struct scsi_cmnd *cmd;

	/*
	 * Waiting for all commands for the designated target in the active
	 * array
	 */
	status = 0;
	for (cnt = 1; cnt < MAX_SRBS; cnt++) {
		spin_lock(&ha->hardware_lock);
		sp = ha->active_srb_array[cnt];
		if (sp) {
			cmd = sp->cmd;
			spin_unlock(&ha->hardware_lock);
			if (cmd->device->id == t && cmd->device->lun == l) {
				if (!qla4xxx_eh_wait_on_command(ha, cmd)) {
					status++;
					break;
				}
			}
		} else {
			spin_unlock(&ha->hardware_lock);
		}
	}
	return status;
}

/**************************************************************************
 * qla4xxx_eh_device_reset
 *      This routine is called by the Linux OS to reset all luns on the
 * 	specified target.
 *
 * Input:
 *      cmd - Pointer to Linux's SCSI command structure
 *
 * Output:
 *      None
 *
 * Remarks:
 *      None
 *
 * Returns:
 *      SUCCESS - Successfully reset target/lun
 *      FAILED  - Failed to reset target/lun
 *
 * Context:
 *      Kernel context.  io_request_lock LOCKED
 **************************************************************************/
 int
qla4xxx_eh_device_reset(struct scsi_cmnd *cmd)
{
	int return_status = FAILED;
 	scsi_qla_host_t *ha;
	os_lun_t *lun_entry;
	os_tgt_t *tgt_entry;
	fc_lun_t *fclun;
	uint8_t stat;
	srb_t		*srb;
	struct list_head *list, *temp;

	if (cmd == NULL) {
		printk(KERN_INFO
		    "%s(): **** SCSI mid-layer passing in NULL cmd\n",
		    __func__);
		return (return_status);
	}

	if (!CMD_SP(cmd)) {
		/* Already returned to upper-layers. */
		ql4_printk(KERN_INFO, to_qla_host(cmd->device->host),
		    "Command already completed cmd=%p, pid=%ld.\n",
			   cmd, cmd->serial_number);
		DEBUG2(printk("%s: Command already completed cmd=%p, pid=%ld.\n",
			   __func__, cmd, cmd->serial_number);)
		return SUCCESS;
	}

 	ha = (scsi_qla_host_t *) cmd->device->host->hostdata;

	/* Retrieve device and lun handles */
 	tgt_entry = qla4xxx_lookup_target_by_SCSIID(ha, cmd->device->channel,
 	    cmd->device->id);
	if (!tgt_entry)	{
		DEBUG2(printk("scsi%d: %s: **** CMD derives a NULL TGT_Q\n",
			   ha->host_no, __func__);)
		return FAILED;
	}

	lun_entry = qla4xxx_lookup_lun_handle(ha, tgt_entry, cmd->device->lun);
	if (!lun_entry)	{
		DEBUG2(printk("scsi%d: %s: **** CMD derives a NULL LUN_Q\n",
		       ha->host_no, __func__);)
		return FAILED;
	}

	fclun = lun_entry->fclun;
	if (!fclun) {
		DEBUG2(printk("scsi%d: %s: **** CMD derives a NULL FC_LUN_Q\n",
		       ha->host_no, __func__);)
		return FAILED;
	}

	ql4_printk(KERN_INFO, ha,
	    "scsi%d:%d:%d:%d: DEVICE RESET ISSUED.\n", ha->host_no,
	    cmd->device->channel, cmd->device->id, cmd->device->lun);

	DEBUG2(printk(
	    "scsi%d: DEVICE_RESET cmd=%p jiffies = 0x%lx, timeout=%x, "
	    "dpc_flags=%lx, status=%x allowed=%d\n",
	    ha->host_no, cmd, jiffies, cmd->timeout_per_command / HZ,
	    ha->dpc_flags, cmd->result, cmd->allowed));

	/* If we are coming in from the back-door, stall I/O until complete. */
	if (!qla4xxx_is_eh_active(cmd->device->host)) {
		set_bit(TQF_SUSPENDED, &tgt_entry->flags);
	}
	
 	/* Clear commands from the retry queue. */
 	spin_lock(&ha->list_lock);
 	list_for_each_safe(list, temp, &ha->retry_srb_q) {
 		srb = list_entry(list, srb_t, list_entry);

 		if (tgt_entry->id != srb->cmd->device->id)
 			continue;

 		DEBUG2(printk(
		    "scsi%d: %s: found in retry queue. SP=%p\n",
		    ha->host_no, __func__, srb));

 		__del_from_retry_srb_q(ha, srb);
 		srb->cmd->result = DID_RESET << 16;
 		__add_to_done_srb_q(ha, srb);
 	}
 	spin_unlock(&ha->list_lock);
	
	spin_unlock_irq(ha->host->host_lock);
	if (qla4xxx_wait_for_hba_online(ha) != QLA_SUCCESS) {
		DEBUG2(printk(
		    "scsi%d: %s:  failed:board disabled\n", ha->host_no, __func__));

		spin_lock_irq(ha->host->host_lock);
		goto eh_dev_reset_done;
	}

	stat = qla4xxx_reset_lun(ha, fclun->fcport->ddbptr, fclun);
	spin_lock_irq(ha->host->host_lock);
	if (stat != QLA_SUCCESS) {
		ql4_printk(KERN_INFO, ha, "DEVICE RESET FAILED. %d\n", stat);

		goto eh_dev_reset_done;
	}

	/* Send marker. */
	ha->marker_needed = 1;

	/*
	 * If we are coming down the EH path, wait for all commands to complete
	 * for the device.
	 */
	if (qla4xxx_is_eh_active(cmd->device->host)) {
		if (qla4xxx_eh_wait_for_active_target_commands(ha,
		    cmd->device->id, cmd->device->lun)) {
			ql4_printk(KERN_INFO, ha, "DEVICE RESET FAILED - "
			    "waiting for commands.\n");

			goto eh_dev_reset_done;
 		}
 	}

	ql4_printk(KERN_INFO, ha,
	    "scsi(%d:%d:%d:%d): DEVICE RESET SUCCEEDED.\n", ha->host_no,
	    cmd->device->channel, cmd->device->id, cmd->device->lun);

	return_status = SUCCESS;

eh_dev_reset_done:

	if (!qla4xxx_is_eh_active(cmd->device->host))
		clear_bit(TQF_SUSPENDED, &tgt_entry->flags);

 	QL4PRINT(QLP2, printk("scsi%d: %s: return with status %s\n",
                              ha->host_no, __func__,
			      (return_status == FAILED) ? "FAILED" : "SUCCEDED"));

	return return_status;
 }


/**************************************************************************
 * qla4xxx_eh_bus_reset
 *      This routine is called by the Linux OS to reset the specified
 *      adapter/bus.
 *
 * Input:
 *      cmd - Pointer to Linux's SCSI command structure
 *
 * Returns:
 *      SUCCESS - Successfully reset adapter/bus
 *      FAILED  - Failed to reset adapter/bus
 *
 * Context:
 *      Kernel context.  io_request_lock LOCKED
 **************************************************************************/
int
qla4xxx_eh_bus_reset(struct scsi_cmnd *cmd)
{
	uint8_t status = QLA_SUCCESS;
	int     return_status = FAILED;
	scsi_qla_host_t *ha;
	ddb_entry_t *ddb_entry, *dtemp;
	ha = (scsi_qla_host_t *) cmd->device->host->hostdata;

	ql4_printk(KERN_INFO, ha,
	    "scsi(%d:%d:%d:%d): BUS RESET ISSUED.\n", ha->host_no,
	    cmd->device->channel, cmd->device->id, cmd->device->lun);

	spin_unlock_irq(ha->host->host_lock);
	if (qla4xxx_wait_for_hba_online(ha) != QLA_SUCCESS) {
		QL4PRINT(QLP2, printk("scsi%d:%d: %s: Unable to reset "
		    "bus.  Adapter DEAD.\n", ha->host_no,
		    cmd->device->channel, __func__));

		spin_lock_irq(ha->host->host_lock);
		return FAILED;
	}
	spin_lock_irq(ha->host->host_lock);

	/* Attempt to reset all valid targets with outstanding commands */
	list_for_each_entry_safe(ddb_entry, dtemp, &ha->ddb_list, list_entry) {
		QL4PRINT(QLP5, printk("scsi%d: %s: reset target b%d, t%x, "
		    "index [%d]\n", ha->host_no, __func__, ddb_entry->bus,
		    ddb_entry->target, ddb_entry->fw_ddb_index));

		/* Issue a reset */
		status |= qla4xxx_reset_target(ha, ddb_entry);
	}

	/*
	 * Status is QLA_SUCCESS if target resets for ALL devices completed
	 * successfully.  Otherwise the status is QLA_ERROR.
	 */
	if (status == QLA_SUCCESS)
		return_status = SUCCESS;

	ql4_printk(KERN_INFO, ha, "BUS RESET %s.\n",
	    (return_status == FAILED) ? "FAILED" : "SUCCEDED");
	DEBUG2(printk("%s: EXIT (%s)\n", __func__,
		    (return_status == FAILED) ? "FAILED" : "SUCCEDED");)
	return return_status;
}

/**************************************************************************
 * qla4xxx_reset_target
 *      This routine issues either a warm or cold target reset to the
 *      specified device.
 *
 * Input:
 *      ha - Pointer to host adapter structure.
 *      ddb_entry - Pointer to device database entry
 *
 * Remarks:
 *      The caller must ensure that the ddb_entry pointer is valid before
 *      calling this routine.
 *
 * Returns:
 *      QLA_SUCCESS - Successfully reset target
 *      QLA_ERROR   - Failed to reset target
 *
 * Context:
 *      Kernel context.
 **************************************************************************/
uint8_t
qla4xxx_reset_target(scsi_qla_host_t *ha, ddb_entry_t *ddb_entry)
{
	uint8_t status = QLA_SUCCESS;
	fc_lun_t *fclun;
	fc_port_t *fcport;
	uint8_t stat;

	/* Reset all LUNs on this target */
	fcport = ddb_entry->fcport;
	list_for_each_entry(fclun, &fcport->fcluns, list) {
		spin_unlock_irq(ha->host->host_lock);
		stat = qla4xxx_reset_lun(ha, ddb_entry, fclun);
		spin_lock_irq(ha->host->host_lock);
		if (stat == QLA_SUCCESS) {
			/* Send marker. */
			ha->marker_needed =1;

			/*
			 * Waiting for all active commands to complete for the
			 * device.
			 */
			status |= qla4xxx_eh_wait_for_active_target_commands(
			    ha, ddb_entry->target, fclun->lun);
		} else {
			status |= QLA_ERROR;
		}
	}

	if (status == QLA_SUCCESS) {
		QL4PRINT(QLP2, printk("scsi%d:%d:%d: device reset SUCCEEDED.\n",
		    ha->host_no, ddb_entry->bus, fcport->os_target_id));
	} else {
		QL4PRINT(QLP2, printk("scsi%d:%d:%d: device reset FAILED.\n",
		    ha->host_no, ddb_entry->bus, fcport->os_target_id));

		status = QLA_ERROR;
	}

	return status;
}

/**************************************************************************
 * qla4xxx_flush_active_srbs
 *      This routine is called just prior to a HARD RESET to return all
 *      outstanding commands back to the Operating System.
 *
 * Input:
 *      ha - Pointer to host adapter structure.
 *
 * Remarks:
 *      Caller should make sure that the following locks are released
 *      before this calling routine:
 *              Hardware lock, io_request_lock, list_lock, and lun_lock.
 *
 * Returns:
 *      None
 *
 * Context:
 *      Kernel context.
 **************************************************************************/
static void
qla4xxx_flush_active_srbs(scsi_qla_host_t *ha){
	srb_t    *srb;
	int      i;
	unsigned long flags;

	ENTER("qla4xxx_flush_active_srbs");

	spin_lock_irqsave(&ha->hardware_lock, flags);
	for (i = 1; i < MAX_SRBS; i++) {
		if ((srb = ha->active_srb_array[i]) != NULL) {
			QL4PRINT(QLP5,
				 printk("scsi%d: %s: found srb %p in active array, "
					"returning\n", ha->host_no, __func__, srb));
			del_from_active_array(ha, i);
			srb->cmd->result =  DID_RESET  <<  16;
			sp_put(ha,srb);
		}
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	// if (!list_empty(&ha->done_srb_q)) {
	// while ((srb = del_from_done_srb_q_head(ha)) != NULL)
	// sp_put(ha, srb);
	// }

	LEAVE("qla4xxx_flush_active_srbs");
}

/**************************************************************************
 * qla4xxx_eh_host_reset
 *      This routine is invoked by the Linux kernel to perform fatal error
 *      recovery on the specified adapter.
 *
 * Input:
 *      cmd - Pointer to Linux's SCSI command structure
 *
 * Returns:
 *      SUCCESS - Successfully recovered host adapter
 *      FAILED  - Failed to recover host adapter
 *
 * Context:
 *      Kernel context.  io_request_lock LOCKED
 **************************************************************************/
int
qla4xxx_eh_host_reset(struct scsi_cmnd *cmd)
{
	int return_status = FAILED;
	scsi_qla_host_t *ha;

	ha = (scsi_qla_host_t *) cmd->device->host->hostdata;

	ql4_printk(KERN_INFO, ha,
	    "scsi(%d:%d:%d:%d): HOST RESET ISSUED.\n", ha->host_no,
	    cmd->device->channel, cmd->device->id, cmd->device->lun);
	DEBUG2(printk("scsi(%d:%d:%d:%d): HOST RESET ISSUED.\n",
		      ha->host_no, cmd->device->channel,
		      cmd->device->id, cmd->device->lun);)

	spin_unlock_irq(ha->host->host_lock);

	if (qla4xxx_wait_for_hba_online(ha) != QLA_SUCCESS) {
		QL4PRINT(QLP2, printk("scsi%d:%d: %s: Unable to reset "
		    "host.  Adapter DEAD.\n", ha->host_no,
		    cmd->device->channel, __func__));

		spin_lock_irq(ha->host->host_lock);
		return FAILED;
	}

	if (qla4xxx_recover_adapter(ha, PRESERVE_DDB_LIST) == QLA_SUCCESS) {
		return_status = SUCCESS;
	}

	ql4_printk(KERN_INFO, ha, "HOST RESET %s.\n",
	    (return_status == FAILED) ? "FAILED" : "SUCCEDED");
	DEBUG2(printk("HOST RESET %s.\n",
	    (return_status == FAILED) ? "FAILED" : "SUCCEDED");)

	spin_lock_irq(ha->host->host_lock);

	return return_status;
}

/*
* qla4xxx_free_other_mem
*      Frees all adapter allocated memory.
*
* Input:
*      ha = adapter block pointer.
*/
static void
qla4xxx_free_other_mem(scsi_qla_host_t *ha)
{
	uint32_t        t;
	fc_port_t       *fcport, *fptemp;
	fc_lun_t        *fclun, *fltemp;

	if (ha == NULL) {
		/* error */
		DEBUG2(printk("%s: ERROR invalid ha pointer.\n", __func__));
		return;
	}

	/* Free the target and lun queues */
	for (t = 0; t < MAX_TARGETS; t++) {
		qla4xxx_tgt_free(ha, t);
	}

	/* Free fcport and fcluns */
	list_for_each_entry_safe(fcport, fptemp, &ha->fcports, list) {
		list_for_each_entry_safe(fclun, fltemp, &fcport->fcluns, list) {
			list_del_init(&fclun->list);
			kfree(fclun);
		}
		list_del_init(&fcport->list);
		kfree(fcport);
	}
	INIT_LIST_HEAD(&ha->fcports);
}

/*
 * Overrides for Emacs so that we almost follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 2
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -2
 * c-argdecl-indent: 2
 * c-label-offset: -2
 * c-continued-statement-offset: 2
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */



