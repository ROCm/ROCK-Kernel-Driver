/* x11_kern */

struct x11_window;
struct x11_kerndata;

void x11_kbd_input(struct x11_kerndata *kd, int key, int down);
void x11_mouse_input(struct x11_kerndata *kd, int key, int down,
		     int x, int y);
void x11_cad(struct x11_kerndata *kd);
