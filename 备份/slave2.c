#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/rmt.h"
#include "esp_log.h"

// ====================== 硬件引脚定义 ======================
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

// 从机地址，可按需修改 0x01/0x02/0x03/0x04/0x05
#define DEVICE_ADDR 0x04
#define MASTER_ADDR 0x00

// ====================== WS2812 配置 ======================
#define WS2812_GPIO GPIO_NUM_4
#define RMT_CHANNEL RMT_CHANNEL_0
#define LED_COUNT 60
static const char *WS2812_TAG = "ws2812";
static uint8_t led_data[LED_COUNT][3];
static rmt_item32_t ws2812_items[LED_COUNT * 24];
static bool flow_light_en = false; // 流水灯使能标志

// ====================== LORA 工具函数 ======================
void lora_wait_idle(void)
{
    while (gpio_get_level(LORA_AUX_PIN) == 0) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    vTaskDelay(pdMS_TO_TICKS(10));
}

// ====================== WS2812 底层驱动（完全沿用你提供的原版） ======================
static void ws2812_build_bit(rmt_item32_t *item, int bit)
{
    if (bit) {
        item->level0 = 1;
        item->duration0 = 8;
        item->level1 = 0;
        item->duration1 = 4;
    } else {
        item->level0 = 1;
        item->duration0 = 4;
        item->level1 = 0;
        item->duration1 = 8;
    }
}

static void ws2812_refresh(void)
{
    int idx = 0;
    for (int i = 0; i < LED_COUNT; i++) {
        // GRB格式（WS2812标准）
        uint8_t colors[3] = {led_data[i][1], led_data[i][0], led_data[i][2]};
        for (int color = 0; color < 3; color++) {
            for (int bit = 7; bit >= 0; bit--) {
                ws2812_build_bit(&ws2812_items[idx++], (colors[color] >> bit) & 0x01);
            }
        }
    }

    ESP_ERROR_CHECK(rmt_write_items(RMT_CHANNEL, ws2812_items, idx, false));
    ESP_ERROR_CHECK(rmt_wait_tx_done(RMT_CHANNEL, portMAX_DELAY));
    vTaskDelay(pdMS_TO_TICKS(1));
}

static void ws2812_set_pixel(int pos, uint8_t r, uint8_t g, uint8_t b)
{
    if (pos < 0 || pos >= LED_COUNT) {
        return;
    }
    led_data[pos][0] = r;
    led_data[pos][1] = g;
    led_data[pos][2] = b;
}

static void ws2812_clear(void)
{
    memset(led_data, 0, sizeof(led_data));
    ws2812_refresh();
}

// ====================== 流水灯任务：远端→接线口，无0号灯常亮BUG ======================
static void ws2812_flow_task(void *arg)
{
    int cur_pos = LED_COUNT - 1;
    while (1) {
        if (flow_light_en) {
            ws2812_clear();
            ws2812_set_pixel(cur_pos, 0x00, 0x50, 0x50); // 青绿色光点
            ws2812_refresh();
            vTaskDelay(pdMS_TO_TICKS(80));

            // 向接线端移动一格
            cur_pos--;
            // 走到0号后自动跳回最远端，不会停留在0号常亮
            if (cur_pos < 0) {
                cur_pos = LED_COUNT - 1;
            }
        } else {
            ws2812_clear();
            cur_pos = LED_COUNT - 1;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

// ====================== WS2812 初始化 ======================
static void ws2812_init(void)
{
    rmt_config_t rmt_cfg = {
        .rmt_mode = RMT_MODE_TX,
        .channel = RMT_CHANNEL,
        .gpio_num = WS2812_GPIO,
        .mem_block_num = 1,
        .clk_div = 8,
    };
    ESP_ERROR_CHECK(rmt_config(&rmt_cfg));
    ESP_ERROR_CHECK(rmt_driver_install(rmt_cfg.channel, 0, 0));

    ws2812_clear();
    ESP_LOGI(WS2812_TAG, "WS2812 init success");
    // 创建流水灯后台任务
    xTaskCreate(ws2812_flow_task, "ws2812_flow", 4096, NULL, 5, NULL);
}

// ====================== GPIO初始化 ======================
void gpio_init(void)
{
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << LED_R_PIN) | (1ULL << LED_G_PIN) | (1ULL << LED_B_PIN) |
                        (1ULL << LED1_PIN) | (1ULL << LED2_PIN) | (1ULL << LED3_PIN) |
                        (1ULL << LED4_PIN) | (1ULL << LED5_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&led_conf);

    // 所有板载LED默认熄灭
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

// ====================== UART LORA串口初始化 ======================
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
    ESP_LOGI(TAG, "Slave %02X init done", DEVICE_ADDR);
    lora_wait_idle();
}

// ====================== 向主机上报车位状态 ======================
void lora_send_status(uint8_t status)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%02X,%02X,STATUS:%d", MASTER_ADDR, DEVICE_ADDR, status);
    lora_wait_idle();
    uart_write_bytes(UART_NUM, buf, strlen(buf));
    ESP_LOGI(TAG, "Upload status: %s", buf);
}

// ====================== LORA指令解析：QUERY查询 / WATERFALL启动流水 ======================
bool parse_cmd(uint8_t *src_addr, const char *buf)
{
    char dst_str[4], src_str[4], data[32];
    if (sscanf(buf, "%2s,%2s,%s", dst_str, src_str, data) == 3) {
        uint8_t dst = strtoul(dst_str, NULL, 16);
        *src_addr = strtoul(src_str, NULL, 16);

        // 不是发给本机的指令直接忽略
        if (dst != DEVICE_ADDR) return false;

        if (strcmp(data, "QUERY") == 0) {
            // 主机查询车位，返回当前状态
            return true;
        } else if (strcmp(data, "WATERFALL") == 0) {
            // 收到流水指令，开启灯带流动
            flow_light_en = true;
            ESP_LOGI(TAG, "Receive WATERFALL cmd, start strip flow");
            return false;
        }
    }
    return false;
}

// ====================== RGB状态指示灯更新 ======================
void update_rgb(uint8_t occupied)
{
    if (occupied) {
        // 有车：红灯亮
        gpio_set_level(LED_R_PIN, 0);
        gpio_set_level(LED_G_PIN, 1);
        // 车位占用，关闭流水灯带
        flow_light_en = false;
        ws2812_clear();
        ESP_LOGI(TAG, "Parking occupied, turn off WS2812");
    } else {
        // 空闲：绿灯亮
        gpio_set_level(LED_R_PIN, 1);
        gpio_set_level(LED_G_PIN, 0);
    }
}

// ====================== 获取车位占用状态（按键+雷达双判断） ======================
uint8_t get_parking_status(void)
{
    uint8_t key = gpio_get_level(KEY_PIN);
    uint8_t radar = gpio_get_level(RADAR_OUTPUT_PIN);
    return (key == 1 || (key == 0 && radar == 1)) ? 1 : 0;
}

// ====================== 主入口 ======================
void app_main(void)
{
    gpio_init();
    uart_init();
    ws2812_init();

    uint8_t buf[UART_BUF_SIZE];
    uint8_t last_status = 0xFF;

    while (1) {
        uint8_t occupied = get_parking_status();
        update_rgb(occupied);

        // 车位状态发生变化，主动上报主机
        if (occupied != last_status) {
            last_status = occupied;
            lora_send_status(occupied);
        }

        // 接收主机LORA指令
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