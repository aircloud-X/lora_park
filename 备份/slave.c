#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define LED_R_PIN GPIO_NUM_26
#define LED_G_PIN GPIO_NUM_25
#define LED_B_PIN GPIO_NUM_27
#define LED1_PIN GPIO_NUM_19
#define LED2_PIN GPIO_NUM_21
#define LED3_PIN GPIO_NUM_13
#define LED4_PIN GPIO_NUM_14
#define LED5_PIN GPIO_NUM_32
#define KEY_PIN GPIO_NUM_18
#define LORA_RX_PIN GPIO_NUM_22
#define LORA_TX_PIN GPIO_NUM_23
#define LORA_AUX_PIN GPIO_NUM_34
#define RADAR_OUTPUT_PIN GPIO_NUM_33

#define UART_NUM UART_NUM_1
#define UART_BUF_SIZE 1024
#define TAG "SLAVE"

// ====================== 从机地址 ======================
#define DEVICE_ADDR 0x01  // 你只用 0x01
#define MASTER_ADDR 0x00
// ======================================================

void lora_wait_idle(void)
{
    while (gpio_get_level(LORA_AUX_PIN) == 0) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    vTaskDelay(pdMS_TO_TICKS(10));
}

void gpio_init(void)
{
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << LED_R_PIN) | (1ULL << LED_G_PIN) | (1ULL << LED_B_PIN) |
                        (1ULL << LED1_PIN) | (1ULL << LED2_PIN) | (1ULL << LED3_PIN) |
                        (1ULL << LED4_PIN) | (1ULL << LED5_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&led_conf);

    gpio_set_level(LED_R_PIN, 1);
    gpio_set_level(LED_G_PIN, 1);
    gpio_set_level(LED_B_PIN, 1);
    gpio_set_level(LED1_PIN, 1);
    gpio_set_level(LED2_PIN, 1);
    gpio_set_level(LED3_PIN, 1);
    gpio_set_level(LED4_PIN, 1);
    gpio_set_level(LED5_PIN, 1);

    gpio_config_t key_conf = {
        .pin_bit_mask = (1ULL << KEY_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&key_conf);

    gpio_config_t aux_conf = {
        .pin_bit_mask = (1ULL << LORA_AUX_PIN) | (1ULL << RADAR_OUTPUT_PIN),
        .mode = GPIO_MODE_INPUT,
    };
    gpio_config(&aux_conf);
}

void uart_init(void)
{
    uart_config_t uart_cfg = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_param_config(UART_NUM, &uart_cfg);
    uart_set_pin(UART_NUM, LORA_RX_PIN, LORA_TX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM, UART_BUF_SIZE, UART_BUF_SIZE, 0, NULL, 0);
    ESP_LOGI(TAG, "Slave %02X init", DEVICE_ADDR);
    lora_wait_idle();
}

// 正确格式：00,01,STATUS:1
void lora_send_status(uint8_t status)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%02X,%02X,STATUS:%d", MASTER_ADDR, DEVICE_ADDR, status);
    lora_wait_idle();
    uart_write_bytes(UART_NUM, buf, strlen(buf));
    ESP_LOGI(TAG, "Send: %s", buf);
}

bool parse_cmd(uint8_t *src_addr, const char *buf)
{
    char dst_str[4], src_str[4], data[32];
    if (sscanf(buf, "%2s,%2s,%s", dst_str, src_str, data) == 3) {
        uint8_t dst = strtoul(dst_str, NULL, 16);
        *src_addr = strtoul(src_str, NULL, 16);
        return (dst == DEVICE_ADDR && strcmp(data, "QUERY") == 0);
    }
    return false;
}

void update_rgb(uint8_t occupied)
{
    if (occupied) {
        gpio_set_level(LED_R_PIN, 0);
        gpio_set_level(LED_G_PIN, 1);
    } else {
        gpio_set_level(LED_R_PIN, 1);
        gpio_set_level(LED_G_PIN, 0);
    }
}

uint8_t get_parking_status(void)
{
    uint8_t key = gpio_get_level(KEY_PIN);
    uint8_t radar = gpio_get_level(RADAR_OUTPUT_PIN);
    return (key == 1 || (key == 0 && radar == 1)) ? 1 : 0;
}

void app_main(void)
{
    gpio_init();
    uart_init();
    uint8_t buf[UART_BUF_SIZE];
    uint8_t last_status = 0xFF;

    while (1) {
        uint8_t occupied = get_parking_status();
        update_rgb(occupied);

        if (occupied != last_status) {
            last_status = occupied;
            lora_send_status(occupied);
        }

        int len = uart_read_bytes(UART_NUM, buf, UART_BUF_SIZE, pdMS_TO_TICKS(50));
        if (len > 0) {
            buf[len] = 0;
            uint8_t src;
            if (parse_cmd(&src, (char*)buf)) {
                lora_send_status(occupied);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}