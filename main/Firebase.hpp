#ifndef FIREBASE_H_
#define FIREBASE_H_

#include "IotSSL.hpp"

using namespace std;
/*

*/


#define EMAIL_SIZE (64) // Maximum username size
#define PASSWORD_SIZE (64) // Maximum username size
#define FB_TOKEN_SIZE (1024) // Maximum token size
#define REFRESH_TOKEN_SIZE (1024) // Maximum token size

#define SIZEOF(x)  (sizeof(x) / sizeof((x)[0]))



typedef struct {
	char token[FB_TOKEN_SIZE];
	char refresh_token[REFRESH_TOKEN_SIZE];
} token_info_t;

class Firebase {
	IotSSL post;

	static const int RESPONSE_SIZE = 4096;
	static const int REQUEST_SIZE = 2048;
	static constexpr const char* FIREBASE_NAMESPACE = "firebase";
	static constexpr const char* KEY_TOKEN_INFO = "token_info";
	static constexpr const char* KEY_FB_VERSION = "fb_version";
	char firebase_apikey[40];
    char response[Firebase::RESPONSE_SIZE];
    char request[Firebase::REQUEST_SIZE];
	static constexpr const char* TAG = "Firebase";
	static constexpr const uint32_t g_version=0x0300;
	token_info_t token_info;	

	public:
	void connect(const char* server, const char* port);
	void buildSigninRequest(char* email,char* password);
	void buildVerifyRequest();
	void buildRefreshRequest();
	void buildDatabaseQuery();
	void buildNotification(char* body);
	cJSON* query(char* path);
	void signIn(char* email, char* password);
	static int getTokenInfo(token_info_t *pTokenInfo);
	static void saveTokenInfo(token_info_t *pTokenInfo);

};

#endif

