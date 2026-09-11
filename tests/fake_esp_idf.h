// Adaptador SOMENTE para testes no host; não participa do firmware.
#ifndef FLOWCOUNT_FAKE_ESP_IDF_H
#define FLOWCOUNT_FAKE_ESP_IDF_H
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/time.h>
#include <pthread.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103

typedef unsigned TickType_t;
typedef unsigned UBaseType_t;
typedef int BaseType_t;
typedef void *TaskHandle_t;
typedef struct fake_queue *QueueHandle_t;
#define pdTRUE 1
#define pdPASS 1
#define pdFALSE 0
#define portMAX_DELAY UINT32_MAX
#define tskIDLE_PRIORITY 0
#define pdMS_TO_TICKS(ms) ((ms) / 10)
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(lock) ((void)pthread_mutex_lock(lock))
#define portEXIT_CRITICAL(lock) ((void)pthread_mutex_unlock(lock))
#define CONFIG_FLOWCOUNT_NTP_SERVER "test.ntp.invalid"
#define CONFIG_FLOWCOUNT_CLOCK_MAX_AGE_SECONDS 86400
#define CONFIG_LWIP_SNTP_UPDATE_DELAY 3600000

typedef struct {
    bool wait_for_sync;
    bool smooth_sync;
    bool start;
    void (*sync_cb)(struct timeval *);
    const char *server;
} esp_sntp_config_t;
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(name) { .start = true, .server = (name) }
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *config);
int64_t esp_timer_get_time(void);
int fake_time_gettimeofday(struct timeval *tv, void *tz);

#define CONFIG_FLOWCOUNT_BANCADA "B01"
#define CONFIG_FLOWCOUNT_STATION_ID 7
#define CONFIG_FLOWCOUNT_EVENT_QUEUE_CAPACITY 72

#ifndef CONFIG_FLOWCOUNT_DIAGNOSTIC_CONSUMER
#define CONFIG_FLOWCOUNT_DIAGNOSTIC_CONSUMER 0
#endif

#ifndef CONFIG_FLOWCOUNT_COMM_ENABLED
#define CONFIG_FLOWCOUNT_COMM_ENABLED 0
#endif

#define CONFIG_FLOWCOUNT_DIAGNOSTIC_DELAY_SECONDS 180

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait);
BaseType_t xQueuePeek(QueueHandle_t queue, void *item, TickType_t wait);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue);
void vQueueDelete(QueueHandle_t queue);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                      void *arg, UBaseType_t priority, TaskHandle_t *handle);
void vTaskDelay(TickType_t ticks);
void bootloader_random_enable(void);
void bootloader_random_disable(void);
void esp_fill_random(void *buffer, size_t size);
void fake_log(const char *tag, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
#define ESP_LOGE(...) fake_log(__VA_ARGS__)
#define ESP_LOGW(...) fake_log(__VA_ARGS__)
#define ESP_LOGI(...) fake_log(__VA_ARGS__)
#endif
