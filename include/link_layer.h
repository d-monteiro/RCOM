#ifndef LINK_LAYER_H
#define LINK_LAYER_H

#define FLAG 0x7E
#define A_TX 0x03
#define A_RX 0x01
#define C_SET  0x03
#define C_UA   0x07
#define C_DISC 0x0B
#define C_I0   0x00
#define C_I1   0x40
#define C_RR0  0x05
#define C_RR1  0x85
#define C_REJ0 0x01
#define C_REJ1 0x81

#define ESC    0x7D

typedef enum { tx, rx } LLRole;

typedef struct {
    char serialPort[50];
    LLRole role;
    int baudRate;
    int nRetransmissions;
    int timeout;
} LinkLayer;

// Abre conexao. Retorna fd, ou -1 em erro.
int llopen(LinkLayer connectionParameters);

// Envia buffer. Retorna bytes escritos, ou -1 em erro.
int llwrite(const unsigned char *buf, int bufSize);

// Recebe packet. Retorna bytes lidos, ou -1 em erro.
int llread(unsigned char *packet);

// Fecha conexao. Retorna 0 ok, -1 erro.
int llclose(int showStatistics);

#endif
