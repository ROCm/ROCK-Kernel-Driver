/* $Id: adapter.h,v 1.1.2.2 2002/10/02 14:38:37 armin Exp $ */

#ifndef __DIVA_USER_MODE_IDI_ADAPTER_H__
#define __DIVA_USER_MODE_IDI_ADAPTER_H__

#define DIVA_UM_IDI_ADAPTER_REMOVED 0x00000001

typedef struct _diva_um_idi_adapter {
	diva_entity_link_t link;
	DESCRIPTOR d;
	int adapter_nr;
	diva_entity_queue_t entity_q;	/* entities linked to this adapter */
	dword status;
} diva_um_idi_adapter_t;


#endif
