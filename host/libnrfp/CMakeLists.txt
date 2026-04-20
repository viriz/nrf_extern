add_library(nrfp STATIC
	src/nrfp_client.c
	src/nrfp_transport.c
)

target_include_directories(nrfp PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${CMAKE_CURRENT_SOURCE_DIR}/../../common/include
)

enable_testing()
add_executable(nrfp-transport-test
	tests/nrfp_transport_test.c
)
target_include_directories(nrfp-transport-test PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${CMAKE_CURRENT_SOURCE_DIR}/../../common/include
)
target_link_libraries(nrfp-transport-test PRIVATE nrfp)
add_test(NAME nrfp_transport_test COMMAND nrfp-transport-test)
