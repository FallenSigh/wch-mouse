# Generates a disassembly listing for an ELF file.
#
# Invoked from CMakeLists.txt as a post-build step:
#   cmake -DOBJDUMP=<objdump> -DINPUT=<elf> -DOUTPUT=<lst> -P gen_listing.cmake
#
# Using execute_process (instead of a shell redirect) keeps this portable and
# safe against spaces/quotes in paths across Makefile and Ninja generators.

if(NOT OBJDUMP OR NOT INPUT OR NOT OUTPUT)
  message(FATAL_ERROR "gen_listing.cmake requires -DOBJDUMP=, -DINPUT= and -DOUTPUT=")
endif()

execute_process(
  COMMAND "${OBJDUMP}" --source --all-headers --demangle -M xw
          --line-numbers --wide "${INPUT}"
  OUTPUT_FILE "${OUTPUT}"
  RESULT_VARIABLE _result)

if(NOT _result EQUAL 0)
  message(FATAL_ERROR "objdump failed with ${_result} for ${INPUT}")
endif()
