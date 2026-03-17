#include "application.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        // TODO: abrir ficheiro
        // TODO: enviar START packet (nome + tamanho)
        // TODO: ler ficheiro em bocados, enviar DATA packets
        // TODO: enviar END packet
    } else {
        // TODO: receber START packet, extrair nome e tamanho
        // TODO: loop: receber DATA packets, escrever no ficheiro
        // TODO: receber END packet
    }

    llclose(0);
    return 0;
}
