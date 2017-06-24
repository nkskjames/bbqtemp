#include <string.h>
#include <stdlib.h>
#include "sys/param.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include <timer_platform.h>

#include "mbedtls/platform.h"
#include "mbedtls/net.h"
#include "mbedtls/esp_debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/certs.h"


#include "IotSSL.hpp"


extern const uint8_t server_root_cert_pem_start[] asm("_binary_server_root_cert_pem_start");
extern const uint8_t server_root_cert_pem_end[]   asm("_binary_server_root_cert_pem_end");
extern const uint8_t certificate_pem_crt_start[] asm("_binary_certificate_pem_crt_start");
extern const uint8_t certificate_pem_crt_end[] asm("_binary_certificate_pem_crt_end");
extern const uint8_t private_pem_key_start[] asm("_binary_private_pem_key_start");
extern const uint8_t private_pem_key_end[] asm("_binary_private_pem_key_end");
extern const uint8_t certificate_and_ca_pem_crt_start[] asm("_binary_certificate_and_ca_pem_crt_start");
extern const uint8_t certificate_and_ca_pem_crt_end[] asm("_binary_certificate_and_ca_pem_crt_end");

#define SSL_CONNECTION_ERROR -10
#define SUCCESS 0

int IotSSL::connect() {
	int ret;

	mbedtls_net_init(&server_fd);
	mbedtls_ssl_init(&ssl);
	mbedtls_ssl_config_init(&conf);
	mbedtls_ctr_drbg_init(&ctr_drbg);
	mbedtls_x509_crt_init(&cacert);
	mbedtls_x509_crt_init(&clicert);
	mbedtls_pk_init(&pkey);

    mbedtls_entropy_init(&entropy);
    if((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                    NULL, 0)) != 0)
    {
        ESP_LOGE(TAG, "mbedtls_ctr_drbg_seed returned %d", ret);
        return ret;
    }


	ESP_LOGI(TAG,"  . Loading the CA root certificate ...");
	ret = mbedtls_x509_crt_parse(&cacert, server_root_cert_pem_start,
                                 server_root_cert_pem_end-server_root_cert_pem_start);
	if(ret < 0) {
		ESP_LOGI(TAG," failed\n  !  mbedtls_x509_crt_parse returned -0x%x while parsing root cert\n\n", -ret);
		return ret;
	}

	ESP_LOGI(TAG,"  . Loading the client cert. and key...");
	ret = mbedtls_x509_crt_parse(&clicert, certificate_pem_crt_start,certificate_pem_crt_end-certificate_pem_crt_start);
	if(ret != 0) {
		ESP_LOGI(TAG," failed\n  !  mbedtls_x509_crt_parse returned -0x%x while parsing device cert\n\n", -ret);
		return ret;
	}

	ret = mbedtls_pk_parse_key(&pkey, private_pem_key_start, private_pem_key_end-private_pem_key_start, (unsigned const char*)"", 0);
	if(ret != 0) {
		ESP_LOGI(TAG," failed\n  !  mbedtls_pk_parse_key returned -0x%x while parsing private key\n\n", -ret);
		return ret;
	}
	ESP_LOGI(TAG,"Connecting: %s",server);
	if((ret = mbedtls_net_connect(&server_fd, server,
								  port, MBEDTLS_NET_PROTO_TCP)) != 0) {
		ESP_LOGI(TAG," failed\n  ! mbedtls_net_connect returned -0x%x\n\n", -ret);
		return ret;
	}

	ret = mbedtls_net_set_block(&server_fd);
	if(ret != 0) {
		ESP_LOGI(TAG," failed\n  ! net_set_(non)block() returned -0x%x\n\n", -ret);
		return SSL_CONNECTION_ERROR;
	}

	ESP_LOGI(TAG,"  . Setting up the SSL/TLS structure...");
	if((ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
										  MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
		ESP_LOGI(TAG," failed\n  ! mbedtls_ssl_config_defaults returned -0x%x\n\n", -ret);
		return SSL_CONNECTION_ERROR;
	}

	//mbedtls_ssl_conf_verify(&conf, _iot_tls_verify_cert, NULL);
	//if(pNetwork->tlsConnectParams.ServerVerificationFlag == true) {
	//	mbedtls_ssl_conf_authmode(&(tlsDataParams->conf), MBEDTLS_SSL_VERIFY_REQUIRED);
	//} else {
		mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
	//}
	mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);

	mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
	if((ret = mbedtls_ssl_conf_own_cert(&conf, &clicert, &pkey)) !=
	   0) {
		ESP_LOGE(TAG," failed\n  ! mbedtls_ssl_conf_own_cert returned %d\n\n", ret);
		return SSL_CONNECTION_ERROR;
	}

	mbedtls_ssl_conf_read_timeout(&conf, 3000);

	if((ret = mbedtls_ssl_setup(&ssl, &conf)) != 0) {
		ESP_LOGE(TAG," failed\n  ! mbedtls_ssl_setup returned -0x%x\n\n", -ret);
		return SSL_CONNECTION_ERROR;
	}
	if((ret = mbedtls_ssl_set_hostname(&ssl, server)) != 0) {
		ESP_LOGE(TAG," failed\n  ! mbedtls_ssl_set_hostname returned %d\n\n", ret);
		return SSL_CONNECTION_ERROR;
	}
	mbedtls_ssl_set_bio(&ssl, &server_fd, mbedtls_net_send, NULL,
						mbedtls_net_recv_timeout);
	ESP_LOGI(TAG," ok\n");

	ESP_LOGI(TAG,"  . Performing the SSL/TLS handshake...");
	while((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
		if(ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
			ESP_LOGE(TAG," failed\n  ! mbedtls_ssl_handshake returned -0x%x\n", -ret);
			if(ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
				ESP_LOGE(TAG,"    Unable to verify the server's certificate. "
							  "Either it is invalid,\n"
							  "    or you didn't set ca_file or ca_path "
							  "to an appropriate value.\n"
							  "    Alternatively, you may want to use "
							  "auth_mode=optional for testing purposes.\n");
			}
			return SSL_CONNECTION_ERROR;
		}
	}

	ESP_LOGI(TAG," ok\n    [ Protocol is %s ]\n    [ Ciphersuite is %s ]\n", mbedtls_ssl_get_version(&ssl),
		  mbedtls_ssl_get_ciphersuite(&ssl));
	if((ret = mbedtls_ssl_get_record_expansion(&ssl)) >= 0) {
		ESP_LOGI(TAG,"    [ Record expansion is %d ]\n", ret);
	} else {
		ESP_LOGI(TAG,"    [ Record expansion is unknown (compression) ]\n");
	}

	ESP_LOGI(TAG,"  . Verifying peer X.509 certificate...");
/*
	if(pNetwork->tlsConnectParams.ServerVerificationFlag == true) {
		if((flags = mbedtls_ssl_get_verify_result(&ssl)) != 0) {
			ESP_LOGE(TAG," failed\n");
			mbedtls_x509_crt_verify_info(vrfy_buf, sizeof(vrfy_buf), "  ! ", flags);
			ESP_LOGE(TAG,"%s\n", vrfy_buf);
			ret = SSL_CONNECTION_ERROR;
		} else {
			ESP_LOGI(TAG," ok\n");
			ret = SUCCESS;
		}
	} else {
*/
		ESP_LOGI(TAG," Server Verification skipped\n");
		ret = SUCCESS;
//	}

	mbedtls_ssl_conf_read_timeout(&conf, 1000);

	return ret;

}


int IotSSL::stop() {
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    if (&cacert != NULL) {
        mbedtls_x509_crt_free(&cacert);
    }
    if (&clicert != NULL) {
        mbedtls_x509_crt_free(&clicert);
    }
    if (&pkey != NULL) {
        mbedtls_pk_free(&pkey);
    }
	return 0;
}

int IotSSL::write(unsigned char *data, uint16_t len) {
    int ret = -1;
    if (error) {
        ESP_LOGE(TAG, "In error state, write not allowed");
    }
    while ((ret = mbedtls_ssl_write(&ssl, data, len)) <= 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            ESP_LOGE(TAG, "Write Error: %d",ret);
            error = ret;
            return ret;
        }
    }
    if (ret < 0) {
        ESP_LOGE(TAG, "Write Error: %d",ret);
        error = ret;
    } else {    
        ESP_LOGI(TAG, "Bytes written: %d",ret);
    }
    len = ret;
    return ret;
}

int IotSSL::read(unsigned char *data, int len) {
	size_t rxLen = 0;
	int ret = 0;
	Timer timer;
	init_timer(&timer);
	countdown_ms(&timer, 500);

    uint32_t read_timeout;
    read_timeout = conf.read_timeout;

	while (len > 0) {
		mbedtls_ssl_conf_read_timeout(&conf, MAX(1, MIN(read_timeout, left_ms(&timer))));
		ret = mbedtls_ssl_read(&ssl, data, len);
		mbedtls_ssl_conf_read_timeout(&conf, read_timeout);
		if (ret > 0) {
			rxLen += ret;
			data += ret;
			len -= ret;
		} else if (ret == 0 || (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE && ret != MBEDTLS_ERR_SSL_TIMEOUT)) {
			error = 1;
			return -10; //NETWORK_SSL_READ_ERROR;
		}
		// TODO: Evaluate timeout after the read to make sure read is done at least once
		if (has_timer_expired(&timer)) {
			break;
		}
	}
	ESP_LOGI(TAG,"Read end: %d,%d,%d,%d",len,rxLen,ret,strlen((const char*)data));
	if (rxLen == 0) {
		error = 2;
		return -10;
	}
	if (len >= 0) {
		return rxLen;
	}

	if (rxLen == 0) {
			error = 2;
		return -10; //nothing to read
	} else {
			error = 3;
		return -20; //timeout
	}
}

int IotSSL::sendRetry(unsigned char *writebuf, char *jsonbuf, int read_size, int retries) {
	int cnt = 0;
	error = 0;
	while(cnt<retries) {
		send(writebuf,jsonbuf,read_size);
		if (error > 0) {
			ESP_LOGI(TAG,"Send error, retrying: %d",cnt);
			this->stop();
			this->connect();
		} else {
			break;
		}
		cnt++;
	}
	return 0;
}

int IotSSL::send(unsigned char *writebuf, char *jsonbuf, int read_size) {
	unsigned char readbuf[4096];
	memset(readbuf, '\0', sizeof(char) * read_size);
	memset(jsonbuf, '\0', sizeof(char) * read_size);
    int rc = write(writebuf, strlen((const char*)writebuf));
	readptr=0;
	rc = read(readbuf,4096);
	
	ESP_LOGI(TAG,"Done Reading: %s",readbuf);
    if (error) {
        return -1;
    }
	//Get body of response
	int start = 0;
	int len = strlen((const char*)readbuf);
	for (int i=3;i<len;i++) {
		if (readbuf[i]=='\n' && readbuf[i-1]=='\r' && readbuf[i-2]=='\n' && readbuf[i-3]=='\r') {
			start = i+1;
			break;
		}
	}
	if (start < len) {
		strncpy(jsonbuf,(const char*)&readbuf[start],len-start);
	}
    return rc;
}
