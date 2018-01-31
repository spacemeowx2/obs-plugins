#include <obs-module.h>
#include <obs-frontend-api.h>
#include <stdio.h>

OBS_DECLARE_MODULE();
typedef enum now_state_def {
    state_playing,
    state_other
} now_state;
struct filter_data_def {
    obs_source_t *source;
    gs_texrender_t *render;
    gs_stagesurf_t *copy;
    uint32_t counter;
    uint32_t cx;
    uint32_t cy;
    uint32_t linesize;
    uint8_t *ptr;
    uint8_t near;
    bool target_valid;

    bool last_is_time_panel;
    bool is_time_panel;
    float time_panel_begin;
    float time;

    now_state state;
};
typedef struct filter_data_def filter_data;
struct mRGB_def {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t rev;
};
typedef struct mRGB_def mRGB;
mRGB RGB_BLACK = {0, 0, 0, 0};
mRGB RGB_WHITE = {255, 255, 255, 0};

void elog(const char* s)
{
    blog(LOG_DEBUG, "%s", s);
}

void reset_textures(filter_data *f)
{
    obs_enter_graphics();
    if (f->copy)
    {
        gs_stagesurface_destroy(f->copy);        
    }
    f->copy = gs_stagesurface_create(f->cx, f->cy, GS_RGBA);
    obs_leave_graphics();
}

void check_size(filter_data *f)
{
    obs_source_t *target = obs_filter_get_target(f->source);

    f->target_valid = !!target;
    if (!f->target_valid)
    {
        return;
    }

    uint32_t cx = obs_source_get_base_width(target);
    uint32_t cy = obs_source_get_base_height(target);
    
    f->target_valid = !!cx && !!cy;
    if (!f->target_valid)
    {
        return;
    }

    if (cx != f->cx || cy != f->cy) {
        f->cx = cx;
        f->cy = cy;
        reset_textures(f);
        return;
    }
}

void draw_render(gs_texrender_t *render, uint32_t cx, uint32_t cy)
{
    gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_texture_t *tex = gs_texrender_get_texture(render);
    if (tex) {
        gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
        gs_effect_set_texture(image, tex);

        while (gs_effect_loop(effect, "Draw"))
        {
            gs_draw_sprite(tex, 0, cx, cy);
        }
    }
}

const char *my_source_name(void *type_data)
{
    return "Pixel Switcher Filter";
}

void *my_source_create(obs_data_t *settings, obs_source_t *source)
{
    elog("filter create");
    filter_data *f = bzalloc(sizeof(*f));
    f->source = source;
    f->counter = 0;
    f->near = 1;
    f->state = state_other;
    obs_enter_graphics();
    f->render = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    check_size(f);
    obs_leave_graphics();
    return f;
}

void my_source_destroy(void* data)
{
    elog("filter destroy");
    filter_data *f = data;
    obs_enter_graphics();
    gs_texrender_destroy(f->render);
    if (f->copy)
    {
        gs_stagesurface_destroy(f->copy);
    }
    obs_leave_graphics();
    bfree(f);
}

void my_source_update(void *data, obs_data_t *settings)
{

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

int round_int(float x)
{
    return x + 0.5;
}

mRGB get_point_abs(filter_data *f, uint32_t x, uint32_t y)
{
    uint8_t *p = f->ptr + (y * f->linesize + x * 4);
    mRGB r;
    r.r = p[0];
    r.g = p[1];
    r.b = p[2];
    r.rev = 0;
    return r;
}

/**
 * ptr: RGBA pixels
 * width: in pixel
 * return: RGB
 */
mRGB get_point(filter_data *f, float x, float y)
{
    int ix = round_int(x * f->cx);
    int iy = round_int(y * f->cy);
    uint32_t r = 0;
    uint32_t g = 0;
    uint32_t b = 0;
    int n = f->near;
    mRGB c = get_point_abs(f, ix, iy);
    return c;
}

bool color_compare(filter_data *f, float x, float y, mRGB rgb, uint32_t threshold)
{
    mRGB c = get_point(f, x, y);
    int dr = abs(c.r - rgb.r);
    int dg = abs(c.g - rgb.g);
    int db = abs(c.b - rgb.b);
    return (dr * dr + dg * dg + db * db < threshold * threshold);
}

bool is_all_color(filter_data *f, float *xy, int count, mRGB rgb, int threshold)
{
    for (int i = 0; i < count; i++)
    {
        float x = xy[i * 2];
        float y = xy[i * 2 + 1];
        if (!color_compare(f, x, y, rgb, threshold))
        {
            return false;
        }
    }
    return true;
}
bool is_yellow(filter_data *f, float x, float y)
{
    mRGB c = get_point(f, x, y);
    if (c.b > 200)
    {
        return false;
    }
    if (c.r < 200 || c.g < 200)
    {
        return false;
    }
    return true;
}
bool is_gray(filter_data *f, float x, float y)
{
    int t1 = 45;
    int t2 = 15;
    mRGB c = get_point(f, x, y);
    if (c.r > t1 || c.g > t1 || c.b > t1)
    {
        return false;
    }
    float a = (c.r + c.g + c.b) / 3.0;
    float m = 0;
    m = fmax(m, abs(c.r - a));
    m = fmax(m, abs(c.g - a));
    m = fmax(m, abs(c.b - a));
    if (m > t2)
    {
        return false;
    }
    return true;
}
void check_all_xy(filter_data *f, bool* result, float *xy, int count, bool (*func)(filter_data *, float, float))
{
    for (int i = 0; i < count; i++)
    {
        float x = xy[i * 2];
        float y = xy[i * 2 + 1];
        result[i] = func(f, x, y);
    }
}
bool bools_to_bool(bool *bs, int count, int ge)
{
    int c = 0;
    for (int i = 0; i < count; i++)
    {
        c += bs[i] ? 1 : 0;
    }
    return c >= ge;
}

bool is_time_split(filter_data *f)
{
    float xy_time_split[] = {
        0.49316, 0.06771,
        0.49316, 0.08333
    };
    mRGB rgb_yellow = {0};

    bool white = is_all_color(f, xy_time_split, 2, RGB_WHITE, 30);
    bool yellow = false;
    bool b_time_split[2] = {false};
    if (!white)
    {
        check_all_xy(f, b_time_split, xy_time_split, 2, is_yellow);
        yellow = b_time_split[0] && b_time_split[1];
    }

    char buf[512];
    mRGB c1 = get_point(f, 0.49316, 0.06771);
    mRGB c2 = get_point(f, 0.49316, 0.08333);
    sprintf(buf, "white %d\nyellow %d\n%d %d\n%d %d %d\n%d %d %d", white, yellow, b_time_split[0], b_time_split[1],
        c1.r, c1.g, c1.b,
        c2.r, c2.g, c2.b);
    output(buf, "output1");
    return white || yellow;
}

bool is_time_panel(filter_data *f)
{
    float xy_time_lr[] = {
        0.46094, 0.07986,
        0.47266, 0.07986,
        0.52734, 0.07986,
        0.53906, 0.07986
    };
    float xy_time_tb[] = {
        0.50586, 0.03472,
        0.52344, 0.03472,
        0.50391, 0.09201,
        0.49023, 0.09201
    };

    bool b_time_lr[4] = {false};
    bool b_time_tb[4] = {false};
    check_all_xy(f, b_time_lr, xy_time_lr, 4, is_gray);
    check_all_xy(f, b_time_tb, xy_time_tb, 4, is_gray);

    bool time_lr = bools_to_bool(b_time_lr, 4, 3);
    bool time_tb = bools_to_bool(b_time_tb, 4, 3);
    bool time_split = is_time_split(f);

    char buf[512];
    sprintf(buf, "time lr %x\ntime tb %x\ntime sp %d\n%d\n%08x\n%08x", time_lr, time_tb, time_split, f->is_time_panel,
        *(int*)b_time_lr, *(int*)b_time_tb);
    // mRGB p = get_point(f, 0.29883, 0.03472);
    // sprintf(buf, "%d: %d %d %d", round_int(0.29883 * f->cx), p.r, p.g, p.b);
    output(buf, "output2");

    return time_lr && time_tb && time_split;
}

void identify(filter_data *f)
{
    uint8_t *ptr = f->ptr;
    uint32_t width = f->cx;
    f->last_is_time_panel = f->is_time_panel;
    f->is_time_panel = is_time_panel(f);
}

void switch_scene(const char *scene)
{
    obs_source_t *source = obs_get_source_by_name(scene);
    if (source)
    {
        if (obs_source_get_type(source) == OBS_SOURCE_TYPE_SCENE)
        {
            obs_frontend_set_current_scene(source);
        }
        obs_source_release(source);
    }
}

void my_source_tick(void *data, float tk)
{
    filter_data *f = data;
    check_size(f);
    f->time += tk;
    float t = f->time;
    char buf[128];
    sprintf(buf, "last %d cur %d t %.2f b %.2f s %d", f->last_is_time_panel, f->is_time_panel, t, f->time_panel_begin, f->state);
    output(buf, "output3");
    switch (f->state)
    {
        case state_other:
            if (!f->last_is_time_panel && f->is_time_panel)
            {
                f->time_panel_begin = t;
            }
            if (f->is_time_panel && t - f->time_panel_begin > 3)
            {
                switch_scene("pure-switch");
                f->state = state_playing;
            }
            break;
        case state_playing:
            if (f->last_is_time_panel && !f->is_time_panel)
            {
                f->time_panel_begin = t;
            }
            if (!f->is_time_panel && t - f->time_panel_begin > 10)
            {
                switch_scene("switch");
                f->state = state_other;
            }
            break;
    }
}

void my_source_render(void *data, gs_effect_t *effect)
{
    filter_data *f = data;

    if (f->counter >= 15) {
        f->counter = 0;
    }
    if (0 != f->counter++)
    {
        obs_source_skip_video_filter(f->source);
        return;
    }

    obs_source_t *source = f->source;
    uint32_t width = obs_source_get_width(source);
    uint32_t height = obs_source_get_height(source);
    obs_source_t *target = obs_filter_get_target(source);
    obs_source_t *parent = obs_filter_get_parent(source);
    gs_texrender_reset(f->render);
    gs_blend_state_push();
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

    if (gs_texrender_begin(f->render, width, height)) {
        uint32_t parent_flags = obs_source_get_output_flags(target);
        bool custom_draw = (parent_flags & OBS_SOURCE_CUSTOM_DRAW) != 0;
        bool async = (parent_flags & OBS_SOURCE_ASYNC) != 0;
        struct vec4 clear_color;

        vec4_zero(&clear_color);
        gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
        gs_ortho(0.0f, (float)width, 0.0f, (float)height,
                -100.0f, 100.0f);

        if (target == parent && !custom_draw && !async)
            obs_source_default_render(target);
        else
            obs_source_video_render(target);

        gs_texture_t *tex = gs_texrender_get_texture(f->render);
        gs_stage_texture(f->copy, tex);

        
        if (gs_stagesurface_map(f->copy, &f->ptr, &f->linesize))
        {
            blog(LOG_DEBUG, "map success linesize: %u", f->linesize);
            identify(f);
            
            gs_stagesurface_unmap(f->copy);
            f->ptr = NULL;
        }
        else
        {
            blog(LOG_DEBUG, "texture map failed %p", f->copy);
        }

        gs_texrender_end(f->render);
    }
    gs_blend_state_pop();

    draw_render(f->render, width, height);
}

struct obs_source_info my_source = {
        .id           = "pixel_switcher_filter",
        .type         = OBS_SOURCE_TYPE_FILTER,
        .output_flags = OBS_SOURCE_VIDEO,
        .get_name     = my_source_name,
        .create       = my_source_create,
        .destroy      = my_source_destroy,
        .update       = my_source_update,
        .video_tick   = my_source_tick,
        .video_render = my_source_render
};

bool obs_module_load(void)
{
        obs_register_source(&my_source);
        return true;
}
