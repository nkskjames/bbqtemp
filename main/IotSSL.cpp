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

int IotSSL::init() {
    int ret, flags;

    mbedtls_ssl_init(&ssl);
    mbedtls_x509_crt_init(&cacert);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    ESP_LOGI(TAG, "Seeding the random number generator");

    mbedtls_ssl_config_init(&conf);

    mbedtls_entropy_init(&entropy);
    if((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                    NULL, 0)) != 0)
    {
        ESP_LOGE(TAG, "mbedtls_ctr_drbg_seed returned %d", ret);
        return ret;
    }

    ESP_LOGI(TAG, "Loading the CA root certificate...");

    ret = mbedtls_x509_crt_parse(&cacert, server_root_cert_pem_start,
                                 server_root_cert_pem_end-server_root_cert_pem_start);

    if(ret < 0) {
        ESP_LOGE(TAG, "mbedtls_x509_crt_parse returned -0x%x\n\n", -ret);
        return ret;
    }

    ESP_LOGI(TAG, "Setting hostname for TLS session...");

     /* Hostname set here should match CN in server certificate */
    if((ret = mbedtls_ssl_set_hostname(&ssl, WEB_SERVER)) != 0) {
        ESP_LOGE(TAG, "mbedtls_ssl_set_hostname returned -0x%x", -ret);
        return ret;
    }

    ESP_LOGI(TAG, "Setting up the SSL/TLS structure...");

    if((ret = mbedtls_ssl_config_defaults(&conf,
                                          MBEDTLS_SSL_IS_CLIENT,
                                          MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT)) != 0)
    {
        ESP_LOGE(TAG, "mbedtls_ssl_config_defaults returned %d", ret);
        return ret;
    }

    /* MBEDTLS_SSL_VERIFY_OPTIONAL is bad for security, in this example it will print
       a warning if CA verification fails but it will continue to connect.
       You should consider using MBEDTLS_SSL_VERIFY_REQUIRED in your own code.
    */
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
    mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);



	return ret;
}

int IotSSL::send() {
	init();
	char request[512];

	char body[] = "{\"user_id\" : \"jack\", \"text\" : \"Ahoy 2\"}";
    int ret, len, flags;



    if ((ret = mbedtls_ssl_setup(&ssl, &conf)) != 0)
    {
        ESP_LOGE(TAG, "mbedtls_ssl_setup returned -0x%x\n\n", -ret);
           mbedtls_strerror(ret, buf, 100);
            ESP_LOGE(TAG, "Last error was: -0x%x - %s", -ret, buf);
        return ret;
    }

    mbedtls_net_init(&server_fd);
    ESP_LOGI(TAG, "Connecting to %s:%s...", WEB_SERVER, WEB_PORT);

    if ((ret = mbedtls_net_connect(&server_fd, WEB_SERVER,
                                  WEB_PORT, MBEDTLS_NET_PROTO_TCP)) != 0)
    {
         ESP_LOGE(TAG, "mbedtls_net_connect returned -%x", -ret);
         return ret;
    }

    ESP_LOGI(TAG, "Connected.");
    mbedtls_ssl_set_bio(&ssl, &server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);
    ESP_LOGI(TAG, "Performing the SSL/TLS handshake...");
    while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            ESP_LOGE(TAG, "mbedtls_ssl_handshake returned -0x%x", -ret);
            return ret;
        }
    }
    ESP_LOGI(TAG, "Verifying peer X.509 certificate...");
    if ((flags = mbedtls_ssl_get_verify_result(&ssl)) != 0)
    {       /* In real life, we probably want to close connection if ret != 0 */
        ESP_LOGW(TAG, "Failed to verify peer certificate!");
        bzero(buf, sizeof(buf));
        mbedtls_x509_crt_verify_info(buf, sizeof(buf), "  ! ", flags);
        ESP_LOGW(TAG, "verification info: %s", buf);
    } else {
        ESP_LOGI(TAG, "Certificate verified.");
    }



    ESP_LOGI(TAG, "Writing HTTP request...");
	snprintf(request, 512, "POST /message_list.json HTTP/1.1\n"
			"Host: bbqtest-c47a8.firebaseio.com\n"
			"User-Agent: BBQTemp\n"
			"Accept: */*\n"
			"Connection: keep-alive\n"
			"Content-Type: application/x-www-form-urlencoded\n"
			"Content-length: %d\n\n"
			"%s",
			strlen(body), body);


        while((ret = mbedtls_ssl_write(&ssl, (const unsigned char *)request, strlen(request))) <= 0)
        {
            if(ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                ESP_LOGE(TAG, "mbedtls_ssl_write returned -0x%x", -ret);
                return ret;
            }
        }

        len = ret;
        ESP_LOGI(TAG, "%d bytes written", len);
        ESP_LOGI(TAG, "Reading HTTP response...");
		fcntl( server_fd.fd, F_SETFL, fcntl( server_fd.fd, F_GETFL, 0 ) | O_NONBLOCK );
        do
        {
            len = sizeof(buf) - 1;
            bzero(buf, sizeof(buf));
            ret = mbedtls_ssl_read(&ssl, (unsigned char *)buf, len);
			ESP_LOGI(TAG,"Statis: %d,%d",len,ret);
            if(ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
				ESP_LOGI(TAG,"WANT_READ");
                continue;
			}
            if(ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
				ESP_LOGI(TAG,"CLOSE");
                ret = 0;
                break;
            }
            if(ret < 0)
            {
                ESP_LOGE(TAG, "mbedtls_ssl_read returned -0x%x", -ret);
                break;
            }
            if(ret == 0)
            {
                ESP_LOGI(TAG, "connection closed");
                break;
            }
            len = ret;
            ESP_LOGI(TAG, "%d bytes read", len);
            /* Print response directly to stdout as it is read */
            for(int i = 0; i < len; i++) {
                putchar(buf[i]);
            }
        } while(1);

mbedtls_net_free( &server_fd );
mbedtls_ssl_free( &ssl );
mbedtls_ssl_config_free( &conf );
mbedtls_ctr_drbg_free( &ctr_drbg );
mbedtls_entropy_free( &entropy );

		return 0;
}

void IotSSL::close() {
//	mbedtls_ssl_close_notify(&ssl);
 //   mbedtls_ssl_session_reset(&ssl);
  //  mbedtls_net_free(&server_fd);
}
