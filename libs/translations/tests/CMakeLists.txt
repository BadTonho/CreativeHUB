set(TRANSLATION_TEST_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
set(TRANSLATION_TEST_BUNDLED_QM "${TRANSLATION_TEST_FIXTURE_DIR}/bundled_pt_BR.qm")
set(TRANSLATION_TEST_DOWNLOADED_QM "${TRANSLATION_TEST_FIXTURE_DIR}/downloaded_pt_BR.qm")
add_custom_command(
    OUTPUT "${TRANSLATION_TEST_BUNDLED_QM}" "${TRANSLATION_TEST_DOWNLOADED_QM}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${TRANSLATION_TEST_FIXTURE_DIR}"
    COMMAND "$<TARGET_FILE:Qt6::lrelease>"
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/bundled.ts"
        -qm "${TRANSLATION_TEST_BUNDLED_QM}"
    COMMAND "$<TARGET_FILE:Qt6::lrelease>"
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/downloaded.ts"
        -qm "${TRANSLATION_TEST_DOWNLOADED_QM}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/bundled.ts"
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/downloaded.ts"
    VERBATIM
)
add_custom_target(creative-suite-translation-test-fixtures
    DEPENDS "${TRANSLATION_TEST_BUNDLED_QM}" "${TRANSLATION_TEST_DOWNLOADED_QM}")

add_executable(creative-suite-translations-test translation_catalog_test.cpp)
add_dependencies(creative-suite-translations-test creative-suite-translation-test-fixtures)
target_link_libraries(creative-suite-translations-test PRIVATE
    creative-suite::translations
)
target_include_directories(creative-suite-translations-test PRIVATE
    "${PROJECT_SOURCE_DIR}/cmake/test_support"
)
target_compile_definitions(creative-suite-translations-test PRIVATE
    TRANSLATION_TEST_BUNDLED_QM="${TRANSLATION_TEST_BUNDLED_QM}"
    TRANSLATION_TEST_DOWNLOADED_QM="${TRANSLATION_TEST_DOWNLOADED_QM}"
    TRANSLATION_PUBLISHED_HUB_QM="${PROJECT_SOURCE_DIR}/languages/hub/pt_BR.qm"
    TRANSLATION_PUBLISHED_IMAGE_EDITOR_QM="${PROJECT_SOURCE_DIR}/languages/image-editor/pt_BR.qm"
    TRANSLATION_PUBLISHED_MOTION_EDITOR_QM="${PROJECT_SOURCE_DIR}/languages/motion-editor/pt_BR.qm"
    TRANSLATION_PUBLISHED_MANIFEST="${PROJECT_SOURCE_DIR}/languages/manifest.json"
)
target_compile_features(creative-suite-translations-test PRIVATE cxx_std_20)
add_creative_suite_test(
    NAME creative-suite-translations
    COMMAND creative-suite-translations-test)

if(TARGET creative-suite::translation-updater)
    add_executable(creative-suite-translation-updater-test translation_update_service_test.cpp)
    add_dependencies(creative-suite-translation-updater-test creative-suite-translation-test-fixtures)
    target_link_libraries(creative-suite-translation-updater-test PRIVATE
        creative-suite::translation-updater
    )
    target_include_directories(creative-suite-translation-updater-test PRIVATE
        "${PROJECT_SOURCE_DIR}/cmake/test_support"
    )
    target_compile_definitions(creative-suite-translation-updater-test PRIVATE
        TRANSLATION_TEST_BUNDLED_QM="${TRANSLATION_TEST_BUNDLED_QM}"
        TRANSLATION_TEST_DOWNLOADED_QM="${TRANSLATION_TEST_DOWNLOADED_QM}"
    )
    target_compile_features(creative-suite-translation-updater-test PRIVATE cxx_std_20)
    add_creative_suite_test(
        NAME creative-suite-translation-updater
        COMMAND creative-suite-translation-updater-test)
endif()

if(WIN32)
    get_target_property(QT_TRANSLATION_TEST_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_TRANSLATION_TEST_CORE_DLL)
        get_target_property(QT_TRANSLATION_TEST_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()
    get_filename_component(QT_TRANSLATION_TEST_BIN_DIR
        "${QT_TRANSLATION_TEST_CORE_DLL}" DIRECTORY)
    set_tests_properties(creative-suite-translations PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${QT_TRANSLATION_TEST_BIN_DIR}")
    if(TEST creative-suite-translation-updater)
        set_tests_properties(creative-suite-translation-updater PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${QT_TRANSLATION_TEST_BIN_DIR}")
    endif()
endif()
