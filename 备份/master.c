#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define LED_R_PIN      GPIO_NUM_26
#define LED_G_PIN      GPIO_NUM_25
#define LED_B_PIN      GPIO_NUM_27
#define LED2_PIN       GPIO_NUM_19
#define LED3_PIN       GPIO_NUM_21
#define KEY_PIN        GPIO_NUM_18
#define LORA_RX_PIN    GPIO_NUM_22
#define LORA_TX_PIN    GPIO_NUM_23
#define LORA_AUX_PIN   GPIO_NUM_34

#define UART_NUM       UART_NUM_1
#define UART_BUF_SIZE   1024
#define TAG "LORA_MASTER"

#define DEVICE_ADDR        0x01    // 主机地址
#define FIRST_SLAVE        0x02    // 第一个从机
#define SECOND_SLAVE       0x03    // 第二个从机

void lora_wait_idle(void) {
    while (gpio_get_level(LORA_AUX_PIN) == 0) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    vTaskDelay(pdMS_TO_TICKS(10));
}

void gpio_init(void) {
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << LED2_PIN) | (1ULL << LED3_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&led_conf);
    gpio_set_level(LED2_PIN, 0);
    gpio_set_level(LED3_PIN, 0);

    gpio_config_t key_conf = {
        .pin_bit_mask = (1ULL << KEY_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&key_conf);

    gpio_config_t aux_conf = {
        .pin_bit_mask = (1ULL << LORA_AUX_PIN),
        .mode = GPIO_MODE_INPUT,
    };
    gpio_config(&aux_conf);
}

void uart_init(void) {
    uart_config_t uart_cfg = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_param_config(UART_NUM, &uart_cfg);
    uart_set_pin(UART_NUM, LORA_RX_PIN, LORA_TX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM, UART_BUF_SIZE, UART_BUF_SIZE, 0, NULL, 0);
    ESP_LOGI(TAG, "✅ 主机初始化 地址:0x%02X", DEVICE_ADDR);
    vTaskDelay(pdMS_TO_TICKS(100));
    lora_wait_idle();
}

void lora_send_to(uint8_t dest_addr, const char *data) {
    char packet[128];
    snprintf(packet, sizeof(packet), "%02X,%02X,%s", dest_addr, DEVICE_ADDR, data);

    lora_wait_idle();
    uart_write_bytes(UART_NUM, packet, strlen(packet));

    gpio_set_level(LED2_PIN, 1);
    ESP_LOGI(TAG, "==> 主机 → 从机[0x%02X]: %s", dest_addr, data);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(LED2_PIN, 0);
}

bool lora_parse_packet(uint8_t *dest_addr, uint8_t *src_addr, char *out_data, const char *buf) {
    char dest_str[4], src_str[4];
    if (sscanf(buf, "%2s,%2s,%[^\n]", dest_str, src_str, out_data) == 3) {
        *dest_addr = (uint8_t)strtoul(dest_str, NULL, 16);
        *src_addr  = (uint8_t)strtoul(src_str, NULL, 16);
        return true;
    }
    return false;
}

void app_main(void) {
    gpio_init();
    uart_init();
    uint8_t buf[UART_BUF_SIZE];
    uint32_t tick = 0;
    uint8_t current_slave = FIRST_SLAVE;

    while (1) {
        // 主机轮询从机
        if (xTaskGetTickCount() - tick >= pdMS_TO_TICKS(3000)) {
            tick = xTaskGetTickCount();
            lora_send_to(current_slave, "PING");
            
            // 切换从机
            current_slave = (current_slave == FIRST_SLAVE) ? SECOND_SLAVE : FIRST_SLAVE;
        }

        // 接收从机回复
        int len = uart_read_bytes(UART_NUM, buf, UART_BUF_SIZE, pdMS_TO_TICKS(20));
        if (len > 0) {
            buf[len] = 0;
            uint8_t dest_addr, src_addr;
            char data[100];

            if (lora_parse_packet(&dest_addr, &src_addr, data, (char*)buf)) {
                if (dest_addr == DEVICE_ADDR) {
                    // 检查是否是 KEY 状态变化
                    if (strncmp(data, "KEY:", 4) == 0) {
                        int key_state = atoi(&data[4]);
                        ESP_LOGI(TAG, "🔑 从机[0x%02X] KEY变化: %d", src_addr, key_state);
                        // 根据不同从机的KEY状态控制不同LED
                        if (src_addr == FIRST_SLAVE && key_state == 0) {
                            gpio_set_level(LED2_PIN, 1);
                            vTaskDelay(pdMS_TO_TICKS(200));
                            gpio_set_level(LED2_PIN, 0);
                        } else if (src_addr == SECOND_SLAVE && key_state == 0) {
                            gpio_set_level(LED3_PIN, 1);
                            vTaskDelay(pdMS_TO_TICKS(200));
                            gpio_set_level(LED3_PIN, 0);
                        }
                    } else {
                        // 一般的PONG回复
                        ESP_LOGI(TAG, "✅ 从机[0x%02X]回复: %s", src_addr, data);
                        gpio_set_level(LED3_PIN, 1);
                        vTaskDelay(pdMS_TO_TICKS(100));
                        gpio_set_level(LED3_PIN, 0);
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}