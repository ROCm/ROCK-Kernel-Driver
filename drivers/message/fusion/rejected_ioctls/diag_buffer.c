/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* REGISTER DIAG BUFFER Routine.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-EBUSY  if previous command timout and IOC reset is not complete.
 *		-ENODEV if no such device/adapter
 *		-ETIME	if timer expires
 *		-ENOMEM if memory allocation error
 */
static int
mptctl_register_diag_buffer (unsigned long arg)
{
	mpt_diag_register_t	__user *uarg = (void __user *) arg;
	mpt_diag_register_t	karg;
	MPT_ADAPTER		*ioc;
	int			iocnum, rc, ii;
	void *			request_data;
	dma_addr_t		request_data_dma;
	u32			request_data_sz;
	MPT_FRAME_HDR		*mf;
	DiagBufferPostRequest_t *diag_buffer_post_request;
	DiagBufferPostReply_t	*diag_buffer_post_reply;
	u32 			tmp;
	u8			buffer_type;
	unsigned long 		timeleft;

	rc = 0;
	if (copy_from_user(&karg, uarg, sizeof(mpt_diag_register_t))) {
		printk(KERN_ERR "%s@%d::%s - "
		"Unable to read in mpt_diag_register_t struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.hdr.iocnum, &ioc)) < 0) ||
		(ioc == NULL)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	dctlprintk(ioc, printk(MYIOC_s_DEBUG_FMT "%s enter.\n", ioc->name,
	       	__FUNCTION__));
	buffer_type = karg.data.BufferType;
	if (!(ioc->facts.IOCCapabilities & MPT_DIAG_CAPABILITY(buffer_type))) {
		printk(MYIOC_s_DEBUG_FMT "%s: doesn't have Capability for "
		    "buffer_type=%x\n", ioc->name, __FUNCTION__, buffer_type);
		return -ENODEV;
	}

	if (ioc->DiagBuffer_Status[buffer_type] &
	    MPT_DIAG_BUFFER_IS_REGISTERED) {
		printk(MYIOC_s_DEBUG_FMT "%s: already has a Registered "
		    "buffer for buffer_type=%x\n", ioc->name, __FUNCTION__,
		    buffer_type);
		return -EFAULT;
	}

	/* Get a free request frame and save the message context.
	 */
	if ((mf = mpt_get_msg_frame(mptctl_id, ioc)) == NULL)
		return -EAGAIN;

	request_data = ioc->DiagBuffer[buffer_type];
	request_data_sz = karg.data.RequestedBufferSize;

	if (request_data) {
		request_data_dma = ioc->DiagBuffer_dma[buffer_type];
		if (request_data_sz != ioc->DiagBuffer_sz[buffer_type]) {
			pci_free_consistent(ioc->pcidev,
			    ioc->DiagBuffer_sz[buffer_type],
			    request_data, request_data_dma);
			request_data = NULL;
		}
	}

	if (request_data == NULL) {
		ioc->DiagBuffer_sz[buffer_type] = 0;
		ioc->DiagBuffer_dma[buffer_type] = 0;
		ioc->DataSize[buffer_type] = 0;
		request_data = pci_alloc_consistent(
			ioc->pcidev, request_data_sz, &request_data_dma);
		if (request_data == NULL) {
			printk(MYIOC_s_DEBUG_FMT "%s: pci_alloc_consistent"
			    " FAILED, (request_sz=%d)\n", ioc->name,
			    __FUNCTION__, request_data_sz);
			mpt_free_msg_frame(ioc, mf);
                	return -EAGAIN;
		}
		ioc->DiagBuffer[buffer_type] = request_data;
		ioc->DiagBuffer_sz[buffer_type] = request_data_sz;
		ioc->DiagBuffer_dma[buffer_type] = request_data_dma;
	}

	ioc->DiagBuffer_Status[buffer_type] = 0;
 	diag_buffer_post_request = (DiagBufferPostRequest_t *)mf;
	diag_buffer_post_request->Function = MPI_FUNCTION_DIAG_BUFFER_POST;
	diag_buffer_post_request->ChainOffset = 0;
	diag_buffer_post_request->BufferType = karg.data.BufferType;
	diag_buffer_post_request->TraceLevel = ioc->TraceLevel[buffer_type] =
	    karg.data.TraceLevel;
	diag_buffer_post_request->MsgFlags = 0;
	diag_buffer_post_request->Reserved1 = 0;
	diag_buffer_post_request->Reserved2 = 0;
	diag_buffer_post_request->Reserved3 = 0;
	diag_buffer_post_request->BufferAddress.High = 0;
	if (buffer_type == MPI_DIAG_BUF_TYPE_EXTENDED)
		ioc->ExtendedType[buffer_type] = karg.data.ExtendedType;
	else
		ioc->ExtendedType[buffer_type] = 0;
	diag_buffer_post_request->ExtendedType =
	    cpu_to_le32(ioc->ExtendedType[buffer_type]);
	ioc->UniqueId[buffer_type] = karg.data.UniqueId;
	diag_buffer_post_request->BufferLength = cpu_to_le32(request_data_sz);
	for (ii = 0; ii < 4; ii++) {
		ioc->ProductSpecific[buffer_type][ii] =
			karg.data.ProductSpecific[ii];
		diag_buffer_post_request->ProductSpecific[ii] =
			cpu_to_le32(ioc->ProductSpecific[buffer_type][ii]);
	}

	tmp = request_data_dma & 0xFFFFFFFF;
	diag_buffer_post_request->BufferAddress.Low = cpu_to_le32(tmp);
	if (ioc->sg_addr_size == sizeof(u64)) {
		tmp = (u32)((u64)request_data_dma >> 32);
		diag_buffer_post_request->BufferAddress.High = cpu_to_le32(tmp);
	}

	SET_MGMT_MSG_CONTEXT(ioc->ioctl_cmds.msg_context,
	    diag_buffer_post_request->MsgContext);
	INITIALIZE_MGMT_STATUS(ioc->ioctl_cmds.status)
	mpt_put_msg_frame(mptctl_id, ioc, mf);
	timeleft = wait_for_completion_timeout(&ioc->ioctl_cmds.done,
	    MPT_IOCTL_DEFAULT_TIMEOUT*HZ);
	if (!(ioc->ioctl_cmds.status & MPT_MGMT_STATUS_COMMAND_GOOD)) {
		rc = -ETIME;
		printk(MYIOC_s_WARN_FMT "%s: failed\n", ioc->name,
		    __FUNCTION__);
		if (ioc->ioctl_cmds.status & MPT_MGMT_STATUS_DID_IOCRESET) {
			mpt_free_msg_frame(ioc, mf);
			goto out;
		}
		if (!timeleft)
			mptctl_timeout_expired(ioc, mf);
		goto out;
	}

	/* process the completed Reply Message Frame */
	if ((ioc->ioctl_cmds.status & MPT_MGMT_STATUS_RF_VALID) == 0) {
		dctlprintk(ioc, printk(MYIOC_s_DEBUG_FMT "%s: status=%x\n",
		    ioc->name, __FUNCTION__, ioc->ioctl_cmds.status));
		rc = -EFAULT;
		goto out;
	}

	diag_buffer_post_reply = (DiagBufferPostReply_t *)ioc->ioctl_cmds.reply;
	if (le16_to_cpu(diag_buffer_post_reply->IOCStatus) ==
	    MPI_IOCSTATUS_SUCCESS) {
		if (diag_buffer_post_reply->MsgLength > 5)
			ioc->DataSize[buffer_type] =
			     le32_to_cpu(diag_buffer_post_reply->TransferLength);
		ioc->DiagBuffer_Status[buffer_type] |=
			MPT_DIAG_BUFFER_IS_REGISTERED;
	} else {
		dctlprintk(ioc, printk(MYIOC_s_DEBUG_FMT "%s: IOCStatus=%x "
		    "IOCLogInfo=%x\n", ioc->name, __FUNCTION__,
		    diag_buffer_post_reply->IOCStatus,
		    diag_buffer_post_reply->IOCLogInfo));
		rc = -EFAULT;
	}

 out:

	CLEAR_MGMT_STATUS(ioc->ioctl_cmds.status)
	SET_MGMT_MSG_CONTEXT(ioc->ioctl_cmds.msg_context, 0);
	if (rc)
		pci_free_consistent(ioc->pcidev, request_data_sz,
		    request_data, request_data_dma);
	return rc;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* RELEASE DIAG BUFFER Routine.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-EBUSY  if previous command timout and IOC reset is not complete.
 *		-ENODEV if no such device/adapter
 *		-ETIME	if timer expires
 *		-ENOMEM if memory allocation error
 */
static int
mptctl_release_diag_buffer (unsigned long arg)
{
	mpt_diag_release_t	__user *uarg = (void __user *) arg;
	mpt_diag_release_t	karg;
	MPT_ADAPTER		*ioc;
	void *			request_data;
	int			iocnum, rc;
	MPT_FRAME_HDR		*mf;
	DiagReleaseRequest_t 	*diag_release;
	DiagReleaseReply_t	*diag_release_reply;
	u8			buffer_type;
	unsigned long 		timeleft;

	rc = 0;
	if (copy_from_user(&karg, uarg, sizeof(mpt_diag_release_t))) {
		printk(KERN_ERR "%s@%d::%s - "
		"Unable to read in mpt_diag_release_t struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.hdr.iocnum, &ioc)) < 0) ||
		(ioc == NULL)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	dctlprintk(ioc, printk(MYIOC_s_DEBUG_FMT "%s enter.\n", ioc->name,
	       	__FUNCTION__));
	buffer_type = karg.data.UniqueId & 0x000000ff;
	if (!(ioc->facts.IOCCapabilities & MPT_DIAG_CAPABILITY(buffer_type))) {
		printk(MYIOC_s_DEBUG_FMT "%s: doesn't have Capability for "
		    "buffer_type=%x\n", ioc->name, __FUNCTION__, buffer_type);
		return -ENODEV;
	}

	if ((ioc->DiagBuffer_Status[buffer_type] &
	       	MPT_DIAG_BUFFER_IS_REGISTERED) == 0 ) {
		printk(MYIOC_s_DEBUG_FMT "%s: buffer_type=%x is not "
		    "registered\n", ioc->name, __FUNCTION__, buffer_type);
		return -EFAULT;
	}

	if (karg.data.UniqueId != ioc->UniqueId[buffer_type]) {
		printk(MYIOC_s_DEBUG_FMT "%s: unique_id=%x is not registered\n",
		    ioc->name, __FUNCTION__, karg.data.UniqueId);
		return -EFAULT;
	}

	if (ioc->DiagBuffer_Status[buffer_type] & MPT_DIAG_BUFFER_IS_RELEASED) {
		dctlprintk(ioc, printk(MYIOC_s_DEBUG_FMT "%s: buffer_type=%x "
		    "is already released\n", ioc->name, __FUNCTION__,
		    buffer_type));
		return rc;
	}

	request_data = ioc->DiagBuffer[buffer_type];

	if (request_data == NULL) {
		printk(MYIOC_s_DEBUG_FMT "%s: doesn't have buffer for "
		    "buffer_type=%x\n", ioc->name, __FUNCTION__, buffer_type);
		return -ENODEV;
	}

	/* Get a free request frame and save the message context.
	 */
	if ((mf = mpt_get_msg_frame(mptctl_id, ioc)) == NULL)
		return -EAGAIN;

	diag_release = (DiagReleaseRequest_t *)mf;
	diag_release->Function = MPI_FUNCTION_DIAG_RELEASE;
	diag_release->BufferType = buffer_type;
	diag_release->ChainOffset = 0;
	diag_release->Reserved1 = 0;
	diag_release->Reserved2 = 0;
	diag_release->Reserved3 = 0;
	diag_release->MsgFlags = 0;

	SET_MGMT_MSG_CONTEXT(ioc->ioctl_cmds.msg_context,
	    diag_release->MsgContext);
	INITIALIZE_MGMT_STATUS(ioc->ioctl_cmds.status)
	mpt_put_msg_frame(mptctl_id, ioc, mf);
	timeleft = wait_for_completion_timeout(&ioc->ioctl_cmds.done,
	    MPT_IOCTL_DEFAULT_TIMEOUT*HZ);
	if (!(ioc->ioctl_cmds.status & MPT_MGMT_STATUS_COMMAND_GOOD)) {
		rc = -ETIME;
		printk(MYIOC_s_WARN_FMT "%s: failed\n", ioc->name,
		    __FUNCTION__);
		if (ioc->ioctl_cmds.status & MPT_MGMT_STATUS_DID_IOCRESET) {
			mpt_free_msg_frame(ioc, mf);
			goto out;
		}
		if (!timeleft)
			mptctl_timeout_expired(ioc, mf);
		goto out;
	}

	/* process the completed Reply Message Frame */
	if ((ioc->ioctl_cmds.status & MPT_MGMT_STATUS_RF_VALID) == 0) {
		dctlprintk(ioc, printk(MYIOC_s_DEBUG_FMT "%s: status=%x\n",
		    ioc->name, __FUNCTION__, ioc->ioctl_cmds.status));
		rc = -EFAULT;
		goto out;
	}

	diag_release_reply = (DiagReleaseReply_t *)ioc->ioctl_cmds.reply;
	if (le16_to_cpu(diag_release_reply->IOCStatus) !=
	    MPI_IOCSTATUS_SUCCESS) {
		dctlprintk(ioc, printk(MYIOC_s_DEBUG_FMT "%s: IOCStatus=%x "
			"IOCLogInfo=%x\n",
		    ioc->name, __FUNCTION__, diag_release_reply->IOCStatus,
		    diag_release_reply->IOCLogInfo));
		rc = -EFAULT;
	} else
		ioc->DiagBuffer_Status[buffer_type] |=
		    MPT_DIAG_BUFFER_IS_RELEASED;

 out:

	CLEAR_MGMT_STATUS(ioc->ioctl_cmds.status)
	SET_MGMT_MSG_CONTEXT(ioc->ioctl_cmds.msg_context, 0);
	return rc;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* UNREGISTER DIAG BUFFER Routine.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-EBUSY  if previous command timout and IOC reset is not complete.
 *		-ENODEV if no such device/adapter
 *		-ETIME	if timer expires
 *		-ENOMEM if memory allocation error
 */
static int
mptctl_unregister_diag_buffer (unsigned long arg)
{
	mpt_diag_unregister_t	__user *uarg = (void __user *) arg;
	mpt_diag_unregister_t	karg;
	MPT_ADAPTER		*ioc;
	int			iocnum;
	void *			request_data;
	dma_addr_t		request_data_dma;
	u32			request_data_sz;
	u8			buffer_type;

	if (copy_from_user(&karg, uarg, sizeof(mpt_diag_unregister_t))) {
		printk(KERN_ERR "%s@%d::%s - "
		"Unable to read in mpt_diag_unregister_t struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.hdr.iocnum, &ioc)) < 0) ||
		(ioc == NULL)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	dctlprintk(ioc, printk(MYIOC_s_DEBUG_FMT "%s enter.\n", ioc->name,
		__FUNCTION__));
	buffer_type = karg.data.UniqueId & 0x000000ff;
	if (!(ioc->facts.IOCCapabilities & MPT_DIAG_CAPABILITY(buffer_type))) {
		printk(MYIOC_s_DEBUG_FMT "%s: doesn't have Capability for "
		    "buffer_type=%x\n", ioc->name, __FUNCTION__, buffer_type);
		return -ENODEV;
	}

	if ((ioc->DiagBuffer_Status[buffer_type] &
		MPT_DIAG_BUFFER_IS_REGISTERED) == 0) {
		printk(MYIOC_s_DEBUG_FMT "%s: buffer_type=%x is not "
		    "registered\n", ioc->name, __FUNCTION__, buffer_type);
		return -EFAULT;
	}
	if ((ioc->DiagBuffer_Status[buffer_type] &
		MPT_DIAG_BUFFER_IS_RELEASED) == 0) {
		printk(MYIOC_s_DEBUG_FMT "%s: buffer_type=%x has not been "
		    "released\n", ioc->name, __FUNCTION__, buffer_type);
		return -EFAULT;
	}

	if (karg.data.UniqueId != ioc->UniqueId[buffer_type]) {
		printk(MYIOC_s_DEBUG_FMT "%s: unique_id=%x is not registered\n",
		    ioc->name, __FUNCTION__, karg.data.UniqueId);
		return -EFAULT;
	}

	request_data = ioc->DiagBuffer[buffer_type];
	if (!request_data) {
		printk(MYIOC_s_DEBUG_FMT "%s: doesn't have buffer for "
		    "buffer_type=%x\n", ioc->name, __FUNCTION__, buffer_type);
		return -ENODEV;
	}

	request_data_sz = ioc->DiagBuffer_sz[buffer_type];
	request_data_dma = ioc->DiagBuffer_dma[buffer_type];
	pci_free_consistent(ioc->pcidev, request_data_sz,
	    request_data, request_data_dma);
	ioc->DiagBuffer[buffer_type] = NULL;
	ioc->DiagBuffer_Status[buffer_type] = 0;
	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* QUERY DIAG BUFFER Routine.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-EBUSY  if previous command timout and IOC reset is not complete.
 *		-ENODEV if no such device/adapter
 *		-ETIME	if timer expires
 *		-ENOMEM if memory allocation error
 */
static int
mptctl_query_diag_buffer (unsigned long arg)
{
	mpt_diag_query_t	__user *uarg = (void __user *)arg;
	mpt_diag_query_t	karg;
	MPT_ADAPTER		*ioc;
	void *			request_data;
	int			iocnum, ii, rc;
	u8			buffer_type;

	rc = -EFAULT;
	if (copy_from_user(&karg, uarg, sizeof(mpt_diag_query_t))) {
		printk(KERN_ERR "%s@%d::%s - "
		"Unable to read in mpt_diag_query_t struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	karg.data.Flags = 0;
	if (((iocnum = mpt_verify_adapter(karg.hdr.iocnum, &ioc)) < 0) ||
		(ioc == NULL)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		goto out;
	}

	dctlprintk(ioc, printk(MYIOC_s_DEBUG_FMT "%s enter.\n", ioc->name,
	       	__FUNCTION__));
	buffer_type = karg.data.BufferType;
	if (!(ioc->facts.IOCCapabilities & MPT_DIAG_CAPABILITY(buffer_type))) {
		printk(MYIOC_s_DEBUG_FMT "%s: doesn't have Capability for "
		    "buffer_type=%x\n", ioc->name, __FUNCTION__, buffer_type);
		goto out;
	}

	if ((ioc->DiagBuffer_Status[buffer_type] &
	       	MPT_DIAG_BUFFER_IS_REGISTERED) == 0) {
		printk(MYIOC_s_DEBUG_FMT "%s: buffer_type=%x is not "
		    "registered\n", ioc->name, __FUNCTION__, buffer_type);
		goto out;
	}

	if (karg.data.UniqueId & 0xffffff00) {
		if (karg.data.UniqueId != ioc->UniqueId[buffer_type]) {
			printk(MYIOC_s_DEBUG_FMT "%s: unique_id=%x is not "
			    "registered\n", ioc->name, __FUNCTION__,
			    karg.data.UniqueId);
			goto out;
		}
	}

	request_data = ioc->DiagBuffer[buffer_type];
	if (!request_data) {
		printk(MYIOC_s_DEBUG_FMT "%s: doesn't have buffer for "
		    "buffer_type=%x\n", ioc->name, __FUNCTION__, buffer_type);
		goto out;
	}
	
	rc = 0;
	if (buffer_type == MPI_DIAG_BUF_TYPE_EXTENDED) {
		if (karg.data.ExtendedType != ioc->ExtendedType[buffer_type])
			goto out;
	} else
		karg.data.ExtendedType = 0;

	if (ioc->DiagBuffer_Status[buffer_type] & MPT_DIAG_BUFFER_IS_RELEASED)
		karg.data.Flags = 3;
	else
		karg.data.Flags = 7;
	karg.data.TraceLevel = ioc->TraceLevel[buffer_type];
	for (ii = 0; ii < 4; ii++)
		karg.data.ProductSpecific[ii] =
		    ioc->ProductSpecific[buffer_type][ii];
	karg.data.DataSize = ioc->DiagBuffer_sz[buffer_type];
	karg.data.DriverAddedBufferSize = 0;
	karg.data.UniqueId = ioc->UniqueId[buffer_type];

 out:
	if (copy_to_user(uarg, &karg, sizeof(mpt_diag_query_t))) {
		printk(MYIOC_s_ERR_FMT "%s Unable to write mpt_diag_query_t "
		    "data @ %p\n", ioc->name, __FUNCTION__, uarg);
		return -EFAULT;
	}
	return rc;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* READ DIAG BUFFER Routine.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-EBUSY  if previous command timout and IOC reset is not complete.
 *		-ENODEV if no such device/adapter
 *		-ETIME	if timer expires
 *		-ENOMEM if memory allocation error
 */
static int
mptctl_read_diag_buffer (unsigned long arg)
{
	mpt_diag_read_buffer_t	__user *uarg = (void __user *) arg;
	mpt_diag_read_buffer_t	karg;
	MPT_ADAPTER		*ioc;
	void			*request_data, *diagData;
	dma_addr_t		request_data_dma;
	DiagBufferPostRequest_t *diag_buffer_post_request;
	DiagBufferPostReply_t	*diag_buffer_post_reply;
	MPT_FRAME_HDR		*mf;
	int			iocnum, rc, ii;
	u8			buffer_type;
	u32 			tmp;
	unsigned long 		timeleft;

	rc = 0;
	if (copy_from_user(&karg, uarg, sizeof(mpt_diag_read_buffer_t))) {
		printk(KERN_ERR "%s@%d::%s - "
		"Unable to read in mpt_diag_read_buffer_t struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.hdr.iocnum, &ioc)) < 0) ||
		(ioc == NULL)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	dctlprintk(ioc, printk(MYIOC_s_DEBUG_FMT "%s enter.\n", ioc->name,
	    __FUNCTION__));
	buffer_type = karg.data.UniqueId & 0x000000ff;
	if (!(ioc->facts.IOCCapabilities & MPT_DIAG_CAPABILITY(buffer_type))) {
		printk(MYIOC_s_DEBUG_FMT "%s: doesn't have Capability "
		    "for buffer_type=%x\n", ioc->name, __FUNCTION__,
		    buffer_type);
		return -EFAULT;
	}

	if (karg.data.UniqueId != ioc->UniqueId[buffer_type]) {
		printk(MYIOC_s_DEBUG_FMT "%s: unique_id=%x is not registered\n",
		    ioc->name, __FUNCTION__, karg.data.UniqueId);
		return -EFAULT;
	}

	request_data = ioc->DiagBuffer[buffer_type];
	if (!request_data) {
		printk(MYIOC_s_DEBUG_FMT "%s: doesn't have buffer for "
		    "buffer_type=%x\n", ioc->name, __FUNCTION__, buffer_type);
		return -EFAULT;
	}

	diagData = (void *)(request_data + karg.data.StartingOffset);
	dctlprintk(ioc, printk(MYIOC_s_DEBUG_FMT "%s: diagData=%p "
	    "request_data=%p StartingOffset=%x\n", ioc->name, __FUNCTION__,
	    diagData, request_data, karg.data.StartingOffset));

	if (copy_to_user((void __user *)&uarg->data.DiagnosticData[0],
	    diagData, karg.data.BytesToRead)) {
		printk(MYIOC_s_ERR_FMT "%s: Unable to write "
		    "mpt_diag_read_buffer_t data @ %p\n", ioc->name,
		    __FUNCTION__, diagData);
		return -EFAULT;
	}

	if ((karg.data.Flags & MPI_FW_DIAG_FLAG_REREGISTER) == 0)
		goto out;

	dctlprintk(ioc, printk(MYIOC_s_DEBUG_FMT "%s: Reregister "
		"buffer_type=%x\n", ioc->name, __FUNCTION__, buffer_type));
	if ((ioc->DiagBuffer_Status[buffer_type] &
	    MPT_DIAG_BUFFER_IS_RELEASED) == 0) {
		dctlprintk(ioc, printk(MYIOC_s_DEBUG_FMT "%s: buffer_type=%x "
		    "is still registered\n", ioc->name, __FUNCTION__,
		    buffer_type));
		return rc;
	}
	/* Get a free request frame and save the message context.
	*/
	if ((mf = mpt_get_msg_frame(mptctl_id, ioc)) == NULL)
		return -EAGAIN;

	diag_buffer_post_request = (DiagBufferPostRequest_t *)mf;
	diag_buffer_post_request->Function = MPI_FUNCTION_DIAG_BUFFER_POST;
	diag_buffer_post_request->ChainOffset = 0;
	diag_buffer_post_request->BufferType = buffer_type;
	diag_buffer_post_request->TraceLevel =
		ioc->TraceLevel[buffer_type];
	diag_buffer_post_request->MsgFlags = 0;
	diag_buffer_post_request->Reserved1 = 0;
	diag_buffer_post_request->Reserved2 = 0;
	diag_buffer_post_request->Reserved3 = 0;
	diag_buffer_post_request->BufferAddress.High = 0;
	if ( buffer_type == MPI_DIAG_BUF_TYPE_EXTENDED )
		diag_buffer_post_request->ExtendedType =
			cpu_to_le32(ioc->ExtendedType[buffer_type]);
	diag_buffer_post_request->BufferLength =
	    cpu_to_le32(ioc->DiagBuffer_sz[buffer_type]);
	for (ii = 0; ii < 4; ii++)
		diag_buffer_post_request->ProductSpecific[ii] =
		    cpu_to_le32(ioc->ProductSpecific[buffer_type][ii]);
	request_data_dma = ioc->DiagBuffer_dma[buffer_type];
	tmp = request_data_dma & 0xFFFFFFFF;
	diag_buffer_post_request->BufferAddress.Low = cpu_to_le32(tmp);
	if (ioc->sg_addr_size == sizeof(u64)) {
		tmp = (u32)((u64)request_data_dma >> 32);
		diag_buffer_post_request->BufferAddress.High = cpu_to_le32(tmp);
	}

	SET_MGMT_MSG_CONTEXT(ioc->ioctl_cmds.msg_context,
	    diag_buffer_post_request->MsgContext);
	INITIALIZE_MGMT_STATUS(ioc->ioctl_cmds.status)
	mpt_put_msg_frame(mptctl_id, ioc, mf);
	timeleft = wait_for_completion_timeout(&ioc->ioctl_cmds.done,
	    MPT_IOCTL_DEFAULT_TIMEOUT*HZ);
	if (!(ioc->ioctl_cmds.status & MPT_MGMT_STATUS_COMMAND_GOOD)) {
		rc = -ETIME;
		printk(MYIOC_s_WARN_FMT "%s: failed\n", ioc->name,
		    __FUNCTION__);
		if (ioc->ioctl_cmds.status & MPT_MGMT_STATUS_DID_IOCRESET) {
			mpt_free_msg_frame(ioc, mf);
			goto out;
		}
		if (!timeleft)
			mptctl_timeout_expired(ioc, mf);
		goto out;
	}

	/* process the completed Reply Message Frame */
	if ((ioc->ioctl_cmds.status & MPT_MGMT_STATUS_RF_VALID) == 0) {
		dctlprintk(ioc, printk(MYIOC_s_DEBUG_FMT "%s: status=%x\n",
		    ioc->name, __FUNCTION__, ioc->ioctl_cmds.status));
		rc = -EFAULT;
	}

	diag_buffer_post_reply = (DiagBufferPostReply_t *)ioc->ioctl_cmds.reply;
	if (le16_to_cpu(diag_buffer_post_reply->IOCStatus) ==
	    MPI_IOCSTATUS_SUCCESS) {
		if (diag_buffer_post_reply->MsgLength > 5)
			ioc->DataSize[buffer_type] =
			    le32_to_cpu(diag_buffer_post_reply->TransferLength);
		ioc->DiagBuffer_Status[buffer_type] |=
		    MPT_DIAG_BUFFER_IS_REGISTERED;
	} else {
		dctlprintk(ioc, printk(MYIOC_s_DEBUG_FMT "%s: IOCStatus=%x "
		    "IOCLogInfo=%x\n", ioc->name, __FUNCTION__,
		    diag_buffer_post_reply->IOCStatus,
		    diag_buffer_post_reply->IOCLogInfo));
		rc = -EFAULT;
	}

 out:
	CLEAR_MGMT_STATUS(ioc->ioctl_cmds.status)
	SET_MGMT_MSG_CONTEXT(ioc->ioctl_cmds.msg_context, 0);
	return rc;
}
