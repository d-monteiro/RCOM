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
        // Abrir ficheiro para leitura
        FILE *file = fopen(filename, "rb");
        if (file == NULL) {
            perror("fopen");
            llclose(0);
            return -1;
        }

        unsigned char buffer[CHUNK_SIZE];
        int bytesRead;

        // Ler ficheiro em bocados e enviar com llwrite
        while ((bytesRead = fread(buffer, 1, CHUNK_SIZE, file)) > 0) {
            if (llwrite(buffer, bytesRead) < 0) {
                printf("llwrite failed\n");
                fclose(file);
                llclose(0);
                return -1;
            }
        }

        fclose(file);
        printf("File sent successfully\n");
    }
    else {
        // Abrir ficheiro para escrita
        FILE *file = fopen(filename, "wb");
        if (file == NULL) {
            perror("fopen");
            llclose(0);
            return -1;
        }

        unsigned char packet[CHUNK_SIZE];
        int bytesRead;

        // Receber dados com llread e escrever no ficheiro
        while ((bytesRead = llread(packet)) > 0) {
            fwrite(packet, 1, bytesRead, file);

            // Se vier menos que o chunk, assumimos fim do ficheiro
            if (bytesRead < CHUNK_SIZE) {
                break;
            }
        }

        fclose(file);
        printf("File received successfully\n");
    }
        // TODO: receber START packet, extrair nome e tamanho
        // TODO: loop: receber DATA packets, escrever no ficheiro
        // TODO: receber END packet
    llclose(0);
    return 0;
}
