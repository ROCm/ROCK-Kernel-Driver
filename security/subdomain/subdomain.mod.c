#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

#undef unix
struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
 .name = __stringify(KBUILD_MODNAME),
 .init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
 .exit = cleanup_module,
#endif
};

static const struct modversion_info ____versions[]
__attribute_used__
__attribute__((section("__versions"))) = {
	{ 0x2a5c0745, "struct_module" },
	{ 0x710dac30, "d_path" },
	{ 0x7da8156e, "__kmalloc" },
	{ 0xd6ee688f, "vmalloc" },
	{ 0x5c69e2a7, "_read_lock" },
	{ 0xc2a74f6a, "seq_open" },
	{ 0x74cc238d, "current_kernel_time" },
	{ 0xfcb2e1be, "malloc_sizes" },
	{ 0xd9fc43c1, "dput" },
	{ 0x5b4eb2e4, "seq_printf" },
	{ 0x250b3495, "unregister_security" },
	{ 0x2fd1d81c, "vfree" },
	{ 0x9327521d, "_spin_lock_irqsave" },
	{ 0x590af651, "path_walk" },
	{ 0xc453d57b, "seq_read" },
	{ 0x883638fe, "lookup_hash" },
	{ 0xc10bbfc8, "cap_capset_set" },
	{ 0x5fabed4, "get_sb_single" },
	{ 0x4b4f2c55, "kill_litter_super" },
	{ 0x1b7d4074, "printk" },
	{ 0x859204af, "sscanf" },
	{ 0xbd3bd9fc, "d_rehash" },
	{ 0xb52bb3dc, "rwsem_wake" },
	{ 0xd5ea041e, "d_alloc_root" },
	{ 0x2f287f0d, "copy_to_user" },
	{ 0x22139c1d, "cap_capable" },
	{ 0xc9ce34b4, "cap_bprm_apply_creds" },
	{ 0x85a32c76, "_spin_unlock_irqrestore" },
	{ 0x2824c12b, "cap_capset_check" },
	{ 0xf5d14d9e, "path_release" },
	{ 0xdcc18619, "cap_bprm_set_security" },
	{ 0x372390b2, "cap_task_post_setuid" },
	{ 0x7dceceac, "capable" },
	{ 0x71c9eaa6, "_write_lock" },
	{ 0xe98f1978, "kmem_cache_alloc" },
	{ 0x4a357ecc, "path_lookup" },
	{ 0xe937ba24, "simple_dir_operations" },
	{ 0x5a0baac3, "cap_task_reparent_to_init" },
	{ 0x5b19987, "d_alloc" },
	{ 0x4784e424, "__get_free_pages" },
	{ 0x7f1d644d, "cap_capget" },
	{ 0xe1dd5368, "cap_syslog" },
	{ 0xcf96cd46, "_write_unlock" },
	{ 0x5f1dd3fb, "register_filesystem" },
	{ 0xd8565995, "_read_unlock" },
	{ 0x9941ccb8, "free_pages" },
	{ 0x89b43e96, "seq_lseek" },
	{ 0x3a9e6c1a, "iput" },
	{ 0x37a0cba, "kfree" },
	{ 0xb5a22e22, "send_sig_info" },
	{ 0xd8a55d6c, "unregister_filesystem" },
	{ 0x25da070, "snprintf" },
	{ 0x34b7343a, "seq_release" },
	{ 0x5a9f5fe6, "new_inode" },
	{ 0xd6c963c, "copy_from_user" },
	{ 0xefe4cd93, "d_instantiate" },
	{ 0x8765f00a, "rwsem_down_read_failed" },
	{ 0x81799cee, "vscnprintf" },
	{ 0x13f048e0, "cap_ptrace" },
	{ 0x24525c4c, "register_security" },
};

static const char __module_depends[]
__attribute_used__
__attribute__((section(".modinfo"))) =
"depends=commoncap";

