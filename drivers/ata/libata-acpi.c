/*
 * libata-acpi.c
 * Provides ACPI support for PATA/SATA.
 *
 * Copyright (C) 2006 Intel Corp.
 * Copyright (C) 2006 Randy Dunlap
 * Copyright (C) 2006 SUSE Linux Products GmbH
 * Copyright (C) 2006 Hannes Reinecke
 */

#include <linux/ata.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/acpi.h>
#include <linux/libata.h>
#include <linux/pci.h>
#include "libata.h"

#include <acpi/acpi_bus.h>
#include <acpi/acnames.h>
#include <acpi/acnamesp.h>
#include <acpi/acparser.h>
#include <acpi/acexcep.h>
#include <acpi/acmacros.h>
#include <acpi/actypes.h>

#define SATA_ROOT_PORT(x)	(((x) >> 16) & 0xffff)
#define SATA_PORT_NUMBER(x)	((x) & 0xffff)	/* or NO_PORT_MULT */
#define SATA_PORT_ADR(x,y)      ((((x) & 0xffff) << 16) | ((y) & 0xffff))
#define NO_PORT_MULT		0xffff
#define SATA_ADR_RSVD		0xffffffff

#define REGS_PER_GTF		7
struct taskfile_array {
	u8	tfa[REGS_PER_GTF];	/* regs. 0x1f1 - 0x1f7 */
};

struct GTM_buffer {
	u32	PIO_speed0;
	u32	DMA_speed0;
	u32	PIO_speed1;
	u32	DMA_speed1;
	u32	GTM_flags;
};

/**
 * ata_acpi_get_name - Retrieve the ACPI name of an object
 * @handle: handle of the ACPI object
 *
 * Returns a string with the ACPI object name or NULL on error.
 * The returned string has to be freed by the caller.
 */
static char *ata_acpi_get_name(acpi_handle *handle)
{
	struct acpi_buffer namebuf = {.length = ACPI_ALLOCATE_BUFFER,
				      .pointer = NULL};
	int status;

	status = acpi_get_name(handle, ACPI_FULL_PATHNAME, &namebuf);
	if (ACPI_FAILURE(status))
		return NULL;

	return namebuf.pointer;
}

/**
 * ata_acpi_check_handle - Check the ACPI handle of an ata_port
 * @ap: target ata_port
 * @dev: device to locate
 * @handle: returned acpi_handle for @dev
 * @pcidevfn: return PCI device.func for @dev
 *
 * This function performs some sanity checks on the ACPI handle
 * for a given ata_port. Once the ACPI interpreter has somewhat
 * stabilized we can probably get rid of this.
 *
 * Returns 0 on success, <0 on error.
 */
static int ata_acpi_check_handle(struct ata_port *ap, acpi_handle *handle)
{
	unsigned int bus, devnum, func;
	acpi_integer addr;
	acpi_handle dev_handle, parent_handle;
	struct acpi_buffer buffer = {.length = ACPI_ALLOCATE_BUFFER,
				     .pointer = NULL};
	char *pathname = NULL;
	acpi_status status;
	struct acpi_device_info	*dinfo = NULL;
	int ret = -ENODEV;
	struct pci_dev *pdev = to_pci_dev(ap->dev);

	bus = pdev->bus->number;
	devnum = PCI_SLOT(pdev->devfn);
	func = PCI_FUNC(pdev->devfn);

	dev_handle = DEVICE_ACPI_HANDLE(ap->dev);
	parent_handle = DEVICE_ACPI_HANDLE(ap->dev->parent);

	if (!dev_handle)
		return -ENODEV;

	/* Get the ACPI object name */
	pathname = ata_acpi_get_name(dev_handle);
	if (!pathname) {
		if (ata_msg_probe(ap))
			ata_port_printk(ap, KERN_DEBUG,
					"%s: get_name failed (dev %s, handle %p)\n",
					__FUNCTION__, pci_name(pdev), dev_handle);
		return -ENODEV;
	}

	/* Check whether the parent object has the same bus address */
	status = acpi_get_object_info(parent_handle, &buffer);
	if (ACPI_FAILURE(status)) {
		if (ata_msg_probe(ap))
			ata_port_printk(ap, KERN_DEBUG,
					"%s: no info for parent of %s"
					"(pci %s, handle %p)\n",
					__FUNCTION__, pathname,
					pci_name(pdev), parent_handle);
		goto err;
	}
	dinfo = buffer.pointer;
	if (!dinfo || !(dinfo->valid & ACPI_VALID_ADR) || 
	    dinfo->address != bus) {
		if (ata_msg_probe(ap))
			ata_port_printk(ap, KERN_DEBUG,
					"%s: wrong bus for parent of %s"
					" (%llu, should be %d)\n",
					__FUNCTION__, pathname, 
					dinfo ? (unsigned long long)dinfo->address
					: -1ULL, bus);
		goto err;
	}


	/* ACPI spec for _ADR for PCI bus: */
	addr = (acpi_integer)(devnum << 16 | func);
	dev_handle = acpi_get_child(parent_handle, addr);
	if (!dev_handle) {
		if (ata_msg_probe(ap))
			ata_port_printk(ap, KERN_DEBUG,
					"%s: parent of %s has no child "
					"with addr: 0x%llx\n",
					__FUNCTION__, pathname,
					(unsigned long long)addr );
		goto err;
	}

	if (dev_handle != DEVICE_ACPI_HANDLE(ap->dev)) {
		if (ata_msg_probe(ap))
			ata_port_printk(ap, KERN_DEBUG,
					"%s: handle for object %s "
					"do not match (is %p, should %p)\n",
					__FUNCTION__, pathname,
					DEVICE_ACPI_HANDLE(ap->dev),
					dev_handle );
	}

	if (ata_msg_probe(ap)) {
		ata_port_printk(ap, KERN_DEBUG,
				"%s: found %s (addr: 0x%llx, handle: 0x%p)\n",
				__FUNCTION__, pathname,
				(unsigned long long)addr, dev_handle);
	}
	*handle = dev_handle;
	ret = 0;
err:
	kfree(dinfo);
	kfree(pathname);
	return ret;
}

/**
 * sata_get_dev_handle - finds acpi_handle for a given SATA device
 * @atadev: target ata_device
 * @dev_handle: acpi_handle for @atadev->ap
 *
 * Returns the ACPI handle for a given SATA device. The ACPI layout
 * for SATA devices is:
 *
 * \_SB
 *     PCI0
 *         SATA          S-ATA controller
 *             _ADR      PCI Address of the SATA controller
 *             PRT0      Port 0 device
 *                 _ADR  Physical port and multiplier topology
 *             PRTn      Port n device
 *
 *
 * Returns 0 on success, <0 on error.
 */
static int sata_get_dev_handle(struct ata_device *atadev)
{
	struct ata_port *ap = atadev->ap;
	acpi_integer	addr;
	acpi_handle     dev_handle, port_handle;
	int             err = 0;
	char            *objname;

	/* Check APCI handle of the controller */
	err = ata_acpi_check_handle(ap, &dev_handle);
	if (err < 0) {
		if (ata_msg_probe(ap))
			ata_dev_printk(atadev, KERN_DEBUG,
				       "%s: ata_acpi_check_handle failed (%d)\n",
				       __FUNCTION__, err);
		return -ENODEV;
	}

	/* Try device without port multiplier first */
	addr = SATA_PORT_ADR(ap->port_no, NO_PORT_MULT);
	port_handle = acpi_get_child(dev_handle, addr);
	if (!port_handle) {
		/* Check for port multiplier */
		if (ata_msg_probe(ap))
			ata_dev_printk(atadev, KERN_DEBUG,
					"%s: No ACPI handle for SATA "
					"adr=0x%08llx, checking for "
					"port multiplier\n",
					__FUNCTION__, (unsigned long long)addr);
		addr = SATA_PORT_ADR(ap->port_no, atadev->devno);
		port_handle = acpi_get_child(dev_handle, addr);
		if (!port_handle) {
			if (ata_msg_probe(ap))
				ata_dev_printk(atadev, KERN_DEBUG,
						"%s: No ACPI handle for SATA "
						"adr=0x%08lx\n",__FUNCTION__,
						(unsigned long)addr);
			return -ENODEV;
		}
	}

	/* Get the ACPI object name */
	objname = ata_acpi_get_name(port_handle);
	if (!objname) {
		if (ata_msg_probe(ap))
			ata_dev_printk(atadev, KERN_DEBUG,
				       "%s: get_name failed (adr 0x%llx, "
				       "handle %p)\n", __FUNCTION__,
				       (unsigned long long)addr, dev_handle);
		return -ENODEV;
	}

	if (ata_msg_probe(ap))
		ata_dev_printk(atadev, KERN_DEBUG,
			       "%s: using %s (adr=0x%08llx, handle=0x%p)\n",
				__FUNCTION__, objname,
				(unsigned long long)addr, port_handle);
	kfree(objname);

	atadev->obj_handle = port_handle;
	return 0;
}


/**
 * pata_get_dev_handle - finds acpi_handle for a given PATA device
 * @atadev: target ata_device
 * @dev_handle: acpi_handle for @atadev->ap
 *
 * Returns the ACPI handle for a given PATA device. The ACPI layout
 * for IDE devices is:
 *
 * \_SB
 *    PCI0
 *        IDE0                IDE controller
 *            _ADR            PCI Address of the first IDE channel
 *            PRIM            IDE channel
 *                _ADR        Address of the channel (0 primary, 1 secondary)
 *                MSTR        IDE drive
 *                    _ADR    Adress of the device (0 master, 1 slave)
 *
 *
 * When a correct ACPI handle is found it is being attached to 
 * @atadev->obj_handle.
 * Returns 0 on success, <0 on error.
 */
static int pata_get_dev_handle(struct ata_device *atadev)
{
	struct ata_port *ap = atadev->ap;
	int err = 0;
	acpi_handle dev_handle, chan_handle, drive_handle;
	char *objname;

	/* Check APCI handle of the controller */
	err = ata_acpi_check_handle(ap, &dev_handle);
	if (err < 0) {
		if (ata_msg_probe(ap))
			ata_dev_printk(atadev, KERN_DEBUG,
				       "%s: ata_acpi_check_handle failed (%d)\n",
				       __FUNCTION__, err);
		return -ENODEV;
	}

	/* Get the IDE channel object */
	/* Channel address is ap->port_no */
	chan_handle = acpi_get_child(dev_handle, ap->port_no);
	if (!chan_handle) {
		if (ata_msg_probe(ap))
			ata_dev_printk(atadev, KERN_DEBUG,
				       "%s: no ACPI handle for chan=%d\n",
				       __FUNCTION__, ap->port_no);
		return -ENODEV;
	}

	/* Get the IDE drive object */
	/* Drive address is atadev->devno */
	drive_handle = acpi_get_child(chan_handle, atadev->devno);
	if (!drive_handle) {
		if (ata_msg_probe(ap))
			ata_dev_printk(atadev, KERN_DEBUG,
				       "%s: no ACPI handle for drive=%d:%d\n",
				       __FUNCTION__, ap->port_no, 
				       atadev->devno);
		return -ENODEV;
	}

	/* Get the ACPI object name */
	objname = ata_acpi_get_name(drive_handle);
	if (!objname) {
		if (ata_msg_probe(ap))
			ata_dev_printk(atadev, KERN_DEBUG,
				       "%s: get_name failed (handle %p)\n",
				       __FUNCTION__, drive_handle);
		return -ENODEV;
	}

	if (ata_msg_probe(ap))
		ata_dev_printk(atadev, KERN_DEBUG,
			       "%s: using %s (drive=%d:%d, handle=0x%p)\n",
			       __FUNCTION__, objname,
			       ap->port_no, atadev->devno, drive_handle);
	kfree(objname);

	atadev->obj_handle = drive_handle;
	return err;
}

/**
 * pata_get_chan_handle - finds acpi_handle for a given PATA port
 * @ap: target ata_port
 * @ret_handle: returns the acpi_handle for @ap
 *
 * Returns the ACPI handle for a given PATA device. The ACPI layout
 * for IDE devices is:
 *
 * \_SB
 *    PCI0
 *        IDE0                IDE controller
 *            _ADR            PCI Address of the first IDE channel
 *            PRIM            IDE channel
 *                _ADR        Address of the channel (0 primary, 1 secondary)
 *
 *
 * Returns 0 on success, <0 on error.
 */
static int pata_get_chan_handle(struct ata_port *ap,
				acpi_handle *ret_handle)
{
	int err = 0;
	acpi_handle dev_handle, chan_handle;
	char *objname;

	/* Check APCI handle of the controller */
	err = ata_acpi_check_handle(ap, &dev_handle);
	if (err < 0) {
		if (ata_msg_probe(ap))
			ata_port_printk(ap, KERN_DEBUG,
				       "%s: ata_acpi_check_handle failed (%d)\n",
				       __FUNCTION__, err);
		return -ENODEV;
	}

	/* Get the IDE channel object */
	/* Channel address is ap->port_no */
	chan_handle = acpi_get_child(dev_handle, ap->port_no);
	if (!chan_handle) {
		if (ata_msg_probe(ap))
			ata_port_printk(ap, KERN_DEBUG,
				       "%s: no ACPI handle for chan=%d\n",
				       __FUNCTION__, ap->port_no);
		return -ENODEV;
	}

	/* Get the ACPI object name */
	objname = ata_acpi_get_name(chan_handle);
	if (!objname) {
		if (ata_msg_probe(ap))
			ata_port_printk(ap, KERN_DEBUG,
					"%s: get_name failed\n",
					__FUNCTION__);
		return -ENODEV;
	}

	if (ata_msg_probe(ap))
		ata_port_printk(ap, KERN_DEBUG,
				"%s: using %s (chan=%d, handle=0x%p)\n",
				__FUNCTION__, objname, 
				ap->port_no, chan_handle);
	kfree(objname);

	*ret_handle = chan_handle;
	return err;
}

/**
 * ata_acpi_push_id - send Identify data to a drive
 * @atadev: the ata_device for the drive
 *
 * Executes _SDD ACPI object; this is for SATA mode only.
 * The _SDD objects sends the device identification as
 * received by Identify (Packet) Device to the ACPI code.
 * This allows the ACPI code to modify the contents of
 * _GTF (which has to be executed after _SDD) according
 * to the detected device.
 */
int ata_acpi_push_id(struct ata_device *atadev)
{
	int				err = -ENODEV;
	struct ata_port			*ap = atadev->ap;
	acpi_status			status;
	struct acpi_object_list		input;
	union acpi_object 		in_params[1];

	if (!(libata_acpi & ATA_ACPI_SATA_SDD))
		return 0;

	if (ata_msg_probe(ap))
		ata_dev_printk(atadev, KERN_DEBUG,
			"%s: ap->id: %d, devno = %d, port#: %d\n",
			__FUNCTION__, ap->id, atadev->devno,
			ap->port_no);

	/* Don't continue if it's not a SATA device. */
	if (!ata_id_is_sata(atadev->id)) {
		if (ata_msg_probe(ap))
			ata_dev_printk(atadev, KERN_DEBUG,
				       "%s: skipping for PATA mode\n",
				       __FUNCTION__);
		goto out;
	}

	/* Don't continue if device has no _ADR method.
	 * _SDD is intended for known motherboard devices. */

	/* Get this drive's _ADR info. if not already known. */
	if (!atadev->obj_handle) {
		err = sata_get_dev_handle(atadev);
		if (err < 0 || !atadev->obj_handle) {
			if (ata_msg_probe(ap))
				ata_dev_printk(atadev, KERN_DEBUG,
					       "%s: sata_get_dev_handle failed\n",
					       __FUNCTION__);
			goto out;
		}
	}

	/* Give the drive Identify data to the drive via the _SDD method */
	/* _SDD: set up input parameters */
	input.count = 1;
	input.pointer = in_params;
	in_params[0].type = ACPI_TYPE_BUFFER;
	in_params[0].buffer.length = sizeof(atadev->id[0]) * ATA_ID_WORDS;
	in_params[0].buffer.pointer = (u8 *)atadev->id;
	/* Output buffer: _SDD has no output */

	/* It's OK for _SDD to be missing too. */
	swap_buf_le16(atadev->id, ATA_ID_WORDS);
	status = acpi_evaluate_object(atadev->obj_handle, "_SDD", &input, NULL);
	swap_buf_le16(atadev->id, ATA_ID_WORDS);

	err = ACPI_FAILURE(status) ? -EIO : 0;
	if (err < 0) {
		if (ata_msg_probe(ap))
			ata_dev_printk(atadev, KERN_DEBUG,
				       "%s _SDD error: status = 0x%x\n",
				       __FUNCTION__, status);
	}
out:
	return err;
}
EXPORT_SYMBOL_GPL(ata_acpi_push_id);

/**
 * do_drive_get_GTF - get the drive bootup default taskfile settings
 * @atadev: the ata_device for the drive
 * @gtf_length: number of bytes of _GTF data returned at @gtf_address
 * @gtf_address: buffer containing _GTF taskfile arrays
 *
 * This applies to both PATA and SATA drives.
 *
 * The _GTF method has no input parameters.
 * It returns a variable number of register set values (registers
 * hex 1F1..1F7, taskfiles).
 * The <variable number> is not known in advance, so have ACPI-CA
 * allocate the buffer as needed and return it, then free it later.
 *
 * The returned @gtf_length and @gtf_address are only valid if the
 * function return value is 0.
 */
static int do_drive_get_GTF(struct ata_device *atadev,
			    unsigned int *gtf_length, 
			    unsigned long *gtf_address,
			    unsigned long *obj_loc)
{
	acpi_status			status;
	struct acpi_buffer		output;
	union acpi_object 		*out_obj;
	struct ata_port			*ap = atadev->ap;
	int				err = -ENODEV;

	*gtf_length = 0;
	*gtf_address = 0UL;
	*obj_loc = 0UL;

	if (ata_msg_probe(ap))
		ata_dev_printk(atadev, KERN_DEBUG,
			"%s: ENTER: ap->id: %d, port#: %d, dev#: %d\n",
		       __FUNCTION__, ap->id, ap->port_no, atadev->devno);

	/* Don't continue if device has no _ADR method.
	 * _GTF is intended for known motherboard devices. */

	/* Get this drive's _ADR info. if not already known. */
	if (!atadev->obj_handle) {
		if (!ata_id_is_sata(atadev->id)) {
			err = pata_get_dev_handle(atadev);
			if (err < 0 || !atadev->obj_handle) {
				if (ata_msg_probe(ap))
					ata_dev_printk(atadev, KERN_DEBUG,
						       "%s: pata_get_dev_handle "
						       "failed (%d)\n",
						       __FUNCTION__, err);
				goto out;
			}
		} else {
			err = sata_get_dev_handle(atadev);
			if (err < 0 || !atadev->obj_handle) {
				if (ata_msg_probe(ap))
					ata_dev_printk(atadev, KERN_DEBUG,
						       "%s: sata_get_dev_handle "
						       "failed (%d)\n",
						       __FUNCTION__, err);
				goto out;
			}
		}
	}

	/* Setting up output buffer */
	output.length = ACPI_ALLOCATE_BUFFER;
	output.pointer = NULL;	/* ACPI-CA sets this; save/free it later */

	/* _GTF has no input parameters */
	err = -EIO;
	status = acpi_evaluate_object(atadev->obj_handle, "_GTF",
					NULL, &output);
	if (ACPI_FAILURE(status)) {
		if (ata_msg_probe(ap))
			ata_dev_printk(atadev, KERN_DEBUG,
				       "%s: Run _GTF error: status = 0x%x\n",
				       __FUNCTION__, status);
		goto out;
	}

	if (!output.length || !output.pointer) {
		if (ata_msg_probe(ap))
			ata_dev_printk(atadev, KERN_DEBUG,
				       "%s: Run _GTF: "
				       "length or ptr is NULL "
				       "(0x%llx, 0x%p)\n",
				       __FUNCTION__,
				       (unsigned long long)output.length,
				       output.pointer);
		kfree(output.pointer);
		goto out;
	}

	out_obj = output.pointer;
	if (out_obj->type != ACPI_TYPE_BUFFER) {
		kfree(output.pointer);
		if (ata_msg_probe(ap))
			ata_dev_printk(atadev, KERN_DEBUG,
				       "%s: Run _GTF: error: "
				       "expected object type of ACPI_TYPE_BUFFER, "
				       "got 0x%x\n",
				       __FUNCTION__, out_obj->type);
		err = -ENOENT;
		goto out;
	}

	if (!out_obj->buffer.length || !out_obj->buffer.pointer ||
	    out_obj->buffer.length % REGS_PER_GTF) {
		if (ata_msg_drv(ap))
			ata_dev_printk(atadev, KERN_ERR,
				       "%s: unexpected GTF length (%d) or addr (0x%p)\n",
				       __FUNCTION__, out_obj->buffer.length,
				       out_obj->buffer.pointer);
		err = -ENOENT;
		goto out;
	}

	*gtf_length = out_obj->buffer.length;
	*gtf_address = (unsigned long)out_obj->buffer.pointer;
	*obj_loc = (unsigned long)out_obj;
	if (ata_msg_probe(ap))
		ata_dev_printk(atadev, KERN_DEBUG, "%s: returning "
			       "gtf_length=%d, gtf_address=0x%lx, obj_loc=0x%lx\n",
			       __FUNCTION__, *gtf_length, *gtf_address, *obj_loc);
	err = 0;
out:
	return err;
}

/**
 * taskfile_load_raw - send taskfile registers to host controller
 * @atadev: device to which the taskfile is sent
 * @gtf: raw ATA taskfile register set (0x1f1 - 0x1f7)
 *
 * Outputs ATA taskfile to a given ATA device.
 * Writes the control, feature, nsect, lbal, lbam, and lbah registers.
 * Optionally (ATA_TFLAG_LBA48) writes hob_feature, hob_nsect,
 * hob_lbal, hob_lbam, and hob_lbah.
 *
 * This function waits for idle (!BUSY and !DRQ) after writing
 * registers.  If the control register has a new value, this
 * function also waits for idle after writing control and before
 * writing the remaining registers.
 *
 * BIG FAT WARNING: This function allows the ACPI code to sent
 * arbitrary commands to the drive. SATA devices seem to work
 * properly, but for PATA devices this is a good way to lock up
 * the drive.
 *
 */
static void taskfile_load_raw(struct ata_device *atadev,
			      const struct taskfile_array *gtf)
{
	struct ata_port *ap = atadev->ap;

	if (ata_msg_probe(ap))
		ata_dev_printk(atadev, KERN_DEBUG,
			       "%s: (0x1f1-1f7): hex: "
			       "%02x %02x %02x %02x %02x %02x %02x\n",
			       __FUNCTION__,
			       gtf->tfa[0], gtf->tfa[1], gtf->tfa[2],
			       gtf->tfa[3], gtf->tfa[4], gtf->tfa[5],
			       gtf->tfa[6]);

	if (!(ata_acpi_flags(atadev,libata_acpi) & ATA_ACPI_TFX)) {
		if (ata_msg_probe(ap))
			ata_dev_printk(atadev, KERN_DEBUG,
				       "%s: ACPI tf execution disabled\n",
				       __FUNCTION__);
		return;
	}

	if (ap->ops->qc_issue) {
		struct ata_taskfile tf;
		unsigned int err;

		ata_tf_init(atadev, &tf);

		/* convert gtf to tf */
		tf.flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE; /* TBD */
		tf.protocol = atadev->class == ATA_DEV_ATAPI ?
			ATA_PROT_ATAPI_NODATA : ATA_PROT_NODATA;
		tf.feature = gtf->tfa[0];	/* 0x1f1 */
		tf.nsect   = gtf->tfa[1];	/* 0x1f2 */
		tf.lbal    = gtf->tfa[2];	/* 0x1f3 */
		tf.lbam    = gtf->tfa[3];	/* 0x1f4 */
		tf.lbah    = gtf->tfa[4];	/* 0x1f5 */
		tf.device  = gtf->tfa[5];	/* 0x1f6 */
		tf.command = gtf->tfa[6];	/* 0x1f7 */

		err = ata_exec_internal(atadev, &tf, NULL, DMA_NONE, NULL, 0);
		if (ata_msg_probe(ap))
			ata_dev_printk(atadev, KERN_DEBUG,
				       "%s: ata_exec_internal %s "
				       "with errmask 0x%x\n",__FUNCTION__,
				       err?"failed":"suceeded", err);
	} else
		if (ata_msg_warn(ap))
			ata_dev_printk(atadev, KERN_WARNING,
				       "%s: SATA driver is missing qc_issue function entry points\n",
				       __FUNCTION__);
}

/**
 * do_drive_set_taskfiles - write the drive taskfile settings from _GTF
 * @ap: the ata_port for the drive
 * @atadev: target ata_device
 * @gtf_length: total number of bytes of _GTF taskfiles
 * @gtf_address: location of _GTF taskfile arrays
 *
 * This applies to both PATA and SATA drives.
 *
 * Write {gtf_address, length gtf_length} in groups of
 * REGS_PER_GTF bytes.
 */
static int do_drive_set_taskfiles(struct ata_device *atadev,
				  unsigned int gtf_length,
				  unsigned long gtf_address)
{
	struct ata_port		*ap = atadev->ap;
	int			err = -ENODEV;
	int			gtf_count = gtf_length / REGS_PER_GTF;
	int			ix;
	struct taskfile_array	*gtf;

	if (!gtf_count)		/* shouldn't be here */
		goto out;

	if (ata_msg_probe(ap))
		ata_dev_printk(atadev, KERN_DEBUG,
			       "%s: total GTF bytes=%u (0x%x), "
			       "gtf_count=%d, addr=0x%lx\n",
			       __FUNCTION__, gtf_length, gtf_length,
			       gtf_count, gtf_address);
	if (gtf_length % REGS_PER_GTF) {
		if (ata_msg_drv(ap))
			ata_dev_printk(atadev, KERN_ERR,
				       "%s: unexpected GTF length (%d)\n",
				       __FUNCTION__, gtf_length);
		goto out;
	}

	for (ix = 0; ix < gtf_count; ix++) {
		gtf = (struct taskfile_array *)
			(gtf_address + ix * REGS_PER_GTF);

		/* send all TaskFile registers (0x1f1-0x1f7) *in*that*order* */
		taskfile_load_raw(atadev, gtf);
	}

	err = 0;
out:
	return err;
}

/**
 * ata_acpi_exec_tfs - get then write drive taskfile settings
 * @ap: the ata_port for the drive
 *
 * This applies to both PATA and SATA drives.
 * Has to be called after ata_acpi_push_id() (for SATA devices)
 * or ata_acpi_push_timings() (for PATA devices) as the
 * contents of taskfile registers might be modified by the
 * ACPI code according to the received data.
 */
int ata_acpi_exec_tfs(struct ata_device *atadev)
{
	struct ata_port *ap = atadev->ap;
	int		ret;
	unsigned int	gtf_length;
	unsigned long	gtf_address;
	unsigned long	obj_loc;

	if (!(ata_acpi_flags(atadev,libata_acpi) & 
	      (ATA_ACPI_GTF | ATA_ACPI_TFX))) {
		if (ata_msg_probe(ap))
			ata_dev_printk(atadev, KERN_DEBUG,
				       "%s: disabled\n",
				       __FUNCTION__);
		return 0;
	}

	ret = do_drive_get_GTF(atadev,
			       &gtf_length, &gtf_address, &obj_loc);
	if (ret < 0) {
		if (ata_msg_probe(ap))
			ata_dev_printk(atadev, KERN_DEBUG,
				       "%s: get_GTF error (%d)\n",
				       __FUNCTION__, ret);
		return 0;
	}

	ret = do_drive_set_taskfiles(atadev, gtf_length, gtf_address);
	kfree((void *)obj_loc);
	if (ret < 0) {
		if (ata_msg_probe(ap))
			ata_dev_printk(atadev, KERN_DEBUG,
				       "%s: set_taskfiles error (%d)\n",
				       __FUNCTION__, ret);
		return 0;
	}

	if (ata_msg_probe(ap))
		ata_dev_printk(atadev, KERN_DEBUG,
			       "%s: ret=%d\n", __FUNCTION__, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(ata_acpi_exec_tfs);

/**
 * ata_acpi_get_timing - get the channel (controller) timings
 * @ap: target ata_port (channel)
 *
 * For PATA ACPI, this function executes the _GTM ACPI method for the
 * target channel.
 *
 * _GTM only applies to ATA controllers in PATA (legacy) mode, not to SATA.
 * In legacy mode, ap->port_no is channel (controller) number.
 */
void ata_acpi_get_timing(struct ata_port *ap)
{
	int			err;
	acpi_handle		chan_handle;
	acpi_status		status;
	struct acpi_buffer	output;
	union acpi_object 	*out_obj;
	struct GTM_buffer	*gtm;

	if (!(libata_acpi & ATA_ACPI_PATA_GTM))
		goto out;

	/* TODO: Check for legacy mode */

	err = pata_get_chan_handle(ap, &chan_handle);
	if (err < 0) {
		if (ata_msg_probe(ap))
			ata_port_printk(ap, KERN_DEBUG,
					"%s: pata_get_dev_handle failed (%d)\n",
					__FUNCTION__, err);
		goto out;
	}

	/* Setting up output buffer for _GTM */
	output.length = ACPI_ALLOCATE_BUFFER;
	output.pointer = NULL;	/* ACPI-CA sets this; save/free it later */

	/* _GTM has no input parameters */
	status = acpi_evaluate_object(chan_handle, "_GTM",
					NULL, &output);
	if (ata_msg_probe(ap))
		ata_port_printk(ap, KERN_DEBUG,
				"%s: _GTM status: %d, outptr: 0x%p, outlen: 0x%llx\n",
				__FUNCTION__, status, output.pointer,
				(unsigned long long)output.length);
	if (ACPI_FAILURE(status)) {
		if (ata_msg_probe(ap))
			ata_port_printk(ap, KERN_DEBUG,
					"%s: Run _GTM error: status = 0x%x\n",
					__FUNCTION__, status);
		goto out;
	}

	if (!output.length || !output.pointer) {
		if (ata_msg_probe(ap))
			ata_port_printk(ap, KERN_DEBUG, "%s: Run _GTM: "
					"length or ptr is NULL (0x%llx, 0x%p)\n",
					__FUNCTION__,
					(unsigned long long)output.length,
					output.pointer);
		kfree(output.pointer);
		goto out;
	}

	out_obj = output.pointer;
	if (out_obj->type != ACPI_TYPE_BUFFER) {
		kfree(output.pointer);
		if (ata_msg_probe(ap))
			ata_port_printk(ap, KERN_DEBUG, "%s: Run _GTM: error: "
					"expected object type of ACPI_TYPE_BUFFER, "
					"got 0x%x\n",
					__FUNCTION__, out_obj->type);
		goto out;
	}

	if (!out_obj->buffer.length || !out_obj->buffer.pointer ||
	    out_obj->buffer.length != sizeof(struct GTM_buffer)) {
		kfree(output.pointer);
		if (ata_msg_drv(ap))
			ata_port_printk(ap, KERN_ERR,
					"%s: unexpected _GTM length (0x%x) [should be 0x%zx] or addr (0x%p)\n",
					__FUNCTION__, out_obj->buffer.length,
					sizeof(struct GTM_buffer), out_obj->buffer.pointer);
		goto out;
	}

	gtm = (struct GTM_buffer *)out_obj->buffer.pointer;
	if (ata_msg_probe(ap)) {
		ata_port_printk(ap, KERN_DEBUG,
				"%s: _GTM info: ptr: 0x%p, len: 0x%x, exp.len: 0x%Zx\n",
				__FUNCTION__, out_obj->buffer.pointer,
				out_obj->buffer.length, sizeof(struct GTM_buffer));
		ata_port_printk(ap, KERN_DEBUG,
				"%s: _GTM fields: 0x%x, 0x%x, 0x%x, 0x%x, 0x%x\n",
				__FUNCTION__, gtm->PIO_speed0, gtm->DMA_speed0,
				gtm->PIO_speed1, gtm->DMA_speed1, gtm->GTM_flags);
	}

	/* TBD: when to free gtm */
	ap->gtm = gtm;
	kfree(ap->gtm_object_area); /* free previous then store new one */
	ap->gtm_object_area = out_obj;
out:;
}
EXPORT_SYMBOL_GPL(ata_acpi_get_timing);

/**
 * platform_set_timing - set the channel (controller) timings
 * @ap: target ata_port (channel)
 *
 * For PATA ACPI, this function executes the _STM ACPI method for the
 * target channel.
 *
 * _STM only applies to ATA controllers in PATA (legacy) mode, not to SATA.
 * In legacy mode, ap->port_no is channel (controller) number.
 *
 * _STM requires Identify Drive data, which must already be present in
 * ata_device->id[] (i.e., it's not fetched here).
 */
void ata_acpi_push_timing(struct ata_port *ap)
{
	int			err;
	acpi_handle		chan_handle;
	acpi_status		status;
	struct acpi_object_list	input;
	union acpi_object 	in_params[3];

	if (!(libata_acpi & ATA_ACPI_PATA_GTM))
		goto out;

	if (!ap->gtm) {
		if (ata_msg_probe(ap))
			ata_port_printk(ap, KERN_DEBUG, "%s: no GTM data\n",
					__FUNCTION__);
		goto out;
	}

	/* TODO: Check for legacy mode */

	if (!ap->device[0].id[49] && !ap->device[1].id[49]) {
		if (ata_msg_probe(ap))
			ata_port_printk(ap, KERN_DEBUG,
					"%s: missing Identify data\n",
					__FUNCTION__);
		goto out;
	}

	err = pata_get_chan_handle(ap, &chan_handle);
	if (err < 0) {
		if (ata_msg_probe(ap))
			ata_port_printk(ap, KERN_DEBUG,
					"%s: pata_get_dev_handle failed (%d)\n",
					__FUNCTION__, err);
		goto out;
	}

	/* Give the GTM buffer + drive Identify data to the channel via the
	 * _STM method: */
	/* setup input parameters buffer for _STM */
	input.count = 3;
	input.pointer = in_params;
	in_params[0].type = ACPI_TYPE_BUFFER;
	in_params[0].buffer.length = sizeof(struct GTM_buffer);
	in_params[0].buffer.pointer = (u8 *)ap->gtm;
	in_params[1].type = ACPI_TYPE_BUFFER;
	in_params[1].buffer.length = sizeof(ap->device[0].id[0]) * ATA_ID_WORDS;
	in_params[1].buffer.pointer = (u8 *)ap->device[0].id;
	in_params[2].type = ACPI_TYPE_BUFFER;
	in_params[2].buffer.length = sizeof(ap->device[1].id[1]) * ATA_ID_WORDS;
	in_params[2].buffer.pointer = (u8 *)ap->device[1].id;
	/* Output buffer: _STM has no output */

	swap_buf_le16(ap->device[0].id, ATA_ID_WORDS);
	swap_buf_le16(ap->device[1].id, ATA_ID_WORDS);
	status = acpi_evaluate_object(chan_handle, "_STM", &input, NULL);
	swap_buf_le16(ap->device[0].id, ATA_ID_WORDS);
	swap_buf_le16(ap->device[1].id, ATA_ID_WORDS);
	if (ata_msg_probe(ap)) {
		if (ACPI_FAILURE(status)) {
			ata_port_printk(ap, KERN_DEBUG,
					"%s: _STM error: status = 0x%x\n",
					__FUNCTION__, status);
		} else {
			ata_port_printk(ap, KERN_DEBUG, "%s: _STM success\n",
					__FUNCTION__);
		}
	}
out:;
}
EXPORT_SYMBOL_GPL(ata_acpi_push_timing);
