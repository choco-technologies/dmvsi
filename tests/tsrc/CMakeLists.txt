# ======================================================================
#               dmvs_tsrc - a converter plugin for the tests
# ======================================================================
# "TSRC" files: a line per shape (see tsrc.c).
set(DMOD_MODULE_NAME        dmvs_tsrc)
set(DMOD_MODULE_VERSION     ${PROJECT_VERSION})
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         1024)
set(DMOD_PRIORITY           0)
set(DMOD_DIF_IMPLS          dmvsi)
unset(DMOD_DMR_PATH)            # a test module, not a release package

dmod_add_library(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    tsrc.c
)

target_link_libraries(${DMOD_MODULE_NAME} dmvsi_if)
