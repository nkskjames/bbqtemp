
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>

#include "esp_deep_sleep.h"
#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include <nvs.h>
#include <nvs_flash.h>
#include "driver/gpio.h"
#include "driver/adc.h"
#include "lwip/err.h"
#include "apps/sntp/sntp.h"

extern "C" {
#include "bootwifi.h"
}
#include "IotDataMqtt.hpp"
#include "IotSSL.hpp"


static const adc1_channel_t ADC_TEMP1 = ADC1_CHANNEL_7;
static const char *TAG = "main";
static const int MAX_LENGTH_OF_UPDATE_JSON_BUFFER = 400;
static const int ADC_NUM_STEPS = 4096;
static const double ADC_STEP = 3.3/ADC_NUM_STEPS;
static const int Ro = 100000;
static const int Rt = 5950;
static const int B = 3950;
static const double To = 298.15;
static const double Ri = Ro * exp(-B/To);
static const gpio_num_t CONFIG_RESET_GPIO = GPIO_NUM_0;
static const float DELTA_TEMP = 2;

extern char* token[150];

static void initialize_sntp(void)
{
    char ntp_server[] = "pool.ntp.org";
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, ntp_server);
    sntp_init();
}

static void obtain_time(void)
{
    initialize_sntp();

    // wait for time to be set
    time_t now = 0;
    struct tm timeinfo;
    int retry = 0;
    const int retry_count = 10;
    timeinfo.tm_year = 0;
    while(timeinfo.tm_year < (2016 - 1900) && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }
}

void get_mac_address(char* macAddress) {
    uint8_t mac[6];
    esp_efuse_read_mac(mac);
    ESP_LOGI(TAG,"size: %zu",sizeof(macAddress));
    sprintf(macAddress,"%02X%02X%02X%02X%02X%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
}

float getTemperature(adc1_channel_t channel) {
    int adc_value = adc1_get_voltage(channel);
    double adc_voltage = adc_value * ADC_STEP;

    double R = (Rt * adc_voltage)/(3.3 - adc_voltage);
    float temperature = B/(log(R/Ri)) - 273.15;
	ESP_LOGI(TAG,"%d, %lf  R = %lf, Temp = %lf",adc_value,adc_voltage,R,temperature);
	if (adc_value < 100 || adc_value > ADC_NUM_STEPS-100 || temperature < 25) {
		ESP_LOGI(TAG,"Temperature out of range");
		temperature = 0;
	}
	return temperature;
}

void subscribe_task(void *param) {
    char macAddress[14];
    get_mac_address(macAddress);

    char thing_id[32];    

    int sample_num = 0;
	char fullName[256];
	sprintf(fullName,"BBQTemp_%s",macAddress);

    IotDataMqtt data;
    data.subscribe(fullName);
    vTaskDelete(NULL);
}

void sample_task(void *param) {
    adc1_config_width(ADC_WIDTH_12Bit);
    adc1_config_channel_atten(ADC_TEMP1,ADC_ATTEN_11db);
    connection_info_t connectionInfo;
    getConnectionInfo(&connectionInfo);
    ESP_LOGI(TAG,"Username: %s",connectionInfo.username);    
    char macAddress[14];
    get_mac_address(macAddress);

    char thing_id[32];    
    char JsonDocumentBuffer[MAX_LENGTH_OF_UPDATE_JSON_BUFFER];
    //size_t sizeOfJsonDocumentBuffer = sizeof(JsonDocumentBuffer) / sizeof(JsonDocumentBuffer[0]);
	//ESP_LOGI(TAG,"Buffer: %d",sizeOfJsonDocumentBuffer);

    int sample_num = 0;
	char fullName[256];
	sprintf(fullName,"BBQTemp_%s",macAddress);


	IotSSL post;

//	post.init();

    time_t now = 0;
    struct tm timeinfo;

	float last_temp[4] = {0,0,0,0};
	float temp[4] = {0,0,0,0};
	//getTemperature(ADC_TEMP1);
	temp[1] = 10;
	for (int i =0 ; i<10; i++) {
		post.send();
	    vTaskDelay(5000 / portTICK_PERIOD_MS);
	}
    for (sample_num = 0; sample_num<6000; sample_num++) {
	    ESP_LOGI(TAG,"Sample: %d",sample_num);    
		time(&now);
        localtime_r(&now, &timeinfo);
		
		temp[0] = getTemperature(ADC_TEMP1);

		ESP_LOGI(TAG,"Temp0: %0.1f",temp[0]);
		ESP_LOGI(TAG,"Temp1: %0.1f",temp[1]);
		bool update = false;
		for (int i=0;i<4;i++) {
			if (abs(last_temp[i]-temp[i]) > DELTA_TEMP) {
				update = true;
			}
		}

		if (update) {
			//sprintf(thing_id,"%s-%d/%d;%d:%d",macAddress,timeinfo.tm_mon,timeinfo.tm_mday,timeinfo.tm_hour,timeinfo.tm_min);
			sprintf(thing_id,"%s-%d",macAddress,sample_num);
			sprintf(JsonDocumentBuffer,
				"{\"state\": {\"reported\": {\"thingname\": \"%s\", \"username\":\"%s\",\"t\": [%0.0f,%0.0f,%0.0f]}}, \"clientToken\":\"%s\"}",
				fullName,connectionInfo.username,temp[0],temp[1],temp[2],thing_id);

			//TODO: add error checking
			//data.sendraw(JsonDocumentBuffer);
			for (int i=0;i<4;i++) {
				last_temp[i] = temp[i];
			}
		}
        
		//temp[1] = temp[1]+2;
	    vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
    post.close();
    vTaskDelete(NULL);
}

void delayed_reboot_task(void *param) {
	ESP_LOGI(TAG,"Delayed reboot");
    vTaskDelay(3000 / portTICK_PERIOD_MS);
	esp_restart();
    vTaskDelete(NULL);
}

void wifi_setup_done(int rc) {
    printf("Wifi setup done\n");

//  xTaskCreate(&https_get_task, "https_get_task", 8192, NULL, 5, NULL);

	if (rc == 2) {
    	xTaskCreate(&delayed_reboot_task, "delayed_reboot_task", 36*1024, NULL, 5, NULL);		
	} else {
	    //obtain_time();
		//xTaskCreate(&subscribe_task, "subscribe_task", 36*1024, NULL, 5, NULL);
    	xTaskCreate(&sample_task, "sample_task", 48*1024, NULL, 4, NULL);

	}

}

/**
 * Save our connection info for retrieval on a subsequent restart.
 */
static void clearConfig() {
	nvs_handle handle;
	
	//TODO: use constants
	nvs_open("mqtt", NVS_READWRITE, &handle);
	nvs_erase_all(handle);
	nvs_open("bootwifi", NVS_READWRITE, &handle);
	nvs_erase_all(handle);
	nvs_close(handle);
}



extern "C" void app_main(void)
{
    // Setup IO
	gpio_pad_select_gpio(CONFIG_RESET_GPIO);
	gpio_set_direction(CONFIG_RESET_GPIO, GPIO_MODE_INPUT);

    gpio_set_direction(GPIO_NUM_5, GPIO_MODE_OUTPUT);


    // blink LED
    int level = 0;
    for (int c=0;c<10;c++) {
        gpio_set_level(GPIO_NUM_5, level);
        level = !level;
        vTaskDelay(300 / portTICK_PERIOD_MS);
    }
    //check reset button and clear config if pushed
	nvs_flash_init();
    if (!gpio_get_level(CONFIG_RESET_GPIO)) {
        ESP_LOGW(TAG,"Button pressed, clearing config");
        clearConfig();
    }
    
    //Init Wifi; call callback when done
    bootWiFi(wifi_setup_done);

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    /*
    struct timeval now;
    gettimeofday(&now, NULL);
    int sleep_time_ms = (now.tv_sec - sleep_enter_time.tv_sec) * 1000 + (now.tv_usec - sleep_enter_time.tv_usec) / 1000;
    const int wakeup_time_sec = 20;
    printf("Enabling timer wakeup, %ds\n", wakeup_time_sec);
    esp_deep_sleep_enable_timer_wakeup(wakeup_time_sec * 1000000);
    */

}

