project(iptech-filters)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/iptech-filters-config.h.in"
	"${CMAKE_BINARY_DIR}/plugins/iptech-filters/config/iptech-filters-config.h")

set(iptech-filters_config_HEADERS
	"${CMAKE_BINARY_DIR}/plugins/iptech-filters/config/iptech-filters-config.h")
include_directories("${CMAKE_BINARY_DIR}/plugins/iptech-filters/config")

if(MSVC)
	set(iptech-filters_PLATFORM_DEPS
		w32-pthreads)
endif()

set(iptech-filters_SOURCES
	iptech-filters.c
	background-key-filter.c)

add_library(iptech-filters MODULE
	${iptech-filters_SOURCES}
	${iptech-filters_config_HEADERS})

target_link_libraries(iptech-filters
	libobs
	${iptech-filters_PLATFORM_DEPS})

install_obs_plugin_with_data(iptech-filters data)
