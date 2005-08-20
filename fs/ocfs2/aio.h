/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * aio.h
 *
 * Function prototypes
 *
 * Copyright (C) 2002, 2004, 2005 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#ifndef OCFS2_AIO_H
#define OCFS2_AIO_H

ssize_t ocfs2_file_aio_write(struct kiocb *iocb, const char __user *buf,
			     size_t count, loff_t pos);
ssize_t ocfs2_file_aio_read(struct kiocb *iocb, char __user *buf, size_t count,
			    loff_t pos);

void okp_teardown_from_list(void *data);
void ocfs2_wait_for_okp_destruction(ocfs2_super *osb);

#endif /* OCFS2_AIO_H */
