/*
 *  drivers/mtd/nand.c
 *
 *  Copyright (C) 2000 Steven J. Hill (sjhill@cotw.com)
 *
 * $Id: nand.c,v 1.12 2001/10/02 15:05:14 dwmw2 Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Overview:
 *   This is the generic MTD driver for NAND flash devices. It should be
 *   capable of working with almost all NAND chips currently available.
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ids.h>
#include <linux/interrupt.h>
#include <asm/io.h>

#ifdef CONFIG_MTD_NAND_ECC
#include <linux/mtd/nand_ecc.h>
#endif

/*
 * Macros for low-level register control
 */
#define NAND_CTRL (*(volatile unsigned char *) \
			((struct nand_chip *) mtd->priv)->CTRL_ADDR)
#define nand_select()	NAND_CTRL &= ~this->NCE; \
			nand_command(mtd, NAND_CMD_RESET, -1, -1); \
			udelay (10);
#define nand_deselect() NAND_CTRL |= ~this->NCE;

/*
 * NAND low-level MTD interface functions
 */
static int nand_read (struct mtd_info *mtd, loff_t from, size_t len,
			size_t *retlen, u_char *buf);
static int nand_read_ecc (struct mtd_info *mtd, loff_t from, size_t len,
				size_t *retlen, u_char *buf, u_char *ecc_code);
static int nand_read_oob (struct mtd_info *mtd, loff_t from, size_t len,
				size_t *retlen, u_char *buf);
static int nand_write (struct mtd_info *mtd, loff_t to, size_t len,
			size_t *retlen, const u_char *buf);
static int nand_write_ecc (struct mtd_info *mtd, loff_t to, size_t len,
				size_t *retlen, const u_char *buf,
				u_char *ecc_code);
static int nand_write_oob (struct mtd_info *mtd, loff_t to, size_t len,
				size_t *retlen, const u_char *buf);
static int nand_writev (struct mtd_info *mtd, const struct iovec *vecs,
				unsigned long count, loff_t to, size_t *retlen);
static int nand_erase (struct mtd_info *mtd, struct erase_info *instr);
static void nand_sync (struct mtd_info *mtd);

/*
 * Send command to NAND device
 */
static void nand_command (struct mtd_info *mtd, unsigned command,
				int column, int page_addr)
{
	register struct nand_chip *this = mtd->priv;
	register unsigned long NAND_IO_ADDR = this->IO_ADDR;

	/* Begin command latch cycle */
	NAND_CTRL |= this->CLE;

	/*
	 * Write out the command to the device.
	 */
	if (command != NAND_CMD_SEQIN)	
		writeb (command, NAND_IO_ADDR);
	else {
		if (mtd->oobblock == 256 && column >= 256) {
			column -= 256;
			writeb(NAND_CMD_RESET, NAND_IO_ADDR);
			writeb(NAND_CMD_READOOB, NAND_IO_ADDR);
			writeb(NAND_CMD_SEQIN, NAND_IO_ADDR);
		}
		else if (mtd->oobblock == 512 && column >= 256) {
			if (column < 512) {
				column -= 256;
				writeb(NAND_CMD_READ1, NAND_IO_ADDR);
				writeb(NAND_CMD_SEQIN, NAND_IO_ADDR);
			}
			else {
				column -= 512;
				writeb(NAND_CMD_READOOB, NAND_IO_ADDR);
				writeb(NAND_CMD_SEQIN, NAND_IO_ADDR);
			}
		}
		else {
			writeb(NAND_CMD_READ0, NAND_IO_ADDR);
			writeb(NAND_CMD_SEQIN, NAND_IO_ADDR);
		}
	}

	/* Set ALE and clear CLE to start address cycle */
	NAND_CTRL &= ~this->CLE;
	NAND_CTRL |= this->ALE;

	/* Serially input address */
	if (column != -1)
		writeb (column, NAND_IO_ADDR);
	if (page_addr != -1) {
		writeb ((unsigned char) (page_addr & 0xff), NAND_IO_ADDR);
		writeb ((unsigned char) ((page_addr >> 8) & 0xff), NAND_IO_ADDR);
		/* One more address cycle for higher density devices */
		if (mtd->size & 0x0c000000) {
			writeb ((unsigned char) ((page_addr >> 16) & 0x0f),
					NAND_IO_ADDR);
		}
	}

	/* Latch in address */
	NAND_CTRL &= ~this->ALE;

	/* Pause for 15us */
	udelay (15);
}

/*
 * NAND read
 */
static int nand_read (struct mtd_info *mtd, loff_t from, size_t len,
			size_t *retlen, u_char *buf)
{
#ifdef CONFIG_MTD_NAND_ECC
	struct nand_chip *this = mtd->priv;
	
	return nand_read_ecc (mtd, from, len, retlen, buf, this->ecc_code_buf);
#else
	return nand_read_ecc (mtd, from, len, retlen, buf, NULL);
#endif
}

/*
 * NAND read with ECC
 */
static int nand_read_ecc (struct mtd_info *mtd, loff_t from, size_t len,
				size_t *retlen, u_char *buf, u_char *ecc_code)
{
	int j, col, page, state;
	int erase_state = 0;
	struct nand_chip *this = mtd->priv;
	DECLARE_WAITQUEUE(wait, current);
#ifdef CONFIG_MTD_NAND_ECC
	int ecc_result;
	u_char ecc_calc[6];
#endif

	DEBUG (MTD_DEBUG_LEVEL3,
		"nand_read_ecc: from = 0x%08x, len = %i\n", (unsigned int) from,
		(int) len);

	/* Do not allow reads past end of device */
	if ((from + len) > mtd->size) {
		DEBUG (MTD_DEBUG_LEVEL0,
			"nand_read_ecc: Attempt read beyond end of device\n");
		*retlen = 0;
		return -EINVAL;
	}

	/* Grab the lock and see if the device is available */
retry:
	spin_lock_bh (&this->chip_lock);

	switch (this->state) {
	case FL_READY:
		this->state = FL_READING;
		spin_unlock_bh (&this->chip_lock);
		break;

	case FL_ERASING:
		this->state = FL_READING;
		erase_state = 1;
		spin_unlock_bh (&this->chip_lock);
		break;

	default:
		set_current_state (TASK_UNINTERRUPTIBLE);
		add_wait_queue (&this->wq, &wait);
		spin_unlock_bh (&this->chip_lock);
		schedule();

		remove_wait_queue (&this->wq, &wait);
		goto retry;
	};

	/* First we calculate the starting page */
	page = from >> this->page_shift;

	/* Get raw starting column */
	col = from & (mtd->oobblock - 1);

	/* State machine for devices having pages larger than 256 bytes */
	state = (col < mtd->eccsize) ? 0 : 1;

	/* Calculate column address within ECC block context */
	col = (col >= mtd->eccsize) ? (col - mtd->eccsize) : col;

	/* Initialize return value */
	*retlen = 0;

	/* Select the NAND device */
	nand_select ();

	/* Loop until all data read */
	while (*retlen < len) {

#ifdef CONFIG_MTD_NAND_ECC
		/* Send the read command */
		if (!state)
			nand_command (mtd, NAND_CMD_READ0, 0x00, page);
		else 
			nand_command (mtd, NAND_CMD_READ1, 0x00, page);

		/* Read in a block big enough for ECC */
		for (j=0 ; j < mtd->eccsize ; j++)
			this->data_buf[j] = readb (this->IO_ADDR);

		/* Read in the out-of-band data */
		if (!state) {
			nand_command (mtd, NAND_CMD_READOOB, 0x00, page);
			for (j=0 ; j<3 ; j++)
				ecc_code[j] = readb(this->IO_ADDR);
			nand_command (mtd, NAND_CMD_READ0, 0x00, page);
		}
		else {
			nand_command (mtd, NAND_CMD_READOOB, 0x03, page);
			for (j=3 ; j<6 ; j++)
				ecc_code[j] = readb(this->IO_ADDR);
			nand_command (mtd, NAND_CMD_READ0, 0x00, page);
		}

		/* Calculate the ECC and verify it */
		if (!state) {
			nand_calculate_ecc (&this->data_buf[0],
						&ecc_calc[0]);
			ecc_result = nand_correct_data (&this->data_buf[0],
						&ecc_code[0], &ecc_calc[0]);
		}
		else {
			nand_calculate_ecc (&this->data_buf[0],
						&ecc_calc[3]);
			ecc_result = nand_correct_data (&this->data_buf[0],
						&ecc_code[3], &ecc_calc[3]);
		}
		if (ecc_result == -1) {
			DEBUG (MTD_DEBUG_LEVEL0,
				"nand_read_ecc: " \
				"Failed ECC read, page 0x%08x\n", page);
			nand_deselect ();
			spin_lock_bh (&this->chip_lock);
			if (erase_state)
				this->state = FL_ERASING;
			else
				this->state = FL_READY;
			wake_up (&this->wq);
			spin_unlock_bh (&this->chip_lock);
			return -EIO;
		}

		/* Read the data from ECC data buffer into return buffer */
		if ((*retlen + (mtd->eccsize - col)) >= len) {
			while (*retlen < len)
				buf[(*retlen)++] = this->data_buf[col++];
			/* We're done */
			continue;
		}
		else
			for (j=col ; j < mtd->eccsize ; j++)
				buf[(*retlen)++] = this->data_buf[j];
#else
		/* Send the read command */
		if (!state)
			nand_command (mtd, NAND_CMD_READ0, col, page);
		else 
			nand_command (mtd, NAND_CMD_READ1, col, page);

		/* Read the data directly into the return buffer */ 
		if ((*retlen + (mtd->eccsize - col)) >= len) {
			while (*retlen < len)
				buf[(*retlen)++] = readb (this->IO_ADDR);
			/* We're done */
			continue;
		}
		else
			for (j=col ; j < mtd->eccsize ; j++)
				buf[(*retlen)++] = readb (this->IO_ADDR);
#endif

		/*
		 * If the amount of data to be read is greater than
		 * (256 - col), then all subsequent reads will take
		 * place on page or half-page (in the case of 512 byte
		 * page devices) aligned boundaries and the column
		 * address will be zero. Setting the column address to
		 * to zero after the first read allows us to simplify
		 * the reading of data and the if/else statements above.
		 */
		if (col)
			col = 0x00;

		/* Increment page address */
		if ((mtd->oobblock == 256) || state)
			page++;

		/* Toggle state machine */
		if (mtd->oobblock == 512)
			state = state ? 0 : 1;
	}

	/* De-select the NAND device */
	nand_deselect ();

	/* Wake up anyone waiting on the device */
	spin_lock_bh (&this->chip_lock);
	if (erase_state)
		this->state = FL_ERASING;
	else
		this->state = FL_READY;
	wake_up (&this->wq);
	spin_unlock_bh (&this->chip_lock);
	
	/* Return happy */
	return 0;
}

/*
 * NAND read out-of-band
 */
static int nand_read_oob (struct mtd_info *mtd, loff_t from, size_t len,
				size_t *retlen, u_char *buf)
{
	int i, col, page;
	int erase_state = 0;
	struct nand_chip *this = mtd->priv;
	DECLARE_WAITQUEUE(wait, current);
	
	DEBUG (MTD_DEBUG_LEVEL3,
		"nand_read_oob: from = 0x%08x, len = %i\n", (unsigned int) from,
		(int) len);

	/* Shift to get page */
	page = ((int) from) >> this->page_shift;

	/* Mask to get column */
	col = from & 0x0f;

	/* Initialize return length value */
	*retlen = 0;

	/* Do not allow read past end of page */
	if ((col + len) > mtd->oobsize) {
		DEBUG (MTD_DEBUG_LEVEL0,
			"nand_read_oob: Attempt read past end of page " \
			"0x%08x, column %i, length %i\n", page, col, len);
		return -EINVAL;
	}

retry:
	/* Grab the lock and see if the device is available */
	spin_lock_bh (&this->chip_lock);

	switch (this->state) {
	case FL_READY:
		this->state = FL_READING;
		spin_unlock_bh (&this->chip_lock);
		break;

	case FL_ERASING:
		this->state = FL_READING;
		erase_state = 1;
		spin_unlock_bh (&this->chip_lock);
		break;

	default:
		set_current_state (TASK_UNINTERRUPTIBLE);
		add_wait_queue (&this->wq, &wait);
		spin_unlock_bh (&this->chip_lock);
		schedule();

		remove_wait_queue (&this->wq, &wait);
		goto retry;
	};

	/* Select the NAND device */
	nand_select ();

	/* Send the read command */
	nand_command (mtd, NAND_CMD_READOOB, col, page);	

	/* Read the data */
	for (i = 0 ; i < len ; i++)
		buf[i] = readb (this->IO_ADDR);

	/* De-select the NAND device */
	nand_deselect ();

	/* Wake up anyone waiting on the device */
	spin_lock_bh (&this->chip_lock);
	if (erase_state)
		this->state = FL_ERASING;
	else
		this->state = FL_READY;
	wake_up (&this->wq);
	spin_unlock_bh (&this->chip_lock);

	/* Return happy */
	*retlen = len;
	return 0;
}

/*
 * NAND write
 */
static int nand_write (struct mtd_info *mtd, loff_t to, size_t len,
			size_t *retlen, const u_char *buf)
{
#ifdef CONFIG_MTD_NAND_ECC
	struct nand_chip *this = mtd->priv;
	
	return nand_write_ecc (mtd, to, len, retlen, buf, this->ecc_code_buf);
#else
	return nand_write_ecc (mtd, to, len, retlen, buf, NULL);
#endif
}

/*
 * NAND write with ECC
 */
static int nand_write_ecc (struct mtd_info *mtd, loff_t to, size_t len,
				size_t *retlen, const u_char *buf,
				u_char *ecc_code)
{
	int i, page, col, cnt, status;
	struct nand_chip *this = mtd->priv;
	DECLARE_WAITQUEUE(wait, current);
#ifdef CONFIG_MTD_NAND_ECC
	int ecc_bytes = (mtd->oobblock == 512) ? 6 : 3;
#endif

	DEBUG (MTD_DEBUG_LEVEL3,
		"nand_write_ecc: to = 0x%08x, len = %i\n", (unsigned int) to,
		(int) len);

	/* Do not allow write past end of page */
	if ((to + len) > mtd->size) {
		DEBUG (MTD_DEBUG_LEVEL0,
			"nand_write_ecc: Attempted write past end of device\n");
		return -EINVAL;
	}

retry:
	/* Grab the lock and see if the device is available */
	spin_lock_bh (&this->chip_lock);

	switch (this->state) {
	case FL_READY:
		this->state = FL_WRITING;
		spin_unlock_bh (&this->chip_lock);
		break;

	default:
		set_current_state (TASK_UNINTERRUPTIBLE);
		add_wait_queue (&this->wq, &wait);
		spin_unlock_bh (&this->chip_lock);
		schedule();

		remove_wait_queue (&this->wq, &wait);
		goto retry;
	};

	/* Shift to get page */
	page = ((int) to) >> this->page_shift;

	/* Get the starting column */
	col = to & (mtd->oobblock - 1);

	/* Initialize return length value */
	*retlen = 0;

	/* Select the NAND device */
	nand_select ();

	/* Check the WP bit */
	nand_command (mtd, NAND_CMD_STATUS, -1, -1);
	if (!(readb (this->IO_ADDR) & 0x80)) {
		DEBUG (MTD_DEBUG_LEVEL0,
			"nand_write_ecc: Device is write protected!!!\n");
		nand_deselect ();
		spin_lock_bh (&this->chip_lock);
		this->state = FL_READY;
		wake_up (&this->wq);
		spin_unlock_bh (&this->chip_lock);
		return -EIO;
	}

	/* Loop until all data is written */
	while (*retlen < len) {
		/* Write data into buffer */
		if ((col + len) >= mtd->oobblock)
			for(i=col, cnt=0 ; i < mtd->oobblock ; i++, cnt++)
				this->data_buf[i] = buf[(*retlen + cnt)];
		else
			for(i=col, cnt=0 ; cnt < (len - *retlen) ; i++, cnt++)
				this->data_buf[i] = buf[(*retlen + cnt)];
		
#ifdef CONFIG_MTD_NAND_ECC
		/* Zero out the ECC array */
		for (i=0 ; i < 6 ; i++)
			ecc_code[i] = 0x00;

		/* Calculate and write the ECC if we have enough data */
		if ((col < mtd->eccsize) &&
			((col + (len - *retlen)) >= mtd->eccsize)) {
			nand_command (mtd, NAND_CMD_READ0, col, page);
			for (i=0 ; i < col ; i++)
				this->data_buf[i] = readb (this->IO_ADDR); 
			nand_calculate_ecc (&this->data_buf[0], &ecc_code[0]);
			for (i=0 ; i<3 ; i++)
				this->data_buf[(mtd->oobblock + i)] =
					ecc_code[i];
		}

		/* Calculate and write the second ECC if we have enough data */
		if ((mtd->oobblock == 512) &&
			((col + (len - *retlen)) >= mtd->oobblock)) {
			nand_calculate_ecc (&this->data_buf[256], &ecc_code[3]);
			for (i=3 ; i<6 ; i++)
				this->data_buf[(mtd->oobblock + i)] =
					ecc_code[i];
		}

		/* Write ones for partial page programming */
		for (i=ecc_bytes ; i < mtd->oobsize ; i++)
			this->data_buf[(mtd->oobblock + i)] = 0xff;
#else
		/* Write ones for partial page programming */
		for (i=mtd->oobblock ; i < (mtd->oobblock + mtd->oobsize) ; i++)
			this->data_buf[i] = 0xff;
#endif

		/* Write pre-padding bytes into buffer */
		for (i=0 ; i < col ; i++)
			this->data_buf[i] = 0xff;

		/* Write post-padding bytes into buffer */
		if ((col + (len - *retlen)) < mtd->oobblock) {
			for(i=(col + cnt) ; i < mtd->oobblock ; i++)
				this->data_buf[i] = 0xff;
		}

		/* Send command to begin auto page programming */
		nand_command (mtd, NAND_CMD_SEQIN, 0x00, page);

		/* Write out complete page of data */
		for (i=0 ; i < (mtd->oobblock + mtd->oobsize) ; i++)
			writeb (this->data_buf[i], this->IO_ADDR);

		/* Send command to actually program the data */
		nand_command (mtd, NAND_CMD_PAGEPROG, -1, -1);

		/*
		 * Wait for program operation to complete. This could
		 * take up to 3000us (3ms) on some devices, so we try
		 * and exit as quickly as possible.
		 */
		status = 0;
		for (i=0 ; i<24 ; i++) {
			/* Delay for 125us */
			udelay (125);

			/* Check the status */
			nand_command (mtd, NAND_CMD_STATUS, -1, -1);
			status = (int) readb (this->IO_ADDR);
			if (status & 0x40)
				break;
		}

		/* See if device thinks it succeeded */
		if (status & 0x01) {
			DEBUG (MTD_DEBUG_LEVEL0,
				"nand_write_ecc: " \
				"Failed write, page 0x%08x, " \
				"%6i bytes were succesful\n", page, *retlen);
			nand_deselect ();
			spin_lock_bh (&this->chip_lock);
			this->state = FL_READY;
			wake_up (&this->wq);
			spin_unlock_bh (&this->chip_lock);
			return -EIO;
		}

#ifdef CONFIG_MTD_NAND_VERIFY_WRITE
		/*
		 * The NAND device assumes that it is always writing to
		 * a cleanly erased page. Hence, it performs its internal
		 * write verification only on bits that transitioned from
		 * 1 to 0. The device does NOT verify the whole page on a
		 * byte by byte basis. It is possible that the page was
		 * not completely erased or the page is becoming unusable
		 * due to wear. The read with ECC would catch the error
		 * later when the ECC page check fails, but we would rather
		 * catch it early in the page write stage. Better to write
		 * no data than invalid data.
		 */
		
		/* Send command to read back the page */
		if (col < mtd->eccsize)
			nand_command (mtd, NAND_CMD_READ0, col, page);
		else
			nand_command (mtd, NAND_CMD_READ1, col - 256, page);

		/* Loop through and verify the data */
		for (i=col ; i < cnt ; i++) {
			if (this->data_buf[i] != readb (this->IO_ADDR)) {
				DEBUG (MTD_DEBUG_LEVEL0,
					"nand_write_ecc: " \
					"Failed write verify, page 0x%08x, " \
					"%6i bytes were succesful\n",
					page, *retlen);
				nand_deselect ();
				spin_lock_bh (&this->chip_lock);
				this->state = FL_READY;
				wake_up (&this->wq);
				spin_unlock_bh (&this->chip_lock);
				return -EIO;
			}
		}

#ifdef CONFIG_MTD_NAND_ECC
		/*
		 * We also want to check that the ECC bytes wrote
		 * correctly for the same reasons stated above.
		 */
		nand_command (mtd, NAND_CMD_READOOB, 0x00, page);
		for (i=0 ; i < ecc_bytes ; i++) {
			if ((readb (this->IO_ADDR) != ecc_code[i]) &&
					ecc_code[i]) {
				DEBUG (MTD_DEBUG_LEVEL0,
					"nand_write_ecc: Failed ECC write " \
					"verify, page 0x%08x, " \
					"%6i bytes were succesful\n",
					page, i);
				nand_deselect ();
				spin_lock_bh (&this->chip_lock);
				this->state = FL_READY;
				wake_up (&this->wq);
				spin_unlock_bh (&this->chip_lock);
				return -EIO;
			}
		}
#endif

#endif

		/*
		 * If we are writing a large amount of data and/or it
		 * crosses page or half-page boundaries, we set the
		 * the column to zero. It simplifies the program logic.
		 */
		if (col)
			col = 0x00;

		/* Update written bytes count */
		*retlen += cnt;

		/* Increment page address */
		page++;
	}

	/* De-select the NAND device */
	nand_deselect ();

	/* Wake up anyone waiting on the device */
	spin_lock_bh (&this->chip_lock);
	this->state = FL_READY;
	wake_up (&this->wq);
	spin_unlock_bh (&this->chip_lock);

	/* Return happy */
	*retlen = len;
	return 0;
}

/*
 * NAND write out-of-band
 */
static int nand_write_oob (struct mtd_info *mtd, loff_t to, size_t len,
				size_t *retlen, const u_char *buf)
{
	int i, column, page, status;
	struct nand_chip *this = mtd->priv;
	DECLARE_WAITQUEUE(wait, current);
	
	DEBUG (MTD_DEBUG_LEVEL3,
		"nand_write_oob: to = 0x%08x, len = %i\n", (unsigned int) to,
		(int) len);

	/* Shift to get page */
	page = ((int) to) >> this->page_shift;

	/* Mask to get column */
	column = to & 0x1f;

	/* Initialize return length value */
	*retlen = 0;

	/* Do not allow write past end of page */
	if ((column + len) > mtd->oobsize) {
		DEBUG (MTD_DEBUG_LEVEL0,
			"nand_write_oob: Attempt to write past end of page\n");
		return -EINVAL;
	}

retry:
	/* Grab the lock and see if the device is available */
	spin_lock_bh (&this->chip_lock);

	switch (this->state) {
	case FL_READY:
		this->state = FL_WRITING;
		spin_unlock_bh (&this->chip_lock);
		break;

	default:
		set_current_state (TASK_UNINTERRUPTIBLE);
		add_wait_queue (&this->wq, &wait);
		spin_unlock_bh (&this->chip_lock);
		schedule();

		remove_wait_queue (&this->wq, &wait);
		goto retry;
	};

	/* Select the NAND device */
	nand_select ();

	/* Check the WP bit */
	nand_command (mtd, NAND_CMD_STATUS, -1, -1);
	if (!(readb (this->IO_ADDR) & 0x80)) {
		DEBUG (MTD_DEBUG_LEVEL0,
			"nand_write_oob: Device is write protected!!!\n");
		nand_deselect ();
		spin_lock_bh (&this->chip_lock);
		this->state = FL_READY;
		wake_up (&this->wq);
		spin_unlock_bh (&this->chip_lock);
		return -EIO;
	}

	/* Write out desired data */
	nand_command (mtd, NAND_CMD_SEQIN, column + 512, page);
	for (i=0 ; i<len ; i++)
		writeb (buf[i], this->IO_ADDR);

	/* Send command to program the OOB data */
	nand_command (mtd, NAND_CMD_PAGEPROG, -1, -1);

	/*
	 * Wait for program operation to complete. This could
	 * take up to 3000us (3ms) on some devices, so we try
	 * and exit as quickly as possible.
	 */
	status = 0;
	for (i=0 ; i<24 ; i++) {
		/* Delay for 125us */
		udelay (125);

		/* Check the status */
		nand_command (mtd, NAND_CMD_STATUS, -1, -1);
		status = (int) readb (this->IO_ADDR);
		if (status & 0x40)
			break;
	}

	/* See if device thinks it succeeded */
	if (status & 0x01) {
		DEBUG (MTD_DEBUG_LEVEL0,
			"nand_write_oob: " \
			"Failed write, page 0x%08x\n", page);
		nand_deselect ();
		spin_lock_bh (&this->chip_lock);
		this->state = FL_READY;
		wake_up (&this->wq);
		spin_unlock_bh (&this->chip_lock);
		return -EIO;
	}

#ifdef CONFIG_MTD_NAND_VERIFY_WRITE
	/* Send command to read back the data */
	nand_command (mtd, NAND_CMD_READOOB, column, page);

	/* Loop through and verify the data */
	for (i=0 ; i<len ; i++) {
		if (buf[i] != readb (this->IO_ADDR)) {
			DEBUG (MTD_DEBUG_LEVEL0,
				"nand_write_oob: " \
				"Failed write verify, page 0x%08x\n", page);
			nand_deselect ();
			spin_lock_bh (&this->chip_lock);
			this->state = FL_READY;
			wake_up (&this->wq);
			spin_unlock_bh (&this->chip_lock);
			return -EIO;
		}
	}
#endif

	/* De-select the NAND device */
	nand_deselect ();

	/* Wake up anyone waiting on the device */
	spin_lock_bh (&this->chip_lock);
	this->state = FL_READY;
	wake_up (&this->wq);
	spin_unlock_bh (&this->chip_lock);

	/* Return happy */
	*retlen = len;
	return 0;
}

/*
 * NAND write with iovec
 */
static int nand_writev (struct mtd_info *mtd, const struct iovec *vecs,
				unsigned long count, loff_t to, size_t *retlen)
{
	int i, page, col, cnt, len, total_len, status;
	struct nand_chip *this = mtd->priv;
	DECLARE_WAITQUEUE(wait, current);
#ifdef CONFIG_MTD_NAND_ECC
	int ecc_bytes = (mtd->oobblock == 512) ? 6 : 3;
#endif

	/* Calculate total length of data */
	total_len = 0;
	for (i=0 ; i < count ; i++)
		total_len += (int) vecs[i].iov_len;

	DEBUG (MTD_DEBUG_LEVEL3,
		"nand_writev: to = 0x%08x, len = %i\n", (unsigned int) to,
			(unsigned int) total_len);

	/* Do not allow write past end of page */
	if ((to + total_len) > mtd->size) {
		DEBUG (MTD_DEBUG_LEVEL0,
			"nand_writev: Attempted write past end of device\n");
		return -EINVAL;
	}

retry:
	/* Grab the lock and see if the device is available */
	spin_lock_bh (&this->chip_lock);

	switch (this->state) {
	case FL_READY:
		this->state = FL_WRITING;
		spin_unlock_bh (&this->chip_lock);
		break;

	default:
		set_current_state (TASK_UNINTERRUPTIBLE);
		add_wait_queue (&this->wq, &wait);
		spin_unlock_bh (&this->chip_lock);
		schedule();

		remove_wait_queue (&this->wq, &wait);
		goto retry;
	};

	/* Shift to get page */
	page = ((int) to) >> this->page_shift;

	/* Get the starting column */
	col = to & (mtd->oobblock - 1);

	/* Initialize return length value */
	*retlen = 0;

	/* Select the NAND device */
	nand_select ();

	/* Check the WP bit */
	nand_command (mtd, NAND_CMD_STATUS, -1, -1);
	if (!(readb (this->IO_ADDR) & 0x80)) {
		DEBUG (MTD_DEBUG_LEVEL0,
			"nand_writev: Device is write protected!!!\n");
		nand_deselect ();
		spin_lock_bh (&this->chip_lock);
		this->state = FL_READY;
		wake_up (&this->wq);
		spin_unlock_bh (&this->chip_lock);
		return -EIO;
	}

	/* Loop until all iovecs' data has been written */
	cnt = col;
	len = 0;
	while (count) {
		/* Do any need pre-fill for partial page programming */
		for (i=0 ; i < cnt ; i++)
			this->data_buf[i] = 0xff;

		/*
		 * Read data out of each tuple until we have a full page
		 * to write or we've read all the tuples.
		 */
		while ((cnt < mtd->oobblock) && count) {
			this->data_buf[cnt++] =
				((u_char *) vecs->iov_base)[len++];
			if (len >= (int) vecs->iov_len) {
				vecs++;
				len = 0;
				count--;
			}
		}
		
		/* Do any need post-fill for partial page programming */
		for (i=cnt ; i < mtd->oobblock ; i++)
			this->data_buf[i] = 0xff;

#ifdef CONFIG_MTD_NAND_ECC
		/* Zero out the ECC array */
		for (i=0 ; i < 6 ; i++)
			this->ecc_code_buf[i] = 0x00;

		/* Calculate and write the first ECC */
		if (col >= mtd->eccsize) {
			nand_command (mtd, NAND_CMD_READ0, col, page);
			for (i=0 ; i < col ; i++)
				this->data_buf[i] = readb (this->IO_ADDR); 
			nand_calculate_ecc (&this->data_buf[0],
				&(this->ecc_code_buf[0]));
			for (i=0 ; i<3 ; i++)
				this->data_buf[(mtd->oobblock + i)] =
					this->ecc_code_buf[i];
		}

		/* Calculate and write the second ECC */
		if ((mtd->oobblock == 512) && (cnt == mtd->oobblock)) {
			nand_calculate_ecc (&this->data_buf[256],
				&(this->ecc_code_buf[3]));
			for (i=3 ; i<6 ; i++)
				this->data_buf[(mtd->oobblock + i)] =
					this->ecc_code_buf[i];
		}

		/* Write ones for partial page programming */
		for (i=ecc_bytes ; i < mtd->oobsize ; i++)
			this->data_buf[(mtd->oobblock + i)] = 0xff;
#else
		/* Write ones for partial page programming */
		for (i=mtd->oobblock ; i < (mtd->oobblock + mtd->oobsize) ; i++)
			this->data_buf[i] = 0xff;
#endif
		/* Send command to begin auto page programming */
		nand_command (mtd, NAND_CMD_SEQIN, 0x00, page);

		/* Write out complete page of data */
		for (i=0 ; i < (mtd->oobblock + mtd->oobsize) ; i++)
			writeb (this->data_buf[i], this->IO_ADDR);

		/* Send command to actually program the data */
		nand_command (mtd, NAND_CMD_PAGEPROG, -1, -1);

		/*
		 * Wait for program operation to complete. This could
		 * take up to 3000us (3ms) on some devices, so we try
		 * and exit as quickly as possible.
		 */
		status = 0;
		for (i=0 ; i<24 ; i++) {
			/* Delay for 125us */
			udelay (125);

			/* Check the status */
			nand_command (mtd, NAND_CMD_STATUS, -1, -1);
			status = (int) readb (this->IO_ADDR);
			if (status & 0x40)
				break;
		}

		/* See if device thinks it succeeded */
		if (status & 0x01) {
			DEBUG (MTD_DEBUG_LEVEL0,
				"nand_writev: " \
				"Failed write, page 0x%08x, " \
				"%6i bytes were succesful\n", page, *retlen);
			nand_deselect ();
			spin_lock_bh (&this->chip_lock);
			this->state = FL_READY;
			wake_up (&this->wq);
			spin_unlock_bh (&this->chip_lock);
			return -EIO;
		}

#ifdef CONFIG_MTD_NAND_VERIFY_WRITE
		/*
		 * The NAND device assumes that it is always writing to
		 * a cleanly erased page. Hence, it performs its internal
		 * write verification only on bits that transitioned from
		 * 1 to 0. The device does NOT verify the whole page on a
		 * byte by byte basis. It is possible that the page was
		 * not completely erased or the page is becoming unusable
		 * due to wear. The read with ECC would catch the error
		 * later when the ECC page check fails, but we would rather
		 * catch it early in the page write stage. Better to write
		 * no data than invalid data.
		 */
		
		/* Send command to read back the page */
		if (col < mtd->eccsize)
			nand_command (mtd, NAND_CMD_READ0, col, page);
		else
			nand_command (mtd, NAND_CMD_READ1, col - 256, page);

		/* Loop through and verify the data */
		for (i=col ; i < cnt ; i++) {
			if (this->data_buf[i] != readb (this->IO_ADDR)) {
				DEBUG (MTD_DEBUG_LEVEL0,
					"nand_writev: " \
					"Failed write verify, page 0x%08x, " \
					"%6i bytes were succesful\n",
					page, *retlen);
				nand_deselect ();
				spin_lock_bh (&this->chip_lock);
				this->state = FL_READY;
				wake_up (&this->wq);
				spin_unlock_bh (&this->chip_lock);
				return -EIO;
			}
		}

#ifdef CONFIG_MTD_NAND_ECC
		/*
		 * We also want to check that the ECC bytes wrote
		 * correctly for the same reasons stated above.
		 */
		nand_command (mtd, NAND_CMD_READOOB, 0x00, page);
		for (i=0 ; i < ecc_bytes ; i++) {
			if ((readb (this->IO_ADDR) != this->ecc_code_buf[i]) &&
					this->ecc_code_buf[i]) {
				DEBUG (MTD_DEBUG_LEVEL0,
					"nand_writev: Failed ECC write " \
					"verify, page 0x%08x, " \
					"%6i bytes were succesful\n",
					page, i);
				nand_deselect ();
				spin_lock_bh (&this->chip_lock);
				this->state = FL_READY;
				wake_up (&this->wq);
				spin_unlock_bh (&this->chip_lock);
				return -EIO;
			}
		}
#endif

#endif
		/* Update written bytes count */
		*retlen += (cnt - col);

		/* Reset written byte counter and column */
		col = cnt = 0;

		/* Increment page address */
		page++;
	}

	/* De-select the NAND device */
	nand_deselect ();

	/* Wake up anyone waiting on the device */
	spin_lock_bh (&this->chip_lock);
	this->state = FL_READY;
	wake_up (&this->wq);
	spin_unlock_bh (&this->chip_lock);

	/* Return happy */
	return 0;
}

/*
 * NAND erase a block
 */
static int nand_erase (struct mtd_info *mtd, struct erase_info *instr)
{
	int i, page, len, status, pages_per_block;
	struct nand_chip *this = mtd->priv;
	DECLARE_WAITQUEUE(wait, current);

	DEBUG (MTD_DEBUG_LEVEL3,
		"nand_erase: start = 0x%08x, len = %i\n",
		(unsigned int) instr->addr, (unsigned int) instr->len);

	/* Start address must align on block boundary */
	if (instr->addr & (mtd->erasesize - 1)) {
		DEBUG (MTD_DEBUG_LEVEL0,
			"nand_erase: Unaligned address\n");
		return -EINVAL;
	}

	/* Length must align on block boundary */
	if (instr->len & (mtd->erasesize - 1)) {
		DEBUG (MTD_DEBUG_LEVEL0,
			"nand_erase: Length not block aligned\n");
		return -EINVAL;
	}

	/* Do not allow erase past end of device */
	if ((instr->len + instr->addr) > mtd->size) {
		DEBUG (MTD_DEBUG_LEVEL0,
			"nand_erase: Erase past end of device\n");
		return -EINVAL;
	}

retry:
	/* Grab the lock and see if the device is available */
	spin_lock_bh (&this->chip_lock);

	switch (this->state) {
	case FL_READY:
		this->state = FL_ERASING;
		break;

	default:
		set_current_state (TASK_UNINTERRUPTIBLE);
		add_wait_queue (&this->wq, &wait);
		spin_unlock_bh (&this->chip_lock);
		schedule();

		remove_wait_queue (&this->wq, &wait);
		goto retry;
	};

	/* Shift to get first page */
	page = (int) (instr->addr >> this->page_shift);

	/* Calculate pages in each block */
	pages_per_block = mtd->erasesize / mtd->oobblock;

	/* Select the NAND device */
	nand_select ();

	/* Check the WP bit */
	nand_command (mtd, NAND_CMD_STATUS, -1, -1);
	if (!(readb (this->IO_ADDR) & 0x80)) {
		DEBUG (MTD_DEBUG_LEVEL0,
			"nand_erase: Device is write protected!!!\n");
		nand_deselect ();
		this->state = FL_READY;
		spin_unlock_bh (&this->chip_lock);
		return -EIO;
	}

	/* Loop through the pages */
	len = instr->len;
	while (len) {
		/* Send commands to erase a page */
		nand_command(mtd, NAND_CMD_ERASE1, -1, page);
		nand_command(mtd, NAND_CMD_ERASE2, -1, -1);

		/*
		 * Wait for program operation to complete. This could
		 * take up to 4000us (4ms) on some devices, so we try
		 * and exit as quickly as possible.
		 */
		status = 0;
		for (i=0 ; i<32 ; i++) {
			/* Delay for 125us */
			udelay (125);

			/* Check the status */
			nand_command (mtd, NAND_CMD_STATUS, -1, -1);
			status = (int) readb (this->IO_ADDR);
			if (status & 0x40)
				break;
		}

		/* See if block erase succeeded */
		if (status & 0x01) {
			DEBUG (MTD_DEBUG_LEVEL0,
				"nand_erase: " \
				"Failed erase, page 0x%08x\n", page);
			nand_deselect ();
			this->state = FL_READY;
			spin_unlock_bh (&this->chip_lock);
			return -EIO;
		}

		/* Increment page address and decrement length */
		len -= mtd->erasesize;
		page += pages_per_block;

		/* Release the spin lock */
		spin_unlock_bh (&this->chip_lock);

erase_retry:
		/* Check the state and sleep if it changed */
		spin_lock_bh (&this->chip_lock);
		if (this->state == FL_ERASING) {
			continue;
		}
		else {
			set_current_state (TASK_UNINTERRUPTIBLE);
			add_wait_queue (&this->wq, &wait);
			spin_unlock_bh (&this->chip_lock);
			schedule();

			remove_wait_queue (&this->wq, &wait);
			goto erase_retry;
		}
	}
	spin_unlock_bh (&this->chip_lock);

	/* De-select the NAND device */
	nand_deselect ();

	/* Do call back function */
	if (instr->callback)
		instr->callback (instr);

	/* The device is ready */
	spin_lock_bh (&this->chip_lock);
	this->state = FL_READY;
	spin_unlock_bh (&this->chip_lock);

	/* Return happy */
	return 0;
}

/*
 * NAND sync
 */
static void nand_sync (struct mtd_info *mtd)
{
	struct nand_chip *this = mtd->priv;
	DECLARE_WAITQUEUE(wait, current);

	DEBUG (MTD_DEBUG_LEVEL3, "nand_sync: called\n");

retry:
	/* Grab the spinlock */
	spin_lock_bh(&this->chip_lock);

	/* See what's going on */
	switch(this->state) {
	case FL_READY:
	case FL_SYNCING:
		this->state = FL_SYNCING;
		spin_unlock_bh (&this->chip_lock);
		break;

	default:
		/* Not an idle state */
		add_wait_queue (&this->wq, &wait);
		spin_unlock_bh (&this->chip_lock);
		schedule ();

		remove_wait_queue (&this->wq, &wait);
		goto retry;
	}

        /* Lock the device */
	spin_lock_bh (&this->chip_lock);

	/* Set the device to be ready again */
	if (this->state == FL_SYNCING) {
		this->state = FL_READY;
		wake_up (&this->wq);
	}

        /* Unlock the device */
	spin_unlock_bh (&this->chip_lock);
}

/*
 * Scan for the NAND device
 */
int nand_scan (struct mtd_info *mtd)
{
	int i, nand_maf_id, nand_dev_id;
	struct nand_chip *this = mtd->priv;

	/* Select the device */
	nand_select ();

	/* Send the command for reading device ID */
	nand_command (mtd, NAND_CMD_READID, 0x00, -1);

	/* Read manufacturer and device IDs */
	nand_maf_id = readb (this->IO_ADDR);
	nand_dev_id = readb (this->IO_ADDR);

	/* Print and store flash device information */
	for (i = 0; nand_flash_ids[i].name != NULL; i++) {
		if (nand_maf_id == nand_flash_ids[i].manufacture_id &&
		    nand_dev_id == nand_flash_ids[i].model_id) {
			if (!mtd->size) {
				mtd->name = nand_flash_ids[i].name;
				mtd->erasesize = nand_flash_ids[i].erasesize;
				mtd->size = (1 << nand_flash_ids[i].chipshift);
				mtd->eccsize = 256;
				if (nand_flash_ids[i].page256) {
					mtd->oobblock = 256;
					mtd->oobsize = 8;
					this->page_shift = 8;
				}
				else {
					mtd->oobblock = 512;
					mtd->oobsize = 16;
					this->page_shift = 9;
				}
			}
			printk (KERN_INFO "NAND device: Manufacture ID:" \
				" 0x%02x, Chip ID: 0x%02x (%s)\n",
			       nand_maf_id, nand_dev_id, mtd->name);
			break;
		}
	}

	/* Initialize state and spinlock */
	this->state = FL_READY;
	spin_lock_init(&this->chip_lock);

	/* De-select the device */
	nand_deselect ();

	/* Print warning message for no device */
	if (!mtd->size) {
		printk (KERN_WARNING "No NAND device found!!!\n");
		return 1;
	}

	/* Fill in remaining MTD driver data */
	mtd->type = MTD_NANDFLASH;
	mtd->flags = MTD_CAP_NANDFLASH | MTD_ECC;
	mtd->module = THIS_MODULE;
	mtd->ecctype = MTD_ECC_SW;
	mtd->erase = nand_erase;
	mtd->point = NULL;
	mtd->unpoint = NULL;
	mtd->read = nand_read;
	mtd->write = nand_write;
	mtd->read_ecc = nand_read_ecc;
	mtd->write_ecc = nand_write_ecc;
	mtd->read_oob = nand_read_oob;
	mtd->write_oob = nand_write_oob;
	mtd->readv = NULL;
	mtd->writev = nand_writev;
	mtd->sync = nand_sync;
	mtd->lock = NULL;
	mtd->unlock = NULL;
	mtd->suspend = NULL;
	mtd->resume = NULL;

	/* Return happy */
	return 0;
}

EXPORT_SYMBOL(nand_scan);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Steven J. Hill <sjhill@cotw.com");
MODULE_DESCRIPTION("Generic NAND flash driver code");
