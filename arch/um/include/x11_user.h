/* x11_user */

struct x11_window;
struct x11_kerndata;

struct x11_window *x11_open(int width, int height);
void x11_close(struct x11_window *win);
int x11_has_data(struct x11_window *win, struct x11_kerndata *kd);
int x11_blit_fb(struct x11_window *win, int y1, int y2);

int x11_get_fd(struct x11_window *win);
struct fb_fix_screeninfo* x11_get_fix(struct x11_window *win);
struct fb_var_screeninfo* x11_get_var(struct x11_window *win);
void* x11_get_fbmem(struct x11_window *win);

