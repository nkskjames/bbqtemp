#ifndef THINGDATA_H_
#define THINGDATA_H_


#define THING_DATA_NUM 4

typedef struct {
    double t[4];
	double tu[4];
	double tl[4];
	char td[4][128];
	char token[164];
    char thingName[64];
	char prettyName[64];
	char username[64];
} data_t;

typedef struct {
    double t[4];
	double sent[4];
} data_last_t;

#ifdef __cplusplus
    extern "C" data_t thingData;
    extern "C" data_last_t thingDataLast;
#else
	data_t thingData;
	data_last_t thingDataLast;
#endif

#endif
