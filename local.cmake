# Bundle ID override for notarization/distribution
set_target_properties(${PROJECT_NAME} PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.annolab.GvEncoder"
)

# ofxExtremeGpuVideo dependencies (squish + lz4, headers only for GV format)
set(OFX_EXTREME_GPU_VIDEO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ofxExtremeGpuVideo"
    CACHE PATH "Path to ofxExtremeGpuVideo addon")

set(GV_SQUISH_DIR "${OFX_EXTREME_GPU_VIDEO_DIR}/libs/squish-1.11_lib")
set(GV_LZ4_DIR "${OFX_EXTREME_GPU_VIDEO_DIR}/libs/lz4/lib")

file(GLOB GV_SQUISH_SOURCES "${GV_SQUISH_DIR}/*.cpp")
set(GV_LZ4_SOURCES
    "${GV_LZ4_DIR}/lz4.c"
    "${GV_LZ4_DIR}/lz4hc.c"
    "${GV_LZ4_DIR}/xxhash.c"
)
set(GV_READER_SOURCES
    "${OFX_EXTREME_GPU_VIDEO_DIR}/src/GpuVideoReader.cpp"
)

target_sources(${PROJECT_NAME} PRIVATE ${GV_SQUISH_SOURCES} ${GV_LZ4_SOURCES} ${GV_READER_SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE
    "${OFX_EXTREME_GPU_VIDEO_DIR}/src"
    "${GV_SQUISH_DIR}"
    "${GV_LZ4_DIR}"
)
