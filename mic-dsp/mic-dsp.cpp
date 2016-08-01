#include <obs-module.h>

OBS_DECLARE_MODULE()

OBS_MODULE_USE_DEFAULT_LOCALE("mic-dsp", "en-US")

extern struct obs_source_info noise_suppress_filter;

bool obs_module_load(void)
{
    obs_register_source(&noise_suppress_filter);
    return true;
}
