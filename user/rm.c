#include <lib.h>

void usage(void) {
    printf("Usage: rm [-r|-rf] <file_or_dir>\n");
    exit();
}

int main(int argc, char **argv) {
    int recursive = 0, force = 0;
    char *path = NULL;

    if (argc == 2) {
        // rm <file_or_dir>
        path = argv[1];
    } else if (argc == 3) {
        // rm -r <file_or_dir> 或 rm -rf <file_or_dir>
        if (strcmp(argv[1], "-r") == 0) {
            recursive = 1;
        } else if (strcmp(argv[1], "-rf") == 0 || strcmp(argv[1], "-fr") == 0) {
            recursive = 1;
            force = 1;
        } else {
            usage();
        }
        path = argv[2];
    } else {
        usage();
    }
    // printf("rm: path is %s\n", path);

    struct Stat st;
    int r;
    if ((r = stat(path, &st)) < 0) {
        // 文件或目录不存在
        if (!force) {
            printf("rm: cannot remove '%s': No such file or directory\n", path);
        }
        return 1;
    }

    // 如果目标是目录，但没有 -r 参数，直接输出错误信息
    if (st.st_isdir && !recursive) {
        printf("rm: cannot remove '%s': Is a directory\n", path);
        return 1;
    }

    // 调用 remove 删除（无论是文件还是目录）
    if ((r = remove(path)) < 0) {
        if (!force) {
            printf("rm: cannot remove '%s': No such file or directory\n", path);
        }
        return 1;
    }
    return 0;
}