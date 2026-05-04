#include "application.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define MAX_CHUNK_SIZE 1024

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
    if (datasize > MAX_CHUNK_SIZE) return -1; // Safety check
    if (datasize == 0) return 0; // Empty data is valid
    memcpy(data, &packet[3], datasize);
    return datasize;
}

void print_progress(long current, long total) {
    if (total <= 0) return;
    int percent = (int)(current * 100 / total);
    if (percent > 100) percent = 100;
    int bar_width = 50;
    int filled = percent * bar_width / 100;

    printf("\r[");
    int i;
    for (i = 0; i < filled; i++) printf("=");
    if (filled < bar_width) printf(">");
    for (i = filled + 1; i < bar_width; i++) printf(" ");
    printf("] %d%% (%ld/%ld bytes)", percent, current, total);
    fflush(stdout);
}

int application(const char *serialPort, const char *role, const char *filename,
                double fer, int tprop, int frameSize) {
    LinkLayer ll;
    strcpy(ll.serialPort, serialPort);
    if (strcmp(role, "tx") == 0) {
        ll.role = tx;
    } else {
        ll.role = rx;
    }
    ll.baudRate = 38400;
    ll.nRetransmissions = 3;
    ll.timeout = 3;
    ll.fer = fer;
    ll.tprop = tprop;
    ll.frameSize = frameSize;

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

        // timing
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        // Send DATA packets
        unsigned char buffer[MAX_CHUNK_SIZE];
        int bytesRead;
        long bytes_sent = 0;
        while ((bytesRead = fread(buffer, 1, frameSize, file)) > 0) {
            unsigned char data_packet[2048];
            int packet_size = create_data_packet(data_packet, buffer, bytesRead, 0);
            if (llwrite(data_packet, packet_size) < 0) {
                printf("Failed to send DATA\n");
                fclose(file);
                llclose(0);
                return -1;
            }
            bytes_sent += bytesRead;
            print_progress(bytes_sent, filesize);
        }
        printf("\n");

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

        clock_gettime(CLOCK_MONOTONIC, &end);
        double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

        if (llclose(1) < 0) {
            printf("llclose failed\n");
            return -1;
        }

        // stats de eficiencia
        double R = 0;
        if (elapsed > 0) {
            R = (filesize * 8.0) / elapsed;
        }
        double C = ll.baudRate;
        double S = R / C;

        printf("\n=== Transfer Statistics ===\n");
        printf("File size: %ld bytes\n", filesize);
        printf("Transfer time: %.2f seconds\n", elapsed);
        printf("Effective bitrate (R): %.0f bit/s\n", R);
        printf("Link capacity (C): %d bit/s\n", ll.baudRate);
        printf("Efficiency (S = R/C): %.4f\n", S);
        printf("\n=== Test Parameters ===\n");
        printf("FER: %.2f\n", fer);
        printf("T_prop: %d ms\n", tprop);
        printf("Frame size: %d bytes\n", frameSize);

        printf("\nFile sent successfully\n");
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

        // timing
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        long bytes_received = 0;

        // Read DATA packets
        int read_errors = 0;
        while (1) {
            packet_size = llread(packet);
            if (packet_size < 0) {
                read_errors++;
                if (read_errors > 100) {
                    printf("Too many read errors\n");
                    fclose(file);
                    llclose(0);
                    return -1;
                }
                continue;
            }

            read_errors = 0;

            if (packet_size == 0) {
                continue;
            }

            if (packet[0] == PACKET_DATA) {
                unsigned char data[MAX_CHUNK_SIZE];
                int datasize = parse_data_packet(packet, packet_size, data);
                if (datasize < 0) {
                    printf("Invalid DATA packet\n");
                    continue;
                }
                fwrite(data, 1, datasize, file);
                bytes_received += datasize;
                print_progress(bytes_received, recv_filesize);
            } else if (packet[0] == PACKET_END) {
                break;
            } else {
                printf("Unexpected packet type: %d\n", packet[0]);
            }
        }
        printf("\n");

        fclose(file);

        clock_gettime(CLOCK_MONOTONIC, &end);
        double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

        if (llclose(1) < 0) {
            printf("llclose failed\n");
            return -1;
        }

        // stats de eficiencia
        double R = 0;
        if (elapsed > 0) {
            R = (bytes_received * 8.0) / elapsed;
        }
        double C = ll.baudRate;
        double S = R / C;

        printf("\n=== Transfer Statistics ===\n");
        printf("File size: %ld bytes (received: %ld)\n", recv_filesize, bytes_received);
        printf("Transfer time: %.2f seconds\n", elapsed);
        printf("Effective bitrate (R): %.0f bit/s\n", R);
        printf("Link capacity (C): %d bit/s\n", ll.baudRate);
        printf("Efficiency (S = R/C): %.4f\n", S);
        printf("\n=== Test Parameters ===\n");
        printf("FER: %.2f\n", fer);
        printf("T_prop: %d ms\n", tprop);
        printf("Frame size: %d bytes\n", frameSize);

        printf("\nFile received successfully\n");
    }

    return 0;
}
