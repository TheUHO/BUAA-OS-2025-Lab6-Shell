#include <string.h>
#include <lib.h>

char *strcat(char *dest, const char *src) {
    char *orig = dest;
    // 移动到 dest 字符串末尾
    while (*dest) {
        dest++;
    }
    // 拷贝 src 到 dest 后面
    while ((*dest++ = *src++)) {
        ;
    }
    return orig;
}

char *strrchr(const char *s, int c) {
    char *last = 0;
    while (*s) {
        if (*s == (char)c) {
            last = (char *)s;
        }
        s++;
    }
    if (c == '\0') {
        return (char *)s;
    }
    return last;
}

int chdir(char *path) {
    return syscall_set_cur_path(path);
}

int getcwd(char *buf) {
    return syscall_get_cur_path(buf);
}

void pathcat(char *path, const char *relpath) {
    char temp[1024];
    // 以基路径的规范形式开始，如果 path 为 "/" 则直接复制，否则复制后去除结尾的 '/'
    strcpy(temp, path);
    int len = strlen(temp);
    if (len > 1 && temp[len - 1] == '/') {
        temp[len - 1] = '\0';
    }

    // 指针扫描 relpath
    const char *p = relpath;
    char token[256];
    while (*p != '\0') {
        // 跳过连续的 '/'
        while (*p == '/') p++;
        if (*p == '\0')
            break;
        // 读取一个路径段
        int i = 0;
        while (p[i] != '\0' && p[i] != '/') {
            token[i] = p[i];
            i++;
        }
        token[i] = '\0';
        p += i;

        if (strcmp(token, ".") == 0) {
            // 当前目录，不做处理
            continue;
        } else if (strcmp(token, "..") == 0) {
            // 回退到上一级目录，但不回退到空
            int tlen = strlen(temp);
            if (tlen > 1) {
                // 去除可能结尾的 '/'
                if (temp[tlen-1]=='/') {
                    temp[tlen-1] = '\0';
                    tlen--;
                }
                char *slash = strrchr(temp, '/');
                if (slash) {
                    *slash = '\0';
                    if (strlen(temp) == 0)
                        strcpy(temp, "/");
                }
            }
        } else {
            // 普通路径段，追加到 temp
            if (!(strcmp(temp, "/") == 0)) {
                strcat(temp, "/");
            }
            strcat(temp, token);
        }
    }
    // 将结果复制回 path
    strcpy(path, temp);
}