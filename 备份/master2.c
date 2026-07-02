#include <string.h>
#include <stdlib.h>
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

#define UART_NUM UART_NUM_1
#define UART_BUF_SIZE 1024
#define TAG "MASTER"

#define MASTER_ADDR 0x00
uint8_t slave_list[] = {0x01, 0x02, 0x03, 0x04, 0x05};
uint8_t parking_status[6] = {0};
uint8_t last_key = 0xFF;

uint8_t target_slot = 0;
bool need_waterfall = false;

void lora_wait_idle(void) {
    while (gpio_get_level(LORA_AUX_PIN) == 0) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    vTaskDelay(pdMS_TO_TICKS(10));
}

void all_led_off(void) {
    gpio_set_level(LED1_PIN, 1);
    gpio_set_level(LED2_PIN, 1);
    gpio_set_level(LED3_PIN, 1);
    gpio_set_level(LED4_PIN, 1);
    gpio_set_level(LED5_PIN, 1);
}

void all_rgb_off(void) {
    gpio_set_level(LED_R_PIN, 1);
    gpio_set_level(LED_G_PIN, 1);
    gpio_set_level(LED_B_PIN, 1);
}

void gpio_init(void) {
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << LED_R_PIN) | (1ULL << LED_G_PIN) | (1ULL << LED_B_PIN) |
                        (1ULL << LED1_PIN) | (1ULL << LED2_PIN) | (1ULL << LED3_PIN) |
                        (1ULL << LED4_PIN) | (1ULL << LED5_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&led_conf);

    gpio_config_t key_conf = {
        .pin_bit_mask = (1ULL << KEY_PIN),
        .mode = GPIO_MODE_INPUT,
    };
    gpio_config(&key_conf);

    gpio_config_t aux_conf = {
        .pin_bit_mask = (1ULL << LORA_AUX_PIN),
        .mode = GPIO_MODE_INPUT,
    };
    gpio_config(&aux_conf);

    all_led_off();
    all_rgb_off();
}

void uart_init(void) {
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
    ESP_LOGI(TAG, "Master init");
    lora_wait_idle();
}

// 下发查询指令
void lora_query(uint8_t addr) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%02X,%02X,QUERY", addr, MASTER_ADDR);
    lora_wait_idle();
    uart_write_bytes(UART_NUM, buf, strlen(buf));
    ESP_LOGI(TAG, "Query: %02X", addr);
}

// 下发流水灯启动指令
void lora_send_waterfall(uint8_t addr) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%02X,%02X,WATERFALL", addr, MASTER_ADDR);
    lora_wait_idle();
    uart_write_bytes(UART_NUM, buf, strlen(buf));
    ESP_LOGI(TAG, "Send WATERFALL to slave: %02X", addr);
}

void parse_packet(const char *buf) {
    char dst_str[4], src_str[4], data[32];
    if (sscanf(buf, "%2s,%2s,%[^,\n]", dst_str, src_str, data) != 3) return;

    uint8_t dst = strtoul(dst_str, NULL, 16);
    uint8_t src = strtoul(src_str, NULL, 16);

    if (dst != 0x00 || src < 1 || src > 5 || !strstr(data, "STATUS:")) return;
    parking_status[src] = atoi(&data[7]);
    ESP_LOGI(TAG, "Slave %02X: %s", src, parking_status[src] ? "OCCUPIED" : "FREE");
    
    // 目标车位被占用，停止流水
    if (parking_status[src] == 1 && src == target_slot) {
        need_waterfall = false;
        target_slot = 0;
        all_led_off();
        all_rgb_off();
        ESP_LOGW(TAG, "Slave %02X occupied, WS2812 stop", src);
    }
}

void run_waterfall(uint8_t target) {
    if (target == 1) {
        gpio_set_level(LED1_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(200));
        gpio_set_level(LED1_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
    } else {
        for (int i = 1; i <= target; i++) {
            all_led_off();
            switch (i) {
                case 1: gpio_set_level(LED1_PIN, 0); break;
                case 2: gpio_set_level(LED2_PIN, 0); break;
                case 3: gpio_set_level(LED3_PIN, 0); break;
                case 4: gpio_set_level(LED4_PIN, 0); break;
                case 5: gpio_set_level(LED5_PIN, 0); break;
            }
            vTaskDelay(pdMS_TO_TICKS(120));
        }
    }
}

uint8_t check_all_slots(void) {
    ESP_LOGI(TAG, "========== CHECK ALL SLOTS ==========");
    for (int i = 1; i <= 5; i++) parking_status[i] = 0xFF;

    // 查询所有从机状态
    for (int i = 0; i < 5; i++) {
        lora_query(slave_list[i]);
        uint32_t start = xTaskGetTickCount();
        while (xTaskGetTickCount() - start < pdMS_TO_TICKS(700)) {
            uint8_t buf[64];
            int len = uart_read_bytes(UART_NUM, buf, 64, pdMS_TO_TICKS(10));
            if (len > 0) { buf[len] = 0; parse_packet((char*)buf); }
        }
    }

    // 查找第一个空闲车位，连续发2次流水指令防丢包
    uint8_t first_free = 0;
    for (int i = 1; i <= 5; i++) {
        if (parking_status[i] == 0) {
            first_free = i;
            lora_send_waterfall(i);
            vTaskDelay(pdMS_TO_TICKS(50));
            lora_send_waterfall(i);
            ESP_LOGI(TAG, "Assign slot %d, send waterfall cmd twice", first_free);
            break;
        }
    }
    return first_free;
}

void app_main(void) {
    gpio_init();
    uart_init();

    while (1) {
        uint8_t key = gpio_get_level(KEY_PIN);
        uint8_t buf[128];

        // 实时接收从机上报
        int len = uart_read_bytes(UART_NUM, buf, 128, pdMS_TO_TICKS(10));
        if (len > 0) { buf[len] = 0; parse_packet((char*)buf); }

        // 按键触发全量查询分配车位
        if (key == 1 && last_key == 0) {
            all_led_off();
            all_rgb_off();
            target_slot = 0;
            need_waterfall = false;
            ESP_LOGI(TAG, "RESET ALL LED STATUS");
            vTaskDelay(pdMS_TO_TICKS(300));

            uint8_t res = check_all_slots();
            if (res != 0) {
                target_slot = res;
                need_waterfall = true;
                all_rgb_off();
                gpio_set_level(LED_G_PIN, 0);
                ESP_LOGI(TAG, "Assigned slot: %d, master led waterfall start", target_slot);
            } else {
                target_slot = 0;
                need_waterfall = false;
                all_led_off();
                all_rgb_off();
                gpio_set_level(LED_R_PIN, 0);
                ESP_LOGI(TAG, "ALL FULL");
            }
        }
        last_key = key;

        // 主机本地LED流水逻辑保留
        if (need_waterfall && target_slot != 0) {
            if (parking_status[target_slot] == 1) {
                need_waterfall = false;
                target_slot = 0;
                all_led_off();
                all_rgb_off();
                ESP_LOGI(TAG, "Car detected, master led stop");
            } else {
                run_waterfall(target_slot);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}