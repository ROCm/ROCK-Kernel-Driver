extern int sa1111_pcmcia_init(struct pcmcia_init *);
extern int sa1111_pcmcia_shutdown(void);
extern int sa1111_pcmcia_socket_state(struct pcmcia_state_array *);
extern int sa1111_pcmcia_get_irq_info(struct pcmcia_irq_info *);
extern int sa1111_pcmcia_configure_socket(int sock, const struct pcmcia_configure *);
extern int sa1111_pcmcia_socket_init(int);
extern int sa1111_pcmcia_socket_suspend(int);
