/*
 * iSCSI digest handling.
 * (C) 2004 Xiranet Communications GmbH <arne.redlich@xiranet.com>
 * This code is licensed under the GPL.
 */

#include <asm/types.h>
#include <asm/scatterlist.h>

#include "iscsi.h"
#include "digest.h"
#include "iscsi_dbg.h"

void digest_alg_available(unsigned int *val)
{
	if (*val & DIGEST_CRC32C && !crypto_alg_available("crc32c", 0)) {
		printk("CRC32C digest algorithm not available in kernel\n");
		*val |= ~DIGEST_CRC32C;
	}
}

/**
 * initialize support for digest calculation.
 *
 * digest_init -
 * @conn: ptr to connection to make use of digests
 *
 * @return: 0 on success, < 0 on error
 */
int digest_init(struct iscsi_conn *conn)
{
	int err = 0;

	if (!(conn->hdigest_type & DIGEST_ALL))
		conn->hdigest_type = DIGEST_NONE;

	if (!(conn->ddigest_type & DIGEST_ALL))
		conn->ddigest_type = DIGEST_NONE;

	if (conn->hdigest_type & DIGEST_CRC32C || conn->ddigest_type & DIGEST_CRC32C) {
		conn->rx_digest_tfm = crypto_alloc_tfm("crc32c", 0);
		if (!conn->rx_digest_tfm) {
			err = -ENOMEM;
			goto out;
		}

		conn->tx_digest_tfm = crypto_alloc_tfm("crc32c", 0);
		if (!conn->tx_digest_tfm) {
			err = -ENOMEM;
			goto out;
		}
	}

out:
	if (err)
		digest_cleanup(conn);

	return err;
}

/**
 * free resources used for digest calculation.
 *
 * digest_cleanup -
 * @conn: ptr to connection that made use of digests
 */
void digest_cleanup(struct iscsi_conn *conn)
{
	if (conn->tx_digest_tfm)
		crypto_free_tfm(conn->tx_digest_tfm);
	if (conn->rx_digest_tfm)
		crypto_free_tfm(conn->rx_digest_tfm);
}

/**
 * debug handling of header digest errors:
 * simulates a digest error after n PDUs / every n-th PDU of type
 * HDIGEST_ERR_CORRUPT_PDU_TYPE.
 */
static inline void __dbg_simulate_header_digest_error(struct iscsi_cmnd *cmnd)
{
#define HDIGEST_ERR_AFTER_N_CMNDS 1000
#define HDIGEST_ERR_ONLY_ONCE     1
#define HDIGEST_ERR_CORRUPT_PDU_TYPE ISCSI_OP_SCSI_CMD
#define HDIGEST_ERR_CORRUPT_PDU_WITH_DATA_ONLY 0

	static int num_cmnds = 0;
	static int num_errs = 0;

	if (cmnd_opcode(cmnd) == HDIGEST_ERR_CORRUPT_PDU_TYPE) {
		if (HDIGEST_ERR_CORRUPT_PDU_WITH_DATA_ONLY) {
			if (cmnd->pdu.datasize)
				num_cmnds++;
		} else
			num_cmnds++;
	}

	if ((num_cmnds == HDIGEST_ERR_AFTER_N_CMNDS)
	    && (!(HDIGEST_ERR_ONLY_ONCE && num_errs))) {
		printk("*** Faking header digest error ***\n");
		printk("\tcmnd: 0x%x, itt 0x%x, sn 0x%x\n",
		       cmnd_opcode(cmnd),
		       be32_to_cpu(cmnd->pdu.bhs.itt),
		       be32_to_cpu(cmnd->pdu.bhs.statsn));
		cmnd->hdigest = ~cmnd->hdigest;
		/* make things even worse by manipulating header fields */
		cmnd->pdu.datasize += 8;
		num_errs++;
		num_cmnds = 0;
	}
	return;
}

/**
 * debug handling of data digest errors:
 * simulates a digest error after n PDUs / every n-th PDU of type
 * DDIGEST_ERR_CORRUPT_PDU_TYPE.
 */
static inline void __dbg_simulate_data_digest_error(struct iscsi_cmnd *cmnd)
{
#define DDIGEST_ERR_AFTER_N_CMNDS 50
#define DDIGEST_ERR_ONLY_ONCE     1
#define DDIGEST_ERR_CORRUPT_PDU_TYPE   ISCSI_OP_SCSI_DATA_OUT
#define DDIGEST_ERR_CORRUPT_UNSOL_DATA_ONLY 0

	static int num_cmnds = 0;
	static int num_errs = 0;

	if ((cmnd->pdu.datasize)
	    && (cmnd_opcode(cmnd) == DDIGEST_ERR_CORRUPT_PDU_TYPE)) {
		switch (cmnd_opcode(cmnd)) {
		case ISCSI_OP_SCSI_DATA_OUT:
			if ((DDIGEST_ERR_CORRUPT_UNSOL_DATA_ONLY)
			    && (cmnd->pdu.bhs.ttt != ISCSI_RESERVED_TAG))
				break;
		default:
			num_cmnds++;
		}
	}

	if ((num_cmnds == DDIGEST_ERR_AFTER_N_CMNDS)
	    && (!(DDIGEST_ERR_ONLY_ONCE && num_errs))
	    && (cmnd->pdu.datasize)
	    && (!cmnd->conn->read_overflow)) {
		printk("*** Faking data digest error: ***");
		printk("\tcmnd 0x%x, itt 0x%x, sn 0x%x\n",
		       cmnd_opcode(cmnd),
		       be32_to_cpu(cmnd->pdu.bhs.itt),
		       be32_to_cpu(cmnd->pdu.bhs.statsn));
		cmnd->ddigest = ~cmnd->ddigest;
		num_errs++;
		num_cmnds = 0;
	}
}

/* Copied from linux-iscsi initiator and slightly adjusted */
#define SETSG(sg, p, l) do {					\
	(sg).page = virt_to_page((p));				\
	(sg).offset = ((unsigned long)(p) & ~PAGE_CACHE_MASK);	\
	(sg).length = (l);					\
} while (0)

static void digest_header(struct crypto_tfm *tfm, struct iscsi_pdu *pdu, u8 *crc)
{
	struct scatterlist sg[2];
	int i = 0;

	SETSG(sg[i], &pdu->bhs, sizeof(struct iscsi_hdr));
	i++;
	if (pdu->ahssize) {
		SETSG(sg[i], pdu->ahs, pdu->ahssize);
		i++;
	}

	crypto_digest_init(tfm);
	crypto_digest_update(tfm, sg, i);
	crypto_digest_final(tfm, crc);
}

int digest_rx_header(struct iscsi_cmnd *cmnd)
{
	u32 crc;

	digest_header(cmnd->conn->rx_digest_tfm, &cmnd->pdu, (u8 *) &crc);
	if (crc != cmnd->hdigest)
		return -EIO;

	return 0;
}

void digest_tx_header(struct iscsi_cmnd *cmnd)
{
	digest_header(cmnd->conn->tx_digest_tfm, &cmnd->pdu, (u8 *) &cmnd->hdigest);
}

static void digest_data(struct crypto_tfm *tfm, struct iscsi_cmnd *cmnd,
			struct tio *tio, u32 offset, u8 *crc)
{
	struct scatterlist sg[ISCSI_CONN_IOV_MAX];
	u32 size, length;
	int i, idx, count;

	size = cmnd->pdu.datasize;
	size = (size + 3) & ~3;

	offset += tio->offset;
	idx = offset >> PAGE_CACHE_SHIFT;
	offset &= ~PAGE_CACHE_MASK;
	count = get_pgcnt(size, offset);
	assert(idx + count <= tio->pg_cnt);

	assert(count < ISCSI_CONN_IOV_MAX);

	crypto_digest_init(tfm);

	for (i = 0; size; i++) {
		if (offset + size > PAGE_CACHE_SIZE)
			length = PAGE_CACHE_SIZE - offset;
		else
			length = size;

		sg[i].page = tio->pvec[idx + i];
		sg[i].offset = offset;
		sg[i].length = length;
		size -= length;
		offset = 0;
	}

	crypto_digest_update(tfm, sg, count);
	crypto_digest_final(tfm, crc);
}

int digest_rx_data(struct iscsi_cmnd *cmnd)
{
	struct tio *tio;
	u32 offset, crc;

	if (cmnd_opcode(cmnd) == ISCSI_OP_SCSI_DATA_OUT) {
		struct iscsi_cmnd *scsi_cmnd = cmnd->req;
		struct iscsi_data *req = (struct iscsi_data *)&cmnd->pdu.bhs;

		tio = scsi_cmnd->tio;
		offset = be32_to_cpu(req->offset);
	} else {
		tio = cmnd->tio;
		offset = 0;
	}

	digest_data(cmnd->conn->rx_digest_tfm, cmnd, tio, offset, (u8 *) &crc);

	if (!cmnd->conn->read_overflow && (cmnd_opcode(cmnd) != ISCSI_OP_PDU_REJECT)) {
		if (crc != cmnd->ddigest)
			return -EIO;
	}

	return 0;
}

void digest_tx_data(struct iscsi_cmnd *cmnd)
{
	struct tio *tio = cmnd->tio;
	struct iscsi_data *req = (struct iscsi_data *)&cmnd->pdu.bhs;

	assert(tio);
	digest_data(cmnd->conn->tx_digest_tfm, cmnd, tio,
		    be32_to_cpu(req->offset), (u8 *) &cmnd->ddigest);
}
