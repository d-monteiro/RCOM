#include "link_layer.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>

// Estado global da conexao (partilhado entre llopen/llwrite/llread/llclose)
static int connFd = -1;
static LinkLayer connParams;
static struct termios oldtio; // para restaurar no llclose

// Alarm
static int alarmEnabled = 0;
static int alarmCount = 0;

static void alarmHandler(int signal) {
    alarmEnabled = 0;
    alarmCount++;
}

int llopen(LinkLayer connectionParameters) {
    // TODO: abrir porta serie, configurar termios
    // TODO: Tx -> envia SET, espera UA (com timer)
    // TODO: Rx -> espera SET, envia UA

    // Alarm setup
    struct sigaction act = {0};
    act.sa_handler = &alarmHandler;


    // Guardar parametros para as outras funcoes usarem
    connParams = connectionParameters;

    int fd = open(connectionParameters.serialPort, O_RDWR | O_NOCTTY);

    if (fd < 0)
    {
        perror(connectionParameters.serialPort);
        return -1;
    }

    // Guardar config antiga da porta serie (para restaurar no llclose)
    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        return -1;
    }

    if (sigaction(SIGALRM, &act, NULL) == -1)
    {
        perror("sigaction");
        return -1;
    }

    struct termios newtio;

    

    // Clear struct for new port settings
    memset(&newtio, 0, sizeof(newtio));

    

    
    newtio.c_cflag = B38400 | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    // Set input mode (non-canonical, no echo,...)
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 0; // Inter-character timer unused
    newtio.c_cc[VMIN] = 0;  // Blocking read until 1 chars received

    // VTIME e VMIN should be changed in order to protect with a
    // timeout the reception of the following character(s)

    // Now clean the line and activate the settings for the port
    // tcflush() discards data written to the object referred to
    // by fd but not transmitted, or data received but not read,
    // depending on the value of queue_selector:
    //   TCIFLUSH - flushes data received but not read.
    tcflush(fd, TCIOFLUSH);
    
    // Set new port settings
    if (tcsetattr(fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        return -1;
    }

    printf("DLL configured\n");

    if (connectionParameters.role == tx) {
        // Montar trama SET
        unsigned char set_frame[5] = {FLAG, A_TX, C_SET, A_TX ^ C_SET, FLAG};

        // State machine states
        enum { START, FLAG_RCV, A_RCV, C_RCV, BCC_OK, STOP } state = START;

        alarmCount = 0;

        while (alarmCount < connectionParameters.nRetransmissions && state != STOP) {
            // Enviar SET
            write(fd, set_frame, 5);
            printf("SET sent (%d/%d)\n", alarmCount + 1, connectionParameters.nRetransmissions);

            // Ativar alarme
            alarmEnabled = 1;
            alarm(connectionParameters.timeout);

            // Ler UA byte a byte com state machine
            unsigned char byte;
            while (alarmEnabled && state != STOP) {
                int res = read(fd, &byte, 1);
                if (res <= 0) continue;

                switch (state) {
                    case START:
                        if (byte == FLAG) state = FLAG_RCV;
                        break;
                    case FLAG_RCV:
                        if (byte == A_TX) state = A_RCV;
                        else if (byte != FLAG) state = START;
                        break;
                    case A_RCV:
                        if (byte == C_UA) state = C_RCV;
                        else if (byte == FLAG) state = FLAG_RCV;
                        else state = START;
                        break;
                    case C_RCV:
                        if (byte == (A_TX ^ C_UA)) state = BCC_OK;
                        else if (byte == FLAG) state = FLAG_RCV;
                        else state = START;
                        break;
                    case BCC_OK:
                        if (byte == FLAG) state = STOP;
                        else state = START;
                        break;
                    default:
                        break;
                }
            }
        }

        alarm(0); // Cancelar alarme pendente

        if (state != STOP) {
            printf("ERROR: UA not received after %d retransmissions\n", connectionParameters.nRetransmissions);
            return -1;
        }

        printf("UA received - connection established\n");
    }

    else {
        // Esperar trama SET com state machine
        enum { START, FLAG_RCV, A_RCV, C_RCV, BCC_OK, STOP } state = START;
        unsigned char byte;

        while (state != STOP) {
            int res = read(fd, &byte, 1);
            if (res <= 0) continue;

            switch (state) {
                case START:
                    if (byte == FLAG) state = FLAG_RCV;
                    break;
                case FLAG_RCV:
                    if (byte == A_TX) state = A_RCV;
                    else if (byte != FLAG) state = START;
                    break;
                case A_RCV:
                    if (byte == C_SET) state = C_RCV;
                    else if (byte == FLAG) state = FLAG_RCV;
                    else state = START;
                    break;
                case C_RCV:
                    if (byte == (A_TX ^ C_SET)) state = BCC_OK;
                    else if (byte == FLAG) state = FLAG_RCV;
                    else state = START;
                    break;
                case BCC_OK:
                    if (byte == FLAG) state = STOP;
                    else state = START;
                    break;
                default:
                    break;
            }
        }

        printf("SET received\n");

        // Enviar UA
        unsigned char ua_frame[5] = {FLAG, A_TX, C_UA, A_TX ^ C_UA, FLAG};
        write(fd, ua_frame, 5);

        printf("UA sent - connection established\n");
    }

    connFd = fd; // guardar para llwrite/llread/llclose
    return fd;
}

int llwrite(const unsigned char *buf, int bufSize) {
    // TODO: construir frame I (header + stuffing + BCC2)
    // TODO: enviar frame, esperar RR/REJ (com timer + retransmissoes)
    return -1;
}

int llread(unsigned char *packet) {
    // TODO: ler frame I byte a byte (state machine)
    // TODO: destuffing, verificar BCC2
    // TODO: enviar RR ou REJ
    return -1;
}

int llclose(int showStatistics) {
    if (connFd < 0) return -1;

    unsigned char byte;
    enum { START, FLAG_RCV, A_RCV, C_RCV, BCC_OK, STOP } state;

    if (connParams.role == tx) {
        // === TRANSMITTER ===
        // 1) Envia DISC, espera DISC do receiver (com timer + retransmissoes)
        unsigned char disc_frame[5] = {FLAG, A_TX, C_DISC, A_TX ^ C_DISC, FLAG};

        state = START;
        alarmCount = 0;

        while (alarmCount < connParams.nRetransmissions && state != STOP) {
            write(connFd, disc_frame, 5);
            printf("DISC sent (%d/%d)\n", alarmCount + 1, connParams.nRetransmissions);

            alarmEnabled = 1;
            alarm(connParams.timeout);

            while (alarmEnabled && state != STOP) {
                int res = read(connFd, &byte, 1);
                if (res <= 0) continue;

                switch (state) {
                    case START:
                        if (byte == FLAG) state = FLAG_RCV;
                        break;
                    case FLAG_RCV:
                        if (byte == A_RX) state = A_RCV;
                        else if (byte != FLAG) state = START;
                        break;
                    case A_RCV:
                        if (byte == C_DISC) state = C_RCV;
                        else if (byte == FLAG) state = FLAG_RCV;
                        else state = START;
                        break;
                    case C_RCV:
                        if (byte == (A_RX ^ C_DISC)) state = BCC_OK;
                        else if (byte == FLAG) state = FLAG_RCV;
                        else state = START;
                        break;
                    case BCC_OK:
                        if (byte == FLAG) state = STOP;
                        else state = START;
                        break;
                    default:
                        break;
                }
            }
        }

        alarm(0);

        if (state != STOP) {
            printf("ERROR: DISC response not received\n");
            return -1;
        }

        // 2) Recebeu DISC do receiver -> responde com UA
        unsigned char ua_frame[5] = {FLAG, A_RX, C_UA, A_RX ^ C_UA, FLAG};
        write(connFd, ua_frame, 5);
        printf("UA sent - connection closed\n");

    } else {
        // === RECEIVER ===
        // 1) Espera DISC do transmitter
        state = START;

        while (state != STOP) {
            int res = read(connFd, &byte, 1);
            if (res <= 0) continue;

            switch (state) {
                case START:
                    if (byte == FLAG) state = FLAG_RCV;
                    break;
                case FLAG_RCV:
                    if (byte == A_TX) state = A_RCV;
                    else if (byte != FLAG) state = START;
                    break;
                case A_RCV:
                    if (byte == C_DISC) state = C_RCV;
                    else if (byte == FLAG) state = FLAG_RCV;
                    else state = START;
                    break;
                case C_RCV:
                    if (byte == (A_TX ^ C_DISC)) state = BCC_OK;
                    else if (byte == FLAG) state = FLAG_RCV;
                    else state = START;
                    break;
                case BCC_OK:
                    if (byte == FLAG) state = STOP;
                    else state = START;
                    break;
                default:
                    break;
            }
        }

        printf("DISC received\n");

        // 2) Responde com DISC
        unsigned char disc_frame[5] = {FLAG, A_RX, C_DISC, A_RX ^ C_DISC, FLAG};
        write(connFd, disc_frame, 5);
        printf("DISC sent\n");

        // 3) Espera UA do transmitter
        state = START;

        while (state != STOP) {
            int res = read(connFd, &byte, 1);
            if (res <= 0) continue;

            switch (state) {
                case START:
                    if (byte == FLAG) state = FLAG_RCV;
                    break;
                case FLAG_RCV:
                    if (byte == A_RX) state = A_RCV;
                    else if (byte != FLAG) state = START;
                    break;
                case A_RCV:
                    if (byte == C_UA) state = C_RCV;
                    else if (byte == FLAG) state = FLAG_RCV;
                    else state = START;
                    break;
                case C_RCV:
                    if (byte == (A_RX ^ C_UA)) state = BCC_OK;
                    else if (byte == FLAG) state = FLAG_RCV;
                    else state = START;
                    break;
                case BCC_OK:
                    if (byte == FLAG) state = STOP;
                    else state = START;
                    break;
                default:
                    break;
            }
        }

        printf("UA received - connection closed\n");
    }

    // Restaurar configuracao antiga da porta serie e fechar
    if (tcsetattr(connFd, TCSANOW, &oldtio) == -1) {
        perror("tcsetattr");
        return -1;
    }

    close(connFd);
    connFd = -1;

    return 0;
}
