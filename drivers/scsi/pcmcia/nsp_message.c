/*==========================================================================
  NinjaSCSI-3 message handler
      By: YOKOTA Hiroshi <yokota@netlab.is.tsukuba.ac.jp>

   This software may be used and distributed according to the terms of
   the GNU General Public License.
 */

/* $Id: nsp_message.c,v 1.7 2001/09/07 04:33:01 elca Exp $ */

static void nsp_message_in(Scsi_Cmnd *SCpnt, nsp_hw_data *data)
{
	unsigned int  base = SCpnt->host->io_port;
	unsigned char data_reg, control_reg;
	int           ret, len;

	/*
	 * XXX: NSP QUIRK
	 * NSP invoke interrupts only in the case of scsi phase changes,
	 * therefore we should poll the scsi phase here to catch 
	 * the next "msg in" if exists (no scsi phase changes).
	 */
	ret = 16;
	len = 0;

	DEBUG(0, " msgin loop\n");
	do {
		/* read data */
		data_reg = nsp_index_read(base, SCSIDATAIN);

		/* assert ACK */
		control_reg = nsp_index_read(base, SCSIBUSCTRL);
		control_reg |= SCSI_ACK;
		nsp_index_write(base, SCSIBUSCTRL, control_reg);
		nsp_negate_signal(SCpnt, BUSMON_REQ, "msgin<REQ>");

		data->MsgBuffer[len] = data_reg; len++;

		/* deassert ACK */
		control_reg =  nsp_index_read(base, SCSIBUSCTRL);
		control_reg &= ~SCSI_ACK;
		nsp_index_write(base, SCSIBUSCTRL, control_reg);

		/* catch a next signal */
		ret = nsp_expect_signal(SCpnt, BUSPHASE_MESSAGE_IN, BUSMON_REQ);
	} while (ret > 0 && MSGBUF_SIZE > len);

	data->MsgLen = len;

}

static void nsp_message_out(Scsi_Cmnd *SCpnt, nsp_hw_data *data)
{
	int ret = 1;
	int len = data->MsgLen;

	/*
	 * XXX: NSP QUIRK
	 * NSP invoke interrupts only in the case of scsi phase changes,
	 * therefore we should poll the scsi phase here to catch 
	 * the next "msg out" if exists (no scsi phase changes).
	 */

	DEBUG(0, " msgout loop\n");
	do {
		if (nsp_xfer(SCpnt, data, BUSPHASE_MESSAGE_OUT)) {
			printk(KERN_DEBUG " " __FUNCTION__ " msgout: xfer short\n");
		}

		/* catch a next signal */
		ret = nsp_expect_signal(SCpnt, BUSPHASE_MESSAGE_OUT, BUSMON_REQ);
	} while (ret > 0 && len-- > 0);

}

/* end */
