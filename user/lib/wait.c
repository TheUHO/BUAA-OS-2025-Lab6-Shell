#include <env.h>
#include <lib.h>
int wait(u_int envid) {
	const volatile struct Env *e;
    u_int who;
	int res;

	e = &envs[ENVX(envid)];
	while (e->env_id == envid && e->env_status != ENV_FREE) {
		// 阻塞式接收来自任何进程的消息
		res = ipc_recv(&who, 0, 0);
		// 如果ipc_recv返回错误（例如，发送方在我们接收前就死了），
		// 我们需要重新检查子进程状态，避免死循环
		if (res < 0) {
			if (e->env_status == ENV_FREE) {
				break; // 子进程已经退出，跳出循环
			}
			syscall_yield(); // 让出CPU，稍后再试
			continue;
		}
		// 检查消息是否来自我们正在等待的那个子进程
		if (who == envid) {
			// 是的，这就是我们等待的结果，返回它
			return res; 
		}
	}
    // 如果子进程已经不存在或由于其他原因退出循环，返回-1
	return -1;
}
