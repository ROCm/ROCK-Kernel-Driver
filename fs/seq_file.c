/*
 * linux/fs/seq_file.c
 *
 * helper functions for making syntetic files from sequences of records.
 * initial implementation -- AV, Oct 2001.
 */

#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include <asm/uaccess.h>

/**
 *	seq_open -	initialize sequential file
 *	@file: file we initialize
 *	@op: method table describing the sequence
 *
 *	seq_open() sets @file, associating it with a sequence described
 *	by @op.  @op->start() sets the iterator up and returns the first
 *	element of sequence. @op->stop() shuts it down.  @op->next()
 *	returns the next element of sequence.  @op->show() prints element
 *	into the buffer.  In case of error ->start() and ->next() return
 *	ERR_PTR(error).  In the end of sequence they return %NULL. ->show()
 *	returns 0 in case of success and negative number in case of error.
 */
int seq_open(struct file *file, struct seq_operations *op)
{
	struct seq_file *p = kmalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	memset(p, 0, sizeof(*p));
	sema_init(&p->sem, 1);
	p->op = op;
	file->private_data = p;
	return 0;
}

/**
 *	seq_read -	->read() method for sequential files.
 *	@file, @buf, @size, @ppos: see file_operations method
 *
 *	Ready-made ->f_op->read()
 */
ssize_t seq_read(struct file *file, char *buf, size_t size, loff_t *ppos)
{
	struct seq_file *m = (struct seq_file *)file->private_data;
	size_t copied = 0;
	loff_t pos;
	size_t n;
	void *p;
	int err = 0;

	if (ppos != &file->f_pos)
		return -EPIPE;

	down(&m->sem);
	/* grab buffer if we didn't have one */
	if (!m->buf) {
		m->buf = kmalloc(m->size = PAGE_SIZE, GFP_KERNEL);
		if (!m->buf)
			goto Enomem;
	}
	/* if not empty - flush it first */
	if (m->count) {
		n = min(m->count, size);
		err = copy_to_user(buf, m->buf + m->from, n);
		if (err)
			goto Efault;
		m->count -= n;
		m->from += n;
		size -= n;
		buf += n;
		copied += n;
		if (!m->count)
			m->index++;
		if (!size)
			goto Done;
	}
	/* we need at least one record in buffer */
	while (1) {
		pos = m->index;
		p = m->op->start(m, &pos);
		err = PTR_ERR(p);
		if (!p || IS_ERR(p))
			break;
		err = m->op->show(m, p);
		if (err)
			break;
		if (m->count < m->size)
			goto Fill;
		m->op->stop(m, p);
		kfree(m->buf);
		m->buf = kmalloc(m->size <<= 1, GFP_KERNEL);
		if (!m->buf)
			goto Enomem;
	}
	m->op->stop(m, p);
	goto Done;
Fill:
	/* they want more? let's try to get some more */
	while (m->count < size) {
		size_t offs = m->count;
		loff_t next = pos;
		p = m->op->next(m, p, &next);
		if (!p || IS_ERR(p)) {
			err = PTR_ERR(p);
			break;
		}
		err = m->op->show(m, p);
		if (err || m->count == m->size) {
			m->count = offs;
			break;
		}
		pos = next;
	}
	m->op->stop(m, p);
	n = min(m->count, size);
	err = copy_to_user(buf, m->buf, n);
	if (err)
		goto Efault;
	copied += n;
	m->count -= n;
	if (m->count)
		m->from = n;
	else
		pos++;
	m->index = pos;
Done:
	if (!copied)
		copied = err;
	else
		*ppos += copied;
	up(&m->sem);
	return copied;
Enomem:
	err = -ENOMEM;
	goto Done;
Efault:
	err = -EFAULT;
	goto Done;
}

static int traverse(struct seq_file *m, loff_t offset)
{
	loff_t pos = 0;
	int error = 0;
	void *p;

	m->index = 0;
	m->count = m->from = 0;
	if (!offset)
		return 0;
	if (!m->buf) {
		m->buf = kmalloc(m->size = PAGE_SIZE, GFP_KERNEL);
		if (!m->buf)
			return -ENOMEM;
	}
	p = m->op->start(m, &m->index);
	while (p) {
		error = PTR_ERR(p);
		if (IS_ERR(p))
			break;
		error = m->op->show(m, p);
		if (error)
			break;
		if (m->count == m->size)
			goto Eoverflow;
		if (pos + m->count > offset) {
			m->from = offset - pos;
			m->count -= m->from;
			break;
		}
		pos += m->count;
		m->count = 0;
		if (pos == offset) {
			m->index++;
			break;
		}
		p = m->op->next(m, p, &m->index);
	}
	m->op->stop(m, p);
	return error;

Eoverflow:
	m->op->stop(m, p);
	kfree(m->buf);
	m->buf = kmalloc(m->size <<= 1, GFP_KERNEL);
	return !m->buf ? -ENOMEM : -EAGAIN;
}

/**
 *	seq_lseek -	->llseek() method for sequential files.
 *	@file, @offset, @origin: see file_operations method
 *
 *	Ready-made ->f_op->llseek()
 */
loff_t seq_lseek(struct file *file, loff_t offset, int origin)
{
	struct seq_file *m = (struct seq_file *)file->private_data;
	long long retval = -EINVAL;

	down(&m->sem);
	switch (origin) {
		case 1:
			offset += file->f_pos;
		case 0:
			if (offset < 0)
				break;
			retval = offset;
			if (offset != file->f_pos) {
				while ((retval=traverse(m, offset)) == -EAGAIN)
					;
				if (retval) {
					/* with extreme perjudice... */
					file->f_pos = 0;
					m->index = 0;
					m->count = 0;
				} else {
					retval = file->f_pos = offset;
				}
			}
	}
	up(&m->sem);
	return retval;
}

/**
 *	seq_release -	free the structures associated with sequential file.
 *	@file: file in question
 *	@inode: file->f_dentry->d_inode
 *
 *	Frees the structures associated with sequential file; can be used
 *	as ->f_op->release() if you don't have private data to destroy.
 */
int seq_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = (struct seq_file *)file->private_data;
	kfree(m->buf);
	kfree(m);
	return 0;
}

/**
 *	seq_escape -	print string into buffer, escaping some characters
 *	@m:	target buffer
 *	@s:	string
 *	@esc:	set of characters that need escaping
 *
 *	Puts string into buffer, replacing each occurence of character from
 *	@esc with usual octal escape.  Returns 0 in case of success, -1 - in
 *	case of overflow.
 */
int seq_escape(struct seq_file *m, const char *s, const char *esc)
{
	char *end = m->buf + m->size;
        char *p;
	char c;

        for (p = m->buf + m->count; (c = *s) != '\0' && p < end; s++) {
		if (!strchr(esc, c)) {
			*p++ = c;
			continue;
		}
		if (p + 3 < end) {
			*p++ = '\\';
			*p++ = '0' + ((c & 0300) >> 6);
			*p++ = '0' + ((c & 070) >> 3);
			*p++ = '0' + (c & 07);
			continue;
		}
		m->count = m->size;
		return -1;
        }
	m->count = p - m->buf;
        return 0;
}

int seq_printf(struct seq_file *m, const char *f, ...)
{
	va_list args;
	int len;

	if (m->count < m->size) {
		va_start(args, f);
		len = vsnprintf(m->buf + m->count, m->size - m->count, f, args);
		va_end(args);
		if (m->count + len < m->size) {
			m->count += len;
			return 0;
		}
	}
	m->count = m->size;
	return -1;
}
