/*
 * IBM Dynamic Probes
 * Copyright (c) International Business Machines Corp., 2000
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or 
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  
 */

#include <linux/dprobes.h>
#include <linux/dprobes_in.h>
#include <asm/io.h>
#include <asm/uaccess.h>

static void dispatch_ex(void);
static void gen_ex(unsigned long, unsigned long, unsigned long);
static void log_st(void);

void dprobes_interpreter_code_start(void) { }

/*
 * RPN stack manipulation
 * Pushes a 32 bit number onto RPN stack.
 */
static inline void rpnpush(unsigned long arg)
{
	dprobes.rpn_tos--;
	dprobes.rpn_tos &= RPN_STACK_SIZE - 1;
	dprobes.rpn_stack[dprobes.rpn_tos] = arg;
}

/*
 * Pops a 32 bit number from RPN stack.
 */
static inline unsigned long rpnpop(void)
{
	unsigned long ret = dprobes.rpn_stack[dprobes.rpn_tos];
	dprobes.rpn_tos++;
	dprobes.rpn_tos &= RPN_STACK_SIZE - 1;
	return ret;
}

/*
 * Returns a 32 bit number for RPN stack at a given position
 * relative to RPN TOS, without poping.
 */
static unsigned long rpnmove(unsigned long pos)
{
	unsigned long index = dprobes.rpn_tos + pos;
	index &= RPN_STACK_SIZE - 1;
	return dprobes.rpn_stack[index];
}

/*
 * Returns a byte(8 bit) operand from rpn code array
 */
static inline u8 get_u8_oprnd(void)
{
	return *dprobes.rpn_ip++;
}


/*
 * Returns a word(16 bit) operand from rpn code array
 */
static inline u16 get_u16_oprnd(void)
{
	u16 w;
	w = *((u16 *) (dprobes.rpn_ip));
	dprobes.rpn_ip += sizeof(u16);
	return w;
}

/*
 * Returns an unsigned long operand from rpn code array
 */
static inline unsigned long get_ulong_oprnd(void)
{
	unsigned long dw;
	dw = *((unsigned long *) (dprobes.rpn_ip));
	dprobes.rpn_ip += sizeof(unsigned long);
	return dw;
}

/*
 * Returns a signed long(32 bit) operand from rpn code array
 */
static inline long get_long_oprnd(void)
{
	long l;
	l = *((long *) (dprobes.rpn_ip));
	dprobes.rpn_ip += sizeof(long);
	return l;
}

#ifdef CONFIG_X86
#include "i386/dprobes_in.c"
#endif

#ifdef CONFIG_IA64
#include "ia64/dprobes_in.c"
#endif

/*
 * Jump instructions.
 * Interpreter is terminated when total number of jumps and loops
 * becomes equal to jmpmax.
 */
static void jmp(void)
{
	long offset = get_long_oprnd();
	if (dprobes.jmp_count == dprobes.mod->pgm.jmpmax) {
		gen_ex(EX_MAX_JMPS, dprobes.mod->pgm.jmpmax, 0);
		return;
	}	
	dprobes.jmp_count++;
	dprobes.rpn_ip += offset;
}

#define COND_JMPS(name, condition) \
static void name(void) \
{ \
	long offset = get_long_oprnd(); \
	if ((long) dprobes.rpn_stack[dprobes.rpn_tos] condition 0) { \
		if (dprobes.jmp_count == dprobes.mod->pgm.jmpmax) { \
			gen_ex(EX_MAX_JMPS, dprobes.mod->pgm.jmpmax, 0); \
			return; \
		} \
		dprobes.jmp_count++; \
		dprobes.rpn_ip += offset; \
	} \
}

COND_JMPS(jlt, <)
COND_JMPS(jgt, >)
COND_JMPS(jle, <=)
COND_JMPS(jge, >=)
COND_JMPS(jz, ==)
COND_JMPS(jnz, !=)
	
/*
 * loop label: Decrement RPN TOS and jump to the label
 * if TOS is not equal to 0.
 * Interpreter is terminated when total number of jumps and loops
 * becomes equal to jmpmax.
 */
static void loop(void)
{
	long offset = get_long_oprnd();
	if (dprobes.jmp_count++ == dprobes.mod->pgm.jmpmax) {
		gen_ex(EX_MAX_JMPS, dprobes.mod->pgm.jmpmax, 0);
		return;
 	}
	if ((long) (--dprobes.rpn_stack[dprobes.rpn_tos]) != 0)
		dprobes.rpn_ip += offset;
}

/*
 * Call and ret instructions. Number of nested calls is limited to 32.
 */
static void call(void)
{
	int i;
	struct dprobes_struct *dp = &dprobes;
	long offset = (long)get_long_oprnd();
	unsigned long called_addr = (dp->rpn_ip - dp->rpn_code + offset);
	if (dp->call_tos < CALL_FRAME_SIZE) {
		gen_ex(EX_CALL_STACK_OVERFLOW, NR_NESTED_CALLS, 0);
		return;
	}
	/*
	 * Push the return value(rpn_ip) onto the call stack.
	 */
	dp->call_stack[--dp->call_tos] = dp->rpn_ip - dp->rpn_code;
	dp->rpn_ip += offset;
	/* 
	 * push the called subroutine address, default exception handler
	 * address, and initialize pv, lv and gv range to zeros.
	 */  		
	dp->call_stack[--dp->call_tos] = called_addr;
	dp->call_stack[--dp->call_tos] = EX_NO_HANDLER;

	for (i = 0; i < CALL_FRAME_SIZE - 4; i++)
		dp->call_stack[--dp->call_tos] = 0;
	/* Save the rpn stack base pointer */
	dp->call_stack[--dp->call_tos] = dp->rpn_sbp;	
}

/*
 * Pops the return value into rpn_ip.
 */
static void ret(void)
{
	struct dprobes_struct *dp = &dprobes;
	if (dp->call_tos >= CALL_STACK_SIZE - CALL_FRAME_SIZE) {
		gen_ex(EX_CALL_STACK_OVERFLOW, 0, 0);
		return;
	}
	dp->rpn_sbp = dp->call_stack[dp->call_tos++];
	dp->call_tos += CALL_FRAME_SIZE - 2;
	/* pop the return address */
	dp->rpn_ip = dp->rpn_code + dp->call_stack[dp->call_tos++];

	if (dp->ex_pending) {
		dp->ex_hand = dp->call_stack[dp->call_tos + 
				OFFSET_EX_HANDLER];
		dispatch_ex();
	}
}

/*
 * Exception handling instructions.
 */
static void sx(void)
{
	long offset = get_long_oprnd();
	dprobes.call_stack[dprobes.call_tos + OFFSET_EX_HANDLER] = 
				(long)dprobes.rpn_ip + offset;
}
	
static void ux(void)
{
	dprobes.call_stack[dprobes.call_tos + OFFSET_EX_HANDLER] = 
							EX_NO_HANDLER;
}

static void dispatch_ex(void)
{
	struct dprobes_struct *dp = &dprobes;
	long ex_handler;
	/* We are at the outermost routine & no exception handler, terminate */
	if ((dp->call_tos == CALL_STACK_SIZE - CALL_FRAME_SIZE) && 
		(dp->call_stack[dp->call_tos + OFFSET_EX_HANDLER] == 
			EX_NO_HANDLER)) {
		dp->status &= ~DP_STATUS_INTERPRETER;
		return;
	}
	ex_handler = dp->ex_hand;
	if (ex_handler != EX_NO_HANDLER) {
		dp->ex_pending = 0;
		dp->call_stack[dp->call_tos + OFFSET_EX_HANDLER] = 
					EX_NO_HANDLER;
		dp->rpn_ip = (byte_t *)ex_handler;
	}
	else {
		ret();
	}
}

static void rx(void)
{
	struct dprobes_struct *dp = &dprobes;
	dp->ex_code = rpnmove(0);
	dp->ex_parm1 = rpnmove(1);
	dp->ex_parm2 = rpnmove(2);
	dp->ex_pending = 1;
	dp->ex_hand = dp->call_stack[dp->call_tos + 
					OFFSET_EX_HANDLER];
	if (dp->rec->mod->pgm.autostacktrace)
		log_st();
	dispatch_ex();
}

static void push_x(void)
{
	rpnpush(dprobes.ex_parm2);
	rpnpush(dprobes.ex_parm1);
	rpnpush(dprobes.ex_code);
}

static void gen_ex(unsigned long ex_code, unsigned long parm1, 
		unsigned long parm2)
{
	struct dprobes_struct *dp = &dprobes;
        if (ex_code & dp->rec->point.ex_mask) {
		dp->ex_code = ex_code;
		dp->ex_parm1 = parm1;
		dp->ex_parm2 = parm2;
		rpnpush(parm2);
		rpnpush(parm1);
		rpnpush(ex_code);
		dp->ex_pending = 1;
		dp->ex_hand = dp->call_stack[dp->call_tos + 
				OFFSET_EX_HANDLER];
		if (dp->rec->mod->pgm.autostacktrace) {
			log_st();
		}
                dispatch_ex();
        } else if (ex_code & EX_NON_MASKABLE_EX) {
		dp->ex_hand = dp->call_stack[dp->call_tos + 
				OFFSET_EX_HANDLER];
		if (dp->rec->mod->pgm.autostacktrace)
			log_st();
                dp->status &= ~DP_STATUS_INTERPRETER;
        }
}

static int write_st_buffer(unsigned char *ptr, int size)
{
	if (dprobes.ex_log_len + size >= dprobes.mod->pgm.ex_logmax)
		return -1;
	else {
		memcpy(dprobes.ex_log + dprobes.ex_log_len, ptr, size);
		dprobes.ex_log_len += size;
		return 0;
	}
}

static int log_gv_entries(unsigned long call_tos)
{
	struct dprobes_struct *dp = &dprobes;
	unsigned char gv_prefix[PREFIX_SIZE] = {TOKEN_GV_ENTRY, 0, 0};
	unsigned long range = dp->call_stack[call_tos + OFFSET_GV_RANGE];
	unsigned long index = dp->call_stack[call_tos + OFFSET_GV_RANGE + 1];
	int i;
	dp->ex_off.gv = dp->ex_log_len + 1;
	if (write_st_buffer(gv_prefix, PREFIX_SIZE))
		return -1;
	for (i = index; i < index + range; i++) {
		if (i >= dp_num_gv) {
			dp->status &= ~DP_STATUS_INTERPRETER;
			return -1;
		}
		read_lock(&dp_gv_lock);
		if (write_st_buffer((unsigned char *)(dp_gv + i), sizeof(unsigned long))) {
			read_unlock(&dp_gv_lock);
			return -1;
		}
		read_unlock(&dp_gv_lock);
		(*(unsigned short *)(dp->ex_log + dp->ex_off.gv))++;
	}
	return 0;
}

static int log_lv_entries(unsigned long call_tos)
{
	struct dprobes_struct *dp = &dprobes;
	unsigned char lv_prefix[PREFIX_SIZE] = {TOKEN_LV_ENTRY, 0, 0};
	unsigned long range = dp->call_stack[call_tos + OFFSET_LV_RANGE];
	unsigned long index = dp->call_stack[call_tos + OFFSET_LV_RANGE + 1];
	int i;
	dp->ex_off.lv = dp->ex_log_len + 1;
	if (write_st_buffer(lv_prefix, PREFIX_SIZE))
		return -1;
	for (i = index; i < index + range; i++) {
		if (i >= dp->rec->mod->pgm.num_lv) {
			dp->status &= ~DP_STATUS_INTERPRETER;
			return -1;
		}
		if (write_st_buffer((unsigned char *)(dp->mod->lv + i), sizeof(unsigned long))) 
			return -1;
		(*(unsigned short *)(dp->ex_log + dp->ex_off.lv))++;
	}
	return 0;
}

static int log_rpnstack_entries(unsigned long call_tos)
{
	struct dprobes_struct *dp = &dprobes;
	unsigned char rpn_prefix[PREFIX_SIZE] = {TOKEN_RPN_ENTRY, 0, 0};
	unsigned long range = dp->call_stack[call_tos + 
						OFFSET_RPN_STACK_RANGE];
	unsigned long index = dp->call_stack[call_tos + 
						OFFSET_RPN_STACK_RANGE + 1];
	int i, j;
	dp->ex_off.rpn = dp->ex_log_len + 1;
	if (write_st_buffer(rpn_prefix, PREFIX_SIZE))
		return -1;
	for (i = j = index; i < index + range; i++, j++) {
		j &= (RPN_STACK_SIZE - 1);
		if (write_st_buffer((unsigned char *)(dp->rpn_stack + j), 
				sizeof(unsigned long)))
			return -1;
		(*(unsigned short *)(dp->ex_log + dp->ex_off.rpn))++;
	}	
	return 0;
}

static int log_stackframes(void)
{
	struct dprobes_struct *dp = &dprobes;
	unsigned long c_tos = dp->call_tos;
	unsigned char sf_prefix[PREFIX_SIZE] = {TOKEN_STACK_FRAME, 0, 0};
	dp->ex_off.sf = dp->ex_log_len + 1;
	if (write_st_buffer(sf_prefix, PREFIX_SIZE))
		return -1;
	
	while (c_tos < (CALL_STACK_SIZE - CALL_FRAME_SIZE)) {
		if (write_st_buffer((unsigned char *)(dp->call_stack + 
			c_tos + OFFSET_CALLED_ADDR), sizeof(unsigned long)))
			return -1;
		if (write_st_buffer((unsigned char *)(dp->call_stack +
			c_tos + OFFSET_RETURN_ADDR), sizeof(unsigned long)))
			return -1;

		if (log_rpnstack_entries(c_tos))
			return -1;
		if (log_lv_entries(c_tos))
			return -1;
		if (log_gv_entries(c_tos))
			return -1;

		(*(short *)(dp->ex_log + dp->ex_off.sf))++;
		c_tos += CALL_FRAME_SIZE;
	}
	return 0;
}

static int log_stacktrace(void)
{
	struct dprobes_struct *dp = &dprobes;
	long ex_handler;
	if (write_st_buffer((unsigned char *)&dp->ex_code, sizeof(unsigned long)))
		return -1;
	ex_handler = dp->ex_hand;
	if (ex_handler != EX_NO_HANDLER)
		ex_handler -= (long)dp->rpn_code;
	if (write_st_buffer((unsigned char *)&ex_handler, sizeof(long)))
		return -1;
	if (write_st_buffer((unsigned char *)&dp->ex_parm1, sizeof(unsigned long)))
		return -1;
	if (write_st_buffer((unsigned char *)&dp->ex_parm2, sizeof(unsigned long)))
		return -1;
	if (log_stackframes())
		return -1;
	(*(short *)(dp->ex_log + dp->ex_off.st))++;
	return 0;
}

static int get_exlog_hdr(void)
{
	unsigned short name_len;
	unsigned char st_prefix[PREFIX_SIZE] = {TOKEN_STACK_TRACE, 0, 0};
	struct dprobes_struct *dp = &dprobes;

	strcpy(dp->ex_log + dp->ex_log_len + PREFIX_SIZE, dp->mod->pgm.name);
	name_len = strlen(dp->mod->pgm.name);
	dp->ex_log[dp->ex_log_len] = TOKEN_ASCII_LOG;
	*(unsigned short *)(dp->ex_log + dp->ex_log_len + 1) = name_len;
	dp->ex_log_len += (name_len + PREFIX_SIZE);

	*(loff_t *)(dp->ex_log + dp->ex_log_len) = 
			dp->rec->point.offset;
	dp->ex_log_len += sizeof(loff_t);
	
	*(unsigned short *)(dp->ex_log + dp->ex_log_len) = 
			dp->mod->pgm.id;
	dp->ex_log_len += sizeof(unsigned short);

	*(unsigned short *)(dp->ex_log + dp->ex_log_len) = 
			dp->major;
	dp->ex_log_len += sizeof(unsigned short);
	
	*(unsigned short *)(dp->ex_log + dp->ex_log_len) = 
			dp->minor;
	dp->ex_log_len += sizeof(unsigned short);

	dp->ex_off.st = dp->ex_log_len + 1;
	if (write_st_buffer(st_prefix, PREFIX_SIZE))
		return -1;
	return 0;
}

static void log_st(void)
{
	if (dprobes.ex_log_len == MIN_ST_SIZE)
		if(get_exlog_hdr()) return;
	log_stacktrace();
}

static void purge_st(void)
{
	dprobes.ex_log_len = MIN_ST_SIZE;
}

static void trace_lv(void)
{
	dprobes.call_stack[dprobes.call_tos + OFFSET_LV_RANGE] = rpnpop();
	dprobes.call_stack[dprobes.call_tos + OFFSET_LV_RANGE + 1] = rpnpop();
}

static void trace_gv(void)
{
	dprobes.call_stack[dprobes.call_tos + OFFSET_GV_RANGE] = rpnpop();
	dprobes.call_stack[dprobes.call_tos + OFFSET_GV_RANGE + 1] = rpnpop();
}

static void trace_pv(void)
{
	dprobes.call_stack[dprobes.call_tos + OFFSET_RPN_STACK_RANGE] = 
								rpnpop();
	dprobes.call_stack[dprobes.call_tos + OFFSET_RPN_STACK_RANGE + 1] =
								rpnpop();
}

static void push_tsp(void)
{
	long index = (long)rpnpop();
	unsigned long tsp_index = ((signed)dprobes.rpn_tos + index) 
					& (RPN_STACK_SIZE - 1);
	rpnpush(dprobes.rpn_stack[tsp_index]);
}

static void pop_tsp(void)
{
	unsigned long value = rpnpop();
	long index = (long)rpnpop();
	unsigned long tsp_index = ((signed)dprobes.rpn_tos + index) 
					& (RPN_STACK_SIZE - 1);
	dprobes.rpn_stack[tsp_index] = value;
}

static void save_sbp(void)
{
	rpnpush(dprobes.rpn_sbp);
}

static void restore_sbp(void)
{
	dprobes.rpn_sbp = rpnpop() & (RPN_STACK_SIZE - 1);
}

static void save_tsp(void)
{
	rpnpush(dprobes.rpn_tos);
}

static void restore_tsp(void)
{
	dprobes.rpn_tos = rpnpop();
}

static void push_sbp(void)
{
	long index = (long)rpnpop();
	unsigned long sbp_index = ((signed)dprobes.rpn_sbp + index) 
					& (RPN_STACK_SIZE - 1);
	rpnpush(dprobes.rpn_stack[sbp_index]);
}

static void pop_sbp(void)
{
	unsigned long value = rpnpop();
	long index = (long)rpnpop();
	unsigned long sbp_index = ((signed)dprobes.rpn_sbp + index) 
					& (RPN_STACK_SIZE - 1);
	dprobes.rpn_stack[sbp_index] = value;
}

static void push_stp(void)
{
	rpnpush(dprobes.ex_log_len);
}

static void pop_stp(void)
{
	dprobes.ex_log_len = rpnpop();
}

static void push_sbp_i(void)
{
	s16 index = (s16)get_u16_oprnd();
	unsigned long sbp_index = ((signed)dprobes.rpn_sbp + index) 
					& (RPN_STACK_SIZE - 1);
	rpnpush(dprobes.rpn_stack[sbp_index]);
}

static void pop_sbp_i(void)
{
	s16 index = (s16)get_u16_oprnd();
	unsigned long sbp_index = ((signed)dprobes.rpn_sbp + index) 
					& (RPN_STACK_SIZE - 1);
	dprobes.rpn_stack[sbp_index] = rpnpop();
}

static void push_tsp_i(void)
{
	s16 index = (s16)get_u16_oprnd();
	unsigned long tsp_index = ((signed)dprobes.rpn_tos + index) 
					& (RPN_STACK_SIZE - 1);
	rpnpush(dprobes.rpn_stack[tsp_index]);
}

static void pop_tsp_i(void)
{
	s16 index = (s16)get_u16_oprnd();
	unsigned long tsp_index = ((signed)dprobes.rpn_tos + index) 
					& (RPN_STACK_SIZE - 1);
	dprobes.rpn_stack[tsp_index] = rpnpop();
}

static void copy_sbp(void)
{
	long index = (long)rpnpop();
	unsigned long sbp_index = ((signed)dprobes.rpn_sbp + index) 
					& (RPN_STACK_SIZE - 1);
	dprobes.rpn_stack[sbp_index] = dprobes.rpn_stack[dprobes.rpn_tos];
}

static void copy_sbp_i(void)
{
	s16 index = (s16)get_u16_oprnd();
	unsigned long sbp_index = ((signed)dprobes.rpn_sbp + index) 
					& (RPN_STACK_SIZE - 1);
	dprobes.rpn_stack[sbp_index] = dprobes.rpn_stack[dprobes.rpn_tos];
}

static void copy_tsp(void)
{
	long index = (long)rpnpop();
	unsigned long tsp_index = ((signed)dprobes.rpn_tos + index) 
					& (RPN_STACK_SIZE - 1);
	dprobes.rpn_stack[tsp_index] = dprobes.rpn_stack[dprobes.rpn_tos];
}

static void copy_tsp_i(void)
{
	s16 index = (s16)get_u16_oprnd();
	unsigned long tsp_index = ((signed)dprobes.rpn_tos + index) 
					& (RPN_STACK_SIZE - 1);
	dprobes.rpn_stack[tsp_index] = dprobes.rpn_stack[dprobes.rpn_tos];
}

/* 
 * Normal exit from the interpreter. 
 */
static inline void dp_exit(void)
{
	dprobes.status &= ~DP_STATUS_INTERPRETER;
}

/*
 * Exit the interpreter without producing the log.
 */
static inline void dp_abort(void)
{
	dprobes.status |= DP_STATUS_ABORT;
	dprobes.status &= ~DP_STATUS_INTERPRETER;
}

static inline void dp_remove(void)
{
	dprobes.rec->status |= DP_REC_STATUS_REMOVED;
	dprobes.status &= ~DP_STATUS_INTERPRETER;
}

static inline void no_op(void)
{
}

/*
 * Stores the fault record in the log buffer, when logging is
 * terminated due to invalid memory access.
 * Fault record comprises of a token byte, which indicates the reason for 
 * fault; an unsigned short length indicating how many bytes are logged, 
 * which is 4; and the 4 logged bytes indicate the faulting address.
 *
 * Token byte -1: fault occurred while reading the memory, faulting address
 * indicates the virtual(flat) address which caused the fault.
 * Token byte -2: fault occurred when converting the segmented address to
 * flat address. The fault address stored will indicate the selector of
 * the segmented address.
 * Note that the fault record will be stored only if sufficient space
 * is available to store the entire fault record.
 *
 * The fault record will be stored when fault occurs because of the
 * following instructions:
 * 	log mrf
 *	log str
 */
static void store_fault_record(byte_t token, unsigned long addr, 
	unsigned long ex_code)
{
	struct dprobes_struct *dp = &dprobes;
	if (dp->log_len + 7 <= dp->mod->pgm.logmax) {
		dp->log[dp->log_len++] = token;
		*((unsigned short *)(dp->log + dp->log_len)) = 4;
		dp->log_len += sizeof(unsigned short);
		*((unsigned long *)(dp->log + dp->log_len)) = addr;
		dp->log_len += sizeof(unsigned long);
		gen_ex(ex_code, addr, 0);
	}
	else
		gen_ex(EX_LOG_OVERFLOW, dp->mod->pgm.logmax, 0);
	return;
}

/*
 * Logging group
 */
static inline void push_lp(void) 
{
	rpnpush(dprobes.log_len);
}

static inline void push_plp(void) 
{
	rpnpush(dprobes.prev_log_len);
}

static inline void pop_lp(void) 
{
	dprobes.log_len = rpnpop();
}

static void 
log_ascii(unsigned long len, u8 *faddr)
{
	struct dprobes_struct *dp = &dprobes;
	u8 b;
	dp->prev_log_len = dp->log_len;
	if (dp->log_len + len + PREFIX_SIZE > dp->mod->pgm.logmax) {
		len = dp->mod->pgm.logmax - dp->log_len;
		if ((signed)len < 0) {
			gen_ex(EX_LOG_OVERFLOW, dp->mod->pgm.logmax, 0);
			return;
		}
	}
	dp->log_len += PREFIX_SIZE; /* reserve for prefix */
	while (len--) {
		if (!dp_intr_copy_from_user(&b, (void *)faddr, 1)) {
			if(b) {
				faddr++;
				dp->log[dp->log_len++] = b;
			} else 
				break;

		} else {
			dp->log_len = dp->prev_log_len;
			store_fault_record(TOKEN_MEMORY_FAULT, 
					(unsigned long)faddr, EX_INVALID_ADDR);
			return;
		}
	}
	if (b && (signed)len >= 0) {
		dp->status |= DP_STATUS_LOG_OVERFLOW;
		dp->log_len = dp->prev_log_len;
		gen_ex(EX_LOG_OVERFLOW, dp->mod->pgm.logmax, 0);
		return;
	}
	dp->log[dp->prev_log_len] = TOKEN_ASCII_LOG;
	*(unsigned short *) (dp->log + dp->prev_log_len + 1) =
		(unsigned short) (dp->log_len - dp->prev_log_len - PREFIX_SIZE);
	dp->prev_log_len = dp->log_len;
	return;
}

static void
log_memory(unsigned long len, u8 *faddr)
{
	struct dprobes_struct *dp = &dprobes;
	unsigned long num_remain;
	dp->prev_log_len = dp->log_len;
	if (dp->log_len + len + PREFIX_SIZE > dp->mod->pgm.logmax) {
		dp->status |= DP_STATUS_LOG_OVERFLOW;
		len = dp->mod->pgm.logmax - dp->log_len - PREFIX_SIZE;
		if ((signed)len < 0) {
			gen_ex(EX_LOG_OVERFLOW, dp->mod->pgm.logmax, 0);
			return;
		}
	}

	if ((num_remain = dp_intr_copy_from_user(
		(dp->log + dp->log_len + PREFIX_SIZE), faddr, len))) {
		store_fault_record(TOKEN_MEMORY_FAULT, 
		(unsigned long)(faddr+len-num_remain), EX_INVALID_ADDR);
		return;
	}

	dp->log[dp->log_len++] = TOKEN_MEMORY_LOG;
	*((unsigned short *)(dp->log + dp->log_len)) = len;
	dp->log_len += (len + sizeof(unsigned short));
	dp->prev_log_len = dp->log_len;
	return;
}

/* log str pops the flat address and length from
 * the RPN stack. It tries to copy length number of
 * bytes from flat address to the log buffer.
 * Logging is discontinued if a NULL byte is encountered.
 * NULL byte is not logged. If the log buffer becomes full
 * LOG_OVERFLOW exception is generated.
 * If the log is successful, the logged string is prefixed
 * by a token byte of 1 and a word indicating the length of
 * the string logged.
 * INVALID_ADDR exception is generated if the flat address is invalid.
 */ 
static void log_str(void)
{
	u8 *faddr = (u8 *) rpnpop();
	log_ascii(rpnpop(), faddr);
}

/*
 * Log memory at flat range.
 * INVALID_ADDR exception is generated if flat address is invalid.
 * Log terminating conditions apply.
 * token byte = 0.
 */
static void log_mrf(void)
{
        u8 *faddr = (u8 *) rpnpop();
	log_memory(rpnpop(), faddr);
}

/* 
 * Log count number of elements from rpn stack to log buffer.
 */
static void log_i(void)
{
	struct dprobes_struct *dp = &dprobes;
	u16 oprnd = get_u16_oprnd();
	u16 count, i;
	unsigned long size = oprnd * sizeof(unsigned long);
	dp->prev_log_len = dp->log_len;

	if (dp->log_len + size > dp->mod->pgm.logmax) {
		size = dp->mod->pgm.logmax - dp->log_len;
	}
	if ((signed)size < 0) {
		gen_ex(EX_LOG_OVERFLOW, dp->mod->pgm.logmax, 0);
		return;
	}
	i = count = size / sizeof(unsigned long);
	while (i--) {
		*(unsigned long *) (dp->log + dp->log_len) = rpnpop();
		dp->log_len += sizeof(unsigned long);
        }
	if (count < oprnd) {
		gen_ex(EX_LOG_OVERFLOW, dp->mod->pgm.logmax, 0);
		return;
	}
        dp->prev_log_len = dp->log_len;
}

static void log(void)
{
	struct dprobes_struct *dp = &dprobes;
	u16 oprnd = (u16) rpnpop();
	u16 count, i;
	unsigned long size = oprnd * sizeof(unsigned long) + PREFIX_SIZE;
	dp->prev_log_len = dp->log_len;

	if (dp->log_len + size > dp->mod->pgm.logmax) {
		size = dp->mod->pgm.logmax - dp->log_len;
	}
	if ((signed)size < PREFIX_SIZE) {
		gen_ex(EX_LOG_OVERFLOW, dp->mod->pgm.logmax, 0);
		return;
	}
	dp->log_len += PREFIX_SIZE;
	size -= PREFIX_SIZE;
	i = count = size / sizeof(unsigned long);

	while (i--) {
		*(unsigned long *) (dp->log + dp->log_len) = rpnpop();
		dp->log_len += sizeof(unsigned long);
        }

	dp->log[dp->prev_log_len] = TOKEN_LOG;
	*(unsigned short *) (dp->log + dp->prev_log_len + 1) =
		(unsigned short) (count);

	if (count < oprnd) {
		gen_ex(EX_LOG_OVERFLOW, dp->mod->pgm.logmax, 0);
		return;
	}
	dp->prev_log_len = dp->log_len;
}

static void log_lv(void)
{
	struct dprobes_struct *dp = &dprobes;
	unsigned long range = rpnpop();
	unsigned long index = rpnpop();
	unsigned long offset;
	unsigned char lv_prefix[PREFIX_SIZE] = {TOKEN_LV_ENTRY, 0, 0};
	int i;

	if (dp->log_len + PREFIX_SIZE > dp->mod->pgm.logmax) {
		gen_ex(EX_LOG_OVERFLOW, dp->mod->pgm.logmax, 0);
		return;
	}
	memcpy(dp->log + dp->log_len, lv_prefix, PREFIX_SIZE);
	offset = dp->log_len + 1;
	dp->log_len += PREFIX_SIZE;

	for (i = index; i < index + range; i++) {
		if (i >= dp->rec->mod->pgm.num_lv) {
			gen_ex(EX_INVALID_OPERAND, 1, i);
			return;
		}
		if (dp->log_len + sizeof (unsigned long) > dp->mod->pgm.logmax) {
			gen_ex(EX_LOG_OVERFLOW, dp->mod->pgm.logmax, 0);
			return;
		}
		*(unsigned long *) (dp->log + dp->log_len) = 
				*(dp->mod->lv + i);
		dp->log_len += sizeof(unsigned long);
		(*(unsigned short *)(dp->log + offset))++;
	}
	return;
}

static void log_gv(void)
{
	struct dprobes_struct *dp = &dprobes;
	unsigned long range = rpnpop();
	unsigned long index = rpnpop();
	unsigned long offset;
	unsigned char gv_prefix[PREFIX_SIZE] = {TOKEN_GV_ENTRY, 0, 0};
	int i;

	if (dp->log_len + PREFIX_SIZE > dp->mod->pgm.logmax) {
		gen_ex(EX_LOG_OVERFLOW, dp->mod->pgm.logmax, 0);
		return;
	}
	memcpy(dp->log + dp->log_len, gv_prefix, PREFIX_SIZE);
	offset = dp->log_len + 1;
	dp->log_len += PREFIX_SIZE;

	for (i = index; i < index + range; i++) {
		if (i >= dp_num_gv) {
			gen_ex(EX_INVALID_OPERAND, 2, i);
			return;
		}
		if (dp->log_len + sizeof (unsigned long) > dp->mod->pgm.logmax) {
			gen_ex(EX_LOG_OVERFLOW, dp->mod->pgm.logmax, 0);
			return;
		}
		read_lock(&dp_gv_lock);
		*(unsigned long *) (dp->log + dp->log_len) = 
				*(dp_gv + i);
		read_unlock(&dp_gv_lock);
		dp->log_len += sizeof(unsigned long);
		(*(unsigned short *)(dp->log + offset))++;
	}
	return;
}

/*
 * Overriding probepoint major and minor.
 */
static void setmin_i(void)
{
	dprobes.minor = get_u16_oprnd();
}

static void setmin(void)
{
	dprobes.minor = (unsigned short) rpnpop();
}

static void setmaj_i(void)
{
	dprobes.major = get_u16_oprnd();
}

static void setmaj(void)
{
	dprobes.major = (unsigned short) rpnpop();
}

/*
 * Local Variables.
 */

static void do_push_lv(unsigned short index)
{
	if (index > dprobes.rec->mod->pgm.num_lv)
		gen_ex(EX_INVALID_OPERAND, 1, index);
	else
		rpnpush(*(dprobes.mod->lv + index));
}

static void push_lvi(void)
{
	do_push_lv(get_u16_oprnd());
}

static void push_lv(void)
{
	do_push_lv(rpnpop());
}

static void pop_lvi(void)
{
	unsigned short index = get_u16_oprnd();
        if (index > dprobes.rec->mod->pgm.num_lv)
                gen_ex(EX_INVALID_OPERAND, 1, index);
        else
		*(dprobes.mod->lv + index) = rpnpop();
}

static void pop_lv(void)
{
	unsigned long value = rpnpop();
	unsigned short index = (unsigned short) rpnpop();
        if (index > dprobes.rec->mod->pgm.num_lv)
                gen_ex(EX_INVALID_OPERAND, 1, index);
        else
		*(dprobes.mod->lv + index) = value;
}

static void do_inc_lv(unsigned short index)
{
	if (index > dprobes.rec->mod->pgm.num_lv)
		gen_ex(EX_INVALID_OPERAND, 1, index);
	else
		(*(dprobes.mod->lv + index))++;
}

static void inc_lvi(void)
{
	do_inc_lv(get_u16_oprnd());
}

static void inc_lv(void)
{
	do_inc_lv(rpnpop());
}
						
static void do_dec_lv(unsigned short index)
{
	if (index > dprobes.rec->mod->pgm.num_lv)
		gen_ex(EX_INVALID_OPERAND, 1, index);
	else
		(*(dprobes.mod->lv + index))--;
}

static void dec_lvi(void)
{
	do_dec_lv(get_u16_oprnd());
}

static void dec_lv(void)
{
	do_dec_lv(rpnpop());
}

static void do_move_lv(unsigned short index)
{
	if (index > dprobes.rec->mod->pgm.num_lv)
		gen_ex(EX_INVALID_OPERAND, 1, index);
	else
		*(dprobes.mod->lv + index) = dprobes.rpn_stack[dprobes.rpn_tos];
}

static void move_lvi(void)
{
	do_move_lv(get_u16_oprnd());
}

static void move_lv(void)
{
	do_move_lv(rpnpop());
}
		
/*
 * global variables.
 */
static void do_push_gv(unsigned short index)
{
	read_lock(&dp_gv_lock);
	if (index >= dp_num_gv) {
		read_unlock(&dp_gv_lock);
		gen_ex(EX_INVALID_OPERAND, 2, index);
		return;
	} else {
		rpnpush(*(dp_gv + index));
		read_unlock(&dp_gv_lock);
	}
}

static void push_gvi(void)
{
	do_push_gv(get_u16_oprnd());
}

static void push_gv(void)
{
	do_push_gv(rpnpop());
}

static void pop_gvi(void)
{
	unsigned short index = get_u16_oprnd();
	write_lock(&dp_gv_lock);
	if (index >= dp_num_gv) {
		write_unlock(&dp_gv_lock);
		gen_ex(EX_INVALID_OPERAND, 2, index);
		return;
	} else {
		*(dp_gv + index) = rpnpop();
		write_unlock(&dp_gv_lock);
	}
}

static void pop_gv(void)
{
	unsigned long value = rpnpop();
	unsigned short index = (unsigned short) rpnpop();
	write_lock(&dp_gv_lock);
	if (index >= dp_num_gv) {
		write_unlock(&dp_gv_lock);
		gen_ex(EX_INVALID_OPERAND, 2, index);
		return;
	} else {
		*(dp_gv + index) = value;
		write_unlock(&dp_gv_lock);
	}
}


static void do_inc_gv(unsigned short index)
{
	write_lock(&dp_gv_lock);
	if (index >= dp_num_gv) {
		write_unlock(&dp_gv_lock);
		gen_ex(EX_INVALID_OPERAND, 2, index);
		return;
	} else {
		(*(dp_gv + index))++;
		write_unlock(&dp_gv_lock);
	}
}

static void inc_gvi(void)
{
	do_inc_gv(get_u16_oprnd());
}

static void inc_gv(void)
{
	do_inc_gv(rpnpop());
}
						
static void do_dec_gv(unsigned short index)
{
	write_lock(&dp_gv_lock);
	if (index >= dp_num_gv) {
		write_unlock(&dp_gv_lock);
		gen_ex(EX_INVALID_OPERAND, 2, index);
		return;
	} else {
		(*(dp_gv + index))--;
		write_unlock(&dp_gv_lock);
	}
}

static void dec_gvi(void)
{
	do_dec_gv(get_u16_oprnd());
}

static void dec_gv(void)
{
	do_dec_gv(rpnpop());
}

static void do_move_gv(unsigned short index)
{
	write_lock(&dp_gv_lock);
	if (index >= dp_num_gv) {
		write_unlock(&dp_gv_lock);
		gen_ex(EX_INVALID_OPERAND, 2, index);
		return;
	} else {
		*(dp_gv + index) = dprobes.rpn_stack[dprobes.rpn_tos];
		write_unlock(&dp_gv_lock);
	}
}

static void move_gvi(void)
{
	do_move_gv(get_u16_oprnd());
}

static void move_gv(void)
{
	do_move_gv(rpnpop());
}


/*
 * Arithmetic and logic group.
 */
static void add(void)
{
	rpnpush(rpnpop() + rpnpop());
}

static void mul(void)
{
	rpnpush(rpnpop() * rpnpop());
}

static void and(void)
{
	rpnpush(rpnpop() & rpnpop());
}

static void or(void)
{
	rpnpush(rpnpop() | rpnpop());
}

static void xor(void)
{
	rpnpush(rpnpop() ^ rpnpop());
}

static void neg(void)
{
	rpnpush(~rpnpop());
}

static void sub(void)
{
	rpnpush(rpnpop() - rpnpop());
}

/* pop divisor first and dividend next, pushes remainder first and quotient next */
static void div(void)
{
	unsigned long divisor = rpnpop();
	unsigned long dividend = rpnpop();
	if (!divisor) {
		gen_ex(EX_DIV_BY_ZERO, 0, 0);
		return;
	}
	rpnpush(dividend % divisor);
	rpnpush(dividend / divisor);
}

static void idiv(void)
{
	signed long divisor = (signed long) rpnpop();
	signed long dividend = (signed long) rpnpop();
	if (!divisor) {
		gen_ex(EX_DIV_BY_ZERO, 0, 0);
		return;
	}
	rpnpush(dividend % divisor);
	rpnpush(dividend / divisor);
}

static void xchng(void)
{
	register unsigned long arg1, arg2;
	arg1 = rpnpop();
	arg2 = rpnpop();
	rpnpush(arg1);
	rpnpush(arg2);
}

static void shl_i(void)
{
	dprobes.rpn_stack[dprobes.rpn_tos] <<= get_u8_oprnd();
}

static void shr_i(void)
{
	dprobes.rpn_stack[dprobes.rpn_tos] >>= get_u8_oprnd();
}

static void shr(void)
{
	unsigned long oprnd = rpnpop();
	unsigned long count = rpnpop();
	oprnd >>= count;
	rpnpush(oprnd);
}

static void shl(void)
{
	unsigned long oprnd = rpnpop();
	unsigned long count = rpnpop();
	oprnd <<= count;
	rpnpush(oprnd);
}

static void dup(void)
{
	unsigned long dup = rpnpop();
	u8 dupcount = rpnpop();
	rpnpush(dup);
	dupcount &= ~RPN_STACK_SIZE;
	while (dupcount--)
		rpnpush(dup);
}

static void dupn(void)
{
	unsigned long dup = rpnpop();
	u8 dupcount = get_u8_oprnd();
	dupcount &= ~RPN_STACK_SIZE;
	while (dupcount--)
		rpnpush(dup);
}

static void ros(void)
{
	dprobes.rpn_tos += get_u8_oprnd();
	dprobes.rpn_tos &= RPN_STACK_SIZE - 1;
}	

/*
 * Verify the read access to a byte pointed by the flat linear address.
 */
static void verify_access(int write)
{
	u8 b;
	void * addr = (void *)rpnpop();
	if (dp_intr_copy_from_user(&b, addr, 1)) {
		goto fail;
	}
	if (write && dp_intr_copy_to_user(addr, &b, 1)) {
		goto fail;
	}
	rpnpush(0);
	return;
fail:
	rpnpush(1);
	return;
}

/*
 * Push immediate value (operand) onto the rpn stack.
 */
static void push_i (void)
{
	rpnpush(get_ulong_oprnd());
}

/*
 * Push byte, word, dword, qword present at the flat address
 * onto rpn stack.
 */
static int push_flat(void *val, void *faddr, unsigned long size)
{
	if (dp_intr_copy_from_user(val, faddr, size)) {
		gen_ex(EX_INVALID_ADDR, (unsigned long)faddr, 0);
		return 0;
	}
	return 1;
}

static void push_mem_u8 (void)
{
	u8 b;
	if(push_flat((void *)&b, (void *)rpnpop(), sizeof(u8)))
		rpnpush(b);
}

static void push_mem_u16 (void)
{
	u16 w;
	if(push_flat((void *)&w, (void *)rpnpop(), sizeof(u16)))
		rpnpush(w);
}

static void push_mem_u32 (void)
{
	u32 dw;
	if(push_flat((void *)&dw, (void *)rpnpop(), sizeof(u32)))
		rpnpush(dw);
}

static void push_mem_u64 (void)
{
	unsigned long q[2];
	void *faddr = (void *) rpnpop();
	if (dp_intr_copy_from_user(q, faddr, 2*sizeof(unsigned long))) {
		gen_ex(EX_INVALID_ADDR, (unsigned long)faddr, 0);
	} else {
		rpnpush(q[1]);
		rpnpush(q[0]);
	}
}

/*
 * Pop byte, word, dword, qword from rpn stack and write 
 * them at the specified linear address.
 */
static void pop_flat(void *faddr, void * val, unsigned long size)
{
	if (dp_intr_copy_to_user(faddr, val, size))
		gen_ex(EX_INVALID_ADDR, (unsigned long)faddr, 0);
}

static void pop_mem_u8 (void)
{
	u8 b = (unsigned char) rpnpop();
	pop_flat((void *)rpnpop(), (void *)&b, sizeof(u8));
}

static void pop_mem_u16 (void)
{
	u16 w = (u16) rpnpop();
	pop_flat((void *)rpnpop(), (void *)&w, sizeof(u16));
}

static void pop_mem_u32 (void)
{
	u32 d = (u32) rpnpop();
	pop_flat((void *)rpnpop(), (void *)&d, sizeof(u32));
}

static void pop_mem_u64 (void)
{
	// PORT: on IA64
	unsigned long q[2];
	q[0] = rpnpop();
	q[1] = rpnpop();
	pop_flat((void *)rpnpop(), (void *)q, 2*sizeof(unsigned long));
}

/*
 * Push pid onto rpn stack.
 */
static void push_pid(void)
{
	rpnpush((unsigned long) current->pid);
}

/*
 * Push processor id onto rpn stack.
 */
static void push_procid(void)
{
	rpnpush((unsigned long)smp_processor_id());
}

/*
 * Push the address of the current task structure onto rpn stack.
 */
static void push_task(void)
{
	rpnpush((unsigned long) current);
}

#ifdef DPROBES_CALLK_HOOK
DECLARE_HOOK_HEAD(DPROBES_CALLK);
USE_HOOK(DPROBES_CALLK);
static void callk(void)
{
	struct dprobes_struct *dp = &dprobes;
	dp->status &= ~DP_STATUS_INTERPRETER;
	HOOK(DPROBES_CALLK, &dprobes);
	dp->status |= DP_STATUS_INTERPRETER;
}
#endif

/*
 * Byte heap management routines.
 */  
#ifdef CONFIG_SMP
struct list_head heap_list_set[NR_CPUS];
#define heap_list heap_list_set[smp_processor_id()]
#else
struct list_head heap_list;
#define heap_list_set (&heap_list)
#endif

/* Validates the heap handle. Returns the block size in @size */
static int heap_validate_handle(byte_t *handle, unsigned long *size)
{
	struct list_head *tmp;
	struct heap_hdr *h;

	list_for_each(tmp, &heap_list) {
		h = list_entry(tmp, struct heap_hdr, list);
		if (h->addr == handle && h->flags == HEAP_ALLOCATED) {
			if (size) *size = h->size;
			return 1;
		}
	}
	return 0;
}

static void heap_coalesce(struct heap_hdr *heap)
{
	struct heap_hdr *h;

	/* Combine this block with the next free block (if available) */
	if (heap->list.next != &heap_list) {
		h = list_entry(heap->list.next, struct heap_hdr, list);
		if (h->flags == HEAP_FREE) {
			heap->size += (h->size + HEAP_HDR_SIZE);
			list_del(&h->list);
		}
	}

	/* Combine this block with the previous free block (if available) */
	if (heap->list.prev != &heap_list) {
		h = list_entry(heap->list.prev, struct heap_hdr, list);
		if (h->flags == HEAP_FREE) {
			h->size += (heap->size + HEAP_HDR_SIZE);
			list_del(&heap->list);
		}
	}
}

/* Allocation succeeds only if (size + HEAP_HDR_SIZE) is available */
static byte_t *heap_alloc(int size)
{
	unsigned long sz;
	struct list_head *tmp;
	struct heap_hdr *next;

	list_for_each(tmp, &heap_list) {
		struct heap_hdr *h = list_entry(tmp, struct heap_hdr, list);
		sz = h->size;
		if ((h->flags == HEAP_FREE) && (sz > size + HEAP_HDR_SIZE)) {
			h->size = size;
			h->flags = HEAP_ALLOCATED;
			
			if (sz > size) {
				next = (struct heap_hdr *)(h->addr + size);
				next->addr = h->addr + size + HEAP_HDR_SIZE;
				next->flags = HEAP_FREE;
				next->size = sz - size - HEAP_HDR_SIZE;
				list_add(&next->list, &h->list);
			}
			return h->addr;
		}
	}
	return NULL;
}

/* Returns the amount of memory freed */
static unsigned long heap_free(byte_t *ptr)
{
	struct list_head *tmp;
	unsigned long size = 0;

	list_for_each(tmp, &heap_list) {
		struct heap_hdr *h = list_entry(tmp, struct heap_hdr, list);
		if (h->addr == ptr && h->flags == HEAP_ALLOCATED) {
			h->flags = HEAP_FREE;
			size = h->size;
			heap_coalesce(h);
			break;
		}
	}
	return size;
}

static void heap_init(byte_t *heap)
{
	struct heap_hdr *h;
	
	INIT_LIST_HEAD(&heap_list);
	h = (struct heap_hdr *)heap;
	h->addr = heap + HEAP_HDR_SIZE;
	h->flags = HEAP_FREE;
	h->size = dprobes.rec->point.heap_size - HEAP_HDR_SIZE;
	list_add(&h->list, &heap_list);
}

/*
 * Byte heap related instructions.
 */
static void pushh_u8_i(void)
{
	unsigned short count = get_u16_oprnd();
	unsigned long offset = rpnpop();
	byte_t *handle = (byte_t *)rpnpop();
	unsigned long blk_size;
	
	/* validate the handle */
	if (!heap_validate_handle(handle, &blk_size)) {
		gen_ex(EX_HEAP_INVALID_HANDLE, (unsigned long)handle, offset);
		return;
	}
	while (count) {
		/* validate the offset */
		if (offset >= blk_size) {
			gen_ex(EX_HEAP_INVALID_OFFSET, (unsigned long)handle, 
					offset);
			return;
		}
		rpnpush((unsigned long)(*((u8 *)(handle + offset))));
		offset++;
		count--;
	}
}

static void pushh_u16_i(void)
{
	unsigned short count = get_u16_oprnd();
	unsigned long offset = rpnpop();
	byte_t *handle = (byte_t *)rpnpop();
	unsigned long blk_size;
	
	/* validate the handle */
	if (!heap_validate_handle(handle, &blk_size)) {
		gen_ex(EX_HEAP_INVALID_HANDLE, (unsigned long)handle, offset);
		return;
	}
	while (count) {
		/* validate the offset */
		if (offset + 2 > blk_size) {
			gen_ex(EX_HEAP_INVALID_OFFSET, (unsigned long)handle, 
					offset);
			return;
		}
		rpnpush((unsigned long)(*((u16 *)(handle + offset))));
		offset += 2;
		count--;
	}
}

static void pushh_u32_i(void)
{
	unsigned short count = get_u16_oprnd();
	unsigned long offset = rpnpop();
	byte_t *handle = (byte_t *)rpnpop();
	unsigned long blk_size;
	
	/* validate the handle */
	if (!heap_validate_handle(handle, &blk_size)) {
		gen_ex(EX_HEAP_INVALID_HANDLE, (unsigned long)handle, offset);
		return;
	}
	while (count) {
		/* validate the offset */
		if (offset + 4 > blk_size) {
			gen_ex(EX_HEAP_INVALID_OFFSET, (unsigned long)handle, 
					offset);
			return;
		}
		rpnpush((unsigned long)(*((u32 *)(handle + offset))));
		offset += 4;
		count--;
	}
}

static void pushh_u64_i(void)
{
	unsigned short count = get_u16_oprnd();
	unsigned long offset = rpnpop();
	byte_t *handle = (byte_t *)rpnpop();
	unsigned long blk_size;
	
	/* validate the handle */
	if (!heap_validate_handle(handle, &blk_size)) {
		gen_ex(EX_HEAP_INVALID_HANDLE, (unsigned long)handle, offset);
		return;
	}
	while (count) {
		/* validate the offset */
		if (offset + 8 > blk_size) {
			gen_ex(EX_HEAP_INVALID_OFFSET, (unsigned long)handle, 
					offset);
			return;
		}
		/* shouldn't this order depend on endianness ? */
		rpnpush((unsigned long)(*((u64 *)(handle + offset))));
		rpnpush((unsigned long)(*((u64 *)(handle + offset))));
		offset += 8;
		count--;
	}
}

static void poph_u8_i(void)
{
	unsigned short count = get_u16_oprnd();
	unsigned long offset = rpnpop();
	byte_t *handle = (byte_t *)rpnpop();
	unsigned long blk_size;
	
	/* validate the handle */
	if (!heap_validate_handle(handle, &blk_size)) {
		gen_ex(EX_HEAP_INVALID_HANDLE, (unsigned long)handle, offset);
		return;
	}
	while (count) {
		/* validate the offset */
		if (offset > blk_size) {
			gen_ex(EX_HEAP_INVALID_OFFSET, (unsigned long)handle,
					offset);
			return;
		}
		*((u8 *)(handle + offset)) = (u8)rpnpop();
		offset++;
		count--;
	}
}

static void poph_u16_i(void)
{
	unsigned short count = get_u16_oprnd();
	unsigned long offset = rpnpop();
	byte_t *handle = (byte_t *)rpnpop();
	unsigned long blk_size;
	
	/* validate the handle */
	if (!heap_validate_handle(handle, &blk_size)) {
		gen_ex(EX_HEAP_INVALID_HANDLE, (unsigned long)handle, offset);
		return;
	}
	while (count) {
		/* validate the offset */
		if (offset + 2 > blk_size) {
			gen_ex(EX_HEAP_INVALID_OFFSET, (unsigned long)handle,
					offset);
			return;
		}
		*((u16 *)(handle + offset)) = (u16)rpnpop();
		offset += 2;
		count--;
	}
}

static void poph_u32_i(void)
{
	unsigned short count = get_u16_oprnd();
	unsigned long offset = rpnpop();
	byte_t *handle = (byte_t *)rpnpop();
	unsigned long blk_size;
	
	/* validate the handle */
	if (!heap_validate_handle(handle, &blk_size)) {
		gen_ex(EX_HEAP_INVALID_HANDLE, (unsigned long)handle, offset);
		return;
	}
	while (count) {
		/* validate the offset */
		if (offset + 4 > blk_size) {
			gen_ex(EX_HEAP_INVALID_OFFSET, (unsigned long)handle,
					offset);
			return;
		}
		*((u32 *)(handle + offset)) = (u32)rpnpop();
		offset += 4;
		count--;
	}
}

static void poph_u64_i(void)
{
	unsigned short count = get_u16_oprnd();
	unsigned long offset = rpnpop();
	byte_t *handle = (byte_t *)rpnpop();
	unsigned long blk_size;
	
	/* validate the handle */
	if (!heap_validate_handle(handle, &blk_size)) {
		gen_ex(EX_HEAP_INVALID_HANDLE, (unsigned long)handle, offset);
		return;
	}
	while (count) {
		/* validate the offset */
		if (offset + 8 > blk_size) {
			gen_ex(EX_HEAP_INVALID_OFFSET, (unsigned long)handle,
					offset);
			return;
		}
		/* shouldn't this order depend on endianness ? */
		*((u32 *)(handle + offset)) = (u32)rpnpop();
		*((u32 *)(handle + offset + 4)) = (u32)rpnpop();
		offset += 8;
		count--;
	}
}

static void malloc(void)
{
	unsigned long size = rpnpop();
	byte_t *handle;

	/* 
	 * Does this allocation exceed the maximum heapsize specified for
	 * this probe ?
	 */ 
	if (size + dprobes.heap_size > dprobes.rec->point.heap_size) {
		gen_ex(EX_HEAP_NOMEM, size, 0);
		return;
	}
	handle = heap_alloc(size);
	if (!handle) {
		gen_ex(EX_HEAP_NOMEM, size, 0);
		return;
	}
	dprobes.heap_size += size;
	rpnpush((unsigned long)handle);
}

static void free(void)
{
	byte_t *handle = (byte_t *)rpnpop();
	unsigned long size;

	if (!(size = heap_free(handle))) {
		gen_ex(EX_HEAP_INVALID_HANDLE, (unsigned long)handle, 0);
		return;
	}
	dprobes.heap_size -= size;
}

static void push_wid(void)
{
	union wid {
		short a;
		char b[sizeof(short)];
	} w;
	w.a = 0x0102;
	
	if (sizeof(short) != 2) {
		rpnpush(0);
		return;
	}
	
	if (w.b[0] == 0x01 && w.b[1] == 0x02) {
		rpnpush(BITS_PER_LONG);
	} else if (w.b[0] == 0x02 && w.b[1] == 0x01) {
		rpnpush(-BITS_PER_LONG);
	} else {
		rpnpush(0);
	}
}

/*
 * Entry point for the dprobes interpreter(Probe handler).
 */
void dp_interpreter(void)
{
	struct dprobes_struct *dp = &dprobes;

	dp->call_tos = CALL_STACK_SIZE - CALL_FRAME_SIZE;
	dp->call_stack[dp->call_tos + OFFSET_EX_HANDLER] = EX_NO_HANDLER;
	dp->rpn_tos = dp->rpn_sbp = RPN_STACK_SIZE;
	dp->rpn_code = dp->rec->mod->pgm.rpn_code;
	dp->rpn_ip = dp->rec->mod->pgm.rpn_code + dp->rec->point.rpn_offset;
	dp->log_len = dp->rec->mod->hdr.len;
	dp->ex_log_len = MIN_ST_SIZE;
	dp->jmp_count = 0;
	dp->ex_parm1 = dp->ex_parm2 = 0;
	if (dp->rec->point.heap_size > 2*HEAP_HDR_SIZE) {
		read_lock(&dp_heap_lock);
		heap_init(dp_heap + dp->mod->pgm.heapmax * smp_processor_id());
		dp->heap_size = 0;
	}
	
	dp->status |= DP_STATUS_INTERPRETER;
	while (dp->status & DP_STATUS_INTERPRETER) {
		register u8 rpn_instr = *dp->rpn_ip++;
		switch(rpn_instr) {
			case DP_NOP: 		no_op(); break;		

			case DP_JMP:		jmp(); break;			
			case DP_JLT:		jlt(); break;			
			case DP_JLE:		jle(); break;			
			case DP_JGT:		jgt(); break;		
			case DP_JGE:		jge(); break;   		
			case DP_JZ:		jz(); break;	
			case DP_JNZ:		jnz(); break;			
			case DP_LOOP:		loop(); break;			
			case DP_CALL:		call(); break;			
			case DP_RET:		ret(); break;			
			case DP_ABORT:		dp_abort(); break;		
			case DP_REM:		dp_remove(); break;		
			case DP_EXIT:		dp_exit(); break;		
			case DP_EXIT_N:		dp_exit_n(); break;		

			case DP_SETMIN_I:	setmin_i(); break;		
			case DP_SETMIN:		setmin(); break;		
			case DP_SETMAJ_I:	setmaj_i(); break;		
			case DP_SETMAJ:		setmaj(); break;		
			case DP_LOG_STR:	log_str(); break;		
			case DP_LOG_MRF:	log_mrf(); break;		
			case DP_LOG_I:		log_i(); break;		

			case DP_PUSH_LVI:	push_lvi(); break;	
			case DP_PUSH_LV:	push_lv(); break;		
			case DP_POP_LVI:	pop_lvi(); break;		
			case DP_POP_LV:		pop_lv(); break;		
			case DP_MOVE_LVI:	move_lvi(); break;		
			case DP_MOVE_LV:	move_lv(); break;		
			case DP_INC_LVI:	inc_lvi(); break;		
			case DP_INC_LV:		inc_lv(); break;		
			case DP_DEC_LVI:	dec_lvi(); break;		
			case DP_DEC_LV:		dec_lv(); break;		

			case DP_PUSH_GVI:	push_gvi(); break;
			case DP_PUSH_GV:	push_gv(); break;
			case DP_POP_GVI:	pop_gvi(); break;
			case DP_POP_GV:		pop_gv(); break;
			case DP_MOVE_GVI:	move_gvi(); break;		
			case DP_MOVE_GV:	move_gv(); break;		
			case DP_INC_GVI:	inc_gvi(); break;		
			case DP_INC_GV:		inc_gv(); break;		
			case DP_DEC_GVI:	dec_gvi(); break;		
			case DP_DEC_GV:		dec_gv(); break;		

			case DP_ADD:		add(); break;			
			case DP_SUB:		sub(); break;			
			case DP_MUL:		mul(); break;			
			case DP_DIV:		div(); break;
			case DP_IDIV:		idiv(); break;

			case DP_NEG:		neg(); break;			
			case DP_AND:		and(); break;			
			case DP_OR:		or(); break;			
			case DP_XOR:		xor(); break;			
			case DP_ROL_I:		rol_i(); break;		
 			case DP_ROL:		rol(); break;			
			case DP_ROR_I:		ror_i(); break;		
			case DP_ROR:		ror(); break;			
			case DP_SHL_I:		shl_i(); break;		
			case DP_SHL:		shl(); break;			
			case DP_SHR_I:		shr_i(); break;		
			case DP_SHR:		shr(); break;			
			case DP_PBL:		pbl(); break;
			case DP_PBR:		pbr(); break;
			case DP_PBL_I:		pbl_i(); break;
			case DP_PBR_I:		pbr_i(); break;
			case DP_PZL:		pzl(); break;
			case DP_PZR:		pzr(); break;
			case DP_PZL_I:		pzl_i(); break;
			case DP_PZR_I:		pzr_i(); break;
#ifdef DPROBES_CALLK_HOOK
			case DP_CALLK:		callk(); break;
#endif

			case DP_XCHG:		xchng(); break;	
			case DP_DUP_I:		dupn(); break;		
			case DP_DUP:		dup(); break;			
			case DP_ROS:		ros(); break;		

 			case DP_PUSH_R:		pushr(); break;		
			case DP_POP_R:		popr(); break;		
			case DP_PUSH_U:		pushu(); break;		
			case DP_POP_U:		popu(); break;		

			case DP_PUSH:		push_i(); break;		
			case DP_PUSH_MEM_U8:	push_mem_u8(); break;		
			case DP_PUSH_MEM_U16:	push_mem_u16(); break;		
			case DP_PUSH_MEM_U32:	push_mem_u32(); break;				
			case DP_PUSH_MEM_U64:	push_mem_u64(); break;		
			case DP_POP_MEM_U8:	pop_mem_u8(); break;		
			case DP_POP_MEM_U16:	pop_mem_u16(); break;		
			case DP_POP_MEM_U32:	pop_mem_u32(); break;		
			case DP_POP_MEM_U64:	pop_mem_u64(); break;		

			case DP_PUSH_TASK:	push_task(); break;		
			case DP_PUSH_PID:	push_pid(); break;		
			case DP_PUSH_PROCID:	push_procid(); break;		

			case DP_VFY_R:		verify_access(0); break;
			case DP_VFY_RW:		verify_access(1); break;

			case DP_SX:		sx(); break;
			case DP_UX:		ux(); break;
			case DP_RX:		rx(); break;
			case DP_PUSH_X:		push_x(); break;
			case DP_PUSH_LP:	push_lp(); break;
			case DP_PUSH_PLP:	push_plp(); break;
			case DP_POP_LP:		pop_lp(); break;
			case DP_LOG_ST:		log_st(); break;
			case DP_PURGE_ST:	purge_st(); break;
			case DP_TRACE_LV:	trace_lv(); break;	
			case DP_TRACE_GV:	trace_gv(); break;	
			case DP_TRACE_PV:	trace_pv(); break;	

			case DP_PUSH_SBP:	push_sbp(); break;	
			case DP_POP_SBP:	pop_sbp(); break;	
			case DP_PUSH_TSP:	push_tsp(); break;	
			case DP_POP_TSP:	pop_tsp(); break;	
			case DP_COPY_SBP:	copy_sbp(); break;	
			case DP_COPY_TSP:	copy_tsp(); break;	
			case DP_PUSH_SBP_I:	push_sbp_i(); break;	
			case DP_POP_SBP_I:	pop_sbp_i(); break;	
			case DP_PUSH_TSP_I:	push_tsp_i(); break;	
			case DP_POP_TSP_I:	pop_tsp_i(); break;	
			case DP_COPY_SBP_I:	copy_sbp_i(); break;	
			case DP_COPY_TSP_I:	copy_tsp_i(); break;	
			case DP_PUSH_STP:	push_stp(); break;
			case DP_POP_STP:	pop_stp(); break;
			case DP_SAVE_SBP:	save_sbp(); break;
			case DP_RESTORE_SBP:	restore_sbp(); break;
			case DP_SAVE_TSP:	save_tsp(); break;
			case DP_RESTORE_TSP:	restore_tsp(); break;

			case DP_LOG:		log(); break;
			case DP_LOG_LV:		log_lv(); break;
			case DP_LOG_GV:		log_gv(); break;

			case DP_MALLOC:		malloc(); break;
			case DP_FREE:		free(); break;
			case DP_PUSH_WID:	push_wid(); break;
			case DP_PUSHH_U8_I:	pushh_u8_i(); break;
			case DP_PUSHH_U16_I:	pushh_u16_i(); break;
			case DP_PUSHH_U32_I:	pushh_u32_i(); break;
			case DP_PUSHH_U64_I:	pushh_u64_i(); break;
			case DP_POPH_U8_I:	poph_u8_i(); break;
			case DP_POPH_U16_I:	poph_u16_i(); break;
			case DP_POPH_U32_I:	poph_u32_i(); break;
			case DP_POPH_U64_I:	poph_u64_i(); break;

			default: dp_asm_interpreter(rpn_instr); break;
		}
	}
	if (dp->rec->point.heap_size > 2*HEAP_HDR_SIZE)
		read_unlock(&dp_heap_lock);
	return;
}

void dprobes_interpreter_code_end(void) { }
