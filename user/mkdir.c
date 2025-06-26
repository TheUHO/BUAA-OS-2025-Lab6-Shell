#include <lib.h>

int dir_exists(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        close(fd);
        return 1;
    }
    return 0;
}

// 递归创建目录（-p 参数）
int mkdir_recursive(const char *path) {
    char new_path[1024];
    int len = strlen(path);
    for (int i = 0; i <= len; i++) {
        if (path[i] == '/' || path[i] == '\0') {
            new_path[i] = '\0';
            if (strlen(new_path) > 0 && !dir_exists(new_path)) {
                // printf("mkdir: recursive make dir: %s\n", new_path);
                create(new_path, FTYPE_DIR);
            }
        }
        new_path[i] = path[i];
    }
    return 0;
}

int main(int argc, char **argv) {
    char *path;
    int p_flag = 0;
    int r;

    // 解析参数
    if (argc == 2) {
        path = argv[1];
    } else if (argc == 3 && strcmp(argv[1], "-p") == 0) {
        p_flag = 1;
        path = argv[2];
    } else {
        debugf("mkdir: use mkdir [-p] <dirname>\n");
        return 1;
    }

    int len = strlen(path);
    while (len > 0 && strchr(" \t\r\n", path[len - 1])) {
        path[--len] = '\0';
    }

    // 如果目录或文件已经存在
    if ((r = dir_exists(path)) != 0) {
        printf("mkdir: cannot create directory '%s': File exists\n", path);
        return 1;
    }

    if (!p_flag) {  // 单层创建
        if ((r = create(path, FTYPE_DIR)) != 0) {
            printf("mkdir: cannot create directory '%s': No such file or directory\n", path);
            return 1;
        }
        return 0;
    } else {  // 递归创建
        return mkdir_recursive(path);
    }
}