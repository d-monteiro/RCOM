#ifndef APPLICATION_H
#define APPLICATION_H

#include "link_layer.h"

// Packet types
#define PACKET_START 1
#define PACKET_END 2
#define PACKET_DATA 3

// TLV types
#define TLV_SIZE 0
#define TLV_NAME 1

// (tx ou rx)
int application(const char *serialPort, const char *role, const char *filename);

#endif
