add_library(test_pdb_target SHARED test_pdb_target.cpp)
target_compile_definitions(test_pdb_target PRIVATE TEST_PDB_EXPORTS)
target_compile_options(test_pdb_target PRIVATE /Oy- /Zi  $<$<COMPILE_LANGUAGE:CXX>:/EHa>)
target_link_options(test_pdb_target PRIVATE /DEBUG:FULL)

add_executable(test_pdb_runner test_pdb_runner.cpp)
target_compile_options(test_pdb_runner PRIVATE /Oy- /Zi $<$<COMPILE_LANGUAGE:CXX>:/EHa>)
target_link_options(test_pdb_runner PRIVATE /DEBUG:FULL)



add_library(test_etw_loader SHARED test_etw_loader.cpp)

