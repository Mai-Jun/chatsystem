find_package(GTest REQUIRED)

add_executable(smoke_test smoke_test.cc)
target_link_libraries(smoke_test PRIVATE GTest::gtest GTest::gtest_main)
add_test(NAME smoke_test COMMAND smoke_test)
