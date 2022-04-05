#include <obs-module.h>
#include <util/threading.h>
#include <util/dstr.h>
#include "plugin-macros.generated.h"

struct main_view_s
{
	obs_source_t *context;
	bool rendering;

	obs_weak_source_t *weak_source;

	gs_texrender_t *texrender;
	gs_texrender_t *texrender_prev;
	bool rendered;

	// properties
	bool cache;
	bool offsceeen_render;
};

static const char *get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("Main View Source");
}

static bool cache_modified(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	bool cache = obs_data_get_bool(settings, "cache");
	obs_property_t *offsceeen_render = obs_properties_get(props, "offsceeen_render");
	obs_property_set_enabled(offsceeen_render, cache);
	return true;
}

static obs_properties_t *get_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();
	obs_property_t *prop;

	prop = obs_properties_add_bool(props, "cache", obs_module_text("Cache the main view"));
	obs_properties_add_bool(props, "offsceeen_render", obs_module_text("Render before output/display rendering"));

	obs_property_set_modified_callback(prop, cache_modified);

	return props;
}

static void get_defaults(obs_data_t *defaults)
{
	obs_data_set_default_bool(defaults, "offsceeen_render", true);
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
	struct main_view_s *s = data;

	s->cache = obs_data_get_bool(settings, "cache");
	bool offsceeen_render = obs_data_get_bool(settings, "offsceeen_render") & s->cache;

	if (offsceeen_render && !s->offsceeen_render)
		register_offscreen_render(s);
	else if (!offsceeen_render && s->offsceeen_render)
		unregister_offscreen_render(s);
	s->offsceeen_render = offsceeen_render;
}

static void *create(obs_data_t *settings, obs_source_t *source)
{
	UNUSED_PARAMETER(settings);

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
	if (s->offsceeen_render)
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
		texrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
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

		s->rendered = true;
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

	/* If the source has already been rendered, for example by Source Record, do nothing. */
	if (s->rendered)
		return;

	if (!obs_source_showing(s->context))
		return;

	obs_source_t *target = obs_weak_source_get_source(s->weak_source);
	if (target) {
		s->rendering = true;
		cache_video(s, target);
		obs_source_release(target);
		s->rendering = false;
	}
}

static void video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct main_view_s *s = data;

	if (s->rendered) {
		render_cached_video(s);
		return;
	}

	bool cache = s->cache;
	if (s->rendering) {
		if (cache)
			render_cached_video(s);
		return;
	}

	obs_source_t *target = obs_weak_source_get_source(s->weak_source);
	if (target) {
		s->rendering = true;
		if (cache)
			cache_video(s, target);
		else
			obs_source_video_render(target);
		obs_source_release(target);
		s->rendering = false;
	}

	if (cache && s->rendered)
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
	.get_defaults = get_defaults,
	.update = update,
	.video_tick = video_tick,
	.video_render = video_render,
	.get_width = get_width,
	.get_height = get_height,
};
