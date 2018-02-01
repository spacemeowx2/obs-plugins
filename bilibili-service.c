#include <obs-module.h>
#include <obs-frontend-api.h>
#include <curl.h>
#include <stdio.h>

OBS_DECLARE_MODULE();
typedef struct {
    char *buf;
    size_t size;
} simple_buffer;
typedef struct bilibili_service_def {
    char *cookie;
    char *area;
    simple_buffer buffer;
    int32_t area_id;
    obs_service_t *context;
    char *addr;
    char *code;
    char *room_id;
    char csrf_token[64];
} bilibili_service;
void bilibili_update(void *data, obs_data_t *settings);
void reset_buffer(simple_buffer *buf);
void reset_service(bilibili_service *s);

const char* bilibili_name(void* data)
{
    return "Bilibili 一键开播 - spacemeowx2";
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
    bfree(s->area);
    bfree(s->code);
    bfree(s->addr);
    reset_buffer(&s->buffer);
}

void trim_cookie(bilibili_service *s)
{
    char *r = s->cookie;
    char *w = s->cookie;
    while (*r != '\0')
    {
        char c = *r;
        if (!(c == '\r' || c == '\n'))
        {
            *w++ = *r;
        }
        r++;
    }
    *w = *r;
}

bool update_csrf_token(bilibili_service *s)
{
    const char *jct = "bili_jct=";
    char *begin = strstr(s->cookie, jct);
    if (begin == NULL)
    {
        // outputf("begin false");
        return false;
    }
    begin += strlen(jct);
    char *end = strstr(begin, ";");
    if (end == NULL && end != begin)
    {
        end = begin + strlen(begin);
    }
    if (end - begin > 60)
    {
        // outputf("csrf too long");
        return false;
    }
    memset(s->csrf_token, 0, sizeof(s->csrf_token));
    memcpy(s->csrf_token, begin, end - begin);
    // outputf("csrf: %s", s->csrf_token);
    return true;
}

void bilibili_update(void *data, obs_data_t *settings)
{
    bilibili_service *service = data;

    reset_service(service);

    service->cookie = bstrdup(obs_data_get_string(settings, "cookie"));
    service->area   = bstrdup(obs_data_get_string(settings, "area"));

    trim_cookie(service);
    update_csrf_token(service);
}

obs_properties_t *bilibili_properties(void *unused)
{
    obs_properties_t *ppts = obs_properties_create();

    obs_properties_add_text(ppts, "cookie", "Cookie", OBS_TEXT_MULTILINE);
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

int32_t get_area_id_in_group(obs_data_t *group, const char *name)
{
    obs_data_array_t *list = obs_data_get_array(group, "list");
    if (list)
    {
        size_t size = obs_data_array_count(list);
        for (int i = 0; i < size; i++)
        {
            obs_data_t *item = obs_data_array_item(list, i);
            if (item)
            {
                const char *n = obs_data_get_string(item, "name");
                if (strcmp(n, name) == 0)
                {
                    const char *id_str = obs_data_get_string(item, "id");
                    if (id_str)
                    {
                        return atoi(id_str);
                    }
                }
                obs_data_release(item);
            }
        }
        obs_data_array_release(list);
    }
    return -1;
}

bool get_area_id(bilibili_service *s)
{
    if (get_url(s, "https://api.live.bilibili.com/room/v1/Area/getList"))
    {
        obs_data_t *res = obs_data_create_from_json(s->buffer.buf);
        if (res)
        {
            obs_data_array_t *data = obs_data_get_array(res, "data");
            if (data)
            {
                size_t size = obs_data_array_count(data);
                for (int i = 0; i < size; i++)
                {
                    obs_data_t *group = obs_data_array_item(data, i);
                    if (group)
                    {
                        int32_t id = get_area_id_in_group(group, s->area);
                        if (id != -1)
                        {
                            s->area_id = id;
                            return true;
                        }
                        obs_data_release(group);
                    }
                }
                obs_data_array_release(data);
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
        sprintf(post_fields, "room_id=%s&platform=pc&csrf_token=%s", s->room_id, s->csrf_token);
        CURLcode res;
        reset_buffer(&s->buffer);
        curl_easy_setopt(curl, CURLOPT_URL, "http://api.live.bilibili.com/room/v1/Room/stopLive");
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
        sprintf(post_fields, "room_id=%s&platform=pc&area_v2=%d&csrf_token=%s", s->room_id, s->area_id, s->csrf_token);
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

void bilibili_frontend_stop(enum obs_frontend_event event, void *data)
{
    bilibili_service *s = data;
    if (event == OBS_FRONTEND_EVENT_STREAMING_STOPPED)
    {
        obs_frontend_remove_event_callback(bilibili_frontend_stop, data);
        stop_live(s);
    }
}

void register_stop_streaming(bilibili_service *s)
{
    obs_frontend_add_event_callback(bilibili_frontend_stop, s);
}

const char *bilibili_url(void *data)
{
    bilibili_service *s = data;
    start_live(s);
    register_stop_streaming(s);
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
