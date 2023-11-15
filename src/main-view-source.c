#include <obs-module.h>
#include <util/threading.h>
#include <util/dstr.h>
#include "plugin-macros.generated.h"

struct main_view_s
{
	obs_source_t *context;
	bool rendered;

	obs_weak_source_t *weak_source;

	gs_texrender_t *texrender;
	gs_texrender_t *texrender_prev;

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

static void main_view_offscreen_render_cb(void *data, uint32_t cx, uint32_t cy);

static void register_offscreen_render(struct main_view_s *s)
{
	obs_add_main_render_callback(main_view_offscreen_render_cb, s);
}

static void unregister_offscreen_render(struct main_view_s *s)
{
	obs_remove_main_render_callback(main_view_offscreen_render_cb, s);
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

	obs_weak_source_release(s->weak_source);
	obs_enter_graphics();
	gs_texrender_destroy(s->texrender);
	gs_texrender_destroy(s->texrender_prev);
	obs_leave_graphics();
	if (s->offscreen_render)
		unregister_offscreen_render(s);

	bfree(s);
}

static void video_tick(void *data, float seconds)
{
	UNUSED_PARAMETER(seconds);
	struct main_view_s *s = data;

	obs_weak_source_release(s->weak_source);

	obs_source_t *target = obs_get_output_source(0);
	s->weak_source = obs_source_get_weak_source(target);
	obs_source_release(target);

	s->rendered = false;
}

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

static void render_cached_video(struct main_view_s *s)
{
	gs_texture_t *tex = gs_texrender_get_texture(s->texrender);
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
