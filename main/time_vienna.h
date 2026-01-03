#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "esp_netif_sntp.h"
#include "lwip/ip_addr.h"
#include "esp_sntp.h"


const char* ntpServer = "pool.ntp.org";
const char* GROUP_ID = "92";


time_t initialize_sntp() {
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, (char*)ntpServer); // server index 0
    esp_sntp_init();

    // optionally wait for time to be set
    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    const int retry_count = 10;
    while (timeinfo.tm_year < (2024 - 1900) && ++retry < retry_count) {
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    return now;
}

// fill a 2d array with formatted time strings for the last 5 minutes at 30 second intervals
void fillTimeArray(char timeArray[10][80]) {
    time_t now = initialize_sntp();
    for (int i = 0; i < 10; i++) {
        time_t timestamp = now - (9 - i) * 30;
        struct tm *timeinfo = gmtime(&timestamp);
        strftime(timeArray[i], 80, "%Y-%m-%d %H:%M:%S+01:00", timeinfo);
        strcat(timeArray[i], ",");
        strcat(timeArray[i], GROUP_ID);
    }
}