file(GLOB entries *)
foreach(entry ${entries})
 if(NOT (IS_DIRECTORY ${entry}) OR (NOT (EXISTS ${entry}/CMakeLists.txt)))
  continue()
 endif()
 #====================================================================================
 get_filename_component(entry_name "${entry}" NAME)
 #====================================================================================
 if(${entry_name} STREQUAL "inspection")
    add_subdirectory(${entry})
 #====================================================================================
 elseif(${entry_name} STREQUAL "win32" OR 
 ${entry_name} STREQUAL "stl" OR
 ${entry_name} STREQUAL "ossl")
 GFN_SET_TARGET_OUTDIR(${SOLUTION_ROOT_DIR} "bin" "objs" "libs")
 add_subdirectory(${entry})
 set(project_folder_name "")
 GFN_GET_DIRECTORY_PREV_NAME(${entry} project_folder_name)
 set_target_properties(${entry_name} PROPERTIES FOLDER ${project_folder_name})
 GFN_SET_TARGET_OUTDIR("")
 else()

 endif()
 
endforeach(entry)


