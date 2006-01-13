/*
 * Copyright (c)  2003-2005 QLogic Corporation
 * QLogic Linux iSCSI Driver
 *
 * This program includes a device driver for Linux 2.6 that may be
 * distributed with QLogic hardware specific firmware binary file.
 * You may modify and redistribute the device driver code under the
 * GNU General Public License as published by the Free Software
 * Foundation (version 2 or a later version) and/or under the
 * following terms, as applicable:
 *
 * 	1. Redistribution of source code must retain the above
 * 	   copyright notice, this list of conditions and the
 * 	   following disclaimer.
 *
 * 	2. Redistribution in binary form must reproduce the above
 * 	   copyright notice, this list of conditions and the
 * 	   following disclaimer in the documentation and/or other
 * 	   materials provided with the distribution.
 *
 * 	3. The name of QLogic Corporation may not be used to
 * 	   endorse or promote products derived from this software
 * 	   without specific prior written permission.
 *
 * You may redistribute the hardware specific firmware binary file
 * under the following terms:
 *
 * 	1. Redistribution of source code (only if applicable),
 * 	   must retain the above copyright notice, this list of
 * 	   conditions and the following disclaimer.
 *
 * 	2. Redistribution in binary form must reproduce the above
 * 	   copyright notice, this list of conditions and the
 * 	   following disclaimer in the documentation and/or other
 * 	   materials provided with the distribution.
 *
 * 	3. The name of QLogic Corporation may not be used to
 * 	   endorse or promote products derived from this software
 * 	   without specific prior written permission
 *
 * REGARDLESS OF WHAT LICENSING MECHANISM IS USED OR APPLICABLE,
 * THIS PROGRAM IS PROVIDED BY QLOGIC CORPORATION "AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * USER ACKNOWLEDGES AND AGREES THAT USE OF THIS PROGRAM WILL NOT
 * CREATE OR GIVE GROUNDS FOR A LICENSE BY IMPLICATION, ESTOPPEL, OR
 * OTHERWISE IN ANY INTELLECTUAL PROPERTY RIGHTS (PATENT, COPYRIGHT,
 * TRADE SECRET, MASK WORK, OR OTHER PROPRIETARY RIGHT) EMBODIED IN
 * ANY OTHER QLOGIC HARDWARE OR SOFTWARE EITHER SOLELY OR IN
 * COMBINATION WITH THIS PROGRAM.
 */

/*
 *
 * qla4xxx_lookup_ddb_by_fw_index
 *      This routine locates a device handle given the firmware device
 *      database index.  If device doesn't exist, returns NULL.
 *
 * Input:
 *      ha - Pointer to host adapter structure.
 *      fw_ddb_index - Firmware's device database index
 *
 * Returns:
 *      Pointer to the corresponding internal device database structure
 *
 * Context:
 *      Kernel context.
 */
static inline ddb_entry_t *
qla4xxx_lookup_ddb_by_fw_index(scsi_qla_host_t *ha, uint32_t fw_ddb_index)
{
        ddb_entry_t *ddb_entry = NULL;

        if ((fw_ddb_index < MAX_DDB_ENTRIES) &&
            (ha->fw_ddb_index_map[fw_ddb_index] !=
                (ddb_entry_t *) INVALID_ENTRY)) {
                ddb_entry = ha->fw_ddb_index_map[fw_ddb_index];
        }

        DEBUG3(printk("scsi%d: %s: index [%d], ddb_entry = %p\n",
            ha->host_no, __func__, fw_ddb_index, ddb_entry));

        return ddb_entry;
}

static inline void
__qla4xxx_enable_intrs(scsi_qla_host_t *ha)
{
	if (IS_QLA4022(ha)) {
		WRT_REG_DWORD(&ha->reg->u1.isp4022.intr_mask,
		    SET_RMASK(IMR_SCSI_INTR_ENABLE));
		PCI_POSTING(&ha->reg->u1.isp4022.intr_mask);
	} else {
		WRT_REG_DWORD(&ha->reg->ctrl_status,
		    SET_RMASK(CSR_SCSI_INTR_ENABLE));
		PCI_POSTING(&ha->reg->ctrl_status);
	}
	set_bit(AF_INTERRUPTS_ON, &ha->flags);
}

static inline void
__qla4xxx_disable_intrs(scsi_qla_host_t *ha)
{
	if (IS_QLA4022(ha)) {
		WRT_REG_DWORD(&ha->reg->u1.isp4022.intr_mask,
		    CLR_RMASK(IMR_SCSI_INTR_ENABLE));
		PCI_POSTING(&ha->reg->u1.isp4022.intr_mask);
	} else {
		WRT_REG_DWORD(&ha->reg->ctrl_status,
		    CLR_RMASK(CSR_SCSI_INTR_ENABLE));
		PCI_POSTING(&ha->reg->ctrl_status);
	}
	clear_bit(AF_INTERRUPTS_ON, &ha->flags);
}

static inline void
qla4xxx_enable_intrs(scsi_qla_host_t *ha)
{
	unsigned long flags;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	__qla4xxx_enable_intrs(ha);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

static inline void
qla4xxx_disable_intrs(scsi_qla_host_t *ha)
{
	unsigned long flags;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	__qla4xxx_disable_intrs(ha);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}
