
#ifndef _sis_drm_public_h_
#define _sis_drm_public_h_

typedef struct {
  int context;
  unsigned int offset;
  unsigned int size;
  unsigned int free;
} drm_sis_mem_t;

typedef struct {
  unsigned int offset, size;
} drm_sis_agp_t;

typedef struct {
  unsigned int left, right;
} drm_sis_flip_t;

#ifdef __KERNEL__

int sis_fb_alloc(struct inode *inode, struct file *filp, unsigned int cmd,
		  unsigned long arg);
int sis_fb_free(struct inode *inode, struct file *filp, unsigned int cmd,
		  unsigned long arg);

int sisp_agp_init(struct inode *inode, struct file *filp, unsigned int cmd,
		  unsigned long arg);
int sisp_agp_alloc(struct inode *inode, struct file *filp, unsigned int cmd,
		  unsigned long arg);
int sisp_agp_free(struct inode *inode, struct file *filp, unsigned int cmd,
		  unsigned long arg);

#endif

#endif
