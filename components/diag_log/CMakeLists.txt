idf_component_register(
    SRCS diag_log.c
    INCLUDE_DIRS include
    PRIV_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../third_party/denzic-platform/observability/embedded/c/include"
    REQUIRES diag_log_platform log
)
