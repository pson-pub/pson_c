

add_executable(pson_test pson_test.c)
target_link_libraries(pson_test pson_c)

add_test(NAME pson_test COMMAND pson_test)