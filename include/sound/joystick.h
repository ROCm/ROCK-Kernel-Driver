/*
 * helper functions for legacy joystick probing
 */

#ifndef __SOUND_JOYSTICK_H
#define __SOUND_JOYSTICK_H

static inline int snd_joystick_probe(const char *name)
{
	static int isa_ports[] = {
		0x201, 0x200, 0x202, 0x203, 0x204, 0x205, 0x207, 0x209,
		0x20b, 0x20c, 0x20e, 0x20f, 0x211, 0x219, 0x101, 0
	};
	int port, *auto_ports;
	for (auto_ports = isa_ports; (port = *auto_ports) != 0; auto_ports++) {
		unsigned char c, u, v;
		int i;
		if (! request_region(port, 1, name))
			continue;
		c = inb(port);
		printk(KERN_ERR "port %x = %x\n", port, c);
		outb(~c & ~3, port);
		if (~(u = v = inb(port)) & 3) {
			printk(KERN_ERR "joystick out #1 [%x]\n", port);
			goto out;
		}
		for (i = 0; i < 1000; i++)
			v &= inb(port);
		if (u == v) {
			printk(KERN_ERR "joystick out #2 [%x]\n", port);
			// goto out;
		}
		msleep(3);
		u = inb(port);
		for (i = 0; i < 1000; i++)
			if ((u ^ inb(port)) & 0xf) {
				printk(KERN_ERR "joystick out #3 [%x]\n", port);
				goto out;
			}

		printk(KERN_ERR "joystick found port %x\n", port);
		return port;
	out:
		outb(c, port);
		release_region(port, 1);
	}
	return 0;
 }

static inline void snd_joystick_register(struct gameport *game)
{
	if (game->io)
		gameport_register_port(game);
}

static inline void snd_joystick_free(struct gameport *game)
{
	if (game->io) {
		gameport_unregister_port(game);
		release_region(game->io, 1);
		game->io = 0;
	}
}

#endif
