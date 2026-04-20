add_library(nrfp_lib_compat INTERFACE)

target_include_directories(nrfp_lib_compat INTERFACE
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${CMAKE_CURRENT_SOURCE_DIR}/../../common/include
)
