#ifndef NVDATA_H_
#define NVDATA_H_

#include "esp_err.h"
#include "nvs_flash.h"
#include <nvs.h>

using namespace std;

class NvData {
	//char nv_namespace[64];
	//char key[64];
	const char* nv_namespace;
	const char* key;
	uint32_t version;
	static constexpr const char* KEY_VERSION = "version";
	static constexpr const char* TAG = "NvData";
	void* pData;
	size_t data_size;
    
public:
	NvData(const char* nv_namespace, const char* key, uint32_t version, void* pData, size_t data_size) {
		//strcpy(this->nv_namespace, nv_namespace);
		//strcpy(this->key, key);
		this->nv_namespace = nv_namespace;
		this->key = key;
		this->version = version;
		this->pData = pData;
		this->data_size = data_size;
	}

	int load() {
		nvs_handle handle;
		esp_err_t err;
		uint32_t saved_version;
		err = nvs_open(nv_namespace, NVS_READWRITE, &handle);
		if (err != 0) {
			ESP_LOGE(NvData::TAG, "nvs_open: %x", err);
			return -1;
		}

		// Get the version that the data was saved against.
		err = nvs_get_u32(handle, NvData::KEY_VERSION, &saved_version);
		if (err != ESP_OK) {
			ESP_LOGD(NvData::TAG, "No version record found (%d).", err);
			nvs_close(handle);
			return -1;
		}

		// Check the versions match
		if ((saved_version & 0xff00) != (NvData::version & 0xff00)) {
			ESP_LOGD(NvData::TAG, "Incompatible versions ... current is %x, found is %x", saved_version, NvData::version);
			nvs_close(handle);
			return -1;
		}

		err = nvs_get_blob(handle, key, pData, &data_size);
		if (err != ESP_OK) {
			ESP_LOGD(NvData::TAG, "No token record found (%d).", err);
			nvs_close(handle);
			return -1;
		}
		if (err != ESP_OK) {
			ESP_LOGE(NvData::TAG, "nvs_open: %x", err);
			nvs_close(handle);
			return -1;
		}

		// Cleanup
		nvs_close(handle);

		return 0;
	}
	void save() {
		nvs_handle handle;
		ESP_ERROR_CHECK(nvs_open(nv_namespace, NVS_READWRITE, &handle));
		ESP_ERROR_CHECK(nvs_set_blob(handle, key, pData, data_size));
		ESP_ERROR_CHECK(nvs_set_u32(handle, NvData::KEY_VERSION, version));
		ESP_ERROR_CHECK(nvs_commit(handle));
		nvs_close(handle);
	}
};

#endif

