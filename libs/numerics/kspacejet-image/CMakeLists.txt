ksj_include_project_bootstrap()

find_package(Intel CONFIG REQUIRED)
find_package(Eigen3 CONFIG REQUIRED)
find_package(OpenCV CONFIG REQUIRED)
find_package(ITK CONFIG REQUIRED COMPONENTS ITKLabelMap ITKSmoothing)

set(PROJ_NAME ksj_image)
set(KSJ_IMAGE_PRIVATE_BINARY_DEPENDENCIES
    # ITKLabelMap covers connected-components and label processing; ITKSmoothing
    # provides the Gaussian and unsharp filters used by the foreground-mask backend.
    ITK::ITKLabelMap
    ITK::ITKSmoothing
    Intel::ipps
    Intel::ippi
    Intel::ippcv
    Intel::ippcore
    # Keep the OpenCV surface exact. opencv_video intentionally brings its own calib3d/DNN closure for findTransformECC;
    # xphoto supplies BM3D.
    opencv_core
    opencv_imgcodecs
    opencv_imgproc
    opencv_photo
    opencv_video
    opencv_xphoto)

add_library(
  ${PROJ_NAME} SHARED
  "${CMAKE_CURRENT_LIST_DIR}/src/eigen/eigen_image_basic.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/eigen/eigen_image_thresholds.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/eigen/eigen_image_hole_filling.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/eigen/eigen_image_regions.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/eigen/eigen_image_components.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/eigen/eigen_image_interpolation.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/eigen/eigen_image_filters.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/eigen/eigen_image_morphology.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/intel/intel_image_filters.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/intel/intel_image_interpolation.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/intel/intel_image_regions.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/intel/intel_image_thresholds.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/intel/intel_image_volume.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/itk/itk_image_foreground_mask.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/opencv/opencv_image_drawing.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/opencv/opencv_image_io.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/opencv/opencv_image_registration.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/opencv/opencv_image_thresholds.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/opencv/opencv_image_resize.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/opencv/opencv_image_interpolation.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/opencv/opencv_image_filters.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/opencv/opencv_image_components.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/opencv/opencv_image_morphology.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/opencv/opencv_image_denoise.cpp")
add_library(KSpaceJet::image ALIAS ${PROJ_NAME})

target_compile_features(${PROJ_NAME} PUBLIC cxx_std_20)
target_link_libraries(
  ${PROJ_NAME}
  PRIVATE KSpaceJet::compile_options KSpaceJet::shared_library_link_options KSpaceJet::intel_compat_definitions
          Eigen3::Eigen Intel::headers ${KSJ_IMAGE_PRIVATE_BINARY_DEPENDENCIES}
  PUBLIC KSpaceJet::array KSpaceJet::linalg KSpaceJet::stats)

# A shared library records its private backends in DT_NEEDED (or its Windows import graph), but a downstream executable
# still needs CMake's complete imported-target link closure while it is being linked.  LINK_ONLY preserves the private
# header/API boundary while propagating that closure on Linux and Windows, including the Conan package search paths and
# runtime DLL graph.
foreach(_ksj_image_private_binary_dependency IN LISTS KSJ_IMAGE_PRIVATE_BINARY_DEPENDENCIES)
  target_link_libraries(${PROJ_NAME} INTERFACE "$<LINK_ONLY:${_ksj_image_private_binary_dependency}>")
endforeach()

target_include_directories(${PROJ_NAME} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
set_target_properties(
  ${PROJ_NAME}
  PROPERTIES CXX_EXTENSIONS NO
             POSITION_INDEPENDENT_CODE ON
             CXX_VISIBILITY_PRESET default
             VISIBILITY_INLINES_HIDDEN OFF
             WINDOWS_EXPORT_ALL_SYMBOLS ON)
ksj_set_library_runtime_path(${PROJ_NAME})
ksj_install_target(${PROJ_NAME})
