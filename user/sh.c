#include <args.h>
#include <lib.h>

#define WHITESPACE " \t\r\n"
#define SYMBOLS "<|>&;()"

int global_shell_id;

/* Overview:
 *   Parse the next token from the string at s.
 *
 * Post-Condition:
 *   Set '*p1' to the beginning of the token and '*p2' to just past the token.
 *   Return:
 *     - 0 if the end of string is reached.
 *     - '<' for < (stdin redirection).
 *     - '>' for > (stdout redirection).
 *     - '|' for | (pipe).
 *     - 'w' for a word (command, argument, or file name).
 *
 *   The buffer is modified to turn the spaces after words into zero bytes ('\0'), so that the
 *   returned token is a null-terminated string.
 */
int _gettoken(char *s, char **p1, char **p2) {
	*p1 = 0;
	*p2 = 0;
	if (s == 0) {
		return 0;
	}

	while (strchr(WHITESPACE, *s)) {
		*s++ = 0;
	}
	if (*s == 0) {
		return 0;
	}

	if (strchr(SYMBOLS, *s)) {
		int t = *s;
		*p1 = s;
		*s++ = 0;
		*p2 = s;
		return t;
	}

	*p1 = s;
	while (*s && !strchr(WHITESPACE SYMBOLS, *s)) {
		s++;
	}
	*p2 = s;
	return 'w';
}

int gettoken(char *s, char **p1) {
	static int c, nc;
	static char *np1, *np2;

	if (s) {
		nc = _gettoken(s, &np1, &np2);
		return 0;
	}
	c = nc;
	*p1 = np1;
	nc = _gettoken(np2, &np1, &np2);
	return c;
}

/* 
 * 变量展开：若 token 以 '$' 开头，通过系统调用获取变量值替换 token。
 */
void expand_token(const char *token, char *dest, int dest_size) {
    if (token[0] != '$') {
        int i = 0;
        while (i < dest_size - 1 && token[i] != '\0') {
            dest[i] = token[i];
            i++;
        }
        dest[i] = '\0';
        return;
    }
    // token以'$'开头，从下标1开始扫描变量名，变量名由字母、数字、下划线组成
    int i = 1, j = 0;
    char varname[64];
    while (token[i] && ((token[i] >= 'a' && token[i] <= 'z') ||
           (token[i] >= 'A' && token[i] <= 'Z') ||
           (token[i] == '_') || (token[i] >= '0' && token[i] <= '9')) && j < (int)sizeof(varname)-1) {
        varname[j++] = token[i++];
    }
    varname[j] = '\0';

    // 通过系统调用获取变量值
    char value[128];
    syscall_get_var(varname, value, sizeof(value));
	// printf("value: %s\n", value);
    // 逐字符拼接，将 value 拷贝到 dest 中
    int d = 0, k = 0;
    while (d < dest_size - 1 && value[k] != '\0') {
        dest[d++] = value[k++];
    }
    // 将 token 剩余部分追加到 dest 中
    k = 0;
    while (d < dest_size - 1 && token[i + k] != '\0') {
        dest[d++] = token[i + k++];
    }
    dest[d] = '\0';
	// printf("dest: %s\n", dest);
}

/* 对 argv 数组中的每个参数进行变量展开 */
void expand_argv(int argc, char **argv) {
    for (int i = 0; i < argc; i++) {
        if (argv[i][0] == '$') {
            char expanded[128];
            expand_token(argv[i], expanded, sizeof(expanded));
            /* 用展开后的字符串替换原 token */
            strcpy(argv[i], expanded);
			// printf("expand: %s\n", expanded);
        }
    }
}

#define MAXARGS 128

int parsecmd(char **argv, int *rightpipe) {
	int argc = 0;
	while (1) {
		char *t;
		int fd, r;
		int c = gettoken(0, &t);
		switch (c) {
		case 0:
			return argc;
		case 'w':
			if (argc >= MAXARGS) {
				debugf("too many arguments\n");
				exit();
			}
			argv[argc++] = t;
			break;
		case '<':
			if (gettoken(0, &t) != 'w') {
				debugf("syntax error: < not followed by word\n");
				exit();
			}
			// Open 't' for reading, dup it onto fd 0, and then close the original fd.
			// If the 'open' function encounters an error,
			// utilize 'debugf' to print relevant messages,
			// and subsequently terminate the process using 'exit'.
			/* Exercise 6.5: Your code here. (1/3) */
			fd = open(t, O_RDONLY);
			if (fd < 0) {
				debugf("failed to open '%s'\n", t);
				exit();
			}
			dup(fd, 0);
			close(fd);
			// user_panic("< redirection not implemented");

			break;
		case '>':
			if (gettoken(0, &t) != 'w') {
				debugf("syntax error: > not followed by word\n");
				exit();
			}
			// Open 't' for writing, create it if not exist and trunc it if exist, dup
			// it onto fd 1, and then close the original fd.
			// If the 'open' function encounters an error,
			// utilize 'debugf' to print relevant messages,
			// and subsequently terminate the process using 'exit'.
			/* Exercise 6.5: Your code here. (2/3) */
			fd = open(t, O_WRONLY | O_CREAT | O_TRUNC);
			if (fd < 0) {
				debugf("failed to open '%s'\n", t);
				exit();
			}
			dup(fd, 1);
			close(fd);
			// user_panic("> redirection not implemented");

			break;
		case '|':;
			/*
			 * First, allocate a pipe.
			 * Then fork, set '*rightpipe' to the returned child envid or zero.
			 * The child runs the right side of the pipe:
			 * - dup the read end of the pipe onto 0
			 * - close the read end of the pipe
			 * - close the write end of the pipe
			 * - and 'return parsecmd(argv, rightpipe)' again, to parse the rest of the
			 *   command line.
			 * The parent runs the left side of the pipe:
			 * - dup the write end of the pipe onto 1
			 * - close the write end of the pipe
			 * - close the read end of the pipe
			 * - and 'return argc', to execute the left of the pipeline.
			 */
			int p[2];
			/* Exercise 6.5: Your code here. (3/3) */
			if ((r = pipe(p)) != 0) {
				debugf("pipe: %d\n", r);
				exit();
			}
			if ((r = fork()) < 0) {
				debugf("fork: %d\n", r);
				exit();
			}
			*rightpipe = r;
			if (r == 0) {
				dup(p[0], 0);
				close(p[0]);
				close(p[1]);
				return parsecmd(argv, rightpipe);
			} else {
				dup(p[1], 1);
				close(p[1]);
				close(p[0]);
				return argc;
			}
			// user_panic("| not implemented");

			break;
		}
	}

	return argc;
}

/*
 * 内建 cd 命令实现：
 * 当用户输入 "cd dir" 时，根据 dir 是否为绝对路径或相对路径，
 * 检查路径合法性后修改当前工作目录。
 */
int chpwd(int argc, char **argv) {
    int r;
	if (argc == 1) {
		if ((r = chdir("/")) < 0) {
            printf("cd failed: %d\n", r);
            return 0;
			// exit();
        }
		return 0;
	}
    if (argc > 2) {
        printf("Too many args for cd command\n");
        return 1;
		exit();
	}
	struct Stat state;
    if (argv[1][0] == '/') { // 绝对路径处理
        if ((r = stat(argv[1], &state)) < 0) {
            printf("cd: The directory '%s' does not exist\n", argv[1]);
            return 1;
			exit();
		}
        if (!state.st_isdir) {
            printf("cd: '%s' is not a directory\n", argv[1]);
            return 1;
			exit();
		}
        if ((r = chdir(argv[1])) < 0) {
            printf("cd failed: %d\n", r);
            return 1;
			exit();
        }
    } else { // 相对路径处理
        char path[128];
        if ((r = getcwd(path)) < 0) {
            printf("cd failed: %d\n", r);
            return 1;
			exit();
        }
        // 特殊处理 ".."：退回上一级目录
        if (argv[1][0] == '.' && argv[1][1] == '.') {
            int len = strlen(path);
            while (len > 1 && path[len - 1] != '/') {
                path[len - 1] = '\0';
                len--;
            }
            if (len > 1) {
                path[len - 1] = '\0';
            }
			if (strlen(argv[1]) > 3) { // "../xxx"
				pathcat(path, argv[1] + 3);
				// printf("current path: %s\n", path);
			}
        } else {
            pathcat(path, argv[1]);
			// printf("current path: %s\n", path);
        }
		if ((r = open(path, O_RDONLY)) < 0) {
			printf("cd: The directory '%s' does not exist\n", argv[1]);
			return 1;
			exit();
		}
		close(r);
        if ((r = stat(path, &state)) < 0) {
            printf("cd: The directory '%s' does not exist\n", argv[1]);
            return 1;
			exit();
        }
        if (!state.st_isdir) {
            printf("cd: '%s' is not a directory\n", argv[1]);
            return 1;
			exit();
        }
        if ((r = chdir(path)) < 0) {
            printf("cd failed: %d\n", r);
            return 1;
			exit();
        }
    }
    return 0;
}

/*
 * 内建 pwd 命令实现：
 * 打印当前进程的工作目录
 */
int pwd(int argc) {
	if (argc != 1) {
		printf("pwd: expected 0 arguments; got %d\n", argc);
		return 2;
		// return;
	}
    char path[128];
    int r;
    if ((r = getcwd(path)) < 0) {
        printf("pwd failed: %d\n", r);
        return 2;
		// return;
    }
    printf("%s\n", path);
	return 0;
}

static void strncpy(char *dest, const char *src, int n) {
    int i = 0;
    while (i < n - 1 && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

/* 内建指令 declare 实现
   格式: declare [-r] [-x] [NAME[=VALUE]]
   其中：-r 表示只读，-x 表示环境变量（caller_shell_id 置为 0 为全局，否则传入shell的id）
*/
int declare(int argc, char **argv, int global_shell_id) {
    int perm = 0; /* 默认为可修改 */
    int export_flag = 0;  /* export_flag==1 表示全局环境变量 */
    int i = 1;
    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        for (int j = 1; argv[i][j] != '\0'; j++) {
            if (argv[i][j] == 'r') {
                perm = 1;
            } else if (argv[i][j] == 'x') {
                export_flag = 1;
            } else {
                printf("declare: unknown flag -%c\n", argv[i][j]);
                return 1;
            }
        }
        i++;
    }
    if (i >= argc) {
        /* 若无后续参数，则输出所有变量 */
        char all[1024];
        syscall_get_all_var(all, sizeof(all));
        printf("%s", all);
        return 0;
    }
    /* 处理 NAME[=VALUE] */
    char *arg = argv[i];
    char name[MAX_VAR_NAME + 1];
    char value[MAX_VAR_VALUE + 1];
    value[0] = '\0';
    const char *eq = strchr(arg, '=');
    if (eq) {
        int nlen = eq - arg + 1;
        if (nlen > MAX_VAR_NAME)
            nlen = MAX_VAR_NAME;
        strncpy(name, arg, nlen);
        strncpy(value, eq + 1, MAX_VAR_VALUE);
    } else {
        strncpy(name, arg, MAX_VAR_NAME);
    }
    int caller = export_flag ? 0 : global_shell_id;
	printf("name: %s, value: %s, caller: %d\n", name, value, caller);
	int ret = syscall_declare_var(name, value, perm, caller);
    if (ret != 0) {
        printf("declare: failed to declare variable %s\n", name);
        return 1;
    }
    // /* 输出所有全局变量 */
    // char all[1024];
    // int len = syscall_get_all_var(all, sizeof(all));
    // printf("%s", all);
    return 0;
}

/* 内建指令 unset 实现 */
int unset(int argc, char **argv, int global_shell_id) {
    if (argc < 2) {
        printf("unset: missing variable name\n");
        return 1;
    }
    int ret = syscall_unset_var(argv[1], global_shell_id);
    if (ret != 0) {
        printf("unset: failed to remove variable %s\n", argv[1]);
        return 1;
    }
    return 0;
}

void runcmd(char *s) {
	gettoken(s, 0);

	char *argv[MAXARGS];
	int rightpipe = 0;
	int r;
	int argc = parsecmd(argv, &rightpipe);
	if (argc == 0) {
		return;
	}
	argv[argc] = 0;

	/* 对参数进行变量展开 */
    expand_argv(argc, argv);

	// 对于内建指令
	// printf("do incmd argc: %d\n", argc);
	if (argc > 0) {
		// printf("do cd2\n");
		if (strcmp(argv[0], "cd") == 0) {
			// printf("do cd3\n");
			if ((r = chpwd(argc, argv)) != 0) {
				return;
			}
			return;
		} else if (strcmp(argv[0], "pwd") == 0) {
			if ((r = pwd(argc)) != 0) {
				return;
			}
			return;
		} else if (strcmp(argv[0], "declare") == 0) {
            if ((r = declare(argc, argv, global_shell_id)) != 0) {
                return;
            }
            return;
        } else if (strcmp(argv[0], "unset") == 0) {
            if ((r = unset(argc, argv, global_shell_id)) != 0) {
                return;
            }
            return;
        }
    }

	int child = spawn(argv[0], argv);
	if (child < 0) {
        char cmd[1024];
        int len = strlen(argv[0]);
        strcpy(cmd, argv[0]);
        if (len >= 2 && cmd[len - 2] == '.' && cmd[len - 1] == 'b') {
            cmd[len - 2] = '\0';
        } else {
            cmd[len] = '.';
            cmd[len + 1] = 'b';
            cmd[len + 2] = '\0';
        }
        child = spawn(cmd, argv);
    }
	close_all();
	if (child >= 0) {
		wait(child);
	} else {
		debugf("spawn %s: %d\n", argv[0], child);
	}
	if (rightpipe) {
		wait(rightpipe);
	}
	exit();
}

void readline(char *buf, u_int n) {
	int r;
	for (int i = 0; i < n; i++) {
		if ((r = read(0, buf + i, 1)) != 1) {
			if (r < 0) {
				debugf("read error: %d\n", r);
			}
			exit();
		}
		if (buf[i] == '\b' || buf[i] == 0x7f) {
			if (i > 0) {
				i -= 2;
			} else {
				i = -1;
			}
			if (buf[i] != '\b') {
				printf("\b");
			}
		}
		if (buf[i] == '\r' || buf[i] == '\n') {
			buf[i] = 0;
			return;
		}
	}
	debugf("line too long\n");
	while ((r = read(0, buf, 1)) == 1 && buf[0] != '\r' && buf[0] != '\n') {
		;
	}
	buf[0] = 0;
}

char buf[1024];

void usage(void) {
	printf("usage: sh [-ix] [script-file]\n");
	exit();
}

int startswith(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s != *prefix)
            return 0;
        s++;
        prefix++;
    }
    return (*s == '\0' || strchr(WHITESPACE, *s) != 0);
}

int main(int argc, char **argv) {
	int r;
	int interactive = iscons(0);
	int echocmds = 0;

	printf("\n:::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::\n");
	printf("::                                                         ::\n");
	printf("::                     MOS Shell 2025                      ::\n");
	printf("::                                                         ::\n");
	printf(":::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::\n");
	ARGBEGIN {
	case 'i':
		interactive = 1;
		break;
	case 'x':
		echocmds = 1;
		break;
	default:
		usage();
	}
	ARGEND

	if (argc > 1) {
		usage();
	}
	if (argc == 1) {
		close(0);
		if ((r = open(argv[0], O_RDONLY)) < 0) {
			user_panic("open %s: %d", argv[0], r);
		}
		if ((r = chdir("/")) < 0) {
			printf("created root path failed: %d\n", r);
		}
		user_assert(r == 0);
	}

	global_shell_id = syscall_alloc_shell_id();

	for (;;) {
		if (interactive) {
			printf("\n$ ");
		}
		readline(buf, sizeof buf);

		if (buf[0] == '#') {
			continue;
		}
		if (echocmds) {
			printf("# %s\n", buf);
		}
		// 根据 buf 的起始判断是否为内建命令 exit、cd 或 pwd
        if (startswith(buf, "exit")) {
			exit();
            break; // 直接退出循环
        }
        if (startswith(buf, "cd") || startswith(buf, "pwd") 
			|| startswith(buf, "declare") || startswith(buf, "unset")) {
            runcmd(buf);
            continue;
        }
		if ((r = fork()) < 0) {
			user_panic("fork: %d", r);
		}
		if (r == 0) {
			runcmd(buf);
			exit();
		} else {
			wait(r);
		}
	}
	return 0;
}
