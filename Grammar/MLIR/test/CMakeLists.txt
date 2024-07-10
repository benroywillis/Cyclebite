configure_lit_site_cfg(
        ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
        ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py
        MAIN_CONFIG
        ${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py
)

set(CYCLEBITE_OPT_TEST_DEPENDS
        FileCheck count not
        cyclebite-opt
        )

add_lit_testsuite(check-cyclebite-opt "Running the opt regression tests"
        ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS ${CYCLEBITE_OPT_TEST_DEPENDS}
        )
set_target_properties(check-cyclebite-opt PROPERTIES FOLDER "Tests")

add_lit_testsuites(CYCLEBITE_OPT ${CMAKE_CURRENT_SOURCE_DIR} DEPENDS ${CYCLEBITE_OPT_TEST_DEPENDS})