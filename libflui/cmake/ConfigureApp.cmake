# SDK-owned renderer policy for the locked Flutter macOS engine. Keep clients
# independent of Flutter configuration. Preserve all other application metadata.
if(NOT EXISTS "${APP_PLIST}")
  message(FATAL_ERROR "Missing application Info.plist: ${APP_PLIST}")
endif()
execute_process(COMMAND /usr/libexec/PlistBuddy -c "Set :FLTEnableImpeller false" "${APP_PLIST}"
  RESULT_VARIABLE configured OUTPUT_QUIET ERROR_QUIET)
if(NOT configured EQUAL 0)
  execute_process(COMMAND /usr/libexec/PlistBuddy -c "Add :FLTEnableImpeller bool false" "${APP_PLIST}"
    RESULT_VARIABLE configured OUTPUT_QUIET ERROR_QUIET)
endif()
if(NOT configured EQUAL 0)
  message(FATAL_ERROR "Could not apply libflui renderer policy")
endif()
