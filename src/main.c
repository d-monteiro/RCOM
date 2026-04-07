#include "application.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Usage: %s <serial_port> <tx|rx> <filename> [fer] [tprop_ms] [frame_size]\n", argv[0]);
        printf("Example: %s /dev/ttyS10 tx penguin.gif 0.1 50 256\n", argv[0]);
        return 1;
    }

    double fer = 0.0;
    int tprop = 0;
    int frameSize = 256;

    if (argc > 4) {
        fer = atof(argv[4]);
    }
    if (argc > 5) {
        tprop = atoi(argv[5]);
    }
    if (argc > 6) {
        frameSize = atoi(argv[6]);
    }

    // para a simulacao de FER
    srand(time(NULL));

    return application(argv[1], argv[2], argv[3], fer, tprop, frameSize);
}
