/*
 * SMART PACKET LOGIC:
 *   Cycle 1-2  -> GPS packet:    $6577,LA:18.5204,LO:73.8567*
 *   Cycle 3+   -> Normal packet: $6577,T:32.9,H:60.3,M:24.8,S:29.4,E:384,W:0.8,C:NW*
 *
 * All packets under 58-byte LoRa limit.
 *
 * PIN MAP:
 * PA1             -> DHT22 DATA
 * PA4             -> ADC Wind Speed  (0-5V analog, voltage divider to 0-3.3V)
 * PA5             -> ADC Wind Direction (0-5V analog, voltage divider to 0-3.3V)
 * PB6             -> RS485 DE+RE
 * USART1 PA9/PA10 -> ZTS-3000 soil sensor (RS485)
 * USART2 PA2/PA3  -> LoRa E32 @ 9600
 * USART6 PA11/PA12-> GPS @ 9600
 * PC13            -> LED
 *
 * WIND SENSOR WIRING (MANDATORY voltage divider — sensors output 0-5V):
 *   Sensor Blue wire -> 10kΩ -> PA4/PA5
 *                              |
 *                            20kΩ
 *                              |
 *                             GND
 *   This gives max 3.33V at PA4/PA5 — safe for STM32 ADC
 *
 * If sensors not yet connected: W and D fields will show 0.0 and N
 */

#include "main.h"
#include "dht22.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart6;
ADC_HandleTypeDef  hadc1;

/* ── RS485 direction control ─────────────────────────────── */
#define RS485_PORT  GPIOB
#define RS485_PIN   GPIO_PIN_6
#define RS485_TX()  HAL_GPIO_WritePin(RS485_PORT, RS485_PIN, GPIO_PIN_SET)
#define RS485_RX()  HAL_GPIO_WritePin(RS485_PORT, RS485_PIN, GPIO_PIN_RESET)

/* ── Wind sensor ADC channels ────────────────────────────── */
#define WIND_SPEED_CH      ADC_CHANNEL_4   /* PA4 */
#define WIND_DIR_CH        ADC_CHANNEL_5   /* PA5 */
#define WIND_SPEED_MAX_MS  32.4f           /* m/s at full scale (5V = 32.4 m/s) */
#define ADC_SENSOR_ABSENT  4000U           /* If ADC reads above this, sensor not connected */

/* ── GPS cycles at startup ───────────────────────────────── */
#define GPS_CYCLES  2

/* ── Function prototypes ─────────────────────────────────── */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(uint32_t baud);
static void MX_USART2_UART_Init(void);
static void MX_USART6_UART_Init(void);
static void MX_ADC1_Init(void);
static uint32_t ADC_Read(uint32_t ch);

/* ── LoRa transmit ───────────────────────────────────────── */
static void LoRa_Send(const char *msg)
{
    HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), 1000);
}

/* ── ZTS-3000 soil sensor ────────────────────────────────── */
typedef struct { float m; float t; uint16_t e; uint8_t ok; } ZTS_t;
static const uint8_t ZTS_REQ[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x03, 0x05, 0xCB};

static ZTS_t ZTS_Read(uint32_t baud)
{
    ZTS_t z = {0, 0, 0, 0};
    uint8_t r[11] = {0};

    HAL_UART_DeInit(&huart1);
    MX_USART1_UART_Init(baud);
    HAL_Delay(20);

    RS485_TX();
    HAL_Delay(5);
    HAL_UART_Transmit(&huart1, (uint8_t*)ZTS_REQ, 8, 200);
    HAL_Delay(5);
    RS485_RX();

    if (HAL_UART_Receive(&huart1, r, 11, 700) != HAL_OK) return z;
    if (r[0] != 0x01 || r[1] != 0x03 || r[2] != 0x06) return z;

    z.m  = ((r[3] << 8) | r[4]) / 10.0f;
    z.t  = ((r[5] << 8) | r[6]) / 10.0f;
    z.e  = (r[7] << 8) | r[8];
    z.ok = 1;
    return z;
}

static uint32_t ZTS_FindBaud(void)
{
    uint8_t r[11] = {0};
    uint32_t bauds[] = {4800, 9600, 19200, 2400};

    for (int i = 0; i < 4; i++) {
        HAL_UART_DeInit(&huart1);
        MX_USART1_UART_Init(bauds[i]);
        HAL_Delay(50);

        RS485_TX();
        HAL_Delay(5);
        HAL_UART_Transmit(&huart1, (uint8_t*)ZTS_REQ, 8, 200);
        HAL_Delay(5);
        RS485_RX();

        if (HAL_UART_Receive(&huart1, r, 11, 700) == HAL_OK)
            if (r[0] == 0x01 && r[1] == 0x03 && r[2] == 0x06)
                return bauds[i];

        HAL_Delay(300);
    }
    return 0; /* ZTS not found — will send 0.0 for soil values */
}

/* ── Wind direction degrees -> compass string ────────────── */
static const char *WC(uint16_t d)
{
    if (d < 23  || d >= 338) return "N";
    if (d < 68)              return "NE";
    if (d < 113)             return "E";
    if (d < 158)             return "SE";
    if (d < 203)             return "S";
    if (d < 248)             return "SW";
    if (d < 293)             return "W";
    return "NW";
}

/* ── ADC read single channel ─────────────────────────────── */
static uint32_t ADC_Read(uint32_t ch)
{
    ADC_ChannelConfTypeDef c = {0};
    c.Channel      = ch;
    c.Rank         = 1;
    c.SamplingTime = ADC_SAMPLETIME_480CYCLES;

    HAL_ADC_ConfigChannel(&hadc1, &c);
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 100);
    uint32_t v = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return v;
}

/* ── GPS parser ──────────────────────────────────────────── */
typedef struct { float lat; float lon; uint8_t valid; } GPS_t;
static GPS_t  gps     = {0, 0, 0};
static char   gps_buf[256]; /* Increased from 128 — NMEA sentences can be ~82 chars */

static uint8_t GPS_Read(void)
{
    memset(gps_buf, 0, sizeof(gps_buf));

    if (HAL_UART_Receive(&huart6, (uint8_t*)gps_buf, sizeof(gps_buf) - 1, 1000) != HAL_OK)
        return 0;

    char *p = strstr(gps_buf, "$GPRMC");
    if (!p) p = strstr(gps_buf, "$GNRMC");
    if (!p) return 0;

    char tmp[128];
    strncpy(tmp, p, 127);
    tmp[127] = 0;

    char *tok = strtok(tmp, ",");
    int   idx = 0;
    float lat = 0, lon = 0;
    char  ns = 'N', ew = 'E', fix = 'V';

    while (tok && idx < 9) {
        if (idx == 2) fix = tok[0];
        if (idx == 3) lat = atof(tok);
        if (idx == 4) ns  = tok[0];
        if (idx == 5) lon = atof(tok);
        if (idx == 6) ew  = tok[0];
        tok = strtok(NULL, ",");
        idx++;
    }

    if (fix != 'A') return 0; /* No valid GPS fix */

    int latd = (int)(lat / 100);
    int lond = (int)(lon / 100);
    gps.lat = latd + (lat - latd * 100) / 60.0f;
    gps.lon = lond + (lon - lond * 100) / 60.0f;
    if (ns == 'S') gps.lat = -gps.lat;
    if (ew == 'W') gps.lon = -gps.lon;
    gps.valid = 1;
    return 1;
}

/* ══════════════════════════════════════════════════════════ */
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_ADC1_Init();

    /* Enable DWT cycle counter (used by DHT22 driver for timing) */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;

    MX_USART2_UART_Init();
    MX_USART6_UART_Init();
    HAL_Delay(2000);

    /* Find ZTS baud rate — if not found zb=0, sensor values will be 0.0 */
    uint32_t zb = ZTS_FindBaud();

    /* Generate unique node ID from STM32 factory UID */
    uint32_t node_id  = HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetUIDw2();
    uint16_t short_id = (uint16_t)(node_id & 0xFFFF);

    char    pkt[60];
    uint8_t cycle = 0;

    while (1)
    {
        /* ── DHT22 ─────────────────────────────────────── */
        DHT22_Data dht = DHT22_Read();

        /* ── ZTS-3000 ───────────────────────────────────── */
        ZTS_t zts = {0, 0, 0, 0};
        if (zb) zts = ZTS_Read(zb);

        /* ── Wind sensors ───────────────────────────────── */
        uint32_t sr = ADC_Read(WIND_SPEED_CH);
        uint32_t dr = ADC_Read(WIND_DIR_CH);

        /*
         * If ADC reads near 4095 (floating pin), treat as sensor absent.
         * A connected sensor with voltage divider never exceeds ~2730 counts
         * (3.3V / 3.3V * 4095 = 4095 max, but divider caps at ~2730).
         * Threshold 4000 safely catches a floating/disconnected pin.
         */
        float    ws = 0.0f;
        uint16_t wd = 0;

        if (sr < ADC_SENSOR_ABSENT) {
            /* Voltage divider scales 0-5V sensor to 0-3.3V ADC
             * ADC 0-4095 maps to sensor 0-5V
             * Actual sensor voltage = (sr / 4095.0) * 3.3 * (30k/20k)
             * = (sr / 4095.0) * 4.95V  ≈ (sr / 4095.0) * 5V
             * Wind speed = sensor_voltage / 5.0 * 32.4 m/s
             * Simplified: ws = (sr / 4095.0) * WIND_SPEED_MAX_MS
             */
            ws = (sr / 4095.0f) * WIND_SPEED_MAX_MS;
        }

        if (dr < ADC_SENSOR_ABSENT) {
            wd = (uint16_t)((dr * 360UL) / 4095);
            if (wd >= 360) wd = 359;
        }

        /* ── GPS or sensor packet ───────────────────────── */
        if (cycle < GPS_CYCLES)
        {
            GPS_Read();
            snprintf(pkt, sizeof(pkt),
                "$%04X,LA:%.4f,LO:%.4f*\r\n",
                short_id,
                gps.valid ? gps.lat : 0.0f,
                gps.valid ? gps.lon : 0.0f);
            cycle++;
        }
        else
        {
            snprintf(pkt, sizeof(pkt),
                "$%04X,T:%.1f,H:%.1f,M:%.1f,S:%.1f,E:%u,W:%.1f,C:%s*\r\n",
                short_id,
                dht.valid ? dht.temperature : 0.0f,
                dht.valid ? dht.humidity    : 0.0f,
                zts.ok    ? zts.m           : 0.0f,
                zts.ok    ? zts.t           : 0.0f,
                zts.ok    ? (unsigned int)zts.e : 0U,
                ws,
                WC(wd));
        }

        LoRa_Send(pkt);
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        HAL_Delay(5000);
    }
}

/* ══════════════════════════════════════════════════════════ */
/*  Peripheral Init                                           */
/* ══════════════════════════════════════════════════════════ */

static void MX_USART1_UART_Init(uint32_t baud)
{
    huart1.Instance          = USART1;
    huart1.Init.BaudRate     = baud;
    huart1.Init.WordLength   = UART_WORDLENGTH_8B;
    huart1.Init.StopBits     = UART_STOPBITS_1;
    huart1.Init.Parity       = UART_PARITY_NONE;
    huart1.Init.Mode         = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK) Error_Handler();
}

static void MX_USART2_UART_Init(void)
{
    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = 9600;
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_NONE;
    huart2.Init.Mode         = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart2) != HAL_OK) Error_Handler();
}

static void MX_USART6_UART_Init(void)
{
    huart6.Instance          = USART6;
    huart6.Init.BaudRate     = 9600;
    huart6.Init.WordLength   = UART_WORDLENGTH_8B;
    huart6.Init.StopBits     = UART_STOPBITS_1;
    huart6.Init.Parity       = UART_PARITY_NONE;
    huart6.Init.Mode         = UART_MODE_TX_RX;
    huart6.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart6.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart6) != HAL_OK) Error_Handler();
}

static void MX_ADC1_Init(void)
{
    __HAL_RCC_ADC1_CLK_ENABLE();

    hadc1.Instance                   = ADC1;
    hadc1.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode          = DISABLE;
    hadc1.Init.ContinuousConvMode    = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion       = 1;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;

    if (HAL_ADC_Init(&hadc1) != HAL_OK) Error_Handler();
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* PC13 — Status LED (active LOW) */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    g.Pin   = GPIO_PIN_13;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &g);

    /* PB6 — RS485 DE+RE direction control */
    HAL_GPIO_WritePin(RS485_PORT, RS485_PIN, GPIO_PIN_RESET);
    g.Pin   = RS485_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(RS485_PORT, &g);

    /* PA1 — DHT22 data (input with pull-up) */
    g.Pin  = GPIO_PIN_1;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &g);

    /* PA0 — spare input */
    g.Pin  = GPIO_PIN_0;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &g);

    /* PA4, PA5 — Wind speed and direction ADC (analog, no pull) */
    g.Pin  = GPIO_PIN_4 | GPIO_PIN_5;
    g.Mode = GPIO_MODE_ANALOG;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &g);
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef o  = {0};
    RCC_ClkInitTypeDef cc = {0}; /* Fixed: was declared twice in original */

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

    o.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    o.HSIState            = RCC_HSI_ON;
    o.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    o.PLL.PLLState        = RCC_PLL_ON;
    o.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
    o.PLL.PLLM            = 16;
    o.PLL.PLLN            = 336;
    o.PLL.PLLP            = RCC_PLLP_DIV4;
    o.PLL.PLLQ            = 4;
    if (HAL_RCC_OscConfig(&o) != HAL_OK) Error_Handler();

    cc.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                        RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    cc.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    cc.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    cc.APB1CLKDivider = RCC_HCLK_DIV2;
    cc.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&cc, FLASH_LATENCY_2) != HAL_OK) Error_Handler();
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        HAL_Delay(100);
    }
}
