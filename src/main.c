#include "application.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Usage: %s <serial_port> <tx|rx> <filename>\n", argv[0]);
        printf("Example: %s /dev/ttyS0 tx penguin.gif\n", argv[0]);
        return 1;
    }

    return application(argv[1], argv[2], argv[3]);
}
