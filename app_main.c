/* LED Light Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_standard_devices.h>

#include <app_wifi.h>

#include "app_priv.h"

#include "esp_system.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_tls.h"

#include "esp_http_client.h"

#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 2048

extern const char howsmyssl_com_root_cert_pem_start[] asm("_binary_howsmyssl_com_root_cert_pem_start");
extern const char howsmyssl_com_root_cert_pem_end[] asm("_binary_howsmyssl_com_root_cert_pem_end");

static const char *TAG = "app_main";

char BUFFER_LINE_NOTIFY[200];

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer; // Buffer to store response of http request from event handler
    static int output_len;      // Stores number of bytes read
    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        /*
             *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
             *  However, event handler can also be used in case chunked encoding is used.
             */
        if (!esp_http_client_is_chunked_response(evt->client))
        {
            // If user_data buffer is configured, copy the response into the buffer
            if (evt->user_data)
            {
                memcpy(evt->user_data + output_len, evt->data, evt->data_len);
            }
            else
            {
                if (output_buffer == NULL)
                {
                    output_buffer = (char *)malloc(esp_http_client_get_content_length(evt->client));
                    output_len = 0;
                    if (output_buffer == NULL)
                    {
                        ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                        return ESP_FAIL;
                    }
                }
                memcpy(output_buffer + output_len, evt->data, evt->data_len);
            }
            output_len += evt->data_len;
        }

        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        if (output_buffer != NULL)
        {
            // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
            // ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
            free(output_buffer);
            output_buffer = NULL;
            output_len = 0;
        }
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
        int mbedtls_err = 0;
        esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
        if (err != 0)
        {
            if (output_buffer != NULL)
            {
                free(output_buffer);
                output_buffer = NULL;
                output_len = 0;
            }
            ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
            ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
        }
        break;
    }
    return ESP_OK;
}

static void send_line_notify(uint8_t ch, char *msg)
{
    esp_http_client_config_t config = {
        .url = "https://notify-api.line.me/api/notify",
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    memset(BUFFER_LINE_NOTIFY, 0, sizeof(BUFFER_LINE_NOTIFY));
    sprintf(BUFFER_LINE_NOTIFY, "message=CH%d : %s", ch + 1, msg);

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_header(client, "Authorization", "Bearer XXXXXXX");
    esp_http_client_set_post_field(client, BUFFER_LINE_NOTIFY, strlen(BUFFER_LINE_NOTIFY));
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d",
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
    }
    else
    {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Status = %d, content_length = %d",
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
    }
    esp_http_client_cleanup(client);
}

uint8_t get_device_index(const char *name)
{
    if (strcmp(name, "CH1") == 0)
    {
        return 0;
    }
    else if (strcmp(name, "CH2") == 0)
    {
        return 1;
    }
    else if (strcmp(name, "CH3") == 0)
    {
        return 2;
    }
    else if (strcmp(name, "CH4") == 0)
    {
        return 3;
    }

    return 0;
}
/* Callback to handle commands received from the RainMaker cloud */
static esp_err_t common_callback(const char *dev_name, const char *name, esp_rmaker_param_val_t val, void *priv_data)
{

    uint8_t device_index = get_device_index(dev_name);

    if (strcmp(name, ESP_RMAKER_DEF_POWER_NAME) == 0)
    {
        ESP_LOGI(TAG, "Received value = %s for %s - %s",
                 val.val.b ? "true" : "false", dev_name, name);
        app_light_set_power(device_index, val.val.b);
        send_line_notify(device_index, val.val.b ? "Power On" : "Power Off");
    }
    else if (strcmp(name, "brightness") == 0)
    {
        ESP_LOGI(TAG, "Received value = %d for %s - %s",
                 val.val.i, dev_name, name);
        app_light_set_brightness(device_index, val.val.i);
        send_line_notify(device_index, "Brightness Changed");
    }
    else if (strcmp(name, "hue") == 0)
    {
        ESP_LOGI(TAG, "Received value = %d for %s - %s",
                 val.val.i, dev_name, name);
        app_light_set_hue(device_index, val.val.i);
        send_line_notify(device_index, "Hue Changed");
    }
    else if (strcmp(name, "saturation") == 0)
    {
        ESP_LOGI(TAG, "Received value = %d for %s - %s",
                 val.val.i, dev_name, name);
        app_light_set_saturation(device_index, val.val.i);
        send_line_notify(device_index, "Saturation Changed");
    }
    else
    {
        /* Silently ignoring invalid params */
        return ESP_OK;
    }
    esp_rmaker_update_param(dev_name, name, val);
    return ESP_OK;
}

void app_main()
{

    memset(BUFFER_LINE_NOTIFY, 0, sizeof(BUFFER_LINE_NOTIFY));
    /* Initialize Application specific hardware drivers and
     * set initial state.
     */
    app_driver_init();

    /* Initialize NVS. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* Initialize Wi-Fi. Note that, this should be called before esp_rmaker_init()
     */
    app_wifi_init();

    /* Initialize the ESP RainMaker Agent.
     * Note that this should be called after app_wifi_init() but before app_wifi_start()
     * */
    esp_rmaker_config_t rainmaker_cfg = {
        .info = {
            .name = "AppStack RainMaker Device",
            .type = "Lightbulb",
        },
        .enable_time_sync = false,
    };
    err = esp_rmaker_init(&rainmaker_cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Could not initialise ESP RainMaker. Aborting!!!");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        abort();
    }

    /* Create a device and add the relevant parameters to it */
    esp_rmaker_create_switch_device("CH1", common_callback, NULL, DEFAULT_POWER);
    esp_rmaker_create_switch_device("CH2", common_callback, NULL, DEFAULT_POWER);
    esp_rmaker_create_switch_device("CH3", common_callback, NULL, DEFAULT_POWER);
    esp_rmaker_create_switch_device("CH4", common_callback, NULL, DEFAULT_POWER);

    esp_rmaker_device_add_brightness_param("CH1", "brightness", DEFAULT_BRIGHTNESS);
    esp_rmaker_device_add_hue_param("CH1", "hue", DEFAULT_HUE);
    esp_rmaker_device_add_saturation_param("CH1", "saturation", DEFAULT_SATURATION);

    esp_rmaker_device_add_brightness_param("CH2", "brightness", DEFAULT_BRIGHTNESS);
    esp_rmaker_device_add_hue_param("CH2", "hue", DEFAULT_HUE);
    esp_rmaker_device_add_saturation_param("CH2", "saturation", DEFAULT_SATURATION);

    esp_rmaker_device_add_brightness_param("CH3", "brightness", DEFAULT_BRIGHTNESS);
    esp_rmaker_device_add_hue_param("CH3", "hue", DEFAULT_HUE);
    esp_rmaker_device_add_saturation_param("CH3", "saturation", DEFAULT_SATURATION);

    esp_rmaker_device_add_brightness_param("CH4", "brightness", DEFAULT_BRIGHTNESS);
    esp_rmaker_device_add_hue_param("CH4", "hue", DEFAULT_HUE);
    esp_rmaker_device_add_saturation_param("CH4", "saturation", DEFAULT_SATURATION);

    /* Start the ESP RainMaker Agent */
    esp_rmaker_start();

    /* Start the Wi-Fi.
     * If the node is provisioned, it will start connection attempts,
     * else, it will start Wi-Fi provisioning. The function will return
     * after a connection has been successfully established
     */
    app_wifi_start();
}
