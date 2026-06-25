cmake_minimum_required(VERSION 3.31.2)
project(atom-test)

add_executable(atom-test)

target_sources(atom-test PRIVATE
	src/main.cpp
)

target_link_libraries(atom-test PRIVATE atom)

add_test(NAME atom-test COMMAND atom-test)
