function(drishti_set_unit_test_assets
    DRISHTI_ACF_FACE_MODEL
    DRISHTI_FACE_LANDMARKER
    DRISHTI_EYE_MODEL
    DRISHTI_MEAN_FACE_LANDMARKS
    )

  if(NOT DRISHTI_SERIALIZE_WITH_CEREAL)
    message(FATAL_ERROR "Unit tests currently require cereal ")
  endif()

  # re-export variables from drishti-assets package for initial testing
  # DRISHTI_ASSETS_FACE_DETECTOR 
  # DRISHTI_ASSETS_FACE_LANDMARK_REGRESSOR
  # DRISHTI_ASSETS_FACE_DETECTOR_MEAN
  # DRISHTI_ASSETS_EYE_MODEL_REGRESSOR 

  ### DRISHTI_ACF_FACE_MODEL
  set(file "${DRISHTI_ASSETS_FACE_DETECTOR}")
  set("${DRISHTI_ACF_FACE_MODEL}" "${file}" PARENT_SCOPE)

  if(NOT EXISTS "${file}")
    message(FATAL_ERROR "File not found: '${file}'")
  endif()

  ### DRISHTI_FACE_LANDMARKER
  set(file "${DRISHTI_ASSETS_FACE_LANDMARK_REGRESSOR}")
  set("${DRISHTI_FACE_LANDMARKER}" "${file}" PARENT_SCOPE)

  if(NOT EXISTS "${file}")
    message(FATAL_ERROR "File not found: '${file}'")
  endif()

  ### DRISHTI_EYE_MODEL
  set(file "${DRISHTI_ASSETS_EYE_MODEL_REGRESSOR}")
  set("${DRISHTI_EYE_MODEL}" "${file}" PARENT_SCOPE)

  if(NOT EXISTS "${file}")
    message(FATAL_ERROR "File not found: '${file}'")
  endif()

  ### DRISHTI_MEAN_FACE_LANDMARKS
  set(file "${DRISHTI_ASSETS_FACE_DETECTOR_MEAN}")
  set("${DRISHTI_MEAN_FACE_LANDMARKS}" "${file}" PARENT_SCOPE)

  if(NOT EXISTS "${file}")
    message(FATAL_ERROR "File not found: '${file}'")
  endif()
endfunction()
