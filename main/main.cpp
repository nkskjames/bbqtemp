
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
#include "aws_iot_log.h"
#include <nvs.h>
#include <nvs_flash.h>
#include "driver/gpio.h"
#include "driver/adc.h"
#include "lwip/err.h"
#include "apps/sntp/sntp.h"
#include "cJSON.h"

#include "soc/rtc_cntl_reg.h"
#include "soc/sens_reg.h"
#include "driver/rtc_io.h"
#include "esp32/ulp.h"
#include "ulp_main.h"

#include "thingData.h"
#include "IotSSL.hpp"

extern "C" {
#include "bootwifi.h"
}

static const adc1_channel_t ADC_TEMP1 = ADC1_CHANNEL_7;
static const adc1_channel_t ADC_TEMP2 = ADC1_CHANNEL_6;
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

extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");
extern const uint8_t ulp_main_bin_end[]   asm("_binary_ulp_main_bin_end");

/* This function is called once after power-on reset, to load ULP program into
 * RTC memory and configure the ADC.
 */
static void init_ulp_program();

/* This function is called every time before going into deep sleep.
 * It starts the ULP program and resets measurement counter.
 */
static void start_ulp_program();




#ifdef __cplusplus
extern "C" {
#endif
extern data_t thingData;
#ifdef __cplusplus
}
#endif

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


void sample_task(void *param) {
	gpio_set_level(GPIO_NUM_5, 1);
	char thingName[128];
	IotSSL post;
	char body[512];
	char request[1024];
	char response[1024];

	getThingName(thingName);	
    connection_info_t connectionInfo;
    getConnectionInfo(&connectionInfo);
	if (connectionInfo.setup_done == 0) {
		post.init("bbqtest-c47a8.firebaseio.com","443");
		int rc = post.connect();
		int cnt = 0;
		while(cnt < 20) {
	        vTaskDelay(3000 / portTICK_PERIOD_MS);
			snprintf(request, 1024, "GET /setup/%s.json HTTP/1.1\n"
				"Host: bbqtest-c47a8.firebaseio.com\n"
				"User-Agent: BBQTemp\n"
				"Accept: */*\n\n",connectionInfo.userid);
			post.sendRetry((unsigned char*)request, response, 1024, 3);
			ESP_LOGI(TAG,"Response: %s",response);
			if (strcmp(response,"\"1\"") == 0) {
				ESP_LOGI(TAG,"Go flag set");
				break;
			}
			cnt++;
		}
		if (cnt == 20) {
			ESP_LOGI(TAG,"Timeout waiting for user device to set go flag");
		    vTaskDelete(NULL);
			return;
		}
		connectionInfo.setup_done = 1;
		saveConnectionInfo(&connectionInfo);
	} else {
		post.init("us-central1-bbqtest-c47a8.cloudfunctions.net","443");
		post.connect();
		double t0 = getTemperature(ADC_TEMP1);
		double t1 = 110.0;
		double t2 = 1000.0;
		
		snprintf(body, 512, "{\"user_id\":\"%s\", \"thing_id\": \"%s\", \"t\": [%0.0f,%0.0f,%0.0f] }",connectionInfo.userid, thingName, t0, t1, t2);
		snprintf(request, 1024, "POST /addDatapoint HTTP/1.1\n"
				"Host: us-central1-bbqtest-c47a8.cloudfunctions.net\n"
				"User-Agent: BBQTemp\n"
				"Accept: */*\n"
				"Connection: keep-alive\n"
				"Content-Type: application/json\n"
				"Content-length: %d\n\n"
				"%s",
				strlen(body), body);
		post.sendRetry((unsigned char*)request, response, 1024, 3);
	}
	post.stop();
	ESP_LOGI(TAG,"Sample done");	
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
	nvs_open("bootwifi", NVS_READWRITE, &handle);
	nvs_erase_all(handle);
	nvs_close(handle);
}

static void set_thresholds() {
    int adc_value0 = adc1_get_voltage(ADC_TEMP1);
    int adc_value1 = 11;
	ESP_LOGI(TAG,"ADC Values = %d,%d",adc_value0,adc_value1);
    adc1_ulp_enable();
    ulp_low_thr0 = adc_value0-10;
    ulp_high_thr0 = adc_value0+10;
    ulp_low_thr1 = 0;
    ulp_high_thr1 = 4096;

}

static void init_ulp_program()
{
    esp_err_t err = ulp_load_binary(0, ulp_main_bin_start,
            (ulp_main_bin_end - ulp_main_bin_start) / sizeof(uint32_t));
    ESP_ERROR_CHECK(err);

    /* Configure ADC channel */
    /* Note: when changing channel here, also change 'adc_channel' constant
       in adc.S */
    adc1_config_channel_atten(ADC1_CHANNEL_7, ADC_ATTEN_11db);
    adc1_config_width(ADC_WIDTH_12Bit);
	set_thresholds();


    /* Set ULP wake up period to 100ms */
    ulp_set_wakeup_period(0, 100000);
}

static void start_ulp_program()
{
    /* Reset sample counter */
    ulp_sample_counter = 0;

    /* Start the program */
    esp_err_t err = ulp_run((&ulp_entry0 - RTC_SLOW_MEM) / sizeof(uint32_t));
    ESP_ERROR_CHECK(err);
}


extern "C" void app_main(void)
{
    getThingName(thingData.thingName);
    
    adc1_config_width(ADC_WIDTH_12Bit);
    adc1_config_channel_atten(ADC_TEMP1,ADC_ATTEN_11db);

	gpio_pad_select_gpio(CONFIG_RESET_GPIO);
	gpio_set_direction(CONFIG_RESET_GPIO, GPIO_MODE_INPUT);

    gpio_set_direction(GPIO_NUM_5, GPIO_MODE_OUTPUT);
	gpio_set_level(GPIO_NUM_5, 0);

    // blink LED
/*
    int level = 0;
    for (int c=0;c<10;c++) {
        gpio_set_level(GPIO_NUM_5, level);
        level = !level;
        vTaskDelay(300 / portTICK_PERIOD_MS);
    }
*/
    //check reset button and clear config if pushed
	nvs_flash_init();
    if (!gpio_get_level(CONFIG_RESET_GPIO)) {
        ESP_LOGW(TAG,"Button pressed, clearing config");
        clearConfig();
    }
    
    //Init Wifi; call callback when done
    //bootWiFi(wifi_setup_done);


	esp_deep_sleep_wakeup_cause_t cause = esp_deep_sleep_get_wakeup_cause();
    if (cause != ESP_DEEP_SLEEP_WAKEUP_ULP) {
        printf("Not ULP wakeup\n");
        init_ulp_program();
    } else {
        printf("Deep sleep wakeup\n");
        printf("ULP did %d measurements since last reset\n", ulp_sample_counter & UINT16_MAX);
        printf("Thresholds:  low=%d  high=%d\n", ulp_low_thr0, ulp_high_thr0);
        ulp_last_result0 &= UINT16_MAX;
        printf("Value=%d was %s threshold\n", ulp_last_result0,
                ulp_last_result0 < ulp_low_thr0 ? "below" : "above");

       printf("Thresholds:  low=%d  high=%d\n", ulp_low_thr1, ulp_high_thr1);
        ulp_last_result1 &= UINT16_MAX;
        printf("Value=%d was %s threshold\n", ulp_last_result1,
                ulp_last_result1 < ulp_low_thr1 ? "below" : "above");
		set_thresholds();
    }
    printf("Entering deep sleep\n\n");
    start_ulp_program();
    ESP_ERROR_CHECK( esp_deep_sleep_enable_ulp_wakeup() );
    esp_deep_sleep_start();


    //vTaskDelay(1000 / portTICK_PERIOD_MS);

    /*
    struct timeval now;
    gettimeofday(&now, NULL);
    int sleep_time_ms = (now.tv_sec - sleep_enter_time.tv_sec) * 1000 + (now.tv_usec - sleep_enter_time.tv_usec) / 1000;
    const int wakeup_time_sec = 20;
    printf("Enabling timer wakeup, %ds\n", wakeup_time_sec);
    esp_deep_sleep_enable_timer_wakeup(wakeup_time_sec * 1000000);
    */

}

