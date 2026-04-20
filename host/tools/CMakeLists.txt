add_executable(nrfp-ping
	nrfp_ping.c
)

target_include_directories(nrfp-ping PRIVATE
	${CMAKE_SOURCE_DIR}/libnrfp/include
	${CMAKE_SOURCE_DIR}/../common/include
)

target_link_libraries(nrfp-ping PRIVATE nrfp)

