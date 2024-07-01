file(GLOB entries *)
foreach(entry ${entries})
 if(NOT (IS_DIRECTORY ${entry}) OR (NOT (EXISTS ${entry}/CMakeLists.txt)))
  continue()
 endif()
 #====================================================================================
 get_filename_component(entry_name "${entry}" NAME)
 #====================================================================================
 if(${entry_name} STREQUAL "automation" OR 
 ${entry_name} STREQUAL "capture" OR 
 ${entry_name} STREQUAL "cvpp" OR 
 ${entry_name} STREQUAL "overlay" OR
 ${entry_name} STREQUAL "perform" OR
 ${entry_name} STREQUAL "input")
 GFN_SET_TARGET_OUTDIR(${SOLUTION_ROOT_DIR} "bin" "objs" "libs")
 add_subdirectory(${entry})
 set(project_folder_name "")
 GFN_GET_TARGET_PROJECT_RELATIVE_PATHNAME(${entry} project_folder_name)
 set_target_properties(${entry_name} PROPERTIES FOLDER ${project_folder_name})
 GFN_SET_TARGET_OUTDIR("")
 #====================================================================================
 elseif(${entry_name} STREQUAL "___backup__")

 else()

 endif()
 
endforeach(entry)