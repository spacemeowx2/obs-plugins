#include <obs-module.h>
#include <curl.h>
#include <cJSON.h>
#include <stdio.h>

OBS_DECLARE_MODULE();
typedef struct {
    char *buf;
    size_t size;
} simple_buffer;
typedef struct bilibili_service_def {
    char *cookie;
    char *title;
    char *area;
    simple_buffer buffer;
    int32_t area_id;
    obs_service_t *context;
    char *addr;
    char *code;
    char *room_id;
} bilibili_service;
void bilibili_update(void *data, obs_data_t *settings);
void reset_buffer(simple_buffer *buf);
void reset_service(bilibili_service *s);

const char* bilibili_name(void* data)
{
    return "Bilibili";
}

void *bilibili_create(obs_data_t *settings, obs_service_t *service)
{
    bilibili_service *s = bzalloc(sizeof(bilibili_service));
    bilibili_update(s, settings);

    s->area_id = -1;
    s->context = service;
    return s;
}

void bilibili_destroy(void *data)
{
    bilibili_service *s = data;
    reset_service(s);
    bfree(s->buffer.buf);
    bfree(s);
}

void output(const char *str, const char *src)
{
    obs_source_t *s = obs_get_source_by_name(src);
    if (s)
    {
        if (strcmp("text_gdiplus", obs_source_get_id(s)) == 0)
        {
            obs_data_t *settings = obs_data_create();
            obs_data_set_string(settings, "text", str);
            obs_source_update(s, settings);
            obs_data_release(settings);
        }
        obs_source_release(s);
    }
}

void outputf(const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    char buf[1024];
    vsprintf(buf, format, ap);
    va_end(ap);
    output(buf, "output_bilibili");
}

void reset_service(bilibili_service *s)
{
    bfree(s->room_id);
    bfree(s->cookie);
    bfree(s->title);
    bfree(s->area);
    bfree(s->code);
    bfree(s->addr);
    reset_buffer(&s->buffer);
}

void bilibili_update(void *data, obs_data_t *settings)
{
    bilibili_service *service = data;

    reset_service(service);

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
        curl_easy_setopt(curl, CURLOPT_COOKIE, s->cookie);
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
        cJSON *item;
        cJSON_ArrayForEach(item, list)
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
    return -1;
}

bool get_area_id(bilibili_service *s)
{
    if (get_url(s, "https://api.live.bilibili.com/room/v1/Area/getList"))
    {
        cJSON *json = cJSON_Parse(s->buffer.buf);
        if (json)
        {
            cJSON *data = cJSON_GetObjectItemCaseSensitive(json, "data");
            if (data)
            {
                cJSON *group;
                cJSON_ArrayForEach(group, data)
                {
                    int32_t id = get_area_id_in_group(group, s->area);
                    if (id != -1)
                    {
                        s->area_id = id;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool get_room_id(bilibili_service *s)
{
    if (s->room_id)
    {
        return true;
    }
    if (get_url(s, "http://api.live.bilibili.com/i/api/liveinfo"))
    {
        obs_data_t *res = obs_data_create_from_json(s->buffer.buf);
        if (obs_data_get_int(res, "code") == 0)
        {
            obs_data_t *data = obs_data_get_obj(res, "data");
            s->room_id = bstrdup(obs_data_get_string(data, "roomid"));
            return !!s->room_id;
        }
    }
    return false;
}

void stop_live(bilibili_service *s)
{
    CURL *curl;
    curl = curl_easy_init();
    if (curl)
    {
        char post_fields[1024];
        sprintf(post_fields, "room_id=%s&platform=pc&csrf_token=%s", "930140", "2a558eeb19223cdee5792a4a68c7e88f");
        CURLcode res;
        reset_buffer(&s->buffer);
        curl_easy_setopt(curl, CURLOPT_URL, "http://api.live.bilibili.com/room/v1/Room/startLive");
        curl_easy_setopt(curl, CURLOPT_COOKIE, s->cookie);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, s);

        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields);
        res = curl_easy_perform(curl);
        if(res == CURLE_OK)
        {
            //
        }
        curl_easy_cleanup(curl);
    }
}

bool start_live(bilibili_service *s)
{
    if (!get_area_id(s)) return false;
    if (!get_room_id(s)) return false;
    bool result = false;
    CURL *curl;
    curl = curl_easy_init();
    if (curl)
    {
        char post_fields[1024];
        sprintf(post_fields, "room_id=%s&platform=pc&area_v2=%d&csrf_token=%s", s->room_id, s->area_id, "2a558eeb19223cdee5792a4a68c7e88f");
        CURLcode res;
        reset_buffer(&s->buffer);
        curl_easy_setopt(curl, CURLOPT_URL, "http://api.live.bilibili.com/room/v1/Room/startLive");
        curl_easy_setopt(curl, CURLOPT_COOKIE, s->cookie);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, s);

        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields);
        res = curl_easy_perform(curl);
        if(res == CURLE_OK)
        {
            result = true;
        }
        curl_easy_cleanup(curl);
    }
    if (result)
    {
        obs_data_t *res = obs_data_create_from_json(s->buffer.buf);
        obs_data_t *data = obs_data_get_obj(res, "data");
        obs_data_t *rtmp = obs_data_get_obj(data, "rtmp");
        char* addr = bstrdup(obs_data_get_string(rtmp, "addr"));
        char* code = bstrdup(obs_data_get_string(rtmp, "code"));
        s->addr = addr;
        s->code = code;
        // outputf("p %p %p %p\n%s\n%s\n%s", res, data, rtmp, s->addr, s->code, s->buffer.buf);
    }
    else
    {
        // outputf("failed");
    }
    return result;
}

const char *bilibili_url(void *data)
{
    bilibili_service *s = data;
    start_live(s);
    // outputf("url %s", s->addr);
    return s->addr;
}

const char *bilibili_key(void *data)
{
    bilibili_service *s = data;
    // outputf("key %s", s->code);
    return s->code;
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
