/*
 *  drivers/s390/char/tape_34xx.c
 *    tape device discipline for 3480/3490 tapes.
 *
 *  S390 and zSeries version
 *    Copyright (C) 2001,2002 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *		 Tuan Ngo-Anh <ngoanh@de.ibm.com>
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/bio.h>
#include <linux/workqueue.h>
#include <asm/tape390.h>

#include "tape.h"
#include "tape_std.h"

#define PRINTK_HEADER "T34xx:"

enum tape34xx_type {
	tape_3480,
	tape_3490,
};

/*
 * Medium sense (asynchronous with callback) for 34xx tapes. There is no 'real'
 * medium sense call. So we just do a normal sense.
 */
static void
__tape_34xx_medium_sense_callback(struct tape_request *request, void *data)
{
	unsigned char      *sense;
	struct tape_device *device;

	request->callback = NULL;

	if(request->rc == 0 && (device = request->device) != NULL) {
		sense = request->cpdata;

		/*
		 * This isn't quite correct. But since INTERVENTION_REQUIRED
		 * means that the drive is 'neither ready nor on-line' it is
		 * only slightly inaccurate to say there is no tape loaded if
		 * the drive isn't online...
		 */
		if(sense[0] & SENSE_INTERVENTION_REQUIRED)
			tape_med_state_set(device, MS_UNLOADED);
		else
			tape_med_state_set(device, MS_LOADED);

		if(sense[1] & SENSE_WRITE_PROTECT)
			device->tape_generic_status |= GMT_WR_PROT(~0);
		else
			device->tape_generic_status &= ~GMT_WR_PROT(~0);
	}
	tape_free_request(request);
}

static int
tape_34xx_medium_sense(struct tape_device *device)
{
	struct tape_request *request;
	int                  rc;

	request = tape_alloc_request(1, 32);
	if(IS_ERR(request)) {
		DBF_EXCEPTION(6, "MSEN fail\n");
		return PTR_ERR(request);
	}

	request->op = TO_MSEN;
	tape_ccw_end(request->cpaddr, SENSE, 32, request->cpdata);
	request->callback = __tape_34xx_medium_sense_callback;

	rc = tape_do_io_async(device, request);

	return rc;
}

/*
 * These functions are currently used only to schedule a medium_sense for
 * later execution. This is because we get an interrupt whenever a medium
 * is inserted but cannot call tape_do_io* from an interrupt context.
 * Maybe that's useful for other actions we want to start from the
 * interrupt handler.
 */
static void
tape_34xx_work_handler(void *data)
{
	struct {
		struct tape_device	*device;
		enum tape_op		 op;
		struct work_struct	 work;
	} *p = data;

	switch(p->op) {
		case TO_MSEN:
			tape_34xx_medium_sense(p->device);
		default:
			DBF_EVENT(3, "T34XX: internal error: unknown work\n");
	}

	tape_put_device(p->device);
	kfree(p);
}

static int
tape_34xx_schedule_work(struct tape_device *device, enum tape_op op)
{
	struct {
		struct tape_device	*device;
		enum tape_op		 op;
		struct work_struct	 work;
	} *p;

	if((p = kmalloc(sizeof(*p), GFP_ATOMIC)) == NULL)
		return -ENOMEM;

	memset(p, 0, sizeof(*p));
	INIT_WORK(&p->work, tape_34xx_work_handler, p);

	atomic_inc(&device->ref_count);
	p->device = device;
	p->op     = op;

	schedule_work(&p->work);
	return 0;
}

/*
 * Done Handler is called when dev stat = DEVICE-END (successful operation)
 */
static int
tape_34xx_done(struct tape_device *device, struct tape_request *request)
{
	DBF_EVENT(6, "%s done\n", tape_op_verbose[request->op]);

	return TAPE_IO_SUCCESS;
}

static inline int
tape_34xx_erp_failed(struct tape_device *device,
		     struct tape_request *request, int rc)
{
	DBF_EVENT(3, "Error recovery failed for %s\n",
		  tape_op_verbose[request->op]);
	return rc;
}

static inline int
tape_34xx_erp_succeeded(struct tape_device *device,
		       struct tape_request *request)
{
	DBF_EVENT(3, "Error Recovery successful for %s\n",
		  tape_op_verbose[request->op]);
	return tape_34xx_done(device, request);
}

static inline int
tape_34xx_erp_retry(struct tape_device *device, struct tape_request *request)
{
	DBF_EVENT(3, "xerp retr %s\n",
		  tape_op_verbose[request->op]);
	return TAPE_IO_RETRY;
}

/*
 * This function is called, when no request is outstanding and we get an
 * interrupt
 */
static int
tape_34xx_unsolicited_irq(struct tape_device *device, struct irb *irb)
{
	if (irb->scsw.dstat == 0x85 /* READY */) {
		/* A medium was inserted in the drive. */
		DBF_EVENT(6, "xuud med\n");
		tape_34xx_schedule_work(device, TO_MSEN);
	} else {
		DBF_EVENT(3, "unsol.irq! dev end: %s\n",
				device->cdev->dev.bus_id);
		PRINT_WARN("Unsolicited IRQ (Device End) caught.\n");
		tape_dump_sense(device, NULL, irb);
	}
	return TAPE_IO_SUCCESS;
}

/*
 * Read Opposite Error Recovery Function:
 * Used, when Read Forward does not work
 */
static int
tape_34xx_erp_read_opposite(struct tape_device *device,
			    struct tape_request *request)
{
	if (request->op == TO_RFO) {
		/*
		 * We did read forward, but the data could not be read
		 * *correctly*. We transform the request to a read backward
		 * and try again.
		 */
		tape_std_read_backward(device, request);
		return tape_34xx_erp_retry(device, request);
	}
	if (request->op != TO_RBA)
		PRINT_ERR("read_opposite called with state:%s\n",
			  tape_op_verbose[request->op]);
	/*
	 * We tried to read forward and backward, but hat no
	 * success -> failed.
	 */
	return tape_34xx_erp_failed(device, request, -EIO);
}

static int
tape_34xx_erp_bug(struct tape_device *device, struct tape_request *request,
		  struct irb *irb, int no)
{
	if (request->op != TO_ASSIGN) {
		PRINT_WARN("An unexpected condition #%d was caught in "
			   "tape error recovery.\n", no);
		PRINT_WARN("Please report this incident.\n");
		if (request)
			PRINT_WARN("Operation of tape:%s\n",
				   tape_op_verbose[request->op]);
		tape_dump_sense(device, request, irb);
	}
	return tape_34xx_erp_failed(device, request, -EIO);
}

/*
 * Handle data overrun between cu and drive. The channel speed might
 * be too slow.
 */
static int
tape_34xx_erp_overrun(struct tape_device *device, struct tape_request *request,
		      struct irb *irb)
{
	if (irb->ecw[3] == 0x40) {
		PRINT_WARN ("Data overrun error between control-unit "
			    "and drive. Use a faster channel connection, "
			    "if possible! \n");
		return tape_34xx_erp_failed(device, request, -EIO);
	}
	return tape_34xx_erp_bug(device, request, irb, -1);
}

/*
 * Handle record sequence error.
 */
static int
tape_34xx_erp_sequence(struct tape_device *device,
		       struct tape_request *request, struct irb *irb)
{
	if (irb->ecw[3] == 0x41) {
		/*
		 * cu detected incorrect block-id sequence on tape.
		 */
		PRINT_WARN("Illegal block-id sequence found!\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	}
	/*
	 * Record sequence error bit is set, but erpa does not
	 * show record sequence error.
	 */
	return tape_34xx_erp_bug(device, request, irb, -2);
}

/*
 * This function analyses the tape's sense-data in case of a unit-check.
 * If possible, it tries to recover from the error. Else the user is
 * informed about the problem.
 */
static int
tape_34xx_unit_check(struct tape_device *device, struct tape_request *request,
		     struct irb *irb)
{
	int inhibit_cu_recovery;
	__u8* sense;

#ifdef CONFIG_S390_TAPE_BLOCK
	if (request->op == TO_BLOCK) {
		/*
		 * Recovery for block device requests. Set the block_position
		 * to something invalid and retry.
		 */
		device->blk_data.block_position = -1;
		if (request->retries-- <= 0)
			return tape_34xx_erp_failed(device, request, -EIO);
		else
			return tape_34xx_erp_retry(device, request);
	}
#endif

	inhibit_cu_recovery = (*device->modeset_byte & 0x80) ? 1 : 0;
	sense = irb->ecw;

	if (sense[0] & SENSE_COMMAND_REJECT) {
		if ((sense[1] & SENSE_WRITE_PROTECT) &&
		    (request->op == TO_DSE ||
		     request->op == TO_WRI ||
		     request->op == TO_WTM))
			/* medium is write protected */
			return tape_34xx_erp_failed(device, request, -EACCES);
		else
			return tape_34xx_erp_bug(device, request, irb, -3);
	}

	/*
	 * special cases for various tape-states when reaching
	 * end of recorded area
	 */
	if ((sense[0] == 0x08 || sense[0] == 0x10 || sense[0] == 0x12) &&
	    (sense[1] == 0x40 || sense[1] == 0x0c))
		switch (request->op) {
		case TO_FSF:
			/* Trying to seek beyond end of recorded area */
			return tape_34xx_erp_failed(device, request, -ENOSPC);
		case TO_LBL:
			/* Block could not be located. */
			return tape_34xx_erp_failed(device, request, -EIO);
		case TO_RFO:
			/* Read beyond end of recorded area -> 0 bytes read */
			return tape_34xx_erp_failed(device, request, 0);
		default:
			PRINT_ERR("Invalid op in %s:%i\n",
				  __FUNCTION__, __LINE__);
			return tape_34xx_erp_failed(device, request, 0);
		}

	/* Sensing special bits */
	if (sense[0] & SENSE_BUS_OUT_CHECK)
		return tape_34xx_erp_retry(device, request);

	if (sense[0] & SENSE_DATA_CHECK) {
		/*
		 * hardware failure, damaged tape or improper
		 * operating conditions
		 */
		switch (sense[3]) {
		case 0x23:
			/* a read data check occurred */
			if ((sense[2] & SENSE_TAPE_SYNC_MODE) ||
			    inhibit_cu_recovery)
				// data check is not permanent, may be
				// recovered. We always use async-mode with
				// cu-recovery, so this should *never* happen.
				return tape_34xx_erp_bug(device, request,
							 irb, -4);

			/* data check is permanent, CU recovery has failed */
			PRINT_WARN("Permanent read error\n");
			return tape_34xx_erp_failed(device, request, -EIO);
		case 0x25:
			// a write data check occurred
			if ((sense[2] & SENSE_TAPE_SYNC_MODE) ||
			    inhibit_cu_recovery)
				// data check is not permanent, may be
				// recovered. We always use async-mode with
				// cu-recovery, so this should *never* happen.
				return tape_34xx_erp_bug(device, request,
							 irb, -5);

			// data check is permanent, cu-recovery has failed
			PRINT_WARN("Permanent write error\n");
			return tape_34xx_erp_failed(device, request, -EIO);
		case 0x26:
			/* Data Check (read opposite) occurred. */
			return tape_34xx_erp_read_opposite(device, request);
		case 0x28:
			/* ID-Mark at tape start couldn't be written */
			PRINT_WARN("ID-Mark could not be written.\n");
			return tape_34xx_erp_failed(device, request, -EIO);
		case 0x31:
			/* Tape void. Tried to read beyond end of device. */
			PRINT_WARN("Read beyond end of recorded area.\n");
			return tape_34xx_erp_failed(device, request, -ENOSPC);
		case 0x41:
			/* Record sequence error. */
			PRINT_WARN("Invalid block-id sequence found.\n");
			return tape_34xx_erp_failed(device, request, -EIO);
		default:
			/* all data checks for 3480 should result in one of
			 * the above erpa-codes. For 3490, other data-check
			 * conditions do exist. */
			if (device->cdev->id.driver_info == tape_3480)
				return tape_34xx_erp_bug(device, request,
							 irb, -6);
		}
	}

	if (sense[0] & SENSE_OVERRUN)
		return tape_34xx_erp_overrun(device, request, irb);

	if (sense[1] & SENSE_RECORD_SEQUENCE_ERR)
		return tape_34xx_erp_sequence(device, request, irb);

	/* Sensing erpa codes */
	switch (sense[3]) {
	case 0x00:
		/* Unit check with erpa code 0. Report and ignore. */
		PRINT_WARN("Non-error sense was found. "
			   "Unit-check will be ignored.\n");
		return TAPE_IO_SUCCESS;
	case 0x21:
		/*
		 * Data streaming not operational. CU will switch to
		 * interlock mode. Reissue the command.
		 */
		PRINT_WARN("Data streaming not operational. "
			   "Switching to interlock-mode.\n");
		return tape_34xx_erp_retry(device, request);
	case 0x22:
		/*
		 * Path equipment check. Might be drive adapter error, buffer
		 * error on the lower interface, internal path not usable,
		 * or error during cartridge load.
		 */
		PRINT_WARN("A path equipment check occurred. One of the "
			   "following conditions occurred:\n");
		PRINT_WARN("drive adapter error, buffer error on the lower "
			   "interface, internal path not usable, error "
			   "during cartridge load.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x24:
		/*
		 * Load display check. Load display was command was issued,
		 * but the drive is displaying a drive check message. Can
		 * be threated as "device end".
		 */
		return tape_34xx_erp_succeeded(device, request);
	case 0x27:
		/*
		 * Command reject. May indicate illegal channel program or
		 * buffer over/underrun. Since all channel programs are
		 * issued by this driver and ought be correct, we assume a
		 * over/underrun situation and retry the channel program.
		 */
		return tape_34xx_erp_retry(device, request);
	case 0x29:
		/*
		 * Function incompatible. Either the tape is idrc compressed
		 * but the hardware isn't capable to do idrc, or a perform
		 * subsystem func is issued and the CU is not on-line.
		 */
		PRINT_WARN ("Function incompatible. Try to switch off idrc\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x2a:
		/*
		 * Unsolicited environmental data. An internal counter
		 * overflows, we can ignore this and reissue the cmd.
		 */
		return tape_34xx_erp_retry(device, request);
	case 0x2b:
		/*
		 * Environmental data present. Indicates either unload
		 * completed ok or read buffered log command completed ok.
		 */
		if (request->op == TO_RUN) {
			/* Rewind unload completed ok. */
			tape_med_state_set(device, MS_UNLOADED);
			return tape_34xx_erp_succeeded(device, request);
		}
		/* tape_34xx doesn't use read buffered log commands. */
		return tape_34xx_erp_bug(device, request, irb, sense[3]);
	case 0x2c:
		/*
		 * Permanent equipment check. CU has tried recovery, but
		 * did not succeed.
		 */
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x2d:
		/* Data security erase failure. */
		if (request->op == TO_DSE)
			return tape_34xx_erp_failed(device, request, -EIO);
		/* Data security erase failure, but no such command issued. */
		return tape_34xx_erp_bug(device, request, irb, sense[3]);
	case 0x2e:
		/*
		 * Not capable. This indicates either that the drive fails
		 * reading the format id mark or that that format specified
		 * is not supported by the drive.
		 */
		PRINT_WARN("Drive not capable processing the tape format!");
		return tape_34xx_erp_failed(device, request, -EMEDIUMTYPE);
	case 0x30:
		/* The medium is write protected. */
		PRINT_WARN("Medium is write protected!\n");
		return tape_34xx_erp_failed(device, request, -EACCES);
	case 0x32:
		// Tension loss. We cannot recover this, it's an I/O error.
		PRINT_WARN("The drive lost tape tension.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x33:
		/*
		 * Load Failure. The cartridge was not inserted correctly or
		 * the tape is not threaded correctly.
		 */
		PRINT_WARN("Cartridge load failure. Reload the cartridge "
			   "and try again.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x34:
		/*
		 * Unload failure. The drive cannot maintain tape tension
		 * and control tape movement during an unload operation.
		 */
		PRINT_WARN("Failure during cartridge unload. "
			   "Please try manually.\n");
		if (request->op == TO_RUN)
			return tape_34xx_erp_failed(device, request, -EIO);
		return tape_34xx_erp_bug(device, request, irb, sense[3]);
	case 0x35:
		/*
		 * Drive equipment check. One of the following:
		 * - cu cannot recover from a drive detected error
		 * - a check code message is shown on drive display
		 * - the cartridge loader does not respond correctly
		 * - a failure occurs during an index, load, or unload cycle
		 */
		PRINT_WARN("Equipment check! Please check the drive and "
			   "the cartridge loader.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x36:
		if (device->cdev->id.driver_info == tape_3490)
			/* End of data. */
			return tape_34xx_erp_failed(device, request, -EIO);
		/* This erpa is reserved for 3480 */
		return tape_34xx_erp_bug(device, request, irb, sense[3]);
	case 0x37:
		/*
		 * Tape length error. The tape is shorter than reported in
		 * the beginning-of-tape data.
		 */
		PRINT_WARN("Tape length error.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x38:
		/*
		 * Physical end of tape. A read/write operation reached
		 * the physical end of tape.
		 */
		if (request->op==TO_WRI ||
		    request->op==TO_DSE ||
		    request->op==TO_WTM)
			return tape_34xx_erp_failed(device, request, -ENOSPC);
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x39:
		/* Backward at Beginning of tape. */
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x3a:
		/* Drive switched to not ready. */
		PRINT_WARN("Drive not ready. Turn the ready/not ready switch "
			   "to ready position and try again.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x3b:
		/* Manual rewind or unload. This causes an I/O error. */
		PRINT_WARN("Medium was rewound or unloaded manually.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x42:
		/*
		 * Degraded mode. A condition that can cause degraded
		 * performance is detected.
		 */
		PRINT_WARN("Subsystem is running in degraded mode.\n");
		return tape_34xx_erp_retry(device, request);
	case 0x43:
		/* Drive not ready. */
		tape_med_state_set(device, MS_UNLOADED);
		/* Some commands commands are successful even in this case */
		if(sense[1] & SENSE_DRIVE_ONLINE) {
			switch(request->op) {
				case TO_ASSIGN:
				case TO_UNASSIGN:
				case TO_DIS:
					return tape_34xx_done(device, request);
					break;
				default:
					break;
			}
		}
		PRINT_WARN("The drive is not ready.\n");
		return tape_34xx_erp_failed(device, request, -ENOMEDIUM);
	case 0x44:
		/* Locate Block unsuccessful. */
		if (request->op != TO_BLOCK && request->op != TO_LBL)
			/* No locate block was issued. */
			return tape_34xx_erp_bug(device, request,
						 irb, sense[3]);
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x45:
		/* The drive is assigned to a different channel path. */
		PRINT_WARN("The drive is assigned elsewhere.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x46:
		/*
		 * Drive not on-line. Drive may be switched offline,
		 * the power supply may be switched off or
		 * the drive address may not be set correctly.
		 */
		PRINT_WARN("The drive is not on-line.");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x47:
		/* Volume fenced. CU reports volume integrity is lost. */
		PRINT_WARN("Volume fenced. The volume integrity is lost.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x48:
		/* Log sense data and retry request. */
		return tape_34xx_erp_retry(device, request);
	case 0x49:
		/* Bus out check. A parity check error on the bus was found. */
		PRINT_WARN("Bus out check. A data transfer over the bus "
			   "has been corrupted.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x4a:
		/* Control unit erp failed. */
		PRINT_WARN("The control unit I/O error recovery failed.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x4b:
		/*
		 * CU and drive incompatible. The drive requests micro-program
		 * patches, which are not available on the CU.
		 */
		PRINT_WARN("The drive needs microprogram patches from the "
			   "control unit, which are not available.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x4c:
		/*
		 * Recovered Check-One failure. Cu develops a hardware error,
		 * but is able to recover.
		 */
		return tape_34xx_erp_retry(device, request);
	case 0x4d:
		if (device->cdev->id.driver_info == tape_3490)
			/*
			 * Resetting event received. Since the driver does
			 * not support resetting event recovery (which has to
			 * be handled by the I/O Layer), retry our command.
			 */
			return tape_34xx_erp_retry(device, request);
		/* This erpa is reserved for 3480. */
		return tape_34xx_erp_bug(device, request, irb, sense[3]);
	case 0x4e:
		if (device->cdev->id.driver_info == tape_3490) {
			/*
			 * Maximum block size exceeded. This indicates, that
			 * the block to be written is larger than allowed for
			 * buffered mode.
			 */
			PRINT_WARN("Maximum block size for buffered "
				   "mode exceeded.\n");
			return tape_34xx_erp_failed(device, request, -ENOBUFS);
		}
		/* This erpa is reserved for 3480. */
		return tape_34xx_erp_bug(device, request, irb, sense[3]);
	case 0x50:
		/*
		 * Read buffered log (Overflow). CU is running in extended
		 * buffered log mode, and a counter overflows. This should
		 * never happen, since we're never running in extended
		 * buffered log mode.
		 */
		return tape_34xx_erp_retry(device, request);
	case 0x51:
		/*
		 * Read buffered log (EOV). EOF processing occurs while the
		 * CU is in extended buffered log mode. This should never
		 * happen, since we're never running in extended buffered
		 * log mode.
		 */
		return tape_34xx_erp_retry(device, request);
	case 0x52:
		/* End of Volume complete. Rewind unload completed ok. */
		if (request->op == TO_RUN) {
			tape_med_state_set(device, MS_UNLOADED);
			return tape_34xx_erp_succeeded(device, request);
		}
		return tape_34xx_erp_bug(device, request, irb, sense[3]);
	case 0x53:
		/* Global command intercept. */
		return tape_34xx_erp_retry(device, request);
	case 0x54:
		/* Channel interface recovery (temporary). */
		return tape_34xx_erp_retry(device, request);
	case 0x55:
		/* Channel interface recovery (permanent). */
		PRINT_WARN("A permanent channel interface error occurred.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x56:
		/* Channel protocol error. */
		PRINT_WARN("A channel protocol error occurred.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x57:
		if (device->cdev->id.driver_info == tape_3480) {
			/* Attention intercept. */
			PRINT_WARN("An attention intercept occurred, "
				   "which will be recovered.\n");
			return tape_34xx_erp_retry(device, request);
		} else {
			/* Global status intercept. */
			PRINT_WARN("An global status intercept was received, "
				   "which will be recovered.\n");
			return tape_34xx_erp_retry(device, request);
		}
	case 0x5a:
		/*
		 * Tape length incompatible. The tape inserted is too long,
		 * which could cause damage to the tape or the drive.
		 */
		PRINT_WARN("Tape length incompatible [should be IBM Cartridge "
			   "System Tape]. May cause damage to drive or tape.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x5b:
		/* Format 3480 XF incompatible */
		if (sense[1] & SENSE_BEGINNING_OF_TAPE)
			/* The tape will get overwritten. */
			return tape_34xx_erp_retry(device, request);
		PRINT_WARN("Tape format is incompatible to the drive, "
			   "which writes 3480-2 XF.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x5c:
		/* Format 3480-2 XF incompatible */
		PRINT_WARN("Tape format is incompatible to the drive. "
			   "The drive cannot access 3480-2 XF volumes.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x5d:
		/* Tape length violation. */
		PRINT_WARN("Tape length violation [should be IBM Enhanced "
			   "Capacity Cartridge System Tape]. May cause "
			   "damage to drive or tape.\n");
		return tape_34xx_erp_failed(device, request, -EMEDIUMTYPE);
	case 0x5e:
		/* Compaction algorithm incompatible. */
		PRINT_WARN("The volume is recorded using an incompatible "
			   "compaction algorithm, which is not supported by "
			   "the control unit.\n");
		return tape_34xx_erp_failed(device, request, -EMEDIUMTYPE);

		/* The following erpas should have been covered earlier. */
	case 0x23: /* Read data check. */
	case 0x25: /* Write data check. */
	case 0x26: /* Data check (read opposite). */
	case 0x28: /* Write id mark check. */
	case 0x31: /* Tape void. */
	case 0x40: /* Overrun error. */
	case 0x41: /* Record sequence error. */
		/* All other erpas are reserved for future use. */
	default:
		return tape_34xx_erp_bug(device, request, irb, sense[3]);
	}
}

/*
 * 3480/3490 interrupt handler
 */
static int
tape_34xx_irq(struct tape_device *device, struct tape_request *request,
	      struct irb *irb)
{
	if (request == NULL)
		return tape_34xx_unsolicited_irq(device, irb);

	if ((irb->scsw.dstat & DEV_STAT_UNIT_EXCEP) &&
	    (irb->scsw.dstat & DEV_STAT_DEV_END) &&
	    (request->op == TO_WRI)) {
		/* Write at end of volume */
		PRINT_INFO("End of volume\n"); /* XXX */
		return tape_34xx_erp_failed(device, request, -ENOSPC);
	}

	if (irb->scsw.dstat & DEV_STAT_UNIT_CHECK)
		return tape_34xx_unit_check(device, request, irb);

	if (irb->scsw.dstat & DEV_STAT_DEV_END)
		return tape_34xx_done(device, request);

	DBF_EVENT(6, "xunknownirq\n");
	PRINT_ERR("Unexpected interrupt.\n");
	PRINT_ERR("Current op is: %s", tape_op_verbose[request->op]);
	tape_dump_sense(device, request, irb);
	return TAPE_IO_STOP;
}

/*
 * ioctl_overload
 */
static int
tape_34xx_ioctl(struct tape_device *device,
		unsigned int cmd, unsigned long arg)
{
	if (cmd == TAPE390_DISPLAY)
		return tape_std_display(device, cmd, arg);
	else
		return -EINVAL;
}

static int
tape_34xx_setup_device(struct tape_device * device)
{
	DBF_EVENT(6, "34xx minor1: %x\n", device->first_minor);
	tape_34xx_medium_sense(device);
	return 0;
}

static void
tape_34xx_cleanup_device(struct tape_device * device)
{
	if (device->discdata) {
		kfree(device->discdata);
		device->discdata = NULL;
	}
}

/*
 * MTTELL: Tell block. Return the number of block relative to current file.
 */
static int
tape_34xx_mttell(struct tape_device *device, int mt_count)
{
	__u64 block_id;
	int rc;

	rc = tape_std_read_block_id(device, &block_id);
	if (rc)
		return rc;
	return (block_id >> 32) & 0x3fffff;
}

/*
 * MTSEEK: seek to the specified block.
 */
static int
tape_34xx_mtseek(struct tape_device *device, int mt_count)
{
	struct tape_request *request;

	if (mt_count > 0x400000) {
		DBF_EXCEPTION(6, "xsee parm\n");
		return -EINVAL;
	}
	request = tape_alloc_request(3, 4);
	if (IS_ERR(request))
		return PTR_ERR(request);
	request->op = TO_LBL;
	/* setup ccws */
	tape_ccw_cc(request->cpaddr, MODE_SET_DB, 1, device->modeset_byte);
	*(__u32 *) request->cpdata = mt_count |
		((*device->modeset_byte & 0x08) ? 0x01800000 : 0x01000000);
	tape_ccw_cc(request->cpaddr + 1, LOCATE, 4, request->cpdata);
	tape_ccw_end(request->cpaddr + 2, NOP, 0, NULL);
	/* execute it */
	return tape_do_io_free(device, request);
}

#ifdef CONFIG_S390_TAPE_BLOCK
/*
 * Tape block read for 34xx.
 */
static struct tape_request *
tape_34xx_bread(struct tape_device *device, struct request *req)
{
	struct tape_request *request;
	struct ccw1 *ccw;
	int count = 0,start_block,i;
	unsigned off;
	char *dst;
	struct bio_vec *bv;
	struct bio *bio;

	DBF_EVENT(6, "xBREDid:");
	start_block = req->sector >> TAPEBLOCK_HSEC_S2B;
	DBF_EVENT(6, "start_block = %i\n", start_block);

	/* Count the number of blocks for the request. */
	rq_for_each_bio(bio, req) {
		bio_for_each_segment(bv, bio, i) {
			count += bv->bv_len >> (TAPEBLOCK_HSEC_S2B + 9);
		}
	}
	/* Allocate the ccw request. */
	request = tape_alloc_request(2+count+1, 4);
	if (IS_ERR(request))
		return request;
	/* Setup ccws. */
	request->op = TO_BLOCK;
	*(__u32 *) request->cpdata = (start_block & 0x3fffff) |
		((*device->modeset_byte & 0x08) ? 0x81000000 : 0x01000000);
	ccw = request->cpaddr;
	ccw = tape_ccw_cc(ccw, MODE_SET_DB, 1, device->modeset_byte);

	/*
	 * We always setup a nop after the mode set ccw. This slot is
	 * used in tape_std_check_locate to insert a locate ccw if the
	 * current tape position doesn't match the start block to be read.
	 */
	ccw = tape_ccw_cc(ccw, NOP, 0, NULL);

	rq_for_each_bio(bio, req) {
		bio_for_each_segment(bv, bio, i) {
			dst = kmap(bv->bv_page) + bv->bv_offset;
			for (off = 0; off < bv->bv_len;
			     off += TAPEBLOCK_HSEC_SIZE) {
				ccw->flags = CCW_FLAG_CC;
				ccw->cmd_code = READ_FORWARD;
				ccw->count = TAPEBLOCK_HSEC_SIZE;
				set_normalized_cda(ccw, (void*) __pa(dst));
				ccw++;
				dst += TAPEBLOCK_HSEC_SIZE;
			}
		}
	}

	ccw = tape_ccw_end(ccw, NOP, 0, NULL);
	DBF_EVENT(6, "xBREDccwg\n");
	return request;
}

static void
tape_34xx_free_bread (struct tape_request *request)
{
	struct ccw1* ccw;

	/* Last ccw is a nop and doesn't need clear_normalized_cda */
	for (ccw = request->cpaddr; ccw->flags & CCW_FLAG_CC; ccw++)
		if (ccw->cmd_code == READ_FORWARD)
			clear_normalized_cda(ccw);
	tape_free_request(request);
}

/*
 * check_locate is called just before the tape request is passed to
 * the common io layer for execution. It has to check the current
 * tape position and insert a locate ccw if it doesn't match the
 * start block for the request.
 */
static void
tape_34xx_check_locate (struct tape_device *device,
			struct tape_request *request)
{
	int start_block;

	start_block = *(__u32 *) request->cpdata & 0x3fffff;
	if (start_block != device->blk_data.block_position)
		tape_ccw_cc(request->cpaddr + 1, LOCATE, 4, request->cpdata);
}
#endif

/*
 * List of 3480/3490 magnetic tape commands.
 */
static tape_mtop_fn tape_34xx_mtop[TAPE_NR_MTOPS] = {
	[MTRESET]	 = tape_std_mtreset,
	[MTFSF]		 = tape_std_mtfsf,
	[MTBSF]		 = tape_std_mtbsf,
	[MTFSR]		 = tape_std_mtfsr,
	[MTBSR]		 = tape_std_mtbsr,
	[MTWEOF]	 = tape_std_mtweof,
	[MTREW]		 = tape_std_mtrew,
	[MTOFFL]	 = tape_std_mtoffl,
	[MTNOP]		 = tape_std_mtnop,
	[MTRETEN]	 = tape_std_mtreten,
	[MTBSFM]	 = tape_std_mtbsfm,
	[MTFSFM]	 = tape_std_mtfsfm,
	[MTEOM]		 = tape_std_mteom,
	[MTERASE]	 = tape_std_mterase,
	[MTRAS1]	 = NULL,
	[MTRAS2]	 = NULL,
	[MTRAS3]	 = NULL,
	[MTSETBLK]	 = tape_std_mtsetblk,
	[MTSETDENSITY]	 = NULL,
	[MTSEEK]	 = tape_34xx_mtseek,
	[MTTELL]	 = tape_34xx_mttell,
	[MTSETDRVBUFFER] = NULL,
	[MTFSS]		 = NULL,
	[MTBSS]		 = NULL,
	[MTWSM]		 = NULL,
	[MTLOCK]	 = NULL,
	[MTUNLOCK]	 = NULL,
	[MTLOAD]	 = tape_std_mtload,
	[MTUNLOAD]	 = tape_std_mtunload,
	[MTCOMPRESSION]	 = tape_std_mtcompression,
	[MTSETPART]	 = NULL,
	[MTMKPART]	 = NULL
};

/*
 * Tape discipline structure for 3480 and 3490.
 */
static struct tape_discipline tape_discipline_34xx = {
	.owner = THIS_MODULE,
	.setup_device = tape_34xx_setup_device,
	.cleanup_device = tape_34xx_cleanup_device,
	.process_eov = tape_std_process_eov,
	.irq = tape_34xx_irq,
	.read_block = tape_std_read_block,
	.write_block = tape_std_write_block,
	.assign = tape_std_assign,
	.unassign = tape_std_unassign,
#ifdef CONFIG_S390_TAPE_BLOCK
	.bread = tape_34xx_bread,
	.free_bread = tape_34xx_free_bread,
	.check_locate = tape_34xx_check_locate,
#endif
	.ioctl_fn = tape_34xx_ioctl,
	.mtop_array = tape_34xx_mtop
};

static struct ccw_device_id tape_34xx_ids[] = {
	{ CCW_DEVICE_DEVTYPE(0x3480, 0, 0x3480, 0), driver_info: tape_3480},
	{ CCW_DEVICE_DEVTYPE(0x3490, 0, 0x3490, 0), driver_info: tape_3490},
	{ /* end of list */ }
};

static int
tape_34xx_enable(struct ccw_device *cdev)
{
	return tape_enable_device(cdev->dev.driver_data,
				  &tape_discipline_34xx);
}

static int
tape_34xx_disable(struct ccw_device *cdev)
{
	tape_disable_device(cdev->dev.driver_data);
	return 0;
}

static struct ccw_driver tape_34xx_driver = {
	.name = "tape_34xx",
	.owner = THIS_MODULE,
	.ids = tape_34xx_ids,
	.probe = tape_generic_probe,
	.remove = tape_generic_remove,
	.set_online = tape_34xx_enable,
	.set_offline = tape_34xx_disable,
};

static int
tape_34xx_init (void)
{
	int rc;

	DBF_EVENT(3, "34xx init: $Revision: 1.8 $\n");
	/* Register driver for 3480/3490 tapes. */
	rc = ccw_driver_register(&tape_34xx_driver);
	if (rc)
		DBF_EVENT(3, "34xx init failed\n");
	else
		DBF_EVENT(3, "34xx registered\n");
	return rc;
}

static void
tape_34xx_exit(void)
{
	ccw_driver_unregister(&tape_34xx_driver);
}

MODULE_DEVICE_TABLE(ccw, tape_34xx_ids);
MODULE_AUTHOR("(C) 2001-2002 IBM Deutschland Entwicklung GmbH");
MODULE_DESCRIPTION("Linux on zSeries channel attached 3480 tape "
		   "device driver ($Revision: 1.8 $)");
MODULE_LICENSE("GPL");

module_init(tape_34xx_init);
module_exit(tape_34xx_exit);
