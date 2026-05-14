#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "string.h"

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
#define LORA_AUX_PIN GPIO_NUM_34

// UART配置
#define UART_PORT_NUM UART_NUM_1
#define UART_BAUD_RATE 9600 // E32-433默认波特率9600
#define UART_BUF_SIZE 1024

void app_main(void)
{
    // 所有 LED 引脚配置为输出
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

    // 按顺序测试 RGB 和普通 LED
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

    // 所有 LED 引脚配置为输出
    gpio_config_t input_io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << KEY_PIN) | (1ULL << OT2_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&input_io_conf);

    // 先熄灭所有 LED，保证只有一个灯亮
    for (int i = 0; i < led_count; i++)
    {
        gpio_set_level(leds[i], 1);
    }

    while (1)
    {
        int key_level = gpio_get_level(KEY_PIN);
        int ot2_level = gpio_get_level(OT2_PIN);

        if (key_level == 0)
        {
            gpio_set_level(leds[0], 0);
        }
        else
        {
            gpio_set_level(leds[1], 0);
        }

        if (ot2_level == 1)
        {
            gpio_set_level(leds[3], 0);
        }
        else
        {
            gpio_set_level(leds[4], 0);
        }
    }
}
