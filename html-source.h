#pragma once

#include <curl/curl.h>
#include <obs.h>
#include <util/dstr.h>
#include <util/threading.h>

#define HTML_TEXT 0
#define HTML_FILE 1
#define HTML_WEB 2

#ifdef __cplusplus
extern "C" {
#endif

struct html_source_data {
	obs_source_t *source;
	time_t html_time;
	pthread_t thread;
	bool stop;
	uint32_t sleep;

	gs_texture_t *texture;
	uint32_t cx;
	uint32_t cy;

	CURL *curl;
	struct curl_slist *curl_header;
	char curl_error[CURL_ERROR_SIZE];
	DARRAY(uint8_t) web_data;
};

void render_qt(struct html_source_data *hs, obs_data_t *settings);

#ifdef __cplusplus
};
#endif
