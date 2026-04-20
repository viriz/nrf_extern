add_executable(nrfp-daemon
	src/main.c
)

target_include_directories(nrfp-daemon PRIVATE
	${CMAKE_SOURCE_DIR}/libnrfp/include
	${CMAKE_SOURCE_DIR}/../common/include
)

target_link_libraries(nrfp-daemon PRIVATE nrfp)

