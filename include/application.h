#ifndef APPLICATION_H
#define APPLICATION_H

#include "link_layer.h"

// (tx ou rx)
int application(const char *serialPort, const char *role, const char *filename);

#endif
