// http://api.live.bilibili.com/room/v1/Area/getList?show_pinyin=1

#include <obs-module.h>
#include <curl.h>
#include <cJSON.h>
#include <stdio.h>

OBS_DECLARE_MODULE();
typedef struct {
    char* buf;
    size_t size;
} simple_buffer;
typedef struct bilibili_service_def {
    char* cookie;
    char* title;
    char* area;
    simple_buffer buffer;
    int32_t area_id;
} bilibili_service;
void bilibili_update(void *data, obs_data_t *settings);

const char* bilibili_name(void* data)
{
    return "Bilibili";
}

void *bilibili_create(obs_data_t *settings, obs_service_t *service)
{
    bilibili_service *s = bzalloc(sizeof(bilibili_service));
    bilibili_update(s, settings);

    s->area_id = -1;
    return s;
}

void bilibili_destroy(void *data)
{
    bilibili_service *s = data;
    if (s->buffer.buf)
    {
        bfree(s->buffer.buf);
    }
    bfree(s);
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

size_t writefunc(void *ptr, size_t size, size_t nmemb, bilibili_service *s)
{
    size_t realsize = size * nmemb;
    simple_buffer *buf = &s->buffer;
    buf->buf = brealloc(buf->buf, buf->size + realsize + 1);
    
    if (buf->buf == NULL) {
        blog(LOG_ERROR, "not enough memory (realloc returned NULL)");
        return 0;
    }

    memcpy(&(buf->buf[buf->size]), ptr, realsize);
    buf->size += realsize;
    buf->buf[buf->size] = 0;
    return realsize;
}

void reset_buffer(simple_buffer *buf)
{
    if (buf->buf)
    {
        bfree(buf->buf);
    }
    buf->size = 0;
    buf->buf = bmalloc(1);
}

bool get_url(bilibili_service *s, const char* url)
{
    bool result = false;
    CURL *curl;
    CURLcode res;
    curl = curl_easy_init();
    if (curl)
    {
        reset_buffer(&s->buffer);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, s);
        res = curl_easy_perform(curl);
        if(res == CURLE_OK)
        {
            result = true;
        }
        curl_easy_cleanup(curl);
    }
    return result;
}

int32_t get_area_id_in_group(cJSON *group, const char *name)
{
    cJSON *list = cJSON_GetObjectItemCaseSensitive(group, "list");
    if (list)
    {
        int size = cJSON_GetArraySize(list);
        for (int i = 0; i < size; i++)
        {
            cJSON *item = cJSON_GetArrayItem(list, i);
            if (item)
            {
                cJSON *n = cJSON_GetObjectItemCaseSensitive(item, "name");
                if (n && strcmp(cJSON_GetStringValue(n), name) == 0)
                {
                    cJSON *id_item = cJSON_GetObjectItemCaseSensitive(item, "id");
                    if (id_item)
                    {
                        return atoi(cJSON_GetStringValue(id_item));
                    }
                }
            }
        }
    }
    return -1;
}

int32_t get_area_id(bilibili_service *s)
{
    if (get_url(s, "https://api.live.bilibili.com/room/v1/Area/getList"))
    {
        cJSON *json = cJSON_Parse(s->buffer.buf);
        if (json)
        {
            cJSON *data = cJSON_GetObjectItemCaseSensitive(json, "data");
            if (data)
            {
                int size = cJSON_GetArraySize(data);
                for (int i = 0; i < size; i++)
                {
                    cJSON *group = cJSON_GetArrayItem(data, i);
                    int32_t id = get_area_id_in_group(group, s->area);
                    if (id != -1)
                    {
                        return id;
                    }
                }
            }
        }
    }
    return -1;
}

const char *bilibili_url(void *data)
{
    bilibili_service *s = data;
    get_area_id(s);
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
