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
#include "time_vienna.h"
#include "esp_log.h"             // write out to monitor only for debugging
#include "esp_adc/adc_oneshot.h" // get the raw adc value should be a int ?
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "string.h"
#include <sys/socket.h>
#include <arpa/inet.h>


// SERVER_HOST works with hostname or numerical IP

//#define SERVER_HOST "pbl.permasense.uibk.ac.at"
//#define SERVER_HOST  "192.168.0.113"   //for debugging
#define SERVER_HOST  "MacBookAir.telekom.ip"   //for debugging2

//#define SERVER_PORT 22504 
#define SERVER_PORT 5001      //for debugging

static const char *TAG = "group_2";

// create RTC Slow Memory to store measurement data
RTC_DATA_ATTR static float MEASUREMENTS[10];
RTC_DATA_ATTR static size_t MEASUREMENT_IDX = 0;

#define EXAMPLE_ADC1_CHAN0 ADC_CHANNEL_2
#define EXAMPLE_ADC_ATTEN ADC_ATTEN_DB_12

static bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle);


/*---------------------------------------------------------------
        tcp_function_new
---------------------------------------------------------------*/
void send_tcp_data(void *pvParameters)
{
    char (*data)[80] = pvParameters;

    // Build payload
    char send_buffer[10 * 80 + 1];
    send_buffer[0] = '\0';

    for (int i = 0; i < 10; i++)
    {
        strcat(send_buffer, data[i]);
        strcat(send_buffer, "\n");
    }

 ESP_LOGI(TAG, "Payload built, %d bytes", (int)strlen(send_buffer));

    // Resolve hostname or numeric IP
    struct sockaddr_in server_addr;
    struct in_addr addr;

    if (inet_pton(AF_INET, SERVER_HOST, &addr) == 1)
    {
        // SERVER_HOST is numeric IP
        server_addr.sin_addr = addr;
    }
    else
    {
        // SERVER_HOST is a hostname, resolve via DNS
        struct hostent *he = gethostbyname(SERVER_HOST);
        if (!he)
        {
            ESP_LOGE(TAG, "DNS lookup failed for %s", SERVER_HOST);
            vTaskDelete(NULL);
            return;
        }
        memcpy(&server_addr.sin_addr, he->h_addr, he->h_length);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0)
    {
        ESP_LOGE(TAG, "Unable to create socket");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Connecting to %s:%d...", SERVER_HOST, SERVER_PORT);
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0)
    {
        ESP_LOGE(TAG, "Socket unable to connect");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Connected, sending payload...");

    // Send payload
    if (send(sock, send_buffer, strlen(send_buffer), 0) < 0)
    {
        ESP_LOGE(TAG, "Send failed");
    }
    else
    {
        ESP_LOGI(TAG, "Payload sent successfully");
    }

    // Optional: receive response
    char rx_buffer[512];
    int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
    if (len > 0)
    {
        rx_buffer[len] = 0;
        ESP_LOGI(TAG, "Received: %s", rx_buffer);
    }

    // Close socket
    close(sock);
    ESP_LOGI(TAG, "Socket closed");

    vTaskDelete(NULL);
}

/*---------------------------------------------------------------
        main function
---------------------------------------------------------------*/
int app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    uint64_t wakeup_causes = esp_sleep_get_wakeup_causes();
    bool cold_boot = (wakeup_causes == 0);

    if (cold_boot)
    {
        MEASUREMENT_IDX = 0;
        // ESP_LOGI(TAG, "Cold boot, resetting measurement index");
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

    int adc_raw, voltage;
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, EXAMPLE_ADC1_CHAN0, &adc_raw));
    // ESP_LOGI(TAG, "ADC%d Channel[%d] Raw Data: %d", ADC_UNIT_1 + 1, EXAMPLE_ADC1_CHAN0, adc_raw);

    if (do_calibration1_chan0)
    {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_chan0_handle, adc_raw, &voltage));
        // ESP_LOGI(TAG, "ADC%d Channel[%d] Cali Voltage: %d mV", ADC_UNIT_1 + 1, EXAMPLE_ADC1_CHAN0, voltage);
    }

    // take the measurement, every 30 seconds for 5 minutes (10 measurements)
    if (MEASUREMENT_IDX < 10)
    {

        // float adc_value = 0.0001 * (float) voltage;
        MEASUREMENTS[MEASUREMENT_IDX] = compute_temperature_celsius(0.001 * (float)voltage);
        // ESP_LOGI(TAG, "Measurement %f", MEASUREMENTS[MEASUREMENT_IDX]);
        MEASUREMENT_IDX++;
    }

    // go to deep sleep if more measurements are needed
    if (MEASUREMENT_IDX < 10)
    {
        // ESP_LOGI(TAG, "Going to deep sleep");
        //esp_sleep_enable_timer_wakeup(5 * 100000ULL); // for testing, every 0.5 seconds
        esp_sleep_enable_timer_wakeup(30 * 1000000ULL); // for measuring, every 30 seconds
        esp_deep_sleep_start();
    }
    //  else {
    //     ESP_LOGI(TAG, "All measurements collected");
    // }

    // connec to wi-fi and do a batch HTTP POST with all measurements
    ESP_ERROR_CHECK(example_connect());

    esp_netif_t *sta_netif =
        esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    assert(sta_netif);

    esp_netif_set_default_netif(sta_netif);

    // esp_netif_dns_info_t dns = { 0 };
    // inet_pton(AF_INET, "8.8.8.8", &dns.ip.u_addr.ip4);
    // dns.ip.type = ESP_IPADDR_TYPE_V4;
    // esp_netif_set_dns_info(sta_netif, ESP_NETIF_DNS_MAIN, &dns);

    // // fill time array with formatted time strings
    char dataArray[10][80];
    fillTimeArray(dataArray);
    for (size_t i = 0; i < 10; i++)
    {
        strcat(dataArray[i], ",");
        char measurement_str[9];
        snprintf(measurement_str, 9, "%.4f", MEASUREMENTS[i]);
        strcat(dataArray[i], measurement_str);
        // ESP_LOGI(TAG, "Final Data %s", dataArray[i]);
    }

    send_tcp_data(dataArray);
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
    if (!calibrated)
    {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK)
        {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated)
    {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK)
        {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Calibration Success");
    }
    else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated)
    {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    }
    else
    {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return calibrated;
}
