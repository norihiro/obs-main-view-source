#include <obs-module.h>
#include <util/threading.h>
#include <util/dstr.h>
#include "plugin-macros.generated.h"

#define ASSERT_THREAD(type) \
	do { \
		if (!obs_in_task_thread(type)) \
			blog(LOG_ERROR, "%s: ASSERT_THREAD failed: Expected " #type, __func__); \
	} while (false)
#define ASSERT_THREAD2(type1, type2) \
	do { \
		if (!obs_in_task_thread(type1) && !obs_in_task_thread(type2)) \
			blog(LOG_ERROR, "%s: ASSERT_THREAD2 failed: Expected " #type1 " or " #type2, __func__); \
	} while (false)

enum source_type {
	source_type_video_only = 0,
	source_type_video_audio,
	source_type_audio_only,
};

struct main_view_s
{
	obs_source_t *context;
	bool rendered;
	bool offscreen_render;

	obs_weak_source_t *weak_source;

	gs_texrender_t *texrender;
	gs_texrender_t *texrender_prev;

	// audio properties
	size_t track;
	uint32_t audio_latency_ns;
};

static const char *get_name(void *type_data)
{
	switch ((enum source_type)type_data) {
	case source_type_video_only:
		return obs_module_text("Source.Name");
	case source_type_video_audio:
		return obs_module_text("VideoAudioSource.Name");
	case source_type_audio_only:
		return obs_module_text("AudioSource.Name");
	}
	return NULL;
}

static obs_properties_t *get_properties_video(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();

	return props;
}

static obs_properties_t *get_properties_audio(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();
	obs_property_t *prop;

	obs_properties_add_int(props, "track", obs_module_text("Prop.Audio.Track"), 1, MAX_AUDIO_MIXES, 1);
	prop = obs_properties_add_int(props, "audio_latency_ms", obs_module_text("Prop.Audio.Latency"), 0, 1000, 1);
	obs_property_int_set_suffix(prop, obs_module_text("Unit.ms"));

	return props;
}

static void main_view_offscreen_render_cb(void *data, uint32_t cx, uint32_t cy);

static void register_offscreen_render(struct main_view_s *s)
{
	// TODO: Use obs_add_main_rendered_callback instead
	// Then, call `obs_get_main_texture()` to get the texture, and just cache it.
	// See `decklink_ui_render`.
	obs_add_main_render_callback(main_view_offscreen_render_cb, s);
}

static void unregister_offscreen_render(struct main_view_s *s)
{
	obs_remove_main_render_callback(main_view_offscreen_render_cb, s);
}

static void update_video(void *data, obs_data_t *settings)
{
	ASSERT_THREAD2(OBS_TASK_UI, OBS_TASK_GRAPHICS);
	UNUSED_PARAMETER(settings);

	struct main_view_s *s = data;

	if (!s->offscreen_render)
		register_offscreen_render(s);
	s->offscreen_render = true;
}

void audio_cb(void *param, size_t mix_idx, struct audio_data *data);

void connect_audio(struct main_view_s *s, size_t track)
{
	ASSERT_THREAD(OBS_TASK_AUDIO);
	obs_add_raw_audio_callback(track - 1, NULL, audio_cb, s);
}

void disconnect_audio(struct main_view_s *s, size_t track)
{
	obs_remove_raw_audio_callback(track - 1, audio_cb, s);
}

struct update_audio_data {
	struct main_view_s *s;
	size_t track;
	uint32_t audio_latency_ns;
};

static void update_audio_internal(void *param)
{
	ASSERT_THREAD(OBS_TASK_AUDIO);
	struct update_audio_data *update_data = param;
	struct main_view_s *s = update_data->s;

	if (update_data->track != s->track) {
		if (s->track > 0)
			disconnect_audio(s, s->track);
		if (update_data->track > 0)
			connect_audio(s, update_data->track);
		s->track = update_data->track;
	}

	s->audio_latency_ns = update_data->audio_latency_ns;

	bfree(update_data);
	obs_source_release(s->context);
}

static void update_audio(void *data, obs_data_t *settings)
{
	ASSERT_THREAD2(OBS_TASK_UI, OBS_TASK_GRAPHICS);
	struct main_view_s *s = data;

	/* To avoid the member variables are updated while the audio thread is
	 * processing in audio_cb, update the settings in the audio thread.
	 * These two objects will be released inside the task.
	 * - update_data
	 * - s->context
	 */
	struct update_audio_data *update_data = bzalloc(sizeof(struct update_audio_data));

	update_data->s = s;

	update_data->track = (size_t)obs_data_get_int(settings, "track");
	if (update_data->track < 1)
		update_data->track = 1;
	else if (update_data->track > MAX_AUDIO_MIXES)
		update_data->track = MAX_AUDIO_MIXES;

	update_data->audio_latency_ns = (uint32_t)obs_data_get_int(settings, "audio_latency_ms") * 1000000;

	if (obs_source_get_ref(s->context))
		obs_queue_task(OBS_TASK_AUDIO, update_audio_internal, update_data, false);
	else
		bfree(update_data);
}

static void update_video_audio(void *data, obs_data_t *settings)
{
	update_video(data, settings);
	update_audio(data, settings);
}

static void *create(obs_data_t *settings, obs_source_t *source, enum source_type type)
{
	struct main_view_s *s = bzalloc(sizeof(struct main_view_s));

	s->context = source;

	switch (type) {
	case source_type_video_only:
		update_video(s, settings);
		break;
	case source_type_video_audio:
		update_video(s, settings);
		update_audio(s, settings);
		break;
	case source_type_audio_only:
		update_audio(s, settings);
		break;
	}

	return s;
}

static void *create_video(obs_data_t *settings, obs_source_t *source)
{
	return create(settings, source, source_type_video_only);
}

static void *create_video_audio(obs_data_t *settings, obs_source_t *source)
{
	return create(settings, source, source_type_video_audio);
}

static void *create_audio(obs_data_t *settings, obs_source_t *source)
{
	return create(settings, source, source_type_audio_only);
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

	if (s->track)
		disconnect_audio(s, s->track);

	bfree(s);
}

static void video_tick(void *data, float seconds)
{
	ASSERT_THREAD(OBS_TASK_GRAPHICS);
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
	ASSERT_THREAD(OBS_TASK_GRAPHICS);
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
	ASSERT_THREAD(OBS_TASK_GRAPHICS);
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
	ASSERT_THREAD(OBS_TASK_GRAPHICS);
	UNUSED_PARAMETER(cx);
	UNUSED_PARAMETER(cy);
	struct main_view_s *s = data;

	if (!obs_source_showing(s->context))
		return;

	// When virtual camera is enabled with scene or source type, this callback is called twice or more.
	if (s->rendered)
		return;
	s->rendered = true;

	// I don't think weak_source is necessay anymore.
	obs_source_t *target = obs_weak_source_get_source(s->weak_source);
	if (target) {
		cache_video(s, target);
		obs_source_release(target);
	}
}

static void video_render(void *data, gs_effect_t *effect)
{
	ASSERT_THREAD(OBS_TASK_GRAPHICS);
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

void audio_cb(void *param, size_t mix_idx, struct audio_data *data)
{
	ASSERT_THREAD(OBS_TASK_AUDIO);
	struct main_view_s *s = param;
	if (mix_idx != s->track - 1)
		return;

	struct obs_audio_info oai;
	if (!obs_get_audio_info(&oai))
		return;

	struct obs_source_audio audio = {
		.frames = data->frames,
		.speakers = oai.speakers,
		.format = AUDIO_FORMAT_FLOAT_PLANAR,
		.samples_per_sec = oai.samples_per_sec,
		.timestamp = data->timestamp + s->audio_latency_ns,
	};

	size_t nch = get_audio_channels(oai.speakers);
	for (size_t ch = 0; ch < nch; ch++)
		audio.data[ch] = data->data[ch];

	obs_source_output_audio(s->context, &audio);
}

const struct obs_source_info main_view_source = {
	.id = ID_PREFIX "source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.type_data = (void *)source_type_video_only,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW,
	.get_name = get_name,
	.create = create_video,
	.destroy = destroy,
	.get_properties = get_properties_video,
	.update = update_video,
	.video_tick = video_tick,
	.video_render = video_render,
	.get_width = get_width,
	.get_height = get_height,
};

const struct obs_source_info main_audio_source = {
	.id = ID_PREFIX "audio",
	.type = OBS_SOURCE_TYPE_INPUT,
	.type_data = (void *)source_type_audio_only,
	.output_flags = OBS_SOURCE_AUDIO,
	.get_name = get_name,
	.create = create_audio,
	.destroy = destroy,
	.get_properties = get_properties_audio,
	.update = update_audio,
};

const struct obs_source_info main_view_audio_source = {
	.id = ID_PREFIX "video-audio",
	.type = OBS_SOURCE_TYPE_INPUT,
	.type_data = (void *)source_type_video_audio,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_AUDIO,
	.get_name = get_name,
	.create = create_video_audio,
	.destroy = destroy,
	.get_properties = get_properties_audio, // No properties for video so far.
	.update = update_video_audio,
	.video_tick = video_tick,
	.video_render = video_render,
	.get_width = get_width,
	.get_height = get_height,
};
