/*
 * linux/drivers/s390/scsi/zfcp_qdio.c
 *
 * FCP adapter driver for IBM eServer zSeries
 *
 * QDIO related routines
 *
 * Copyright (C) 2003 IBM Entwicklung GmbH, IBM Corporation
 * Authors:
 *      Martin Peschke <mpeschke@de.ibm.com>
 *      Raimund Schroeder <raimund.schroeder@de.ibm.com>
 *      Wolfgang Taphorn <taphorn@de.ibm.com>
 *      Heiko Carstens <heiko.carstens@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define ZFCP_QDIO_C_REVISION "$Revision: 1.7 $"

#include "zfcp_ext.h"

static qdio_handler_t zfcp_qdio_request_handler;
static qdio_handler_t zfcp_qdio_response_handler;
static int zfcp_qdio_handler_error_check(struct zfcp_adapter *,
					 unsigned int,
					 unsigned int, unsigned int);

#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_QDIO
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_QDIO

/*
 * Allocates BUFFER memory to each of the pointers of the qdio_buffer_t 
 * array in the adapter struct.
 * Cur_buf is the pointer array and count can be any number of required 
 * buffers, the page-fitting arithmetic is done entirely within this funciton.
 *
 * returns:	number of buffers allocated
 * locks:       must only be called with zfcp_data.config_sema taken
 */
static int
zfcp_qdio_buffers_enqueue(struct qdio_buffer **cur_buf, int count)
{
	int buf_pos;
	int qdio_buffers_per_page;
	int page_pos = 0;
	struct qdio_buffer *first_in_page = NULL;

	qdio_buffers_per_page = PAGE_SIZE / sizeof (struct qdio_buffer);
	ZFCP_LOG_TRACE("Buffers per page %d.\n", qdio_buffers_per_page);

	for (buf_pos = 0; buf_pos < count; buf_pos++) {
		if (page_pos == 0) {
			cur_buf[buf_pos] = (struct qdio_buffer *)
			    get_zeroed_page(GFP_KERNEL);
			if (cur_buf[buf_pos] == NULL) {
				ZFCP_LOG_INFO("error: Could not allocate "
					      "memory for qdio transfer "
					      "structures.\n");
				goto out;
			}
			first_in_page = cur_buf[buf_pos];
		} else {
			cur_buf[buf_pos] = first_in_page + page_pos;

		}
		/* was initialised to zero */
		page_pos++;
		page_pos %= qdio_buffers_per_page;
	}
 out:
	return buf_pos;
}

/*
 * Frees BUFFER memory for each of the pointers of the struct qdio_buffer array
 * in the adapter struct cur_buf is the pointer array and count can be any
 * number of buffers in the array that should be freed starting from buffer 0
 *
 * locks:       must only be called with zfcp_data.config_sema taken
 */
static void
zfcp_qdio_buffers_dequeue(struct qdio_buffer **cur_buf, int count)
{
	int buf_pos;
	int qdio_buffers_per_page;

	qdio_buffers_per_page = PAGE_SIZE / sizeof (struct qdio_buffer);
	ZFCP_LOG_TRACE("Buffers per page %d.\n", qdio_buffers_per_page);

	for (buf_pos = 0; buf_pos < count; buf_pos += qdio_buffers_per_page)
		free_page((unsigned long) cur_buf[buf_pos]);
	return;
}

/* locks:       must only be called with zfcp_data.config_sema taken */
int
zfcp_qdio_allocate_queues(struct zfcp_adapter *adapter)
{
	int buffer_count;
	int retval = 0;

	buffer_count =
	    zfcp_qdio_buffers_enqueue(&(adapter->request_queue.buffer[0]),
				      QDIO_MAX_BUFFERS_PER_Q);
	if (buffer_count < QDIO_MAX_BUFFERS_PER_Q) {
		ZFCP_LOG_DEBUG("error: Out of memory allocating "
			       "request queue, only %d buffers got. "
			       "Binning them.\n", buffer_count);
		zfcp_qdio_buffers_dequeue(&(adapter->request_queue.buffer[0]),
					  buffer_count);
		retval = -ENOMEM;
		goto out;
	}

	buffer_count =
	    zfcp_qdio_buffers_enqueue(&(adapter->response_queue.buffer[0]),
				      QDIO_MAX_BUFFERS_PER_Q);
	if (buffer_count < QDIO_MAX_BUFFERS_PER_Q) {
		ZFCP_LOG_DEBUG("error: Out of memory allocating "
			       "response queue, only %d buffers got. "
			       "Binning them.\n", buffer_count);
		zfcp_qdio_buffers_dequeue(&(adapter->response_queue.buffer[0]),
					  buffer_count);
		ZFCP_LOG_TRACE("Deallocating request_queue Buffers.\n");
		zfcp_qdio_buffers_dequeue(&(adapter->request_queue.buffer[0]),
					  QDIO_MAX_BUFFERS_PER_Q);
		retval = -ENOMEM;
		goto out;
	}
 out:
	return retval;
}

/* locks:       must only be called with zfcp_data.config_sema taken */
void
zfcp_qdio_free_queues(struct zfcp_adapter *adapter)
{
	ZFCP_LOG_TRACE("Deallocating request_queue Buffers.\n");
	zfcp_qdio_buffers_dequeue(&(adapter->request_queue.buffer[0]),
				  QDIO_MAX_BUFFERS_PER_Q);

	ZFCP_LOG_TRACE("Deallocating response_queue Buffers.\n");
	zfcp_qdio_buffers_dequeue(&(adapter->response_queue.buffer[0]),
				  QDIO_MAX_BUFFERS_PER_Q);
}

int
zfcp_qdio_allocate(struct zfcp_adapter *adapter)
{
	struct qdio_initialize *init_data;

	init_data = &adapter->qdio_init_data;

	init_data->cdev = adapter->ccw_device;
	init_data->q_format = QDIO_SCSI_QFMT;
	memcpy(init_data->adapter_name, &adapter->name, 8);
	init_data->qib_param_field_format = 0;
	init_data->qib_param_field = NULL;
	init_data->input_slib_elements = NULL;
	init_data->output_slib_elements = NULL;
	init_data->min_input_threshold = ZFCP_MIN_INPUT_THRESHOLD;
	init_data->max_input_threshold = ZFCP_MAX_INPUT_THRESHOLD;
	init_data->min_output_threshold = ZFCP_MIN_OUTPUT_THRESHOLD;
	init_data->max_output_threshold = ZFCP_MAX_OUTPUT_THRESHOLD;
	init_data->no_input_qs = 1;
	init_data->no_output_qs = 1;
	init_data->input_handler = zfcp_qdio_response_handler;
	init_data->output_handler = zfcp_qdio_request_handler;
	init_data->int_parm = (unsigned long) adapter;
	init_data->flags = QDIO_INBOUND_0COPY_SBALS |
	    QDIO_OUTBOUND_0COPY_SBALS | QDIO_USE_OUTBOUND_PCIS;
	init_data->input_sbal_addr_array =
	    (void **) (adapter->response_queue.buffer);
	init_data->output_sbal_addr_array =
	    (void **) (adapter->request_queue.buffer);

	return qdio_allocate(init_data);
}

/*
 * function:   	zfcp_qdio_handler_error_check
 *
 * purpose:     called by the response handler to determine error condition
 *
 * returns:	error flag
 *
 */
static inline int
zfcp_qdio_handler_error_check(struct zfcp_adapter *adapter,
			      unsigned int status,
			      unsigned int qdio_error, unsigned int siga_error)
{
	int retval = 0;

	if (ZFCP_LOG_CHECK(ZFCP_LOG_LEVEL_TRACE)) {
		if (status & QDIO_STATUS_INBOUND_INT) {
			ZFCP_LOG_TRACE("status is"
				       " QDIO_STATUS_INBOUND_INT \n");
		}
		if (status & QDIO_STATUS_OUTBOUND_INT) {
			ZFCP_LOG_TRACE("status is"
				       " QDIO_STATUS_OUTBOUND_INT \n");
		}
	}			// if (ZFCP_LOG_CHECK(ZFCP_LOG_LEVEL_TRACE))
	if (status & QDIO_STATUS_LOOK_FOR_ERROR) {
		retval = -EIO;

		ZFCP_LOG_FLAGS(1, "QDIO_STATUS_LOOK_FOR_ERROR \n");

		ZFCP_LOG_INFO("A qdio problem occured. The status, qdio_error "
			      "and siga_error are 0x%x, 0x%x and 0x%x\n",
			      status, qdio_error, siga_error);

		if (status & QDIO_STATUS_ACTIVATE_CHECK_CONDITION) {
			ZFCP_LOG_FLAGS(2,
				       "QDIO_STATUS_ACTIVATE_CHECK_CONDITION\n");
		}
		if (status & QDIO_STATUS_MORE_THAN_ONE_QDIO_ERROR) {
			ZFCP_LOG_FLAGS(2,
				       "QDIO_STATUS_MORE_THAN_ONE_QDIO_ERROR\n");
		}
		if (status & QDIO_STATUS_MORE_THAN_ONE_SIGA_ERROR) {
			ZFCP_LOG_FLAGS(2,
				       "QDIO_STATUS_MORE_THAN_ONE_SIGA_ERROR\n");
		}

		if (siga_error & QDIO_SIGA_ERROR_ACCESS_EXCEPTION) {
			ZFCP_LOG_FLAGS(2, "QDIO_SIGA_ERROR_ACCESS_EXCEPTION\n");
		}

		if (siga_error & QDIO_SIGA_ERROR_B_BIT_SET) {
			ZFCP_LOG_FLAGS(2, "QDIO_SIGA_ERROR_B_BIT_SET\n");
		}

		switch (qdio_error) {
		case 0:
			ZFCP_LOG_FLAGS(3, "QDIO_OK");
			break;
		case SLSB_P_INPUT_ERROR:
			ZFCP_LOG_FLAGS(1, "SLSB_P_INPUT_ERROR\n");
			break;
		case SLSB_P_OUTPUT_ERROR:
			ZFCP_LOG_FLAGS(1, "SLSB_P_OUTPUT_ERROR\n");
			break;
		default:
			ZFCP_LOG_NORMAL("bug: Unknown qdio error reported "
					"(debug info 0x%x)\n", qdio_error);
			break;
		}
		/* Restarting IO on the failed adapter from scratch */
		debug_text_event(adapter->erp_dbf, 1, "qdio_err");
		zfcp_erp_adapter_reopen(adapter, 0);
	}
	return retval;
}

/*
 * function:    zfcp_qdio_request_handler
 *
 * purpose:	is called by QDIO layer for completed SBALs in request queue
 *
 * returns:	(void)
 */
static void
zfcp_qdio_request_handler(struct ccw_device *ccw_device,
			  unsigned int status,
			  unsigned int qdio_error,
			  unsigned int siga_error,
			  unsigned int queue_number,
			  int first_element,
			  int elements_processed,
			  unsigned long int_parm)
{
	struct zfcp_adapter *adapter;
	struct zfcp_qdio_queue *queue;

	adapter = (struct zfcp_adapter *) int_parm;
	queue = &adapter->request_queue;

	ZFCP_LOG_DEBUG("busid=%s, first=%d, count=%d\n",
		       zfcp_get_busid_by_adapter(adapter),
		       first_element, elements_processed);

	if (zfcp_qdio_handler_error_check(adapter, status, qdio_error,
					  siga_error))
		goto out;
	/*
	 * we stored address of struct zfcp_adapter  data structure
	 * associated with irq in int_parm
	 */

	/* cleanup all SBALs being program-owned now */
	zfcp_qdio_zero_sbals(queue->buffer, first_element, elements_processed);

	/* increase free space in outbound queue */
	atomic_add(elements_processed, &queue->free_count);
	ZFCP_LOG_DEBUG("free_count=%d\n", atomic_read(&queue->free_count));
	wake_up(&adapter->request_wq);
	ZFCP_LOG_DEBUG("Elements_processed = %d, free count=%d \n",
		       elements_processed, atomic_read(&queue->free_count));
 out:
	return;
}

/*
 * function:   	zfcp_qdio_response_handler
 *
 * purpose:	is called by QDIO layer for completed SBALs in response queue
 *
 * returns:	(void)
 */
static void
zfcp_qdio_response_handler(struct ccw_device *ccw_device,
			   unsigned int status,
			   unsigned int qdio_error,
			   unsigned int siga_error,
			   unsigned int queue_number,
			   int first_element,
			   int elements_processed,
			   unsigned long int_parm)
{
	struct zfcp_adapter *adapter;
	struct zfcp_qdio_queue *queue;
	int buffer_index;
	int i;
	struct qdio_buffer *buffer;
	int retval = 0;
	u8 count;
	u8 start;
	volatile struct qdio_buffer_element *buffere = NULL;
	int buffere_index;

	adapter = (struct zfcp_adapter *) int_parm;
	queue = &adapter->response_queue;

	if (zfcp_qdio_handler_error_check(adapter, status, qdio_error,
					  siga_error))
		goto out;

	/*
	 * we stored address of struct zfcp_adapter  data structure
	 * associated with irq in int_parm
	 */

	buffere = &(queue->buffer[first_element]->element[0]);
	ZFCP_LOG_DEBUG("first BUFFERE flags=0x%x \n ", buffere->flags);
	/*
	 * go through all SBALs from input queue currently
	 * returned by QDIO layer
	 */

	for (i = 0; i < elements_processed; i++) {

		buffer_index = first_element + i;
		buffer_index %= QDIO_MAX_BUFFERS_PER_Q;
		buffer = queue->buffer[buffer_index];

		/* go through all SBALEs of SBAL */
		for (buffere_index = 0;
		     buffere_index < QDIO_MAX_ELEMENTS_PER_BUFFER;
		     buffere_index++) {

			/* look for QDIO request identifiers in SB */
			buffere = &buffer->element[buffere_index];
			retval = zfcp_qdio_reqid_check(adapter,
						       (void *) buffere->addr);

			if (retval) {
				ZFCP_LOG_NORMAL
				    ("bug: Inbound packet seems not to "
				     "have been sent at all. It will be "
				     "ignored. (debug info 0x%lx, 0x%lx, "
				     "%d, %d, %s)\n",
				     (unsigned long) buffere->addr,
				     (unsigned long) &(buffere->addr),
				     first_element, elements_processed,
				     zfcp_get_busid_by_adapter(adapter));
				ZFCP_LOG_NORMAL("Dump of inbound BUFFER %d "
						"BUFFERE %d at address 0x%lx\n",
						buffer_index, buffere_index,
						(unsigned long) buffer);
				ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_NORMAL,
					      (char *) buffer, SBAL_SIZE);
			}
			if (buffere->flags & SBAL_FLAGS_LAST_ENTRY)
				break;
		};

		if (!buffere->flags & SBAL_FLAGS_LAST_ENTRY) {
			ZFCP_LOG_NORMAL("bug: End of inbound data "
					"not marked!\n");
		}
	}

	/*
	 * put range of SBALs back to response queue
	 * (including SBALs which have already been free before)
	 */
	count = atomic_read(&queue->free_count) + elements_processed;
	start = queue->free_index;

	ZFCP_LOG_TRACE("Calling do QDIO busid=%s, flags=0x%x, queue_no=%i, "
		       "index_in_queue=%i, count=%i, buffers=0x%lx\n",
		       zfcp_get_busid_by_adapter(adapter),
		       QDIO_FLAG_SYNC_INPUT | QDIO_FLAG_UNDER_INTERRUPT,
		       0, start, count, (unsigned long) &queue->buffer[start]);

	retval = do_QDIO(ccw_device,
			 QDIO_FLAG_SYNC_INPUT | QDIO_FLAG_UNDER_INTERRUPT,
			 0, start, count, NULL);

	if (retval) {
		atomic_set(&queue->free_count, count);
		ZFCP_LOG_DEBUG("Inbound data regions could not be cleared "
			       "Transfer queues may be down. "
			       "(info %d, %d, %d)\n", count, start, retval);
	} else {
		queue->free_index += count;
		queue->free_index %= QDIO_MAX_BUFFERS_PER_Q;
		atomic_set(&queue->free_count, 0);
		ZFCP_LOG_TRACE("%i buffers successfully enqueued to response "
			       "queue starting at position %i\n", count, start);
	}
 out:
	return;
}

/*
 * function:	zfcp_qdio_reqid_check
 *
 * purpose:	checks for valid reqids or unsolicited status
 *
 * returns:	0 - valid request id or unsolicited status
 *		!0 - otherwise
 */
int
zfcp_qdio_reqid_check(struct zfcp_adapter *adapter, void *sbale_addr)
{
	struct zfcp_fsf_req *fsf_req;
	int retval = 0;

#ifdef ZFCP_DEBUG_REQUESTS
	/* Note: seq is entered later */
	debug_text_event(adapter->req_dbf, 1, "i:a/seq");
	debug_event(adapter->req_dbf, 1, &sbale_addr, sizeof (unsigned long));
#endif				/* ZFCP_DEBUG_REQUESTS */

	/* invalid (per convention used in this driver) */
	if (!sbale_addr) {
		ZFCP_LOG_NORMAL
		    ("bug: Inbound data faulty, contains null-pointer!\n");
		retval = -EINVAL;
		goto out;
	}

	/* valid request id and thus (hopefully :) valid fsf_req address */
	fsf_req = (struct zfcp_fsf_req *) sbale_addr;

	if ((fsf_req->common_magic != ZFCP_MAGIC) ||
	    (fsf_req->specific_magic != ZFCP_MAGIC_FSFREQ)) {
		ZFCP_LOG_NORMAL("bug: An inbound FSF acknowledgement was "
				"faulty (debug info 0x%x, 0x%x, 0x%lx)\n",
				fsf_req->common_magic,
				fsf_req->specific_magic,
				(unsigned long) fsf_req);
		retval = -EINVAL;
		goto out;
	}

	if (adapter != fsf_req->adapter) {
		ZFCP_LOG_NORMAL("bug: An inbound FSF acknowledgement was not "
				"correct (debug info 0x%lx, 0x%lx, 0%lx) \n",
				(unsigned long) fsf_req,
				(unsigned long) fsf_req->adapter,
				(unsigned long) adapter);
		retval = -EINVAL;
		goto out;
	}
#ifdef ZFCP_DEBUG_REQUESTS
	/* debug feature stuff (test for QTCB: remember new unsol. status!) */
	if (fsf_req->qtcb) {
		debug_event(adapter->req_dbf, 1,
			    &fsf_req->qtcb->prefix.req_seq_no, sizeof (u32));
	}
#endif				/* ZFCP_DEBUG_REQUESTS */

	ZFCP_LOG_TRACE("fsf_req at 0x%lx, QTCB at 0x%lx\n",
		       (unsigned long) fsf_req, (unsigned long) fsf_req->qtcb);
	if (fsf_req->qtcb) {
		ZFCP_LOG_TRACE("HEX DUMP OF 1ST BUFFERE PAYLOAD (QTCB):\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_TRACE,
			      (char *) fsf_req->qtcb, ZFCP_QTCB_SIZE);
	}

	/* finish the FSF request */
	zfcp_fsf_req_complete(fsf_req);
 out:
	return retval;
}

int
zfcp_qdio_determine_pci(struct zfcp_qdio_queue *req_queue,
			struct zfcp_fsf_req *fsf_req)
{
	int new_distance_from_int;
	int pci_pos;

	new_distance_from_int = req_queue->distance_from_int +
	    fsf_req->sbal_count;
	if (new_distance_from_int >= ZFCP_QDIO_PCI_INTERVAL) {
		new_distance_from_int %= ZFCP_QDIO_PCI_INTERVAL;
		pci_pos = fsf_req->sbal_index;
		pci_pos += fsf_req->sbal_count;
		pci_pos -= new_distance_from_int;
		pci_pos -= 1;
		pci_pos %= QDIO_MAX_BUFFERS_PER_Q;
		req_queue->buffer[pci_pos]->element[0].flags |= SBAL_FLAGS0_PCI;
		ZFCP_LOG_TRACE("Setting PCI flag at pos %d\n", pci_pos);
	}
	return new_distance_from_int;
}

/*
 * function:	zfcp_zero_sbals
 *
 * purpose:	zeros specified range of SBALs
 *
 * returns:
 */
void
zfcp_qdio_zero_sbals(struct qdio_buffer *buf[], int first, int clean_count)
{
	int cur_pos;
	int index;

	for (cur_pos = first; cur_pos < (first + clean_count); cur_pos++) {
		index = cur_pos % QDIO_MAX_BUFFERS_PER_Q;
		memset(buf[index], 0, sizeof (struct qdio_buffer));
		ZFCP_LOG_TRACE("zeroing BUFFER %d at address 0x%lx\n",
			       index, (unsigned long) buf[index]);
	}
}

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
