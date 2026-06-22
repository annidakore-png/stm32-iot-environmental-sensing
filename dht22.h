/* dht22.h */
#ifndef DHT22_H
#define DHT22_H

#include "main.h"

/* ── Change this if you use a different pin ── */
#define DHT22_GPIO_PORT   GPIOA
#define DHT22_GPIO_PIN    GPIO_PIN_1

/* Result struct */
typedef struct {
    float temperature;   /* degrees Celsius */
    float humidity;      /* percent RH      */
    uint8_t valid;       /* 1 = OK, 0 = error */
} DHT22_Data;

/* Public API */
void      DHT22_Init(void);
DHT22_Data DHT22_Read(void);

#endif /* DHT22_H */
