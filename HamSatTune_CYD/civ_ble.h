#pragma once

#include <stddef.h>
#include <stdint.h>

bool civBleBegin();
bool civBleConnected();
int civBleAvailable();
int civBleRead();
size_t civBleWrite(const uint8_t *data, size_t length);
