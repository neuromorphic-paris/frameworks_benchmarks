ADD_LIBRARY(dvsnoisefilter SHARED dvsnoisefilter.c)

SET_TARGET_PROPERTIES(dvsnoisefilter
	PROPERTIES
	PREFIX "caer_"
)

TARGET_LINK_LIBRARIES(dvsnoisefilter ${CAER_LIBS})

INSTALL(TARGETS dvsnoisefilter DESTINATION ${CAER_MODULES_DIR})
