#include "application.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHUNK_SIZE 256

int application(const char *serialPort, const char *role, const char *filename) {
    LinkLayer ll;

    strcpy(ll.serialPort, serialPort);
    ll.role = (strcmp(role, "tx") == 0) ? tx : rx;
    ll.baudRate = 38400;
    ll.nRetransmissions = 3;
    ll.timeout = 3;

    int fd = llopen(ll);
    if (fd < 0) {
        printf("llopen failed\n");
        return -1;
    }

    if (ll.role == tx) {
        FILE *file = fopen(filename, "rb");
        if (file == NULL) {
            perror("fopen");
            llclose(0);
            return -1;
        }

        unsigned char buffer[CHUNK_SIZE];
        int bytesRead;

        while ((bytesRead = fread(buffer, 1, CHUNK_SIZE, file)) > 0) {
            if (llwrite(buffer, bytesRead) < 0) {
                printf("llwrite failed\n");
                fclose(file);
                llclose(0);
                return -1;
            }
        }

        // frame vazia = fim do ficheiro
        if (llwrite(buffer, 0) < 0) {
            printf("failed to send end marker\n");
            fclose(file);
            llclose(0);
            return -1;
        }

        fclose(file);

        if (llclose(0) < 0) {
            printf("llclose failed\n");
            return -1;
        }

        printf("File sent successfully\n");
    } else {
        FILE *file = fopen(filename, "wb");
        if (file == NULL) {
            perror("fopen");
            llclose(0);
            return -1;
        }

        unsigned char buffer[CHUNK_SIZE];

        while (1) {
            int bytesRead = llread(buffer);

            if (bytesRead < 0) {
                printf("llread failed\n");
                fclose(file);
                llclose(0);
                return -1;
            }

            if (bytesRead == 0) {
                // frame vazia = fim do ficheiro
                break;
            }

            fwrite(buffer, 1, bytesRead, file);
        }

        fclose(file);

        if (llclose(0) < 0) {
            printf("llclose failed\n");
            return -1;
        }

        printf("File received successfully\n");
    }

    return 0;
}