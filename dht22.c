/* dht22.c */
#include "dht22.h"

/* ─────────────────────────────────────────────
   Microsecond delay using DWT cycle counter.
   Must call DHT22_Init() once before use.
   ───────────────────────────────────────────── */
static void delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (HAL_RCC_GetHCLKFreq() / 1000000U);
    while ((DWT->CYCCNT - start) < ticks);
}

/* Set DATA pin as output */
static void set_output(void)
{
    GPIO_InitTypeDef g = {0};
    g.Pin   = DHT22_GPIO_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DHT22_GPIO_PORT, &g);
}

/* Set DATA pin as input */
static void set_input(void)
{
    GPIO_InitTypeDef g = {0};
    g.Pin  = DHT22_GPIO_PIN;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(DHT22_GPIO_PORT, &g);
}

/* Read current logic level of DATA pin */
static uint8_t read_pin(void)
{
    return (uint8_t)HAL_GPIO_ReadPin(DHT22_GPIO_PORT, DHT22_GPIO_PIN);
}

/* ── Init: enable DWT for µs timing ── */
void DHT22_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;

    /* Power-on settle: datasheet says wait 1s before first read */
    HAL_Delay(1000);
}

/* ── Main read function ── */
DHT22_Data DHT22_Read(void)
{
    DHT22_Data result = {0};
    uint8_t data[5]   = {0};

    /* ── STEP 1: MCU sends start signal ──
       Pull LOW for at least 1ms (datasheet: min 1ms)
       then release and wait 20-40µs for response        */
    set_output();
    HAL_GPIO_WritePin(DHT22_GPIO_PORT, DHT22_GPIO_PIN, GPIO_PIN_RESET);
    HAL_Delay(1);                          /* 1ms LOW */
    HAL_GPIO_WritePin(DHT22_GPIO_PORT, DHT22_GPIO_PIN, GPIO_PIN_SET);
    delay_us(30);                          /* wait 30µs */
    set_input();

    /* ── STEP 2: Wait for DHT22 response ──
       DHT22 pulls LOW for 80µs then HIGH for 80µs       */
    uint32_t timeout = 0;

    /* Wait for LOW response */
    timeout = 0;
    while (read_pin() == 1) { delay_us(1); if (++timeout > 100) goto error; }

    /* Wait for HIGH */
    timeout = 0;
    while (read_pin() == 0) { delay_us(1); if (++timeout > 100) goto error; }

    /* Wait for HIGH to end (80µs HIGH phase) */
    timeout = 0;
    while (read_pin() == 1) { delay_us(1); if (++timeout > 100) goto error; }

    /* ── STEP 3: Read 40 bits ──
       Each bit starts with 50µs LOW then:
         HIGH 26-28µs = bit 0
         HIGH 70µs    = bit 1
       We sample after 40µs — if still HIGH it is a 1   */
    for (int i = 0; i < 40; i++)
    {
        /* Wait for LOW to end (50µs LOW start of each bit) */
        timeout = 0;
        while (read_pin() == 0) { delay_us(1); if (++timeout > 100) goto error; }

        /* Wait 40µs then sample */
        delay_us(40);

        data[i / 8] <<= 1;
        if (read_pin() == 1)
            data[i / 8] |= 1;

        /* Wait for HIGH to finish */
        timeout = 0;
        while (read_pin() == 1) { delay_us(1); if (++timeout > 100) goto error; }
    }

    /* ── STEP 4: Verify checksum ──
       Last byte must equal sum of first 4 bytes (lower 8 bits) */
    if (data[4] != ((data[0] + data[1] + data[2] + data[3]) & 0xFF))
        goto error;

    /* ── STEP 5: Parse data ──
       Bytes 0-1: Humidity    (×10, e.g. 0x02 0x5A = 602 → 60.2%)
       Bytes 2-3: Temperature (×10, MSB bit15 = negative sign) */
    uint16_t raw_hum  = ((uint16_t)data[0] << 8) | data[1];
    uint16_t raw_temp = ((uint16_t)data[2] << 8) | data[3];

    result.humidity    = raw_hum / 10.0f;

    if (raw_temp & 0x8000)                 /* negative temperature */
        result.temperature = -((raw_temp & 0x7FFF) / 10.0f);
    else
        result.temperature = raw_temp / 10.0f;

    result.valid = 1;
    return result;

error:
    result.valid = 0;
    return result;
}
