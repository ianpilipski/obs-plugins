#include <obs-module.h>
#include <graphics/matrix4.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>

/* clang-format off */

#define SETTING_OPACITY                "opacity"
#define SETTING_CONTRAST               "contrast"
#define SETTING_BRIGHTNESS             "brightness"
#define SETTING_GAMMA                  "gamma"
#define SETTING_SIMILARITY             "similarity"
#define SETTING_SMOOTHNESS             "smoothness"
#define SETTING_SPILL                  "spill"
#define SETTING_CAPTURE_BACKGROUND     "capture_background"

#define TEXT_OPACITY                   obs_module_text("Opacity")
#define TEXT_CONTRAST                  obs_module_text("Contrast")
#define TEXT_BRIGHTNESS                obs_module_text("Brightness")
#define TEXT_GAMMA                     obs_module_text("Gamma")
#define TEXT_SIMILARITY                obs_module_text("Similarity")
#define TEXT_SMOOTHNESS                obs_module_text("Smoothness")
#define TEXT_SPILL                     obs_module_text("ColorSpillReduction")
#define TEXT_CAPTURE_BACKGROUND        obs_module_text("CaptureBackground")


/* clang-format on */

struct background_key_filter_data {
	obs_source_t *context;

	gs_effect_t *effect;

	gs_eparam_t *color_param;
	gs_eparam_t *contrast_param;
	gs_eparam_t *brightness_param;
	gs_eparam_t *gamma_param;

	gs_eparam_t *pixel_size_param;
	gs_eparam_t *background_param;
	gs_eparam_t *similarity_param;
	gs_eparam_t *smoothness_param;
	gs_eparam_t *spill_param;

	struct vec4 color;
	float contrast;
	float brightness;
	float gamma;

	float similarity;
	float smoothness;
	float spill;

	bool process_frames;
	uint32_t process_framesCount;
	gs_texrender_t *frame;
	gs_texture_t *frameTexture;
	gs_texrender_t *frameMin;
	gs_texrender_t *frameMax;
	bool target_valid;
	uint32_t cx;
	uint32_t cy;
};

static const char *background_key_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("BackgroundKeyFilter");
}

/*
static const float yuv_mat[16] = {0.182586f, -0.100644f, 0.439216f,  0.0f,
				  0.614231f, -0.338572f, -0.398942f, 0.0f,
				  0.062007f, 0.439216f,  -0.040274f, 0.0f,
				  0.062745f, 0.501961f,  0.501961f,  1.0f};
*/

static inline void color_settings_update(struct background_key_filter_data *filter,
					 obs_data_t *settings)
{
	uint32_t opacity = (uint32_t)obs_data_get_int(settings, SETTING_OPACITY);
	uint32_t color = 0xFFFFFF | (((opacity * 255) / 100) << 24);
	double contrast = obs_data_get_double(settings, SETTING_CONTRAST);
	double brightness = obs_data_get_double(settings, SETTING_BRIGHTNESS);
	double gamma = obs_data_get_double(settings, SETTING_GAMMA);

	contrast = (contrast < 0.0) ? (1.0 / (-contrast + 1.0)) : (contrast + 1.0);

	brightness *= 0.5;

	gamma = (gamma < 0.0) ? (-gamma + 1.0) : (1.0 / (gamma + 1.0));

	filter->contrast = (float)contrast;
	filter->brightness = (float)brightness;
	filter->gamma = (float)gamma;

	vec4_from_rgba(&filter->color, color);
}

static inline void background_settings_update(struct background_key_filter_data *filter, obs_data_t *settings)
{
	int64_t similarity = obs_data_get_int(settings, SETTING_SIMILARITY);
	int64_t smoothness = obs_data_get_int(settings, SETTING_SMOOTHNESS);
	int64_t spill = obs_data_get_int(settings, SETTING_SPILL);

	//TODO: background color...

	filter->similarity = (float)similarity / 1000.0f;
	filter->smoothness = (float)smoothness / 1000.0f;
	filter->spill = (float)spill / 1000.0f;
}

static void free_textures(struct background_key_filter_data *filter)
{
	if(filter->frame) {
		obs_enter_graphics();
		gs_texrender_destroy(filter->frame);
		gs_texture_destroy(filter->frameTexture);
		obs_leave_graphics();
	}
	filter->frame = NULL;
}

static inline bool check_size(struct background_key_filter_data *filter)
{
	obs_source_t *target = obs_filter_get_target(filter->context);
	uint32_t cx;
	uint32_t cy;

	filter->target_valid = !!target;
	if (!filter->target_valid)
		return true;

	cx = obs_source_get_base_width(target);
	cy = obs_source_get_base_height(target);

	filter->target_valid = !!cx && !!cy;
	if (!filter->target_valid)
		return true;

	if (cx != filter->cx || cy != filter->cy) {
		filter->cx = cx;
		filter->cy = cy;
		free_textures(filter);
		obs_enter_graphics();
		filter->frame = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		filter->frameTexture = gs_texture_create(cx, cy, GS_RGBA, 1, NULL, GS_DYNAMIC);
		obs_leave_graphics();
		printf("Image is cx=%d cy=%d\n", cx, cy);
		return true;
	}

	return false;
}

static void background_key_update(void *data, obs_data_t *settings)
{
	struct background_key_filter_data *filter = data;

	color_settings_update(filter, settings);
	background_settings_update(filter, settings);
}

static void background_key_destroy(void *data)
{
	struct background_key_filter_data *filter = data;

	free_textures(filter);

	if (filter->effect) {
		obs_enter_graphics();
		gs_effect_destroy(filter->effect);
		obs_leave_graphics();
	}

	bfree(data);
}

static void *background_key_create(obs_data_t *settings, obs_source_t *context)
{
	struct background_key_filter_data *filter =	bzalloc(sizeof(struct background_key_filter_data));
	filter->process_frames = false;
	filter->frame = NULL;

	char *effect_path = obs_module_file("background_key_filter.effect");

	filter->context = context;

	obs_enter_graphics();

	filter->effect = gs_effect_create_from_file(effect_path, NULL);
	if (filter->effect) {
		filter->color_param      = gs_effect_get_param_by_name(filter->effect, "color");
		filter->contrast_param   = gs_effect_get_param_by_name(filter->effect, "contrast");
		filter->brightness_param = gs_effect_get_param_by_name(filter->effect, "brightness");
		filter->gamma_param      = gs_effect_get_param_by_name(filter->effect, "gamma");
		filter->background_param = gs_effect_get_param_by_name(filter->effect, "background");
		filter->pixel_size_param = gs_effect_get_param_by_name(filter->effect, "pixel_size");
		filter->similarity_param = gs_effect_get_param_by_name(filter->effect, "similarity");
		filter->smoothness_param = gs_effect_get_param_by_name(filter->effect, "smoothness");
		filter->spill_param      = gs_effect_get_param_by_name(filter->effect, "spill");
	}

	obs_leave_graphics();

	bfree(effect_path);

	if (!filter->effect) {
		background_key_destroy(filter);
		return NULL;
	}

	background_key_update(filter, settings);
	return filter;
}

static void background_key_render_capture(struct background_key_filter_data *filter, gs_effect_t *effect) {
	UNUSED_PARAMETER(effect);

	obs_source_t *target = obs_filter_get_target(filter->context);
	obs_source_t *parent = obs_filter_get_parent(filter->context);

	if (!filter->target_valid || !target || !parent || !filter->frame) {
		obs_source_skip_video_filter(filter->context);
		return;
	}

	gs_texrender_t *tempTex = filter->frame;// gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	gs_texrender_reset(tempTex);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

	if (gs_texrender_begin(tempTex, filter->cx, filter->cy)) {
		uint32_t parent_flags = obs_source_get_output_flags(target);
		bool custom_draw = (parent_flags & OBS_SOURCE_CUSTOM_DRAW) != 0;
		bool async = (parent_flags & OBS_SOURCE_ASYNC) != 0;
		struct vec4 clear_color;

		vec4_zero(&clear_color);
		gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
		gs_ortho(0.0f, (float)filter->cx, 0.0f, (float)filter->cy, -100.0f, 100.0f);

		if (target == parent && !custom_draw && !async)
			obs_source_default_render(target);
		else
			obs_source_video_render(target);

		gs_texrender_end(tempTex);
	}
	gs_blend_state_pop();

	uint8_t *texData;
	uint32_t linesize;
	gs_texture_t *tempTexture = gs_texrender_get_texture(tempTex);

	if(false && gs_texture_map(tempTexture, &texData, &linesize)) {
		if(filter->process_framesCount==0) {
			uint8_t *frameTexData;
			uint32_t frameLineSize;
			gs_texture_t *frameTex = filter->frameTexture;
			if(gs_texture_map(frameTex, &frameTexData, &frameLineSize)) {
				memcpy(frameTexData, texData, filter->cy * frameLineSize);
				gs_texture_unmap(frameTex);
			} else {
				// fail this
			}
		} else {
			// averge out
		}
		gs_texture_unmap(tempTexture);
	}

	if(filter->process_framesCount++ > 30) {
		filter->process_frames = false;
	}
}

static void draw_frame(struct background_key_filter_data *f)
{
	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_texture_t *tex = gs_texrender_get_texture(f->frame);
	if (tex) {
		gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
		gs_effect_set_texture(image, tex);

		while (gs_effect_loop(effect, "Draw")) {
			gs_draw_sprite(tex, 0, f->cx, f->cy);
		}
	}
}

static void background_key_render(void *data, gs_effect_t *effect)
{
	struct background_key_filter_data *filter = data;
	if(filter->process_frames) {
		background_key_render_capture(filter, effect);
		return;
	}

	obs_source_t *target = obs_filter_get_target(filter->context);
	uint32_t width = obs_source_get_base_width(target);
	uint32_t height = obs_source_get_base_height(target);
	struct vec2 pixel_size;

	if (!obs_source_process_filter_begin(filter->context, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING))
		return;

	gs_texture_t *tex = gs_texrender_get_texture(filter->frame);
	//gs_texture_t *tex = filter->frameTexture;

	//vec2_set(&pixel_size, 1.0f / (float)width, 1.0f / (float)height);
	vec2_set(&pixel_size, 1.0f / (float)filter->cx, 1.0f / (float)filter->cy);

	gs_effect_set_vec4(filter->color_param, &filter->color);
	gs_effect_set_float(filter->contrast_param, filter->contrast);
	gs_effect_set_float(filter->brightness_param, filter->brightness);
	gs_effect_set_float(filter->gamma_param, filter->gamma);
	gs_effect_set_texture(filter->background_param, tex);
	gs_effect_set_vec2(filter->pixel_size_param, &pixel_size);
	gs_effect_set_float(filter->similarity_param, filter->similarity);
	gs_effect_set_float(filter->smoothness_param, filter->smoothness);
	gs_effect_set_float(filter->spill_param, filter->spill);

	obs_source_process_filter_end(filter->context, filter->effect, 0, 0);

	//UNUSED_PARAMETER(effect);
}

static bool background_key_capture_clicked(obs_properties_t *props, obs_property_t *property, void *data) {
	struct background_key_filter_data *filter = data;

	filter->process_frames = true;
	filter->process_framesCount = 0;
	check_size(filter);

	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	return true;
}

static obs_properties_t *background_key_getproperties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_button(props, SETTING_CAPTURE_BACKGROUND, TEXT_CAPTURE_BACKGROUND, background_key_capture_clicked);

	obs_properties_add_int_slider(props, SETTING_SIMILARITY, TEXT_SIMILARITY, 0, 1000, 1);
	obs_properties_add_int_slider(props, SETTING_SMOOTHNESS, TEXT_SMOOTHNESS, 1, 1000, 1);
	obs_properties_add_int_slider(props, SETTING_SPILL, TEXT_SPILL, 1, 1000, 1);

	obs_properties_add_int_slider(props, SETTING_OPACITY, TEXT_OPACITY, 0, 100, 1);
	obs_properties_add_float_slider(props, SETTING_CONTRAST, TEXT_CONTRAST, -1.0, 1.0, 0.01);
	obs_properties_add_float_slider(props, SETTING_BRIGHTNESS, TEXT_BRIGHTNESS, -1.0, 1.0, 0.01);
	obs_properties_add_float_slider(props, SETTING_GAMMA, TEXT_GAMMA, -1.0, 1.0, 0.01);

	UNUSED_PARAMETER(data);
	return props;
}


static void background_key_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, SETTING_OPACITY, 100);
	obs_data_set_default_double(settings, SETTING_CONTRAST, 0.0);
	obs_data_set_default_double(settings, SETTING_BRIGHTNESS, 0.0);
	obs_data_set_default_double(settings, SETTING_GAMMA, 0.0);
	obs_data_set_default_int(settings, SETTING_SIMILARITY, 400);
	obs_data_set_default_int(settings, SETTING_SMOOTHNESS, 80);
	obs_data_set_default_int(settings, SETTING_SPILL, 100);
}

static void background_key_filter_video_tick(void *data, float t) {
	//struct background_key_filter_data *filter = data;
	//check_size(filter);
	UNUSED_PARAMETER(t);
	UNUSED_PARAMETER(data);
}

struct obs_source_info background_key_filter = {
	.id = "background_key_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = background_key_name,
	.create = background_key_create,
	.destroy = background_key_destroy,
	.video_render = background_key_render,
	.video_tick = background_key_filter_video_tick,
	.update = background_key_update,
	.get_properties = background_key_getproperties,
	.get_defaults = background_key_defaults,
};
