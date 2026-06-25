cmake_minimum_required(VERSION 4.3.1)
project(atom)

enable_testing()

add_library(atom INTERFACE)
add_library(atom::atom ALIAS atom)

target_sources(atom INTERFACE
	FILE_SET HEADERS
	BASE_DIRS
		include
	FILES
		include/atom.hpp
)

if (BUILD_TESTING)
	add_subdirectory(test)
endif()

include(CMakePackageConfigHelpers)
install(TARGETS atom EXPORT atom-targets FILE_SET HEADERS DESTINATION include/atom)
install(EXPORT atom-targets FILE atom-targets.cmake NAMESPACE atom:: DESTINATION lib/cmake/atom)
configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/cmake/atom-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/atom-config.cmake"
    INSTALL_DESTINATION lib/cmake/atom
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/atom-config.cmake" DESTINATION lib/cmake/atom)
