/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <unistd.h>

int main(void) {
    /* 持续休眠，模拟长期运行的服务进程 */
    while (1) {
        sleep(60);
    }
    return 0;
}
