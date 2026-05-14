#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "string.h"
#include "esp_log.h"

/* ====================== 【硬件引脚定义 - 严格匹配你的原理图】 ====================== */
// LED引脚 (RGB+普通LED)
#define LED_R_PIN GPIO_NUM_26
#define LED_G_PIN GPIO_NUM_25
#define LED_B_PIN GPIO_NUM_27

#define LED1_PIN GPIO_NUM_19 // 普通LED1
#define LED2_PIN GPIO_NUM_21 // 普通LED2
#define LED3_PIN GPIO_NUM_13 // 普通LED3
#define LED4_PIN GPIO_NUM_14 // 普通LED4
#define LED5_PIN GPIO_NUM_32 // 普通LED5

// 按键引脚
#define KEY_PIN GPIO_NUM_18 // 测试按键

// 毫米波雷达输入
#define OT2_PIN GPIO_NUM_33

// lora模块引脚
#define LORA_RX_PIN GPIO_NUM_22
#define LORA_TX_PIN GPIO_NUM_23
#define LORA_AUX_PIN GPIO_NUM_34 // 输入专用引脚，无上下拉

// UART配置
#define UART_PORT_NUM UART_NUM_1
#define UART_BAUD_RATE 9600 // E32-433默认波特率9600
#define UART_BUF_SIZE 1024

static const char *TAG = "MAIN";

// UART初始化函数（LoRa使用）
static void uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    // 配置UART参数
    uart_driver_install(UART_PORT_NUM, UART_BUF_SIZE, 0, 0, NULL, 0);
    uart_param_config(UART_PORT_NUM, &uart_config);
    // 设置TX/RX引脚
    uart_set_pin(UART_PORT_NUM, LORA_TX_PIN, LORA_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

void app_main(void)
{
    // 1. 所有 LED 引脚配置为输出
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << LED_R_PIN) | (1ULL << LED_G_PIN) | (1ULL << LED_B_PIN) |
                           (1ULL << LED1_PIN) | (1ULL << LED2_PIN) | (1ULL << LED3_PIN) |
                           (1ULL << LED4_PIN) | (1ULL << LED5_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // 初始全部熄灭
    gpio_set_level(LED_R_PIN, 1);
    gpio_set_level(LED_G_PIN, 1);
    gpio_set_level(LED_B_PIN, 1);
    gpio_set_level(LED1_PIN, 1);
    gpio_set_level(LED2_PIN, 1);
    gpio_set_level(LED3_PIN, 1);
    gpio_set_level(LED4_PIN, 1);
    gpio_set_level(LED5_PIN, 1);

    // LED数组
    const gpio_num_t leds[] = {
        LED_R_PIN,
        LED_G_PIN,
        LED_B_PIN,
        LED1_PIN,
        LED2_PIN,
        LED3_PIN,
        LED4_PIN,
        LED5_PIN,
    };
    const int led_count = sizeof(leds) / sizeof(leds[0]);

    // 2. 输入引脚配置（按键 + 雷达）【修复：变量名错误】
    gpio_config_t input_io_conf = {};
    input_io_conf.intr_type = GPIO_INTR_DISABLE;
    input_io_conf.mode = GPIO_MODE_INPUT;
    input_io_conf.pin_bit_mask = (1ULL << KEY_PIN) | (1ULL << OT2_PIN) | (1ULL << LORA_AUX_PIN);
    input_io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    input_io_conf.pull_up_en = GPIO_PULLUP_DISABLE; // 34脚必须禁用上下拉
    gpio_config(&input_io_conf);

    // 3. 初始化UART（LoRa）
    uart_init();
    ESP_LOGI(TAG, "UART & GPIO 初始化完成");

    while (1)
    {
        int key_level = gpio_get_level(KEY_PIN);
        int ot2_level = gpio_get_level(OT2_PIN);

        // 按键控制 RGB 红/绿
        if (ot2_level == 1)
        {
            gpio_set_level(leds[0], 0); // 红亮
            gpio_set_level(leds[1], 1); // 绿灭
            ESP_LOGI("RADAR", "雷达检测到目标");
        }
        else
        {
            gpio_set_level(leds[1], 0); // 绿亮
            gpio_set_level(leds[0], 1); // 红灭

            ESP_LOGI("RADAR", "雷达无目标");
        }

        // 雷达控制 LED1 / LED2
        if (key_level == 1)
        {
            gpio_set_level(leds[3], 0); // LED1亮
            gpio_set_level(leds[4], 1); // LED2灭
            ESP_LOGI("KEY", "按键按下");
        }
        else
        {
            gpio_set_level(leds[4], 0); // LED2亮
            gpio_set_level(leds[3], 1); // LED1灭
            ESP_LOGI("KEY", "按键松开");
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}