#ifndef SENSIRION_I2C_HAL_H
#define SENSIRION_I2C_HAL_H

#include <stdint.h>

/* HAL init/sleep/time */
void sensirion_i2c_hal_init(void);
void sensirion_i2c_hal_sleep_usec(uint32_t useconds);
uint64_t sensirion_i2c_hal_get_time_usec(void);

/* Low-level I2C */
int8_t sensirion_i2c_hal_write(uint8_t address, const uint8_t* data, uint16_t count);
int8_t sensirion_i2c_hal_read(uint8_t address, uint8_t* data, uint16_t count);

#endif
