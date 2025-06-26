#include <args.h>
#include <lib.h>

#define WHITESPACE " \t\r\n"
#define SYMBOLS "<|>&;()"

#define HISTFILESIZE 20
char histcmd[HISTFILESIZE][1024] = {{0}};
int hist_latest = 0; // Index to write the *next* command
int hist_count = 0;  // Total number of commands in history

int global_shell_id;


void runcmd(char *);
static void strncpy(char *, const char *, int);

int process_backquote(const char *s, char *output, int outsize, int *consumed) {
    // s[0] 应为反引号 '`'，查找匹配的结束反引号
    const char *start = s + 1;
    const char *closing = strchr(start, '`');
    if (closing == NULL) {
        return -1;  // 未找到匹配的反引号
    }
    int cmd_len = closing - start;  // 反引号中命令的长度

    char cmd[1024];
    if (cmd_len >= (int)sizeof(cmd)) {
        cmd_len = sizeof(cmd) - 1;
    }
    // 提取反引号中的命令内容
    strncpy(cmd, start, cmd_len + 1);
    cmd[cmd_len] = '\0';
    // printf("sub_cmd: %s\n", cmd);
    // 创建管道，用于捕获子命令的标准输出
    int p[2];
    if (pipe(p) < 0) {
        return -1;
    }
    int pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        // --- 子进程 ---
        // 重定向标准输出到管道写端
        dup(p[1],1);
        close(p[0]);
        close(p[1]);
        runcmd(cmd);
        exit();
    } else {
        // --- 父进程 ---
        close(p[1]);
        int pos = 0, n;
        while ((n = read(p[0], output + pos, 1024)) > 0) {
            pos += n;
        }
        output[pos] = '\0';
        // printf("output: %s\n", output);
        close(p[0]);
        wait(pid);
    }
    *consumed = (closing - s) + 1;  // 包括起始和结束反引号
    return 0;
}
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

    // 检查是否为追加重定向 ">>"
    if (*s == '>' && *(s + 1) == '>') {
        *p1 = s;
        s[0] = '\0'; // 将 ">>" 所在位置用空字符截断
        s[1] = '\0';
        *p2 = s + 2; // p2 指向 ">>" 之后的位置
        return '+'; 
    }

	if (strchr(SYMBOLS, *s)) {
		int t = *s;
		*p1 = s;
		*s++ = 0;
		*p2 = s;
		return t;
	}

    // 如果遇到注释符 '#'，则将 '#' 后直到行末的部分视为注释，返回 '#' token
    if (*s == '#') {
        *p1 = s;
        while (*s && *s != '\n') {
            s++;
        }
        *p2 = s;
        return '#';
    }

	*p1 = s;
	while (*s && !strchr(WHITESPACE SYMBOLS, *s)) {
        if (*s == '`') {
            int consumed = 0;
            char substitution[1024] = {0};
            if (process_backquote(s, substitution, sizeof(substitution), &consumed) != 0) {
                debugf("Syntax error: unmatched backquote.\n");
                return -1;
            }
            // 保存反引号之后的剩余部分
            char remainder[1024] = {0};
            strcpy(remainder, s + consumed);
            // 用 substitution 覆盖当前反引号表达式部分
            strcpy(s, substitution);
            // 将 remainder 接到 substitution 后面
            strcpy(s + strlen(substitution), remainder);
            // s 指针向后推进 substitution 部分的长度后继续解析
            s += strlen(substitution);
            continue;  // 继续判断下一个字符
        }
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

/* 将当前指令 buf 写入历史缓冲区，并刷新至 .mos_history 文件 */
void write_history(const char *buf) {
	int is_whitespace = 1;
	for (int i = 0; buf[i]; i++) {
		if (!strchr(WHITESPACE, buf[i])) {
			is_whitespace = 0;
			break;
		}
	}
	if (is_whitespace) {
		return;
	}

	strcpy(histcmd[hist_latest], buf);
	hist_latest = (hist_latest + 1) % HISTFILESIZE;
	if (hist_count < HISTFILESIZE) {
		hist_count++;
	}

	int fd = open("/.mos_history", O_CREAT | O_WRONLY | O_TRUNC);
	if (fd < 0) {
		return;
	}

	for (int i = 0; i < hist_count; i++) {
		int idx = (hist_latest - hist_count + i + HISTFILESIZE) % HISTFILESIZE;
		if (histcmd[idx][0] != '\0') {
			write(fd, histcmd[idx], strlen(histcmd[idx]));
			write(fd, "\n", 1);
		}
	}
	close(fd);
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
		case '#':
			return argc; // 注释
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
        case ';':
            if ((r = fork()) < 0) {
                debugf("fork: %d\n", r);
                exit();
            }
            *rightpipe = r;
            if (r == 0) {
                return argc;
            } else {
                wait(r);
                return parsecmd(argv, rightpipe);
            }
            break;

        case '+':
            if (gettoken(0, &t) != 'w') {
                debugf("syntax error: >> not followed by word\n");
                exit();
            }
            // 以只写方式打开文件；如果文件不存在，则创建它
            if ((fd = open(t, O_WRONLY | O_CREAT)) < 0) {
                debugf("open %s for append: %d\n", t, fd);
                exit();
            }
            // 将文件指针移动到文件末尾以实现追加
            // 1. 从文件描述符数字获取 Fd 结构体指针
            struct Fd *fd_struct;
            if ((r = fd_lookup(fd, &fd_struct)) < 0) {
                debugf("fd_lookup error for fd %d: %d\n", fd, r);
                exit();
            }
            // 2. 将 Fd 结构体转换为 Filefd 结构体以访问文件元数据
            struct Filefd *ffd = (struct Filefd *)fd_struct;
            // 3. 将文件描述符的偏移量设置为文件大小，实现追加效果
            fd_struct->fd_offset = ffd->f_file.f_size;
            // 将标准输出重定向到该文件
            dup(fd, 1);
            close(fd);
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
	// printf("name: %s, value: %s, caller: %d\n", name, value, caller);
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

/* 执行命令的内建指令 history 实现 */
void history() {
    int fd = open("/.mos_history", O_RDONLY);
    if (fd < 0) {
        debugf("cannot open /.mos_history\n");
        return;
    }
    char history_buf[4096];
    int n = read(fd, history_buf, sizeof(history_buf) - 1);
    if (n >= 0) {
        history_buf[n] = '\0';
        printf("%s", history_buf);
    }
    close(fd);
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
        } else if (strcmp(argv[0], "history") == 0) {
			history();
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

void *memmove(void *dest, const void *src, size_t n) {
    char *d = dest;
    const char *s = src;

    if (d == s) {
        return d;
    }

    if (d < s) {
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else {
        for (size_t i = n; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }
    return dest;
}

void refresh_line(const char *prompt, char *buf, int len, int cursor_pos) {
    // 1. 移动光标到行首
    printf("\r");
    // 2. 输出提示符
    printf("%s", prompt);
    // 3. 输出整个缓冲区内容
    printf("%s", buf);
    // 4. 清除光标后多余的旧字符（防止之前更长行残留）
    printf("\x1b[K");
    // 5. 将光标移回到正确的位置（提示符长度 + 当前光标位置）
    printf("\r\x1b[%dC", (int)strlen(prompt) + cursor_pos);
}

void readline(char *buf, u_int n) {
    int i = 0;             // 当前光标位置（在 buf 内索引）
    int len = 0;           // 当前缓冲区有效字符长度
    int hist_nav_idx = -1; // -1 表示当前未处于历史记录浏览模式，否则表示历史数组 histcmd 的索引
    char lastcmd[1024] = {0}; // 用于保存开始浏览历史前当前输入的内容
    char ch;
    const char *prompt = "$ "; // 提示符

    // 清空输入缓冲区
    buf[0] = '\0';

    while (1) {
        int r = read(0, &ch, 1);
        if (r < 1) {
            exit();
        }

        switch (ch) {
        // 回车或换行结束输入
        case '\r':
        case '\n':
            buf[len] = '\0';
            printf("\n");
            return;

        // Ctrl-A: 移动光标到行首 (ASCII 1)
        case 1:
            if (i > 0) {
                i = 0;
                refresh_line(prompt, buf, len, i);
            }
            break;

        // Ctrl-E: 移动光标到行尾 (ASCII 5)
        case 5:
            if (i < len) {
                i = len;
                refresh_line(prompt, buf, len, i);
            }
            break;

		// Ctrl-K: 删除从当前光标位置到行尾 (ASCII 11)
		case 11: 
			if (i < len) {
				buf[i] = '\0';
				len = i;
				refresh_line(prompt, buf, len, i);
			}
			break;

		// Ctrl-U: 删除从行首到当前光标前的所有字符 (ASCII 21)
		case 21: 
			if (i > 0) {
				memmove(&buf[0], &buf[i], len - i + 1);
				len -= i;
				i = 0;
				refresh_line(prompt, buf, len, i);
			}
			break;
		
        // Ctrl-W: 删除光标左侧最近一个单词 (ASCII 23)
        case 23: {
            if (i > 0) {
                int end_pos = i;
                int start_pos = i;
                // 先向左跳过空白字符
                while (start_pos > 0 && strchr(WHITESPACE, buf[start_pos - 1])) {
                    start_pos--;
                }
                // 再向左删除连续非空白字符
                while (start_pos > 0 && !strchr(WHITESPACE, buf[start_pos - 1])) {
                    start_pos--;
                }
                // 删除的字符个数
                int numDeleted = i - start_pos;
                // 使用 memmove 将 buf[i...] 向前覆盖删除的区域
                memmove(&buf[start_pos], &buf[end_pos], len - end_pos + 1); // 包括结束符
                len -= numDeleted;
                i = start_pos;
                refresh_line(prompt, buf, len, i);
            }
            break;
        }

        // Backspace: 删除光标左侧的一个字符 (ASCII 8)
		case 127:
        case '\b':
            if (i > 0) {
                // 删除光标左侧的字符并将后面的字符左移覆盖
                memmove(&buf[i - 1], &buf[i], len - i + 1);
                i--;
                len--;
                refresh_line(prompt, buf, len, i);
            }
            break;

        // ESC 序列，处理方向键（上下左右）
        case 27: {
            char seq[3];
            if (read(0, &seq[0], 1) != 1) continue;
            if (read(0, &seq[1], 1) != 1) continue;
            if (seq[0] == '[') {
                // 如果第一次进入历史浏览模式，则保存当前行
                if (hist_nav_idx == -1) {
                    strcpy(lastcmd, buf);
                }
                switch (seq[1]) {
                case 'A': // 上箭头：浏览上一条历史记录
					printf("\x1b[B"); // 限制光标移动
					if (hist_count == 0) continue; // 若没有历史记录，则忽略
					// 计算出最旧的一条历史记录的索引
					int oldest_idx = (hist_latest - hist_count + HISTFILESIZE) % HISTFILESIZE;

					if (hist_nav_idx == -1) {
						// 第一次按上箭头，从最新记录开始向前浏览
						hist_nav_idx = (hist_latest + HISTFILESIZE - 1) % HISTFILESIZE;
					} else {
						// 如果当前不是最旧的记录，才继续向前
						if (hist_nav_idx != oldest_idx) {
							hist_nav_idx = (hist_nav_idx + HISTFILESIZE - 1) % HISTFILESIZE;
						}
					}
					
					// 由于 histcmd 是循环数组，可能包含旧的空字符串，但此逻辑下不会访问到未赋值的区域
					strcpy(buf, histcmd[hist_nav_idx]);
					len = strlen(buf);
					i = len;
					refresh_line(prompt, buf, len, i);
					break;

                case 'B': // 下箭头：浏览下一条历史记录
					// printf("\x1b[A"); // 限制光标
                    if (hist_nav_idx == -1) continue; // 没有进入历史浏览模式则忽略
                    // 如果已处于最新历史记录，则退出历史浏览模式返回之前行内容
                    if (hist_nav_idx == (hist_latest + HISTFILESIZE - 1) % HISTFILESIZE) {
                        hist_nav_idx = -1;
                        strcpy(buf, lastcmd);
                    } else {
                        hist_nav_idx = (hist_nav_idx + 1) % HISTFILESIZE;
                        while (histcmd[hist_nav_idx][0] == '\0' && hist_nav_idx != (hist_latest + HISTFILESIZE - 1) % HISTFILESIZE) {
                            hist_nav_idx = (hist_nav_idx + 1) % HISTFILESIZE;
                        }
                        strcpy(buf, histcmd[hist_nav_idx]);
                    }
                    len = strlen(buf);
                    i = len;
                    refresh_line(prompt, buf, len, i);
                    break;

                case 'C': // 右箭头：光标向右移动
                    if (i < len) {
                        i++;
                    } else {
                        printf("\x1b[D");
					}
                    break;

                case 'D': // 左箭头：光标向左移动
                    if (i > 0) {
                        i--;
                    } else {
						printf("\x1b[C");
					}
                    break;
                }
            }
            break;
        }

        // 可打印字符的插入处理（普通字符插入）
        default:
            if (ch >= 32 && ch < 127) { // 若字符可打印
                if (len < n - 1) {
                    // 如果光标不在行尾，则先将后面的字符后移，为新字符预留位置
                    memmove(&buf[i + 1], &buf[i], len - i + 1);
                    buf[i] = ch; // 插入新字符
                    len++;
                    i++;
                    refresh_line(prompt, buf, len, i);
                }
            }
            break;
        }
    }
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

	// --- New, Professional Box-style Welcome Screen ---
    // Using box-drawing characters for a clean, window-like border.
    // \x1b[1;36m -> Bright Cyan for the border and details
    // \x1b[1;37m -> Bright White for the main logo
    // \x1b[1;33m -> Bright Yellow for the title
    // \x1b[1;35m -> Bright Magenta for the subtitle
    // \x1b[0m    -> Reset color
    printf("\n");
    printf("\x1b[1;36m╔═══════════════════════════════════════════════════════════════════════════╗\x1b[0m\n");
    printf("\x1b[1;36m║\x1b[0m                                                                           \x1b[1;36m║\x1b[0m\n");
    printf("\x1b[1;36m║\x1b[0m    \x1b[1;37m               ███╗   ███╗   ██████╗    ███████╗                       \x1b[1;36m║\x1b[0m\n");
    printf("\x1b[1;36m║\x1b[0m    \x1b[1;37m               ████╗ ████║  ██╔═══██╗   ██╔════╝                       \x1b[1;36m║\x1b[0m\n");
    printf("\x1b[1;36m║\x1b[0m    \x1b[1;37m               ██╔████╔██║  ██║   ██║   ███████╗                       \x1b[1;36m║\x1b[0m\n");
    printf("\x1b[1;36m║\x1b[0m    \x1b[1;37m               ██║╚██╔╝██║  ██║   ██║   ╚════██║                       \x1b[1;36m║\x1b[0m\n");
    printf("\x1b[1;36m║\x1b[0m    \x1b[1;37m               ██║ ╚═╝ ██║  ╚██████╔╝   ███████║                       \x1b[1;36m║\x1b[0m\n");
    printf("\x1b[1;36m║\x1b[0m    \x1b[1;37m               ╚═╝     ╚═╝   ╚═════╝    ╚══════╝                       \x1b[1;36m║\x1b[0m\n");
    printf("\x1b[1;36m║\x1b[0m                                                                           \x1b[1;36m║\x1b[0m\n");
    printf("\x1b[1;36m╟───────────────────────────────────────────────────────────────────────────╢\x1b[0m\n");
    printf("\x1b[1;36m║\x1b[0m                       \x1b[1;33m  WELCOME TO THE MOS SHELL  \x1b[0m                        \x1b[1;36m║\x1b[0m\n");
    printf("\x1b[1;36m║\x1b[0m                           \x1b[1;35m  ~ BUAA-OS-2025 ~  \x1b[0m                            \x1b[1;36m║\x1b[0m\n");
    printf("\x1b[1;36m╚═══════════════════════════════════════════════════════════════════════════╝\x1b[0m\n");
    printf("\x1b[0m"); // Reset color completely

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

		write_history(buf); // 保存历史指令到.mos-history
		
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
			|| startswith(buf, "declare") || startswith(buf, "unset")
			|| startswith(buf, "history")) {
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
