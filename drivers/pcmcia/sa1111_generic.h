extern int sa1111_pcmcia_init(struct pcmcia_init *);
extern int sa1111_pcmcia_shutdown(void);
extern void sa1111_pcmcia_socket_state(int sock, struct pcmcia_state *);
extern int sa1111_pcmcia_configure_socket(int sock, const struct pcmcia_configure *);
extern int sa1111_pcmcia_socket_init(int);
extern int sa1111_pcmcia_socket_suspend(int);


extern int pcmcia_badge4_init(struct device *);
extern void pcmcia_badge4_exit(struct device *);

extern int pcmcia_jornada720_init(struct device *);
extern void pcmcia_jornada720_exit(struct device *);

extern int pcmcia_neponset_init(struct device *);
extern void pcmcia_neponset_exit(struct device *);


