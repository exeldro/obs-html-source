#include "html-source.h"
#include "version.h"
#include <obs-module.h>
#include <util/platform.h>
#include <sys/stat.h>

#if defined(_WIN32) && LIBCURL_VERSION_NUM >= 0x072c00
#ifdef CURLSSLOPT_REVOKE_BEST_EFFORT
#define CURL_OBS_REVOKE_SETTING CURLSSLOPT_REVOKE_BEST_EFFORT
#else
#define CURL_OBS_REVOKE_SETTING CURLSSLOPT_NO_REVOKE
#endif
#define curl_obs_set_revoke_setting(handle) curl_easy_setopt(handle, CURLOPT_SSL_OPTIONS, CURL_OBS_REVOKE_SETTING)
#else
#define curl_obs_set_revoke_setting(handle)
#endif

static size_t http_write(void *ptr, size_t size, size_t nmemb, void *data)
{
	size_t total = size * nmemb;
	struct html_source_data *hs = (struct html_source_data *)data;

	if (total)
		da_push_back_array(hs->web_data, ptr, total);

	return total;
}

static bool html_source_file_changed(time_t *time, obs_data_t *settings)
{
	bool changed = false;
	const char *path = obs_data_get_string(settings, "html_path");
	if (path == NULL || path[0] == '\0')
		return changed;
	struct stat stats;
	if (os_stat(path, &stats) != 0)
		return changed;
	if (stats.st_mtime == *time)
		return changed;
	char *text = os_quick_read_utf8_file(path);
	if (!text)
		return changed;
	const char *old_text = obs_data_get_string(settings, "text");
	if (strcmp(text, old_text) != 0) {
		obs_data_set_string(settings, "text", text);
		changed = true;
		*time = stats.st_mtime;
	}
	bfree(text);
	return changed;
}

static bool html_source_web_changed(struct html_source_data *hs, obs_data_t *settings)
{
	bool changed = false;
	if (!hs->curl) {
		hs->curl = curl_easy_init();
		if (hs->curl) {
			curl_easy_setopt(hs->curl, CURLOPT_HTTPHEADER, hs->curl_header);
			curl_easy_setopt(hs->curl, CURLOPT_ERRORBUFFER, hs->curl_error);
			curl_easy_setopt(hs->curl, CURLOPT_WRITEFUNCTION, http_write);
			curl_easy_setopt(hs->curl, CURLOPT_WRITEDATA, hs);
			curl_easy_setopt(hs->curl, CURLOPT_FAILONERROR, 1L);
			curl_easy_setopt(hs->curl, CURLOPT_NOSIGNAL, 1L);
			curl_easy_setopt(hs->curl, CURLOPT_ACCEPT_ENCODING, "");
			curl_obs_set_revoke_setting(hs->curl);
		}
	}
	if (!hs->curl)
		return changed;

	da_resize(hs->web_data, 0);
	curl_easy_setopt(hs->curl, CURLOPT_URL, obs_data_get_string(settings, "html_url"));
	CURLcode code = curl_easy_perform(hs->curl);
	if (code != CURLE_OK)
		return changed;

	long response_code;
	if (curl_easy_getinfo(hs->curl, CURLINFO_RESPONSE_CODE, &response_code) != CURLE_OK)
		return changed;
	if (response_code >= 400)
		return changed;
	uint8_t null_terminator = 0;
	da_push_back(hs->web_data, &null_terminator);
	if (strcmp((const char *)hs->web_data.array, obs_data_get_string(settings, "text")) != 0) {
		obs_data_set_string(settings, "text", (const char *)hs->web_data.array);
		changed = true;
	}

	return changed;
}

static void *html_source_thread(void *data)
{
	struct html_source_data *hs = (struct html_source_data *)data;
	os_set_thread_name("html_source_thread");
	while (!hs->stop) {
		os_sleep_ms(hs->sleep);
		obs_data_t *settings = obs_source_get_settings(hs->source);
		if (!obs_data_get_bool(settings, "refresh")) {
			obs_data_release(settings);
			continue;
		}
		long long source_type = obs_data_get_int(settings, "html_source");
		if (source_type == HTML_FILE) {
			if (html_source_file_changed(&hs->html_time, settings))
				obs_source_update(hs->source, NULL);
		} else if (source_type == HTML_WEB) {
			if (html_source_web_changed(hs, settings))
				obs_source_update(hs->source, NULL);
		}
		obs_data_release(settings);
	}
	return NULL;
}

static void *html_source_create(obs_data_t *settings, obs_source_t *source)
{
	struct html_source_data *hs = (struct html_source_data *)bzalloc(sizeof(struct html_source_data));
	hs->source = source;
	hs->sleep = 100;
	dstr_init(&hs->html);

	pthread_create(&hs->thread, NULL, html_source_thread, hs);

	obs_source_update(source, settings);
	return hs;
}

static void html_source_destroy(void *data)
{
	struct html_source_data *hs = (struct html_source_data *)data;
	hs->stop = true;
	pthread_join(hs->thread, NULL);
	if (hs->texture) {
		obs_enter_graphics();
		gs_texture_destroy(hs->texture);
		obs_leave_graphics();
	}
	if (hs->curl_header)
		curl_slist_free_all(hs->curl_header);
	if (hs->curl)
		curl_easy_cleanup(hs->curl);
	dstr_free(&hs->html);
	da_free(hs->web_data);
	bfree(hs);
}

static const char *html_source_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("HtmlSource");
}

uint32_t html_source_width(void *data)
{
	struct html_source_data *hs = (struct html_source_data *)data;
	return hs->cx;
}

uint32_t html_source_height(void *data)
{
	struct html_source_data *hs = (struct html_source_data *)data;
	return hs->cy;
}

void html_source_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct html_source_data *hs = (struct html_source_data *)data;

	if (!hs->texture)
		return;

	gs_technique_t *tech = gs_effect_get_technique(effect, "Draw");
	const bool prev = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(true);
	gs_technique_begin(tech);
	gs_technique_begin_pass(tech, 0);
	gs_effect_set_texture_srgb(gs_effect_get_param_by_name(effect, "image"), hs->texture);
	gs_draw_sprite(hs->texture, 0, hs->cx, hs->cy);
	gs_technique_end_pass(tech);
	gs_technique_end(tech);
	gs_enable_framebuffer_srgb(prev);
}

static void render_qt_task(void *data)
{
	struct html_source_data *hs = (struct html_source_data *)data;
	obs_data_t *settings = obs_source_get_settings(hs->source);
	render_qt(hs, settings);
	obs_data_release(settings);
}

static void html_source_update(void *data, obs_data_t *settings)
{
	struct html_source_data *hs = (struct html_source_data *)data;
	hs->sleep = (uint32_t)obs_data_get_int(settings, "sleep");
	if (!hs->sleep)
		hs->sleep = 100;
	if (!obs_data_get_bool(settings, "refresh")) {
		long long source_type = obs_data_get_int(settings, "html_source");
		if (source_type == HTML_FILE) {
			html_source_file_changed(&hs->html_time, settings);
		} else if (source_type == HTML_WEB) {
			html_source_web_changed(hs, settings);
		}
	}
	obs_queue_task(OBS_TASK_UI, render_qt_task, hs, false);
}

static bool html_source_changed(void *data, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(property);

	long long source_type = obs_data_get_int(settings, "html_source");
	obs_property_t *p = obs_properties_get(props, "text");
	obs_property_set_visible(p, source_type == HTML_TEXT);
	p = obs_properties_get(props, "html_path");
	obs_property_set_visible(p, source_type == HTML_FILE);
	p = obs_properties_get(props, "html_url");
	obs_property_set_visible(p, source_type == HTML_WEB);
	p = obs_properties_get(props, "refesh");
	obs_property_set_visible(p, source_type != HTML_TEXT);
	p = obs_properties_get(props, "sleep");
	obs_property_set_visible(p, source_type != HTML_TEXT);
	return true;
}

static obs_properties_t *html_source_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();
	obs_property_t *p = obs_properties_add_list(props, "html_source", obs_module_text("HtmlSource"), OBS_COMBO_TYPE_LIST,
						    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Text"), HTML_TEXT);
	obs_property_list_add_int(p, obs_module_text("File"), HTML_FILE);
	obs_property_list_add_int(p, obs_module_text("Web"), HTML_WEB);
	obs_property_set_modified_callback2(p, html_source_changed, data);
	p = obs_properties_add_text(props, "text", obs_module_text("Html"), OBS_TEXT_MULTILINE);
	obs_property_text_set_monospace(p, true);

	obs_properties_add_path(props, "html_path", obs_module_text("HtmlFile"), OBS_PATH_FILE,
				"HTML files (*.html *.htm);;All files (*.*)", NULL);

	obs_properties_add_text(props, "html_url", obs_module_text("HtmlUrl"), OBS_TEXT_DEFAULT);

	obs_properties_add_bool(props, "refesh", obs_module_text("AutomaticRefresh"));
	p = obs_properties_add_int(props, "sleep", obs_module_text("RefreshInterval"), 1, 1000000, 1);
	obs_property_int_set_suffix(p, "ms");

	obs_properties_add_font(props, "font", obs_module_text("DefaultFont"));

	obs_properties_add_text(props, "plugin_info",
				"<a href=\"https://github.com/exeldro/obs-html-source\">HTML Source</a> (" PROJECT_VERSION
				") by <a href=\"https://www.exeldro.com\">Exeldro</a>",
				OBS_TEXT_INFO);
	return props;
}

static void html_source_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "sleep", 300);
	obs_data_t *font = obs_data_create();
	obs_data_set_int(font, "size", 72);
	obs_data_set_default_obj(settings, "font", font);
	obs_data_release(font);
}

struct obs_source_info html_source = {
	.id = "html_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB,
	.icon_type = OBS_ICON_TYPE_TEXT,
	.create = html_source_create,
	.destroy = html_source_destroy,
	.update = html_source_update,
	.load = html_source_update,
	.get_name = html_source_name,
	.get_defaults = html_source_defaults,
	.get_width = html_source_width,
	.get_height = html_source_height,
	.video_render = html_source_render,
	.get_properties = html_source_properties,
};

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("html-source", "en-US")

bool obs_module_load(void)
{
	blog(LOG_INFO, "[html-source] loaded version %s", PROJECT_VERSION);
	obs_register_source(&html_source);

	return true;
}

void obs_module_unload(void) {}

MODULE_EXPORT const char *obs_module_name(void)
{
	return obs_module_text("HtmlSource");
}
