/* $Id: entity.h,v 1.1.2.1 2001/02/08 12:25:43 armin Exp $ */

#ifndef __DIVAS_USER_MODE_IDI_ENTITY__
#define __DIVAS_USER_MODE_IDI_ENTITY__

#define DIVA_UM_IDI_RC_PENDING      0x00000001
#define DIVA_UM_IDI_REMOVE_PENDING  0x00000002
#define DIVA_UM_IDI_TX_FLOW_CONTROL 0x00000004
#define DIVA_UM_IDI_REMOVED         0x00000008
#define DIVA_UM_IDI_ASSIGN_PENDING  0x00000010

typedef struct _divas_um_idi_entity {
	diva_entity_link_t        link; /* should be first */
	diva_um_idi_adapter_t*    adapter; /* Back to adapter */
	ENTITY                    e;
	void*                     os_ref;
	dword                     status;
	void*                     os_context;
	int                       rc_count;
	diva_um_idi_data_queue_t  data; /* definad by user 1 ... MAX */
	diva_um_idi_data_queue_t  rc;   /* two entries */
	BUFFERS                   XData;
	BUFFERS                   RData;
	byte                      buffer[2048+512];
} divas_um_idi_entity_t;


#endif
