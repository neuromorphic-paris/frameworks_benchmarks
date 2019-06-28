INCLUDE_DIRECTORIES(${CAER_INCDIRS})
LINK_DIRECTORIES(${CAER_LIBDIRS} ${CMAKE_CURRENT_SOURCE_DIR})

ADD_LIBRARY(benchmark_reader SHARED reader/interface.c reader/source.cpp reader/wrapper.cpp)
SET_TARGET_PROPERTIES(benchmark_reader PROPERTIES PREFIX "caer_")
TARGET_LINK_LIBRARIES(benchmark_reader ${CAER_LIBS})
INSTALL(TARGETS benchmark_reader DESTINATION ${CAER_MODULES_DIR})

ADD_LIBRARY(benchmark_sink SHARED sink/interface.c sink/source.cpp sink/wrapper.cpp)
SET_TARGET_PROPERTIES(benchmark_sink PROPERTIES PREFIX "caer_")
TARGET_LINK_LIBRARIES(benchmark_sink ${CAER_LIBS} benchmark_reader)
INSTALL(TARGETS benchmark_sink DESTINATION ${CAER_MODULES_DIR})

ADD_LIBRARY(benchmark_sink_latencies SHARED sink_latencies/interface.c sink_latencies/source.cpp sink_latencies/wrapper.cpp)
SET_TARGET_PROPERTIES(benchmark_sink_latencies PROPERTIES PREFIX "caer_")
TARGET_LINK_LIBRARIES(benchmark_sink_latencies ${CAER_LIBS} benchmark_reader_latencies)
INSTALL(TARGETS benchmark_sink_latencies DESTINATION ${CAER_MODULES_DIR})

ADD_LIBRARY(benchmark_flow_sink SHARED flow_sink/interface.c flow_sink/source.cpp flow_sink/wrapper.cpp)
SET_TARGET_PROPERTIES(benchmark_flow_sink PROPERTIES PREFIX "caer_")
TARGET_LINK_LIBRARIES(benchmark_flow_sink ${CAER_LIBS} benchmark_reader)
INSTALL(TARGETS benchmark_flow_sink DESTINATION ${CAER_MODULES_DIR})

ADD_LIBRARY(benchmark_flow_sink_latencies SHARED flow_sink_latencies/interface.c flow_sink_latencies/source.cpp flow_sink_latencies/wrapper.cpp)
SET_TARGET_PROPERTIES(benchmark_flow_sink_latencies PROPERTIES PREFIX "caer_")
TARGET_LINK_LIBRARIES(benchmark_flow_sink_latencies ${CAER_LIBS} benchmark_reader_latencies)
INSTALL(TARGETS benchmark_flow_sink_latencies DESTINATION ${CAER_MODULES_DIR})

ADD_LIBRARY(benchmark_activity_sink SHARED activity_sink/interface.c activity_sink/source.cpp activity_sink/wrapper.cpp)
SET_TARGET_PROPERTIES(benchmark_activity_sink PROPERTIES PREFIX "caer_")
TARGET_LINK_LIBRARIES(benchmark_activity_sink ${CAER_LIBS} benchmark_reader)
INSTALL(TARGETS benchmark_activity_sink DESTINATION ${CAER_MODULES_DIR})

ADD_LIBRARY(benchmark_activity_sink_latencies SHARED activity_sink_latencies/interface.c activity_sink_latencies/source.cpp activity_sink_latencies/wrapper.cpp)
SET_TARGET_PROPERTIES(benchmark_activity_sink_latencies PROPERTIES PREFIX "caer_")
TARGET_LINK_LIBRARIES(benchmark_activity_sink_latencies ${CAER_LIBS} benchmark_reader_latencies)
INSTALL(TARGETS benchmark_activity_sink_latencies DESTINATION ${CAER_MODULES_DIR})

ADD_LIBRARY(benchmark_reader_latencies SHARED reader_latencies/interface.c reader_latencies/source.cpp reader_latencies/wrapper.cpp)
SET_TARGET_PROPERTIES(benchmark_reader_latencies PROPERTIES PREFIX "caer_")
TARGET_LINK_LIBRARIES(benchmark_reader_latencies ${CAER_LIBS})
INSTALL(TARGETS benchmark_reader_latencies DESTINATION ${CAER_MODULES_DIR})

ADD_LIBRARY(benchmark_select_rectangle SHARED select_rectangle/interface.c select_rectangle/source.cpp select_rectangle/wrapper.cpp)
SET_TARGET_PROPERTIES(benchmark_select_rectangle PROPERTIES PREFIX "caer_")
TARGET_LINK_LIBRARIES(benchmark_select_rectangle ${CAER_LIBS})
INSTALL(TARGETS benchmark_select_rectangle DESTINATION ${CAER_MODULES_DIR})

ADD_LIBRARY(benchmark_mask_isolated SHARED mask_isolated/interface.c mask_isolated/source.cpp mask_isolated/wrapper.cpp)
SET_TARGET_PROPERTIES(benchmark_mask_isolated PROPERTIES PREFIX "caer_")
TARGET_LINK_LIBRARIES(benchmark_mask_isolated ${CAER_LIBS})
INSTALL(TARGETS benchmark_mask_isolated DESTINATION ${CAER_MODULES_DIR})

ADD_LIBRARY(benchmark_compute_flow SHARED compute_flow/interface.c compute_flow/source.cpp compute_flow/wrapper.cpp)
SET_TARGET_PROPERTIES(benchmark_compute_flow PROPERTIES PREFIX "caer_")
TARGET_LINK_LIBRARIES(benchmark_compute_flow ${CAER_LIBS})
INSTALL(TARGETS benchmark_compute_flow DESTINATION ${CAER_MODULES_DIR})

ADD_LIBRARY(benchmark_split SHARED split/interface.c split/source.cpp split/wrapper.cpp)
SET_TARGET_PROPERTIES(benchmark_split PROPERTIES PREFIX "caer_")
TARGET_LINK_LIBRARIES(benchmark_split ${CAER_LIBS})
INSTALL(TARGETS benchmark_split DESTINATION ${CAER_MODULES_DIR})

ADD_LIBRARY(benchmark_compute_activity SHARED compute_activity/interface.c compute_activity/source.cpp compute_activity/wrapper.cpp)
SET_TARGET_PROPERTIES(benchmark_compute_activity PROPERTIES PREFIX "caer_")
TARGET_LINK_LIBRARIES(benchmark_compute_activity ${CAER_LIBS})
INSTALL(TARGETS benchmark_compute_activity DESTINATION ${CAER_MODULES_DIR})
