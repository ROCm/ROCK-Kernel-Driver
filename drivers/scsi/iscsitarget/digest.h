/*
 * iSCSI digest handling.
 * (C) 2004 Xiranet Communications GmbH <arne.redlich@xiranet.com>
 * This code is licensed under the GPL.
 */

#ifndef __IET_DIGEST_H__
#define __IET_DIGEST_H__

extern void digest_alg_available(unsigned int *val);
extern int digest_init(struct iscsi_conn *conn);
extern void digest_cleanup(struct iscsi_conn *conn);

extern int digest_rx_header(struct iscsi_cmnd *cmnd);
extern int digest_rx_data(struct iscsi_cmnd *cmnd);

extern void digest_tx_header(struct iscsi_cmnd *cmnd);
extern void digest_tx_data(struct iscsi_cmnd *cmnd);

#endif /* __IET_DIGEST_H__ */
