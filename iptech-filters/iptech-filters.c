#include <obs-module.h>
#include "iptech-filters-config.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("iptech-filters", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
	return "IPTech filters";
}

extern struct obs_source_info background_key_filter;

bool obs_module_load(void)
{
	obs_register_source(&background_key_filter);
	return true;
}

