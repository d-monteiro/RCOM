#include "application.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CHUNK_SIZE 256

// Get file size
long get_file_size(const char *filename) {
    struct stat st;
    if (stat(filename, &st) == 0) {
        return st.st_size;
    }
    return -1;
}

// Create START or END packet
int create_control_packet(unsigned char *packet, int type, const char *filename, long filesize) {
    int index = 0;
    packet[index++] = type; // C

    // TLV for size
    packet[index++] = TLV_SIZE; // T
    packet[index++] = 4; // L
    packet[index++] = (filesize >> 24) & 0xFF;
    packet[index++] = (filesize >> 16) & 0xFF;
    packet[index++] = (filesize >> 8) & 0xFF;
    packet[index++] = filesize & 0xFF;

    // TLV for name
    packet[index++] = TLV_NAME; // T
    int namelen = strlen(filename);
    if (namelen > 255) namelen = 255; // Limit filename length
    packet[index++] = namelen; // L
    memcpy(&packet[index], filename, namelen);
    index += namelen;

    return index;
}

// Create DATA packet
int create_data_packet(unsigned char *packet, const unsigned char *data, int datasize, int seq) {
    int index = 0;
    packet[index++] = PACKET_DATA; // C
    packet[index++] = (datasize >> 8) & 0xFF; // L2
    packet[index++] = datasize & 0xFF; // L1
    memcpy(&packet[index], data, datasize);
    index += datasize;
    return index;
}

// Parse control packet (START or END)
int parse_control_packet(const unsigned char *packet, int packetsize, char *filename, long *filesize) {
    int index = 1; // skip C
    while (index < packetsize) {
        if (index + 2 > packetsize) break; // Not enough bytes for T and L
        int t = packet[index++];
        int l = packet[index++];
        if (index + l > packetsize) break; // Not enough bytes for V
        
        if (t == TLV_SIZE && l == 4) {
            *filesize = (packet[index] << 24) | (packet[index+1] << 16) | (packet[index+2] << 8) | packet[index+3];
            index += 4;
        } else if (t == TLV_NAME) {
            if (l >= 256) { // Limit filename to 255 chars + null terminator
                memcpy(filename, &packet[index], 255);
                filename[255] = '\0';
            } else {
                memcpy(filename, &packet[index], l);
                filename[l] = '\0';
            }
            index += l;
        } else {
            index += l; // skip unknown
        }
    }

    return 0;
}

// Parse DATA packet
int parse_data_packet(const unsigned char *packet, int packetsize, unsigned char *data) {
    if (packetsize < 3) return -1;
    int datasize = (packet[1] << 8) | packet[2];
    // Validate datasize is within packet bounds and within CHUNK_SIZE
    if (packetsize != 3 + datasize) return -1;
    if (datasize > CHUNK_SIZE) return -1; // Safety check
    if (datasize == 0) return 0; // Empty data is valid
    memcpy(data, &packet[3], datasize);
    return datasize;
}

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
        long filesize = get_file_size(filename);
        if (filesize < 0) {
            perror("get_file_size");
            llclose(0);
            return -1;
        }

        FILE *file = fopen(filename, "rb");
        if (file == NULL) {
            perror("fopen");
            llclose(0);
            return -1;
        }

        // Send START
        unsigned char start_packet[1024];
        int start_size = create_control_packet(start_packet, PACKET_START, filename, filesize);
        if (llwrite(start_packet, start_size) < 0) {
            printf("Failed to send START\n");
            fclose(file);
            llclose(0);
            return -1;
        }

        // Send DATA packets
        unsigned char buffer[CHUNK_SIZE];
        int bytesRead;
        while ((bytesRead = fread(buffer, 1, CHUNK_SIZE, file)) > 0) {
            unsigned char data_packet[1024];
            int packet_size = create_data_packet(data_packet, buffer, bytesRead, 0); // seq not used
            if (llwrite(data_packet, packet_size) < 0) {
                printf("Failed to send DATA\n");
                fclose(file);
                llclose(0);
                return -1;
            }
        }

        // Send END
        unsigned char end_packet[1024];
        int end_size = create_control_packet(end_packet, PACKET_END, filename, filesize);
        if (llwrite(end_packet, end_size) < 0) {
            printf("Failed to send END\n");
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
        unsigned char packet[1024];
        int packet_size;
        char recv_filename[256];
        long recv_filesize = 0;
        FILE *file = NULL;
        int start_attempts = 0;

        // Read START (with timeout)
        while (start_attempts < 100) {
            packet_size = llread(packet);
            if (packet_size < 0) {
                start_attempts++;
                continue;
            }
            if (packet[0] == PACKET_START) {
                break;
            }
            start_attempts++;
        }
        
        if (start_attempts >= 100 || packet_size < 0) {
            printf("Failed to receive START\n");
            llclose(0);
            return -1;
        }
        
        if (parse_control_packet(packet, packet_size, recv_filename, &recv_filesize) < 0) {
            printf("Failed to parse START packet\n");
            llclose(0);
            return -1;
        }
        printf("Receiving file: %s (%ld bytes)\n", recv_filename, recv_filesize);

        file = fopen(filename, "wb"); // Use passed filename as output
        if (file == NULL) {
            perror("fopen");
            llclose(0);
            return -1;
        }

        // Read DATA packets
        int read_errors = 0;
        while (1) {
            packet_size = llread(packet);
            if (packet_size < 0) {
                // Error or duplicate, increment counter but continue
                read_errors++;
                if (read_errors > 100) {
                    printf("Too many read errors\n");
                    fclose(file);
                    llclose(0);
                    return -1;
                }
                continue;
            }

            read_errors = 0; // Reset error counter on successful read

            if (packet_size == 0) {
                printf("Empty packet received\n");
                continue;
            }

            if (packet[0] == PACKET_DATA) {
                unsigned char data[CHUNK_SIZE];
                int datasize = parse_data_packet(packet, packet_size, data);
                if (datasize < 0) {
                    printf("Invalid DATA packet\n");
                    continue;
                }
                fwrite(data, 1, datasize, file);
            } else if (packet[0] == PACKET_END) {
                break;
            } else {
                printf("Unexpected packet type: %d\n", packet[0]);
            }
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
