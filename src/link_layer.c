#include "link_layer.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>

static int g_fd = -1;
static LLRole g_role;
static struct termios oldtio;
static int g_timeout = 3;
static int g_nRetransmissions = 3;

// Alarm
static int alarmEnabled = 0;
static int alarmCount = 0;

// Sequence number for frames
static int seq_num = 0;

// Expected sequence for receiver
static int expected_seq = 0;

// Stats
static LinkLayerStats g_stats;

// Simulacao
static double g_fer = 0.0;
static int g_tprop = 0;

static void alarmHandler(int signal) {
    alarmEnabled = 0;
    alarmCount++;
}

int llopen(LinkLayer connectionParameters) {
    g_role = connectionParameters.role;
    g_timeout = connectionParameters.timeout;
    g_nRetransmissions = connectionParameters.nRetransmissions;
    seq_num = 0;
    expected_seq = 0;
    memset(&g_stats, 0, sizeof(g_stats));
    g_fer = connectionParameters.fer;
    g_tprop = connectionParameters.tprop;

    // TODO: abrir porta serie, configurar termios
    // TODO: Tx -> envia SET, espera UA (com timer)
    // TODO: Rx -> espera SET, envia UA

    // Alarm setup
    struct sigaction act = {0};
    act.sa_handler = &alarmHandler;

    // Open serial port device for reading and writing, and not as controlling tty
    // because we don't want to get killed if linenoise sends CTRL-C.
    int fd = open(connectionParameters.serialPort, O_RDWR | O_NOCTTY);

    if (fd < 0)
    {
        perror(connectionParameters.serialPort);
        return -1;
    }

    if (sigaction(SIGALRM, &act, NULL) == -1)
    {
        perror("sigaction");
        close(fd);
        return -1;
    }

    struct termios newtio;

    // Save current serial port settings so we can restore them later
    if (tcgetattr(fd, &oldtio) == -1) {
        perror("tcgetattr");
        close(fd);
        return -1;
    }

    // Clear struct for new port settings
    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = B38400 | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    // Set input mode (non-canonical, no echo,...)
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 0; // Inter-character timer unused
    newtio.c_cc[VMIN] = 1;  // Read without blocking

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
        close(fd);
        return -1;
    }

    g_fd = fd;

    printf("DLL configured\n");

    if (connectionParameters.role == tx) {
        // Montar trama SET
        unsigned char set_frame[5] = {FLAG, A_TX, C_SET, A_TX ^ C_SET, FLAG};

        // State machine states
        enum { START, FLAG_RCV, A_RCV, C_RCV, BCC_OK, STOP } state;

        alarmCount = 0;

        while (alarmCount < connectionParameters.nRetransmissions) {
            state = START;

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
                        else if (byte == FLAG) state = FLAG_RCV;
                        else state = START;
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

            alarm(0);

            if (state == STOP) {
                printf("UA received - connection established\n");
                return fd;
            }
        }

        printf("ERROR: UA not received after %d retransmissions\n",
               connectionParameters.nRetransmissions);
        return -1;
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
                    else if (byte == FLAG) state = FLAG_RCV;
                    else state = START;
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
        return fd;
    }
}

  
int llwrite(const unsigned char *buf, int bufSize) {
    if (g_fd < 0) return -1;

    unsigned char frame[2048];
    int frameSize = 0;

    unsigned char control = (seq_num % 2 == 0) ? C_I0 : C_I1;

    // === HEADER ===
    frame[frameSize++] = FLAG;
    frame[frameSize++] = A_TX;
    frame[frameSize++] = control;
    frame[frameSize++] = A_TX ^ control;   // BCC1

    // === DATA + STUFFING ===
    unsigned char bcc2 = 0;
    for (int i = 0; i < bufSize; i++) {
        bcc2 ^= buf[i];
        if (buf[i] == FLAG) {
            if (frameSize + 2 >= (int)sizeof(frame)) return -1;
            frame[frameSize++] = ESC;
            frame[frameSize++] = 0x5E;
        } else if (buf[i] == ESC) {
            if (frameSize + 2 >= (int)sizeof(frame)) return -1;
            frame[frameSize++] = ESC;
            frame[frameSize++] = 0x5D;
        } else {
            if (frameSize + 1 >= (int)sizeof(frame)) return -1;
            frame[frameSize++] = buf[i];
        }
    }

    // Stuff BCC2
    if (bcc2 == FLAG) {
        if (frameSize + 2 >= (int)sizeof(frame)) return -1;
        frame[frameSize++] = ESC;
        frame[frameSize++] = 0x5E;
    } else if (bcc2 == ESC) {
        if (frameSize + 2 >= (int)sizeof(frame)) return -1;
        frame[frameSize++] = ESC;
        frame[frameSize++] = 0x5D;
    } else {
        if (frameSize + 1 >= (int)sizeof(frame)) return -1;
        frame[frameSize++] = bcc2;
    }

    // === END FLAG ===
    if (frameSize + 1 >= (int)sizeof(frame)) return -1;
    frame[frameSize++] = FLAG;

    alarmCount = 0;

    while (alarmCount < g_nRetransmissions) {
        // enviar frame I
        write(g_fd, frame, frameSize);
        g_stats.frames_sent++;
        printf("I frame sent (seq %d, %d/%d)\n", seq_num % 2, alarmCount + 1, g_nRetransmissions);

        alarmEnabled = 1;
        alarm(g_timeout);

        int state = 0;
        unsigned char byte;
        unsigned char resp_control = 0;

        // esperar RR ou REJ
        while (alarmEnabled && state != 5) {
            int res = read(g_fd, &byte, 1);
            if (res <= 0) continue;

            switch (state) {
                case 0: // START
                    if (byte == FLAG) state = 1;
                    break;

                case 1: // FLAG_RCV
                    if (byte == A_TX) state = 2;
                    else if (byte == FLAG) state = 1;
                    else state = 0;
                    break;

                case 2: // A_RCV
                    if (byte == C_RR0 || byte == C_RR1 || byte == C_REJ0 || byte == C_REJ1) {
                        resp_control = byte;
                        state = 3;
                    }
                    else if (byte == FLAG) state = 1;
                    else state = 0;
                    break;

                case 3: // C_RCV
                    if (byte == (A_TX ^ resp_control)) state = 4;
                    else if (byte == FLAG) state = 1;
                    else state = 0;
                    break;

                case 4: // BCC_OK
                    if (byte == FLAG) state = 5;
                    else state = 0;
                    break;

                default:
                    break;
            }
        }

        alarm(0);

        if (state == 5) {
            if (resp_control == C_REJ0 || resp_control == C_REJ1) {
                printf("REJ received -> retransmitting\n");
                g_stats.rej_received++;
                g_stats.retransmissions++;
                alarmCount++;
                continue;
            }

            unsigned char expected_rr = (seq_num % 2 == 0) ? C_RR1 : C_RR0;
            if (resp_control == expected_rr) {
                printf("RR received -> success\n");
                seq_num++;
                return frameSize;
            }

            // unexpected supervisory frame
            alarmCount++;
        }
        else {
            // timeout
            g_stats.timeouts++;
            g_stats.retransmissions++;
        }
    }

    printf("ERROR: frame not acknowledged\n");
    return -1;
}

int llread(unsigned char *packet) {
    if (g_fd < 0) return -1;

    unsigned char byte;
    unsigned char data[2048];
    int dataIndex = 0;

    unsigned char address = 0;
    unsigned char control = 0;

    enum { START, FLAG_RCV, A_RCV, C_RCV, BCC1_OK, DATA_RCV, STOP } state = START;

    while (state != STOP) {
        int res = read(g_fd, &byte, 1);
        if (res <= 0) continue;

        switch (state) {
            case START:
                if (byte == FLAG) state = FLAG_RCV;
                break;

            case FLAG_RCV:
                if (byte == A_TX) {
                    address = byte;
                    state = A_RCV;
                }
                else if (byte == FLAG) {
                    state = FLAG_RCV;
                }
                else {
                    state = START;
                }
                break;

            case A_RCV:
                if (byte == C_I0 || byte == C_I1) {
                    control = byte;
                    state = C_RCV;
                }
                else if (byte == FLAG) {
                    state = FLAG_RCV;
                }
                else {
                    state = START;
                }
                break;

            case C_RCV:
                if (byte == (address ^ control)) {
                    state = BCC1_OK;
                }
                else if (byte == FLAG) {
                    state = FLAG_RCV;
                }
                else {
                    state = START;
                }
                break;

            case BCC1_OK:
                if (byte == FLAG) {
                    state = START;
                }
                else if (byte == ESC) {
                    int res = read(g_fd, &byte, 1);
                    if (res <= 0) {
                        state = BCC1_OK;
                        break;
                    }
                    if (byte == 0x5E) {
                        if (dataIndex >= (int)sizeof(data)) return -1;
                        data[dataIndex++] = FLAG;
                    } else if (byte == 0x5D) {
                        if (dataIndex >= (int)sizeof(data)) return -1;
                        data[dataIndex++] = ESC;
                    } else {
                        if (dataIndex >= (int)sizeof(data)) return -1;
                        data[dataIndex++] = byte;
                    }
                    state = DATA_RCV;
                }
                else {
                    if (dataIndex >= (int)sizeof(data)) return -1;
                    data[dataIndex++] = byte;
                    state = DATA_RCV;
                }
                break;

            case DATA_RCV:
                if (byte == FLAG) {
                    state = STOP;
                }
                else if (byte == ESC) {
                    // Next byte is stuffed - read it immediately
                    int res = read(g_fd, &byte, 1);
                    if (res <= 0) {
                        // No byte available, retry this state
                        state = DATA_RCV;
                        break;
                    }
                    if (byte == 0x5E) {
                        if (dataIndex >= (int)sizeof(data)) return -1;
                        data[dataIndex++] = FLAG;
                    } else if (byte == 0x5D) {
                        if (dataIndex >= (int)sizeof(data)) return -1;
                        data[dataIndex++] = ESC;
                    } else {
                        // Invalid escape sequence; keep raw byte as fallback
                        if (dataIndex >= (int)sizeof(data)) return -1;
                        data[dataIndex++] = byte;
                    }
                }
                else {
                    if (dataIndex >= (int)sizeof(data)) return -1;
                    data[dataIndex++] = byte;
                }
                break;

            default:
                break;
        }
    }

    // simular propagation delay
    if (g_tprop > 0) {
        usleep(g_tprop * 1000);
    }

    // Remove BCC2 from data
    if (dataIndex < 1) {
        return -1;
    }
    unsigned char received_bcc2 = data[--dataIndex];
    unsigned char calculated_bcc2 = 0;

    for (int i = 0; i < dataIndex; i++) {
        calculated_bcc2 ^= data[i];
    }

    int frame_seq = (control == C_I0) ? 0 : 1;

    if (calculated_bcc2 == received_bcc2) {
        if (frame_seq == expected_seq) {
            // simular FER - fingir que houve erro
            if (g_fer > 0.0 && (double)rand() / RAND_MAX < g_fer) {
                printf("FER simulation: frame rejected\n");
                unsigned char rej_control;
                if (expected_seq == 0) {
                    rej_control = C_REJ0;
                } else {
                    rej_control = C_REJ1;
                }
                unsigned char rej_frame[5] = {FLAG, A_TX, rej_control, A_TX ^ rej_control, FLAG};
                write(g_fd, rej_frame, 5);
                g_stats.rej_sent++;
                return -1;
            }

            // New frame
            memcpy(packet, data, dataIndex);
            expected_seq = 1 - expected_seq;
            g_stats.frames_received++;

            // Send RR
            unsigned char rr_control;
            if (expected_seq == 0) {
                rr_control = C_RR0;
            } else {
                rr_control = C_RR1;
            }
            unsigned char rr_frame[5] = {FLAG, A_TX, rr_control, A_TX ^ rr_control, FLAG};
            write(g_fd, rr_frame, 5);

            printf("RR sent (new frame)\n");
            return dataIndex;
        } else {
            // Duplicate
            printf("Duplicate frame received, discarding\n");

            // Send RR for expected
            unsigned char rr_control = (expected_seq == 0) ? C_RR0 : C_RR1;
            unsigned char rr_frame[5] = {FLAG, A_TX, rr_control, A_TX ^ rr_control, FLAG};
            write(g_fd, rr_frame, 5);

            printf("RR sent (duplicate)\n");
            return 0; // Duplicate frame: ignore at application level without treating as error
        }
    }
    else {
        // BCC2 error
        printf("BCC2 error\n");

        // Send REJ
        unsigned char rej_control;
        if (expected_seq == 0) {
            rej_control = C_REJ0;
        } else {
            rej_control = C_REJ1;
        }
        unsigned char rej_frame[5] = {FLAG, A_TX, rej_control, A_TX ^ rej_control, FLAG};
        write(g_fd, rej_frame, 5);
        g_stats.rej_sent++;

        printf("REJ sent\n");
        return -1;
    }
}

int llclose(int showStatistics) {
    if (g_fd < 0) {
        return -1;
    }

    int fd = g_fd;
    unsigned char byte;
    int state;

    alarmCount = 0;

    if (g_role == tx) {
        // Tx sends DISC
        unsigned char disc_frame[5] = {FLAG, A_TX, C_DISC, A_TX ^ C_DISC, FLAG};

        while (alarmCount < g_nRetransmissions) {
            write(fd, disc_frame, 5);
            printf("DISC sent (%d/%d)\n", alarmCount + 1, g_nRetransmissions);

            alarmEnabled = 1;
            alarm(g_timeout);

            state = 0; // START
            while (alarmEnabled && state != 5) {
                int res = read(fd, &byte, 1);
                if (res <= 0) continue;

                switch (state) {
                    case 0: // START
                        if (byte == FLAG) state = 1;
                        break;

                    case 1: // FLAG_RCV
                        if (byte == A_RX) state = 2;
                        else if (byte == FLAG) state = 1;
                        else state = 0;
                        break;

                    case 2: // A_RCV
                        if (byte == C_DISC) state = 3;
                        else if (byte == FLAG) state = 1;
                        else state = 0;
                        break;

                    case 3: // C_RCV
                        if (byte == (A_RX ^ C_DISC)) state = 4;
                        else if (byte == FLAG) state = 1;
                        else state = 0;
                        break;

                    case 4: // BCC_OK
                        if (byte == FLAG) state = 5;
                        else state = 0;
                        break;
                }
            }

            alarm(0);
            if (state == 5) {
                break;
            }
        }

        if (alarmCount >= g_nRetransmissions) {
            fprintf(stderr, "ERROR: DISC not received after %d retransmissions\n", g_nRetransmissions);
            tcsetattr(fd, TCSANOW, &oldtio);
            close(fd);
            g_fd = -1;
            return -1;
        }

        // Tx sends UA
        unsigned char ua_frame[5] = {FLAG, A_RX, C_UA, A_RX ^ C_UA, FLAG};
        write(fd, ua_frame, 5);
        printf("UA sent\n");
    }
    else {
        // Rx waits for DISC from Tx
        state = 0;
        while (state != 5) {
            int res = read(fd, &byte, 1);
            if (res <= 0) continue;

            switch (state) {
                case 0:
                    if (byte == FLAG) state = 1;
                    break;

                case 1:
                    if (byte == A_TX) state = 2;
                    else if (byte == FLAG) state = 1;
                    else state = 0;
                    break;

                case 2:
                    if (byte == C_DISC) state = 3;
                    else if (byte == FLAG) state = 1;
                    else state = 0;
                    break;

                case 3:
                    if (byte == (A_TX ^ C_DISC)) state = 4;
                    else if (byte == FLAG) state = 1;
                    else state = 0;
                    break;

                case 4:
                    if (byte == FLAG) state = 5;
                    else state = 0;
                    break;
            }
        }

        printf("DISC received\n");

        // Rx sends DISC
        unsigned char disc_frame[5] = {FLAG, A_RX, C_DISC, A_RX ^ C_DISC, FLAG};

        alarmCount = 0;
        while (alarmCount < g_nRetransmissions) {
            write(fd, disc_frame, 5);
            printf("DISC sent (%d/%d)\n", alarmCount + 1, g_nRetransmissions);

            alarmEnabled = 1;
            alarm(g_timeout);

            state = 0;
            while (alarmEnabled && state != 5) {
                int res = read(fd, &byte, 1);
                if (res <= 0) continue;

                switch (state) {
                    case 0:
                        if (byte == FLAG) state = 1;
                        break;

                    case 1:
                        if (byte == A_RX) state = 2;
                        else if (byte == FLAG) state = 1;
                        else state = 0;
                        break;

                    case 2:
                        if (byte == C_UA) state = 3;
                        else if (byte == FLAG) state = 1;
                        else state = 0;
                        break;

                    case 3:
                        if (byte == (A_RX ^ C_UA)) state = 4;
                        else if (byte == FLAG) state = 1;
                        else state = 0;
                        break;

                    case 4:
                        if (byte == FLAG) state = 5;
                        else state = 0;
                        break;
                }
            }

            alarm(0);
            if (state == 5) {
                break;
            }
        }

        if (alarmCount >= g_nRetransmissions) {
            fprintf(stderr, "ERROR: UA not received after %d retransmissions\n", g_nRetransmissions);
            tcsetattr(fd, TCSANOW, &oldtio);
            close(fd);
            g_fd = -1;
            return -1;
        }

        printf("UA received - link closed\n");
    }

    alarm(0);
    tcsetattr(fd, TCSANOW, &oldtio);
    close(fd);
    g_fd = -1;

    if (showStatistics) {
        printf("\n=== Link Layer Statistics ===\n");
        printf("Frames sent: %d\n", g_stats.frames_sent);
        printf("Frames received: %d\n", g_stats.frames_received);
        printf("Retransmissions: %d\n", g_stats.retransmissions);
        printf("Timeouts: %d\n", g_stats.timeouts);
        printf("REJs sent: %d\n", g_stats.rej_sent);
        printf("REJs received: %d\n", g_stats.rej_received);
    }

    return 0;
}

LinkLayerStats llget_stats(void) {
    return g_stats;
}
