#include <cstdint>
#include <limits>
#include <memory>

#include <obs-module.h>
#include <include/speex/speex_preprocess.h>

#define do_log(level, format, ...) \
	blog(level, "[noise suppress: '%s'] " format, \
			obs_source_get_name(ng->context), ##__VA_ARGS__)

#define warn(format, ...)  do_log(LOG_WARNING, format, ##__VA_ARGS__)
#define info(format, ...)  do_log(LOG_INFO,    format, ##__VA_ARGS__)

#ifdef _DEBUG
#define debug(format, ...) do_log(LOG_DEBUG,   format, ##__VA_ARGS__)
#else
#define debug(format, ...)
#endif

#define S_SUPPRESS_LEVEL               "suppress_level"

#define MT_ obs_module_text
#define TEXT_SUPPRESS_LEVEL            MT_("NoiseSuppress.SuppressLevel")

#define MAX_PREPROC_CHANNELS 2

struct noise_suppress_data {
    obs_source_t *context;

    // Speex preprocessor state
    SpeexPreprocessState* state[MAX_PREPROC_CHANNELS] = { nullptr, nullptr };

    // 16 bit PCM buffers
    std::unique_ptr<spx_int16_t[]> segment_buffer[MAX_PREPROC_CHANNELS];

    int suppress_level;
};

#define SUP_MIN -60
#define SUP_MAX 0

static constexpr float c_32_to_16 = (float)std::numeric_limits<spx_int16_t>::max();
static constexpr float c_16_to_32 = ((float)std::numeric_limits<spx_int16_t>::max() + 1.0f);

static const char *noise_suppress_name(void *unused)
{
    UNUSED_PARAMETER(unused);
    return obs_module_text("NoiseSuppress");
}

static void noise_suppress_destroy(void *data)
{
    noise_suppress_data *ng = static_cast<struct noise_suppress_data*>(data);

    for (auto &i : ng->state)
    {
        if (i != nullptr)
        {
            speex_preprocess_state_destroy(i);
            i = nullptr;
        }
    }

    for (auto &j : ng->segment_buffer)
    {
        j.reset();
    }

    delete ng;
}

static void noise_suppress_update(void *data, obs_data_t *s)
{
    noise_suppress_data *ng = static_cast<struct noise_suppress_data*>(data);

    int suppress_level = (int) obs_data_get_int(s, S_SUPPRESS_LEVEL);
    uint32_t sample_rate = audio_output_get_sample_rate(obs_get_audio());
    uint32_t segment_size = sample_rate / 100;
    size_t channels = audio_output_get_channels(obs_get_audio());

    ng->suppress_level = suppress_level;

    debug("channels = %u", channels);
    debug("sample_rate = %u", sample_rate);
    debug("segment_size = %u", segment_size);
    debug("block size = %u", audio_output_get_block_size(obs_get_audio()));

    // One speex state for each channel (limit 2)
    if (!ng->state[0])
    {
        debug("Create Channel 1 Speex state");
        ng->state[0] = speex_preprocess_state_init(segment_size, sample_rate);

        if (!ng->segment_buffer[0])
        {
            ng->segment_buffer[0].reset(new spx_int16_t[segment_size]);
            debug("Create Channel 1 Speex buffer: size = %u", segment_size);
        }
    }

    if (channels > 1)
    {
        if (!ng->state[1])
        {
            debug("Create Channel 2 Speex state");
            ng->state[1] = speex_preprocess_state_init(segment_size, sample_rate);

            if (!ng->segment_buffer[1])
            {
                ng->segment_buffer[1].reset(new spx_int16_t[segment_size]);
                debug("Create Channel 2 Speex buffer: size = %u", segment_size);
            }
        }
    }
}

static void *noise_suppress_create(obs_data_t *settings, obs_source_t *filter)
{
    noise_suppress_data *ng = new noise_suppress_data();
    ng->context = filter;
    noise_suppress_update(ng, settings);
    return ng;
}

static struct obs_audio_data *noise_suppress_filter_audio(void *data,
    struct obs_audio_data *audio)
{
    noise_suppress_data *ng = static_cast<struct noise_suppress_data*>(data);

    float *adata[2] = { (float*)audio->data[0], (float*)audio->data[1] };

    // Execute for each available channel
    for (size_t i = 0; i < MAX_PREPROC_CHANNELS; i++)
    {
        if (ng->state[i] != nullptr)
        {
            // Set args
            speex_preprocess_ctl(ng->state[i], SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &ng->suppress_level);

            // Convert to 16bit
            for (size_t j = 0; j < audio->frames; j++)
            {
                ng->segment_buffer[i][j] = (spx_int16_t) (adata[i][j] * c_32_to_16);
            }

            // Execute
            speex_preprocess_run(ng->state[i], ng->segment_buffer[i].get());

            // Convert back to 32bit
            for (size_t j = 0; j < audio->frames; j++)
            {
                adata[i][j] = ng->segment_buffer[i][j] / c_16_to_32;
            }
        }
    }

    return audio;
}

static void noise_suppress_defaults(obs_data_t *s)
{
    obs_data_set_default_int(s, S_SUPPRESS_LEVEL, -30);
}

static obs_properties_t *noise_suppress_properties(void *data)
{
    obs_properties_t *ppts = obs_properties_create();

    obs_properties_add_int_slider(ppts, S_SUPPRESS_LEVEL, TEXT_SUPPRESS_LEVEL, SUP_MIN, SUP_MAX, 0);

    UNUSED_PARAMETER(data);
    return ppts;
}

struct obs_source_info noise_suppress_filter = {
    .id = "noise_suppress_filter",
    .type = OBS_SOURCE_TYPE_FILTER,
    .output_flags = OBS_SOURCE_AUDIO,
    .get_name = noise_suppress_name,
    .create = noise_suppress_create,
    .destroy = noise_suppress_destroy,
    .update = noise_suppress_update,
    .filter_audio = noise_suppress_filter_audio,
    .get_defaults = noise_suppress_defaults,
    .get_properties = noise_suppress_properties,
};
