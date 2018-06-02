set(CMAKE_CXX_STANDARD 17)

add_executable(lru_test lru_test.cpp)
target_link_libraries(lru_test ${CONAN_LIBS})
add_test(NAME LRUTest COMMAND lru_test)

add_executable(mru_test mru_test.cpp)
target_link_libraries(mru_test ${CONAN_LIBS})
add_test(NAME MRUTest COMMAND mru_test)
