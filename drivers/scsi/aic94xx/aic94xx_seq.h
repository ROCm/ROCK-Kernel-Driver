/*
 * Aic94xx SAS/SATA driver sequencer interface header file.
 *
 * Copyright (C) 2005 Adaptec, Inc.  All rights reserved.
 * Copyright (C) 2005 Luben Tuikov <luben_tuikov@adaptec.com>
 *
 * This file is licensed under GPLv2.
 *
 * This file is part of the aic94xx driver.
 *
 * The aic94xx driver is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 of the
 * License.
 *
 * The aic94xx driver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the aic94xx driver; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * $Id: //depot/aic94xx/aic94xx_seq.h#7 $
 */

#ifndef _AIC94XX_SEQ_H_
#define _AIC94XX_SEQ_H_

int asd_pause_cseq(struct asd_ha_struct *asd_ha);
int asd_unpause_cseq(struct asd_ha_struct *asd_ha);
int asd_pause_lseq(struct asd_ha_struct *asd_ha, u8 lseq_mask);
int asd_unpause_lseq(struct asd_ha_struct *asd_ha, u8 lseq_mask);
int asd_init_seqs(struct asd_ha_struct *asd_ha);
int asd_start_seqs(struct asd_ha_struct *asd_ha);

void asd_update_port_links(struct asd_sas_phy *phy);

#endif
