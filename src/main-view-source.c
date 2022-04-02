#include <obs-module.h>
#include <util/threading.h>
#include <util/dstr.h>
#include "plugin-macros.generated.h"

static const char *get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("Main View Source");
}

static void *create(obs_data_t *settings, obs_source_t *source)
{
	UNUSED_PARAMETER(settings);

	return source;
}

static void destroy(void *data) {}

static void video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(effect);

	obs_source_t *target = obs_get_output_source(0);
	if (target) {
		obs_source_video_render(target);
		obs_source_release(target);
	}
}

static uint32_t get_width(void *data)
{
	UNUSED_PARAMETER(data);

	uint32_t ret = 0;
	obs_source_t *target = obs_get_output_source(0);
	if (target) {
		ret = obs_source_get_width(target);
		obs_source_release(target);
	}

	return ret;
}

static uint32_t get_height(void *data)
{
	UNUSED_PARAMETER(data);

	uint32_t ret = 0;
	obs_source_t *target = obs_get_output_source(0);
	if (target) {
		ret = obs_source_get_height(target);
		obs_source_release(target);
	}

	return ret;
}

const struct obs_source_info main_view_source = {
	.id = ID_PREFIX "source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW,
	.get_name = get_name,
	.create = create,
	.destroy = destroy,
	.video_render = video_render,
	.get_width = get_width,
	.get_height = get_height,
};
