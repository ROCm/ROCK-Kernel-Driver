/*
 * This is performance-critical, we want to do it O(1)
 *
 * Most irqs are mapped 1:1 with pins.
 */
struct irq_cfg {
	struct irq_pin_list	*irq_2_pin;
	cpumask_var_t		domain;
	cpumask_var_t		old_domain;
	unsigned		move_cleanup_count;
	u8			vector;
	u8			move_in_progress : 1;
};

extern struct irq_cfg *irq_cfg(unsigned int);
extern int assign_irq_vector(int, struct irq_cfg *, const struct cpumask *);
extern void send_cleanup_vector(struct irq_cfg *);
extern unsigned int set_desc_affinity(struct irq_desc *, const struct cpumask *);
