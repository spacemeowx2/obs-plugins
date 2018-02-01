// http://api.live.bilibili.com/room/v1/Area/getList?show_pinyin=1

#include <obs-module.h>
OBS_DECLARE_MODULE();

typedef struct bilibili_service_def {
    char* cookie;
    char* title;
    char* area;
} bilibili_service;
void bilibili_update(void *data, obs_data_t *settings);

const char* bilibili_name(void* data)
{
    return "Bilibili";
}

void *bilibili_create(obs_data_t *settings, obs_service_t *service)
{
	bilibili_service *data = bzalloc(sizeof(bilibili_service));
	bilibili_update(data, settings);

	UNUSED_PARAMETER(service);
	return data;
}

void bilibili_destroy(void *data)
{
    bfree(data);
}

void bilibili_update(void *data, obs_data_t *settings)
{
	bilibili_service *service = data;

	bfree(service->cookie);
	bfree(service->title);
	bfree(service->area);

	service->cookie = bstrdup(obs_data_get_string(settings, "cookie"));
	service->title  = bstrdup(obs_data_get_string(settings, "title"));
	service->area   = bstrdup(obs_data_get_string(settings, "area"));
}

obs_properties_t *bilibili_properties(void *unused)
{
	obs_properties_t *ppts = obs_properties_create();

	obs_properties_add_text(ppts, "cookie", "Cookie", OBS_TEXT_MULTILINE);
    obs_properties_add_text(ppts, "title", "标题", OBS_TEXT_DEFAULT);
	obs_properties_add_text(ppts, "area", "分区", OBS_TEXT_DEFAULT);

	return ppts;
}

const char *bilibili_url(void *data)
{
    return NULL;
}

const char *bilibili_key(void *data)
{
    return NULL;
}

struct obs_service_info my_service = {
    .id             = "bilibili_service",
    .get_name       = bilibili_name,
	.create         = bilibili_create,
	.destroy        = bilibili_destroy,
	.update         = bilibili_update,
	.get_properties = bilibili_properties,
	.get_url        = bilibili_url,
	.get_key        = bilibili_key,
};

bool obs_module_load(void)
{
    obs_register_service(&my_service);
    return true;
}
