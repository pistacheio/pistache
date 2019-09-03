
#-------------------------------------------------------------------------------
# Copyright (c) 2013-2013, Lars Baehren <lbaehren@gmail.com>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
#  * Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#-------------------------------------------------------------------------------

# - Check for the presence of RAPIDJSON
#
# The following variables are set when RAPIDJSON is found:
#  RAPIDJSON_FOUND      = Set to true, if all components of RapidJSON have been
#                         found.
#  RAPIDJSON_INCLUDES   = Include path for the header files of RAPIDJSON
#  RAPIDJSON_LIBRARIES  = Link these to use RAPIDJSON
#  RAPIDJSON_LFLAGS     = Linker flags (optional)

if (NOT RAPIDJSON_FOUND)

  if (NOT RAPIDJSON_ROOT_DIR)
    set (RAPIDJSON_ROOT_DIR ${CMAKE_INSTALL_PREFIX})
  endif (NOT RAPIDJSON_ROOT_DIR)

  ##____________________________________________________________________________
  ## Check for the header files

  find_path (RAPIDJSON_INCLUDES
    NAMES rapidjson/rapidjson.h rapidjson/reader.h rapidjson/writer.h
    HINTS ${RAPIDJSON_ROOT_DIR} ${CMAKE_INSTALL_PREFIX}
    PATH_SUFFIXES include
    )

  ##____________________________________________________________________________
  ## Actions taken when all components have been found

  include (FindPackageHandleStandardArgs)
  find_package_handle_standard_args (RAPIDJSON DEFAULT_MSG RAPIDJSON_INCLUDES)

  if (RAPIDJSON_FOUND)
    ## Update
    get_filename_component (RAPIDJSON_ROOT_DIR ${RAPIDJSON_INCLUDES} PATH)
    ## Feedback
    if (NOT RAPIDJSON_FIND_QUIETLY)
      message (STATUS "Found components for RapidJSON")
      message (STATUS "RAPIDJSON_ROOT_DIR  = ${RAPIDJSON_ROOT_DIR}")
      message (STATUS "RAPIDJSON_INCLUDES  = ${RAPIDJSON_INCLUDES}")
    endif (NOT RAPIDJSON_FIND_QUIETLY)
  else (RAPIDJSON_FOUND)
    if (RAPIDJSON_FIND_REQUIRED)
      message (FATAL_ERROR "Could not find RapidJSON!")
    endif (RAPIDJSON_FIND_REQUIRED)
  endif (RAPIDJSON_FOUND)

  ##____________________________________________________________________________
  ## Mark advanced variables

  mark_as_advanced (
    RAPIDJSON_ROOT_DIR
    RAPIDJSON_INCLUDES
    )

endif (NOT RAPIDJSON_FOUND)