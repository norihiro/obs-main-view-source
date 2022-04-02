#include <obs-module.h>
#include <util/threading.h>
#include <util/dstr.h>
#include "plugin-macros.generated.h"

struct main_view_s
{
	bool rendering;
	bool getting_size;

	obs_weak_source_t *weak_source;
	uint32_t last_width;
	uint32_t last_height;
};

static const char *get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("Main View Source");
}

static void *create(obs_data_t *settings, obs_source_t *source)
{
	UNUSED_PARAMETER(settings);
	UNUSED_PARAMETER(source);

	struct main_view_s *s = bzalloc(sizeof(struct main_view_s));

	return s;
}

static void destroy(void *data)
{
	struct main_view_s *s = data;

	obs_weak_source_release(s->weak_source);

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
}

static void video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct main_view_s *s = data;

	if (s->rendering)
		return;

	obs_source_t *target = obs_weak_source_get_source(s->weak_source);
	if (target) {
		s->rendering = true;
		obs_source_video_render(target);
		obs_source_release(target);
		s->rendering = false;
	}
}

static uint32_t get_width(void *data)
{
	struct main_view_s *s = data;

	if (s->getting_size)
		return s->last_width;

	obs_source_t *target = obs_weak_source_get_source(s->weak_source);
	if (target) {
		s->getting_size = true;
		s->last_width = obs_source_get_width(target);
		obs_source_release(target);
		s->getting_size = false;
	}

	return s->last_width;
}

static uint32_t get_height(void *data)
{
	struct main_view_s *s = data;

	if (s->getting_size)
		return s->last_height;

	obs_source_t *target = obs_weak_source_get_source(s->weak_source);
	if (target) {
		s->getting_size = true;
		s->last_height = obs_source_get_height(target);
		obs_source_release(target);
		s->getting_size = false;
	}

	return s->last_height;
}

const struct obs_source_info main_view_source = {
	.id = ID_PREFIX "source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW,
	.get_name = get_name,
	.create = create,
	.destroy = destroy,
	.video_tick = video_tick,
	.video_render = video_render,
	.get_width = get_width,
	.get_height = get_height,
};
