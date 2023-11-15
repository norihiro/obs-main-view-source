#include <obs-module.h>
#include <util/threading.h>
#include <util/dstr.h>
#include "plugin-macros.generated.h"

#if LIBOBS_API_VER >= MAKE_SEMANTIC_VERSION(29, 1, 0)
#define USE_RENDERED_CALLBACK
#endif

struct main_view_s
{
	obs_source_t *context;
	bool rendered;

#ifndef USE_RENDERED_CALLBACK
	obs_weak_source_t *weak_source;

	gs_texrender_t *texrender;
	gs_texrender_t *texrender_prev;
#else
	gs_texture_t *cached_texture;
#endif

	bool offscreen_render;
};

static const char *get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("Source.Name");
}

static obs_properties_t *get_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();

	return props;
}

#ifndef USE_RENDERED_CALLBACK
static void main_view_offscreen_render_cb(void *data, uint32_t cx, uint32_t cy);
#else
static void main_view_rendered_callback(void *data);
#endif

static void register_offscreen_render(struct main_view_s *s)
{
#ifndef USE_RENDERED_CALLBACK
	obs_add_main_render_callback(main_view_offscreen_render_cb, s);
#else
	obs_add_main_rendered_callback(main_view_rendered_callback, s);
#endif
}

static void unregister_offscreen_render(struct main_view_s *s)
{
#ifndef USE_RENDERED_CALLBACK
	obs_remove_main_render_callback(main_view_offscreen_render_cb, s);
#else
	obs_remove_main_rendered_callback(main_view_rendered_callback, s);
#endif
}

static void update(void *data, obs_data_t *settings)
{
	UNUSED_PARAMETER(settings);

	struct main_view_s *s = data;

	if (!s->offscreen_render)
		register_offscreen_render(s);
	s->offscreen_render = true;
}

static void *create(obs_data_t *settings, obs_source_t *source)
{
	struct main_view_s *s = bzalloc(sizeof(struct main_view_s));

	s->context = source;

	update(s, settings);

	return s;
}

static void destroy(void *data)
{
	struct main_view_s *s = data;

	if (s->offscreen_render)
		unregister_offscreen_render(s);

#ifndef USE_RENDERED_CALLBACK
	obs_weak_source_release(s->weak_source);
#endif

	obs_enter_graphics();
#ifndef USE_RENDERED_CALLBACK
	gs_texrender_destroy(s->texrender);
	gs_texrender_destroy(s->texrender_prev);
#else
	gs_texture_destroy(s->cached_texture);
#endif
	obs_leave_graphics();

	bfree(s);
}

static void video_tick(void *data, float seconds)
{
	UNUSED_PARAMETER(seconds);
	struct main_view_s *s = data;

#ifndef USE_RENDERED_CALLBACK
	obs_weak_source_release(s->weak_source);

	obs_source_t *target = obs_get_output_source(0);
	s->weak_source = obs_source_get_weak_source(target);
	obs_source_release(target);
#endif

	s->rendered = false;
}

#ifndef USE_RENDERED_CALLBACK
static void cache_video(struct main_view_s *s, obs_source_t *target)
{
	gs_texrender_t *texrender = s->texrender_prev;
	if (!texrender)
		s->texrender_prev = texrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
	else
		gs_texrender_reset(texrender);

	uint32_t width = obs_source_get_width(target);
	uint32_t height = obs_source_get_height(target);

	if (gs_texrender_begin(texrender, width, height)) {
		struct vec4 background;
		vec4_zero(&background);

		gs_clear(GS_CLEAR_COLOR, &background, 0.0f, 0);
		gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);

		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
		obs_source_video_render(target);
		gs_blend_state_pop();
		gs_texrender_end(texrender);

		s->texrender_prev = s->texrender;
		s->texrender = texrender;
	}
}

#else

static void cache_texture(struct main_view_s *s, gs_texture_t *src_tex)
{
	uint32_t width = gs_texture_get_width(src_tex);
	uint32_t height = gs_texture_get_height(src_tex);
	enum gs_color_format format = gs_texture_get_color_format(src_tex);

	gs_texture_t *dst_tex = s->cached_texture;

	if (!dst_tex || gs_texture_get_width(dst_tex) != width || gs_texture_get_height(dst_tex) != height ||
	    gs_texture_get_color_format(dst_tex) != format) {
		gs_texture_destroy(dst_tex);
		dst_tex = s->cached_texture = gs_texture_create(width, height, format, 1, NULL, GS_RENDER_TARGET);
	}

	gs_copy_texture(dst_tex, src_tex);
}
#endif // USE_RENDERED_CALLBACK

static void render_cached_video(struct main_view_s *s)
{
#ifndef USE_RENDERED_CALLBACK
	gs_texture_t *tex = gs_texrender_get_texture(s->texrender);
#else
	gs_texture_t *tex = s->cached_texture;
#endif
	if (!tex)
		return;

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), tex);
	uint32_t cx = gs_texture_get_width(tex);
	uint32_t cy = gs_texture_get_height(tex);

	while (gs_effect_loop(effect, "Draw"))
		gs_draw_sprite(tex, 0, cx, cy);

	gs_blend_state_pop();
}

#ifndef USE_RENDERED_CALLBACK
static void main_view_offscreen_render_cb(void *data, uint32_t cx, uint32_t cy)
{
	UNUSED_PARAMETER(cx);
	UNUSED_PARAMETER(cy);
	struct main_view_s *s = data;

	if (!obs_source_showing(s->context))
		return;

	// When virtual camera is enabled with scene or source type, this callback is called twice or more.
	if (s->rendered)
		return;
	s->rendered = true;

	obs_source_t *target = obs_weak_source_get_source(s->weak_source);
	if (target) {
		cache_video(s, target);
		obs_source_release(target);
	}
}

#else

static void main_view_rendered_callback(void *data)
{
	struct main_view_s *s = data;

	if (!obs_source_showing(s->context))
		return;

	// When virtual camera is enabled with scene or source type, this callback is called twice or more.
	if (s->rendered)
		return;

	gs_texture_t *tex = obs_get_main_texture();
	if (!tex)
		return;

	s->rendered = true;
	cache_texture(s, tex);
}
#endif

static void video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct main_view_s *s = data;

	render_cached_video(s);
}

static uint32_t get_width(void *data)
{
	UNUSED_PARAMETER(data);

	struct obs_video_info ovi;
	if (!obs_get_video_info(&ovi))
		return 0;

	return ovi.base_width;
}

static uint32_t get_height(void *data)
{
	UNUSED_PARAMETER(data);

	struct obs_video_info ovi;
	if (!obs_get_video_info(&ovi))
		return 0;

	return ovi.base_height;
}

const struct obs_source_info main_view_source = {
	.id = ID_PREFIX "source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW,
	.get_name = get_name,
	.create = create,
	.destroy = destroy,
	.get_properties = get_properties,
	.update = update,
	.video_tick = video_tick,
	.video_render = video_render,
	.get_width = get_width,
	.get_height = get_height,
};
