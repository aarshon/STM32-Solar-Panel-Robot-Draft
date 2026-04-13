#ifndef VESC_UART_H
#define VESC_UART_H

#include "hal.h"

typedef struct {
    UARTDriver *uart;
} vesc_uart_t;

#endif