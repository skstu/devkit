file(GLOB entries *)
foreach(entry ${entries})
 if(NOT (IS_DIRECTORY ${entry}) OR (NOT (EXISTS ${entry}/CMakeLists.txt)))
  continue()
 endif() 
 get_filename_component(entry_name "${entry}" NAME)
 #====================================================================================
 if(NOT ${entry_name} STREQUAL "test_win32")
 #====================================================================================
 #elseif(${entry_name} STREQUAL "asdgasdgasdg")
 # add_subdirectory(${entry})  
 else()
    add_subdirectory(${entry})
    set(project_folder_name "")
    GFN_GET_DIRECTORY_PREV_NAME(${entry} project_folder_name)
    set_target_properties(${entry_name} PROPERTIES FOLDER ${project_folder_name})
 endif()
 
endforeach(entry)