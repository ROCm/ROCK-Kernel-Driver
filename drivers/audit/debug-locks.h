/*
 * Debug locks
 *
 */

#ifndef DEBUG_LOCKS_H
#define DEBUG_LOCKS_H

#ifdef AUDIT_DEBUG_LOCKS

extern void		_debug_locks(char *type, char *var, int lock);

static inline void	do_read_lock(rwlock_t *lk) { read_lock(lk); }
static inline void	do_read_unlock(rwlock_t *lk) { read_unlock(lk); }
static inline void	do_write_lock(rwlock_t *lk) { write_lock(lk); }
static inline void	do_write_unlock(rwlock_t *lk) { write_unlock(lk); }
static inline void	do_spin_lock(spinlock_t *lk) { spin_lock(lk); }
static inline void	do_spin_unlock(spinlock_t *lk) { spin_unlock(lk); }
static inline void	do_down_read(struct rw_semaphore *lk) { down_read(lk); }
static inline void	do_up_read(struct rw_semaphore *lk) { up_read(lk); }
static inline void	do_down_write(struct rw_semaphore *lk) { down_write(lk); }
static inline void	do_up_write(struct rw_semaphore *lk) { up_write(lk); }

#define wrap_lock(f, lk) do { \
		printk(KERN_DEBUG "[%d,%d] / " #f "(" #lk "): %s:%s:%d\n", \
			current->cpu, current->pid, \
			__FILE__, __FUNCTION__, __LINE__); \
		_debug_locks(#f, #lk, 1); \
		do_##f(lk); \
		printk(KERN_DEBUG "[%d,%d] \\ " #f "(" #lk "): %s:%s:%d\n", \
			current->cpu, current->pid, \
			__FILE__, __FUNCTION__, __LINE__); \
	} while (0)
#define wrap_unlock(f, lk) do { \
		printk(KERN_DEBUG "[%d,%d] - " #f "(" #lk "): %s:%s:%d\n", \
			current->cpu, current->pid, \
			__FILE__, __FUNCTION__, __LINE__); \
		_debug_locks(#f, #lk, 0); \
		do_##f(lk); \
	} while (0)

#undef read_lock
#undef read_unlock
#undef write_lock
#undef write_unlock
#undef spin_lock
#undef spin_unlock

#define read_lock(lk)		wrap_lock(read_lock, lk)
#define read_unlock(lk)		wrap_unlock(read_unlock, lk)
#define write_lock(lk)		wrap_lock(write_lock, lk)
#define write_unlock(lk)	wrap_unlock(write_unlock, lk)
#define spin_lock(lk)		wrap_lock(spin_lock, lk)
#define spin_unlock(lk)		wrap_unlock(spin_unlock, lk)

#endif /* AUDIT_DEBUG_LOCKS */

#endif /* DEBUG_LOCKS_H */
