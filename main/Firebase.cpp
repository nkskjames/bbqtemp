#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include <nvs.h>
#include "cJSON.h"


#include "IotSSL.hpp"
#include "Firebase.hpp"


extern const char firebase_apikey_start[] asm("_binary_firebase_apikey_start");
extern const char firebase_apikey_end[] asm("_binary_firebase_apikey_end");

void Firebase::connect(const char* server, const char* port) {
		strncpy(firebase_apikey,firebase_apikey_start,39);
		firebase_apikey[39] = '\0';
//		post.connect(server,port);
}

void Firebase::buildSigninRequest(char* email,char* password) {
	
	char body[512];
    snprintf(body, 512, "{\"email\":\"%s\",\"password\":\"%s\",\"returnSecureToken\":true}",email,password);	
    snprintf(request, Firebase::REQUEST_SIZE, "POST /identitytoolkit/v3/relyingparty/verifyPassword?key=%s HTTP/1.1\n"
			"Host: www.googleapis.com\n"
			"User-Agent: BBQTemp\n"
			"Accept: */*\n"
			"Connection: keep-alive\n"
			"Content-Type: application/json\n"
			"Content-length: %d\n\n"
			"%s",
			firebase_apikey, strlen(body), body);

}

void Firebase::buildVerifyRequest() {
	
	char body[1024];
    snprintf(body, 1024, "{\"idToken\":\"%s\"}",token_info.token);	
    snprintf(request, Firebase::REQUEST_SIZE, "POST /identitytoolkit/v3/relyingparty/getAccountInfo?key=%s HTTP/1.1\n"
			"Host: www.googleapis.com\n"
			"User-Agent: BBQTemp\n"
			"Accept: */*\n"
			"Connection: keep-alive\n"
			"Content-Type: application/json\n"
			"Content-length: %d\n\n"
			"%s",
			firebase_apikey, strlen(body), body);
	ESP_LOGI(Firebase::TAG,"Request: %s",request);

}
void Firebase::buildRefreshRequest() {
	
	char body[1024];
    snprintf(body, 1024, "grant_type=refresh_token&refresh_token=%s",token_info.refresh_token);	
    snprintf(request, Firebase::REQUEST_SIZE, "POST /v1/token?key=%s HTTP/1.1\n"
			"Host: securetoken.googleapis.com\n"
			"User-Agent: BBQTemp\n"
			"Accept: */*\n"
			"Connection: keep-alive\n"
			"Content-Type: application/x-www-form-urlencoded\n"
			"Content-length: %d\n\n"
			"%s",
			firebase_apikey, strlen(body), body);
	ESP_LOGI(Firebase::TAG,"Request: %s",request);

}

void Firebase::buildDatabaseQuery() {
	memset(request, '\0', sizeof(char) * Firebase::REQUEST_SIZE);
    snprintf(request, Firebase::REQUEST_SIZE, "GET /users/4DLD1AuL3pdhePLmPawiwyszxwl2/BBQTemp_240AC405D0D4/.json?auth=%s HTTP/1.1\n"
			"Host: bbqtest-c47a8.firebaseio.com\n"
			"User-Agent: BBQTemp/1.0\n"
			"Accept: */*\n\n",token_info.token);
	ESP_LOGI(Firebase::TAG,"Request: %s",request);

}

void Firebase::buildNotification(char* body) {
    snprintf(request, Firebase::REQUEST_SIZE, "POST /fcm/send HTTP/1.1\n"
			"Host: fcm.googleapis.com\n"
			"User-Agent: BBQTemp\n"
			"Accept: */*\n"
			"Connection: keep-alive\n"
			"Content-Type: application/json\n"
			"Authorization: key=%s\n"
			"Content-length: %d\n\n"
			"%s",
			firebase_apikey, strlen(body), body);
		post.send((unsigned char*)request, response, Firebase::RESPONSE_SIZE);
		ESP_LOGI(Firebase::TAG,"Response: %s",(unsigned char*)response);

}

cJSON* Firebase::query(char* path) {
	int rc = getTokenInfo(&token_info);
	if (rc == 0) {
		this->buildDatabaseQuery();
		post.send((unsigned char*)request, response, Firebase::RESPONSE_SIZE);
		ESP_LOGI(Firebase::TAG,"Response: %s",(unsigned char*)response);
		cJSON *root = cJSON_Parse((char*)response);
		return root;
	}
	return NULL;
}


void Firebase::signIn(char* email, char* password) {
	int rc = getTokenInfo(&token_info);
	bool signedIn = false;
	if (rc==0) {
		ESP_LOGI(Firebase::TAG,"Signing with token: %s",token_info.token);
	    //this->buildVerifyRequest();
	    //post.send((unsigned char*)request, response, Firebase::RESPONSE_SIZE);
		//ESP_LOGI(Firebase::TAG,"Response: %s",(unsigned char*)response);
		//cJSON *root = cJSON_Parse((char*)response);
		//cJSON *kind = cJSON_GetObjectItem(root,"kind");	
		//if (kind == NULL) {
			ESP_LOGI(Firebase::TAG,"Invalid Token, refreshing...");
			ESP_LOGI(Firebase::TAG,"Token: %s",token_info.refresh_token);
			this->buildRefreshRequest();
		    post.send((unsigned char*)request, response, Firebase::RESPONSE_SIZE);
			cJSON *root = cJSON_Parse((char*)response);
			cJSON *token = cJSON_GetObjectItem(root,"id_token");
			cJSON *refreshtoken = cJSON_GetObjectItem(root,"refresh_token");
			if (token == NULL) {
				ESP_LOGI(Firebase::TAG,"Invalid Refresh Token.  Signing in...");
			} else {
				strcpy(token_info.token,token->valuestring);
				strcpy(token_info.refresh_token,refreshtoken->valuestring);		
				saveTokenInfo(&token_info);		
				signedIn = true;
			}
		//} else {
		///	signedIn = true;
		//}
	}
	if (!signedIn) {
		//not previously signed in
		ESP_LOGI(Firebase::TAG,"Signing in with email and password");
	    this->buildSigninRequest(email, password);
	    post.send((unsigned char*)request, response, Firebase::RESPONSE_SIZE);
		ESP_LOGI(Firebase::TAG,"Response: %s",(unsigned char*)response);
		cJSON *root = cJSON_Parse((char*)response);
		cJSON *localid = cJSON_GetObjectItem(root,"localId");
		cJSON *token = cJSON_GetObjectItem(root,"idToken");
		cJSON *refreshtoken = cJSON_GetObjectItem(root,"refreshToken");
		strcpy(token_info.token,token->valuestring);
		strcpy(token_info.refresh_token,refreshtoken->valuestring);
		saveTokenInfo(&token_info);		
	} else {
		ESP_LOGI(Firebase::TAG,"Signed in");
	}
}


int Firebase::getTokenInfo(token_info_t *pTokenInfo) {
	nvs_handle handle;
	size_t size;
	esp_err_t err;
	uint32_t version;
	err = nvs_open(Firebase::FIREBASE_NAMESPACE, NVS_READWRITE, &handle);
	if (err != 0) {
		ESP_LOGE(Firebase::TAG, "nvs_open: %x", err);
		return -1;
	}

	// Get the version that the data was saved against.
	err = nvs_get_u32(handle, Firebase::KEY_FB_VERSION, &version);
	if (err != ESP_OK) {
		ESP_LOGD(Firebase::TAG, "No version record found (%d).", err);
		nvs_close(handle);
		return -1;
	}

	// Check the versions match
	if ((version & 0xff00) != (Firebase::g_version & 0xff00)) {
		ESP_LOGD(Firebase::TAG, "Incompatible versions ... current is %x, found is %x", version, g_version);
		nvs_close(handle);
		return -1;
	}

	size = sizeof(token_info_t);
	err = nvs_get_blob(handle, Firebase::KEY_TOKEN_INFO, pTokenInfo, &size);
	if (err != ESP_OK) {
		ESP_LOGD(Firebase::TAG, "No token record found (%d).", err);
		nvs_close(handle);
		return -1;
	}
	if (err != ESP_OK) {
		ESP_LOGE(Firebase::TAG, "nvs_open: %x", err);
		nvs_close(handle);
		return -1;
	}

	// Cleanup
	nvs_close(handle);

	return 0;
}


void Firebase::saveTokenInfo(token_info_t *pTokenInfo) {
	nvs_handle handle;
	ESP_ERROR_CHECK(nvs_open(Firebase::FIREBASE_NAMESPACE, NVS_READWRITE, &handle));
	ESP_ERROR_CHECK(nvs_set_blob(handle, KEY_TOKEN_INFO, pTokenInfo,
			sizeof(token_info_t)));
	ESP_ERROR_CHECK(nvs_set_u32(handle, Firebase::KEY_FB_VERSION, g_version));
	ESP_ERROR_CHECK(nvs_commit(handle));
	nvs_close(handle);
}

