#include "indicators/oled.h"

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"


/*
 * ============================================================
 * PINOS DO HELTEC WIFI LORA 32 V3.2
 * ============================================================
 */

#define OLED_VEXT_GPIO      36
#define OLED_SDA_GPIO       17
#define OLED_SCL_GPIO       18
#define OLED_RST_GPIO       21


/*
 * ============================================================
 * CONFIGURAÇÃO I2C / OLED
 * ============================================================
 */

#define OLED_I2C_PORT       0
#define OLED_I2C_ADDRESS    0x3C
#define OLED_I2C_FREQ_HZ    400000

#define OLED_WIDTH          128
#define OLED_HEIGHT         64

#define OLED_BUFFER_SIZE    (OLED_WIDTH * OLED_HEIGHT / 8)


/*
 * ============================================================
 * ESTADO DO OLED
 * ============================================================
 */

static esp_lcd_panel_handle_t oled_panel = NULL;

static uint8_t oled_buffer[OLED_BUFFER_SIZE];


/*
 * ============================================================
 * FONTE 5x7
 * ============================================================
 *
 * Fonte mínima para as informações exibidas no OLED.
 *
 * Cada caractere possui 5 colunas x 7 linhas.
 */

static const uint8_t *glyph(char c)
{
    static const uint8_t space[5] = {
        0x00, 0x00, 0x00, 0x00, 0x00
    };

    static const uint8_t digits[10][5] = {
        {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
        {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
        {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
        {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
        {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
        {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
        {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
        {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
        {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
        {0x06, 0x49, 0x49, 0x29, 0x1E}  // 9
    };

    static const uint8_t dot[5] = {
        0x00, 0x60, 0x60, 0x00, 0x00
    };

    static const uint8_t A[5] = {
        0x7E, 0x11, 0x11, 0x11, 0x7E
    };

    static const uint8_t C[5] = {
        0x3E, 0x41, 0x41, 0x41, 0x22
    };

    static const uint8_t D[5] = {
        0x7F, 0x41, 0x41, 0x22, 0x1C
    };

    static const uint8_t E[5] = {
        0x7F, 0x49, 0x49, 0x49, 0x41
    };

    static const uint8_t F[5] = {
        0x7F, 0x09, 0x09, 0x09, 0x01
    };

    static const uint8_t I[5] = {
        0x00, 0x41, 0x7F, 0x41, 0x00
    };

    static const uint8_t M[5] = {
        0x7F, 0x02, 0x0C, 0x02, 0x7F
    };

    static const uint8_t N[5] = {
        0x7F, 0x04, 0x08, 0x10, 0x7F
    };

    static const uint8_t O[5] = {
        0x3E, 0x41, 0x41, 0x41, 0x3E
    };

    static const uint8_t Q[5] = {
        0x3E, 0x41, 0x51, 0x21, 0x5E
    };

    static const uint8_t S[5] = {
        0x46, 0x49, 0x49, 0x49, 0x31
    };

    static const uint8_t T[5] = {
        0x01, 0x01, 0x7F, 0x01, 0x01
    };

    static const uint8_t W[5] = {
        0x7F, 0x20, 0x18, 0x20, 0x7F
    };

    switch (c) {

        case '0' ... '9':
            return digits[c - '0'];

        case '.':
            return dot;

        case 'A':
            return A;

        case 'C':
            return C;

        case 'D':
            return D;

        case 'E':
            return E;

        case 'F':
            return F;

        case 'I':
            return I;

        case 'M':
            return M;

        case 'N':
            return N;

        case 'O':
            return O;

        case 'Q':
            return Q;

        case 'S':
            return S;

        case 'T':
            return T;

        case 'W':
            return W;

        default:
            return space;
    }
}


/*
 * ============================================================
 * BUFFER
 * ============================================================
 */

static void oled_clear_buffer(void)
{
    memset(
        oled_buffer,
        0,
        sizeof(oled_buffer)
    );
}


/*
 * ============================================================
 * PIXEL
 * ============================================================
 */

static void oled_pixel(int x, int y)
{
    if (x < 0 || x >= OLED_WIDTH ||
        y < 0 || y >= OLED_HEIGHT) {

        return;
    }

    oled_buffer[(y / 8) * OLED_WIDTH + x] |=
        (uint8_t)(1U << (y % 8));
}


/*
 * ============================================================
 * CARACTERE
 * ============================================================
 */

static void oled_char(
    int x,
    int y,
    char c,
    int scale
)
{
    const uint8_t *bitmap = glyph(c);

    for (int column = 0; column < 5; ++column) {

        for (int row = 0; row < 7; ++row) {

            if (bitmap[column] & (1U << row)) {

                for (int dx = 0; dx < scale; ++dx) {

                    for (int dy = 0; dy < scale; ++dy) {

                        oled_pixel(
                            x + column * scale + dx,
                            y + row * scale + dy
                        );
                    }
                }
            }
        }
    }
}


/*
 * ============================================================
 * TEXTO
 * ============================================================
 */

static void oled_text(
    int x,
    int y,
    const char *text,
    int scale
)
{
    while (*text) {

        oled_char(
            x,
            y,
            *text,
            scale
        );

        x += 6 * scale;

        text++;
    }
}


/*
 * ============================================================
 * CONTADOR
 * ============================================================
 */

static void oled_counter(uint64_t total)
{
    char text[24];

    /*
     * Até 999999:
     *
     * sempre mostra 6 dígitos.
     *
     * Exemplo:
     *
     * 000001
     * 001247
     * 123456
     */

    if (total <= 999999) {

        snprintf(
            text,
            sizeof(text),
            "%06" PRIu64,
            total
        );

        /*
         * 6 caracteres
         * × 6 pixels
         * × escala 3
         *
         * = 108 pixels
         */

        oled_text(
            10,
            40,
            text,
            3
        );

    } else {

        /*
         * Acima de 999999,
         * reduzimos a escala para 2.
         */

        snprintf(
            text,
            sizeof(text),
            "%" PRIu64,
            total
        );

        oled_text(
            8,
            40,
            text,
            2
        );
    }
}


/*
 * ============================================================
 * ENVIO PARA O SSD1306
 * ============================================================
 */

static esp_err_t oled_flush(void)
{
    if (oled_panel == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return esp_lcd_panel_draw_bitmap(
        oled_panel,
        0,
        0,
        OLED_WIDTH,
        OLED_HEIGHT,
        oled_buffer
    );
}


/*
 * ============================================================
 * INICIALIZAÇÃO
 * ============================================================
 */

esp_err_t oled_init(void)
{
    esp_err_t status;

    /*
     * ========================================================
     * 1. LIGA A ALIMENTAÇÃO DO OLED
     * ========================================================
     *
     * No Heltec WiFi LoRa 32 V3.2:
     *
     * GPIO36 = Vext Ctrl
     *
     * GPIO36 LOW = Vext ligado.
     *
     * O OLED onboard recebe alimentação através dessa linha.
     */

    status = gpio_set_direction(
        OLED_VEXT_GPIO,
        GPIO_MODE_OUTPUT
    );

    if (status != ESP_OK) {
        return status;
    }

    status = gpio_set_level(
        OLED_VEXT_GPIO,
        0
    );

    if (status != ESP_OK) {
        return status;
    }


    /*
     * ========================================================
     * 2. CRIA O BARRAMENTO I2C
     * ========================================================
     */

    i2c_master_bus_handle_t i2c_bus = NULL;

    const i2c_master_bus_config_t bus_config = {

        .clk_source = I2C_CLK_SRC_DEFAULT,

        .i2c_port = OLED_I2C_PORT,

        .sda_io_num = OLED_SDA_GPIO,

        .scl_io_num = OLED_SCL_GPIO,

        .glitch_ignore_cnt = 7,

        .flags.enable_internal_pullup = true,
    };

    status = i2c_new_master_bus(
        &bus_config,
        &i2c_bus
    );

    if (status != ESP_OK) {
        return status;
    }


    /*
     * ========================================================
     * 3. CONFIGURA INTERFACE I2C DO LCD
     * ========================================================
     */

    esp_lcd_panel_io_handle_t io_handle = NULL;

    const esp_lcd_panel_io_i2c_config_t io_config = {

        .dev_addr = OLED_I2C_ADDRESS,

        .scl_speed_hz = OLED_I2C_FREQ_HZ,

        .transaction_timeout_ms = 1000,

        .control_phase_bytes = 1,

        .lcd_cmd_bits = 8,

        .lcd_param_bits = 8,

        .dc_bit_offset = 6,
    };

    status = esp_lcd_new_panel_io_i2c(
        i2c_bus,
        &io_config,
        &io_handle
    );

    if (status != ESP_OK) {
        return status;
    }


    /*
     * ========================================================
     * 4. CONFIGURA SSD1306
     * ========================================================
     */

    esp_lcd_panel_dev_config_t panel_config = {

        .bits_per_pixel = 1,

        .reset_gpio_num = OLED_RST_GPIO,
    };

    esp_lcd_panel_ssd1306_config_t ssd1306_config = {

        .height = OLED_HEIGHT,
    };

    panel_config.vendor_config =
        &ssd1306_config;


    /*
     * ========================================================
     * 5. CRIA DRIVER DO SSD1306
     * ========================================================
     */

    status = esp_lcd_new_panel_ssd1306(
        io_handle,
        &panel_config,
        &oled_panel
    );

    if (status != ESP_OK) {
        return status;
    }


    /*
     * ========================================================
     * 6. RESET DO OLED
     * ========================================================
     */

    status = esp_lcd_panel_reset(
        oled_panel
    );

    if (status != ESP_OK) {
        return status;
    }


    /*
     * ========================================================
     * 7. INICIALIZA SSD1306
     * ========================================================
     */

    status = esp_lcd_panel_init(
        oled_panel
    );

    if (status != ESP_OK) {
        return status;
    }


    /*
     * ========================================================
     * 8. LIGA DISPLAY
     * ========================================================
     */

    status = esp_lcd_panel_disp_on_off(
        oled_panel,
        true
    );

    if (status != ESP_OK) {
        return status;
    }


    /*
     * ========================================================
     * 9. LIMPA E ENVIA PRIMEIRO FRAME
     * ========================================================
     */

    oled_clear_buffer();

    return oled_flush();
}


/*
 * ============================================================
 * ATUALIZAÇÃO DA TELA
 * ============================================================
 *
 * Tela:
 *
 * WIFI 192.168.1.10
 * MQTT CONNECTED
 *
 *       001247
 */

void oled_update(
    bool wifi_connected,
    const char *ip,
    bool mqtt_connected,
    uint64_t total
)
{
    if (oled_panel == NULL) {
        return;
    }


    /*
     * ========================================================
     * LINHA DO WIFI
     * ========================================================
     */

    char wifi_line[32];

    if (wifi_connected && ip != NULL) {

        snprintf(
            wifi_line,
            sizeof(wifi_line),
            "WIFI %s",
            ip
        );

    } else {

        snprintf(
            wifi_line,
            sizeof(wifi_line),
            "WIFI DISCONNECTED"
        );
    }


    /*
     * ========================================================
     * LINHA DO MQTT
     * ========================================================
     */

    const char *mqtt_line =
        mqtt_connected
            ? "MQTT CONNECTED"
            : "MQTT DISCONNECTED";


    /*
     * ========================================================
     * MONTA FRAME
     * ========================================================
     */

    oled_clear_buffer();


    /*
     * WIFI
     */

    oled_text(
        0,
        0,
        wifi_line,
        1
    );


    /*
     * MQTT
     */

    oled_text(
        0,
        10,
        mqtt_line,
        1
    );


    /*
     * CONTADOR
     */

    oled_counter(total);


    /*
     * ENVIA PARA O DISPLAY
     */

    (void)oled_flush();
}