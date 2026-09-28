add_executable(creative-suite-animation-tests
    animation_test.cpp
)
target_link_libraries(creative-suite-animation-tests PRIVATE creative-suite::animation)
target_compile_features(creative-suite-animation-tests PRIVATE cxx_std_20)
add_test(NAME creative-suite-animation COMMAND creative-suite-animation-tests)

add_executable(creative-suite-composition-tests
    composition_test.cpp
)
target_link_libraries(creative-suite-composition-tests PRIVATE creative-suite::composition)
target_compile_features(creative-suite-composition-tests PRIVATE cxx_std_20)
add_test(NAME creative-suite-composition COMMAND creative-suite-composition-tests)

foreach(shared_test IN ITEMS
        creative-suite-animation-tests
        creative-suite-composition-tests)
    if(MSVC)
        target_compile_options(${shared_test} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${shared_test} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
