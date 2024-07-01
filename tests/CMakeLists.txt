file(GLOB entries *)
foreach(entry ${entries})
 if(NOT (IS_DIRECTORY ${entry}) OR (NOT (EXISTS ${entry}/CMakeLists.txt)))
  continue()
 endif() 
 get_filename_component(entry_name "${entry}" NAME)
 #====================================================================================
 if(NOT ${entry_name} STREQUAL "test_win32" AND 
 NOT ${entry_name} STREQUAL "test_ossl" AND
 NOT ${entry_name} STREQUAL "test_uiohook" AND
 NOT ${entry_name} STREQUAL "test_inspection")
 #====================================================================================
 #elseif(${entry_name} STREQUAL "asdgasdgasdg")
 # add_subdirectory(${entry})  
 else()
   GFN_SET_TARGET_OUTDIR(${SOLUTION_ROOT_DIR} "bin" "objs" "libs")
   add_subdirectory(${entry})
   set(project_folder_name "")
   GFN_GET_DIRECTORY_PREV_NAME(${entry} project_folder_name)
   set_target_properties(${entry_name} PROPERTIES FOLDER ${project_folder_name})
   GFN_SET_TARGET_OUTDIR("")
 endif()
 
endforeach(entry)