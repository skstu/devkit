file(GLOB entries *)
foreach(entry ${entries})
 if(NOT (IS_DIRECTORY ${entry}) OR (NOT (EXISTS ${entry}/CMakeLists.txt)))
  continue()
 endif() 
 get_filename_component(entry_name "${entry}" NAME)
 #====================================================================================
 if(${entry_name} STREQUAL "main_")
 #====================================================================================
 elseif(${entry_name} STREQUAL "database") 
 #====================================================================================
 elseif(${entry_name} STREQUAL "net") 
 #====================================================================================
 elseif(${entry_name} STREQUAL "pipe")
 #====================================================================================
 elseif(${entry_name} STREQUAL "process")
  add_subdirectory(${entry})  
 else()
 add_subdirectory(${entry})
 set(target_project_relative_path_name "")
 GLOBAL_GET_TARGET_PROJECT_RELATIVE_PATHNAME(${entry} target_project_relative_path_name)
 set_target_properties(${entry_name} PROPERTIES FOLDER ${target_project_relative_path_name})
 endif()
 
endforeach(entry)