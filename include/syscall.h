#ifndef SYSCALL_H
#define SYSCALL_H

#ifndef __ASSEMBLER__

enum {
	SYS_putchar,
	SYS_print_cons,
	SYS_getenvid,
	SYS_yield,
	SYS_env_destroy,
	SYS_set_tlb_mod_entry,
	SYS_mem_alloc,
	SYS_mem_map,
	SYS_mem_unmap,
	SYS_exofork,
	SYS_set_env_status,
	SYS_set_trapframe,
	SYS_panic,
	SYS_ipc_try_send,
	SYS_ipc_recv,
	SYS_cgetc,
	SYS_write_dev,
	SYS_read_dev,
	SYS_get_cur_path,
	SYS_set_cur_path,
	SYS_alloc_shell_id,
	SYS_declare_var,
	SYS_unset_var,
	SYS_get_var,
	SYS_get_all_var,
	SYS_get_parent_id,
	MAX_SYSNO,
};

#endif

#endif
