/* HTTP GET Example using plain POSIX sockets

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "sdkconfig.h"
#include "protocol_examples_common.h"
#include "lwip/err.h"
#include "steinhard_hart_converter.h"
#include "esp_log.h"  // write out to monitor only for debugging 
#include "esp_adc/adc_oneshot.h" // get the raw adc value should be a int ? 
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "http_request_example";

// create RTC Slow Memory to store measurement data
RTC_DATA_ATTR static float MEASUREMENTS[10];
RTC_DATA_ATTR static size_t MEASUREMENT_IDX = 0;

#define EXAMPLE_ADC1_CHAN0          ADC_CHANNEL_2
#define EXAMPLE_ADC_ATTEN           ADC_ATTEN_DB_12 

static int adc_raw[2][10];
static int voltage[2][10];
static bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle);
static void example_adc_calibration_deinit(adc_cali_handle_t handle);



void send_http_post(void *pvParameters)
{
    const char *post_data = "{\"author\":\"beeceptor\"}";

    esp_http_client_config_t config = {
        // .url = "http://echo.free.beeceptor.com/sample-request",
        .url = "http://192.168.178.56:8000/",
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        vTaskDelete(NULL);
        return;
    }

    // Set POST body
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_http_client_set_header(client, "Content-Type", "application/json");
    char content_length_str[30];
    snprintf(content_length_str, sizeof(content_length_str), "%d", (int)strlen(post_data));
    esp_http_client_set_header(client, "Content-Length", content_length_str);


    // Perform request
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        int content_length = esp_http_client_get_content_length(client);
        ESP_LOGI(TAG, "HTTP POST Status = %d, Content length = %d", status, content_length);

        // Read response (if any)
        char buffer[512];
        int total_read_len = 0;
        int read_len = 0;
        do {
            read_len = esp_http_client_read(client, buffer, sizeof(buffer) - 1);
            if (read_len > 0) {
                buffer[read_len] = 0;  // Null-terminate
                ESP_LOGI(TAG, "HTTP Response: %s", buffer);
                total_read_len += read_len;
            }
        } while (read_len > 0);

    } else {
        ESP_LOGE(TAG, "HTTP POST failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    vTaskDelete(NULL);
}


int app_main(void)
{
    ESP_ERROR_CHECK( nvs_flash_init() );
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());


    uint64_t wakeup_causes = esp_sleep_get_wakeup_causes();
    bool cold_boot = (wakeup_causes == 0);

    if (cold_boot) {
        MEASUREMENT_IDX = 0;
        ESP_LOGI(TAG, "Cold boot, resetting measurement index");
    }
//-------------ADC Init---------------//
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    //-------------ADC1 Config---------------//
    adc_oneshot_chan_cfg_t config = {
        .atten = EXAMPLE_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    //-------------ADC1 Calibration Init---------------//
    adc_cali_handle_t adc1_cali_chan0_handle = NULL;

    bool do_calibration1_chan0 = example_adc_calibration_init(ADC_UNIT_1, EXAMPLE_ADC1_CHAN0, EXAMPLE_ADC_ATTEN, &adc1_cali_chan0_handle);

    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, EXAMPLE_ADC1_CHAN0, &config));

    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, EXAMPLE_ADC1_CHAN0, &adc_raw[0][0]));
    ESP_LOGI(TAG, "ADC%d Channel[%d] Raw Data: %d", ADC_UNIT_1 + 1, EXAMPLE_ADC1_CHAN0, adc_raw[0][0]);
    
    if (do_calibration1_chan0) {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_chan0_handle, adc_raw[0][0], &voltage[0][0]));
        ESP_LOGI(TAG, "ADC%d Channel[%d] Cali Voltage: %d mV", ADC_UNIT_1 + 1, EXAMPLE_ADC1_CHAN0, voltage[0][0]);
    } 

    // take the measurement, every 30 seconds for 5 minutes (10 measurements)
    if (MEASUREMENT_IDX < 10) {
        
        float adc_value = 0.0001 * (float) voltage[0][0];
        MEASUREMENTS[MEASUREMENT_IDX] = compute_temperature_celsius(adc_value);
        ESP_LOGI(TAG, "Measurement %f", MEASUREMENTS[MEASUREMENT_IDX]);
        MEASUREMENT_IDX++;
    }

    // go to deep sleep if more measurements are needed
    if (MEASUREMENT_IDX < 10) {
        ESP_LOGI(TAG, "Going to deep sleep");
        esp_sleep_enable_timer_wakeup(5 * 100000ULL); // for testing, every 0.5 seconds
        esp_deep_sleep_start();
    } else { 
        ESP_LOGI(TAG, "All measurements collected");
    }




    // connec to wi-fi and do a batch HTTP POST with all measurements
    // ESP_ERROR_CHECK(example_connect());

    // esp_netif_t *sta_netif =
    //     esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    // assert(sta_netif);

    // esp_netif_set_default_netif(sta_netif);

    // esp_netif_dns_info_t dns = { 0 };
    // inet_pton(AF_INET, "8.8.8.8", &dns.ip.u_addr.ip4);
    // dns.ip.type = ESP_IPADDR_TYPE_V4;
    // esp_netif_set_dns_info(sta_netif, ESP_NETIF_DNS_MAIN, &dns);

    // xTaskCreate(&send_http_post, "http_post_task", 4096, NULL, 5, NULL);


    // xTaskCreate(&http_get_task, "http_get_task", 4096, NULL, 5, NULL);
    return EXIT_SUCCESS;
}


/*---------------------------------------------------------------
        ADC Calibration
---------------------------------------------------------------*/
static bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration Success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return calibrated;
}

static void example_adc_calibration_deinit(adc_cali_handle_t handle)
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "deregister %s calibration scheme", "Curve Fitting");
    ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(handle));

#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "deregister %s calibration scheme", "Line Fitting");
    ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(handle));
#endif
}