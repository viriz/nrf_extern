add_library(nrfp STATIC
	src/nrfp_client.c
)

target_include_directories(nrfp PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${CMAKE_CURRENT_SOURCE_DIR}/../../common/include
)

