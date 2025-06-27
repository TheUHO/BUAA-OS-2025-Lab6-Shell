#include <env.h>
#include <lib.h>
#include <mmu.h>

void exit(int status) {
	// After fs is ready (lab5), all our open files should be closed before dying.
#if !defined(LAB) || LAB >= 5
	close_all();
#endif

	u_int parent_id = syscall_get_parent_id(0);
	// 如果父进程存在，则通过IPC发送返回值
	if (parent_id != 0) {
		ipc_send(parent_id, status, 0, 0);
	}
	syscall_env_destroy(0);
	user_panic("unreachable code");
}

const volatile struct Env *env;
extern int main(int, char **);

void libmain(int argc, char **argv) {
	// set env to point at our env structure in envs[].
	env = &envs[ENVX(syscall_getenvid())];

	// call user main routine
	int r = main(argc, argv);

	// exit gracefully
	exit(r);
}
