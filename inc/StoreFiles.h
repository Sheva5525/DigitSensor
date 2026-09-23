#ifndef STORE_FILES_H
#define STORE_FILES_H

#include <stdint.h>
#include <stdbool.h>

void vReceive(void);
bool ApplyCalibration(const uint8_t *data, uint32_t length);

#endif // STORE_FILES_H
