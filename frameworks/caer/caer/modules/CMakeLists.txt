# Set full RPATH, modules are libraries for cAER
SET(CMAKE_INSTALL_RPATH ${CAER_MODULES_DIR})

# Add SDK library to default libs
SET(CAER_LIBS ${CAER_LIBS} caersdk)

ADD_SUBDIRECTORY(benchmark)
ADD_SUBDIRECTORY(cameracalibration)
ADD_SUBDIRECTORY(cameras)
ADD_SUBDIRECTORY(dvsnoisefilter)
ADD_SUBDIRECTORY(frameenhancer)
ADD_SUBDIRECTORY(framestatistics)
ADD_SUBDIRECTORY(inout)
ADD_SUBDIRECTORY(statistics)
ADD_SUBDIRECTORY(visualizer)
