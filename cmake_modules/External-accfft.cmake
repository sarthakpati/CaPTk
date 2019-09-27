#MESSAGE( "External project - accfft" )

# accfft depends on fftw
SET( ACCFFT_DEPENDENCIES )

SET(CMAKE_CXX_STANDARD 11)
SET(CMAKE_CXX_STANDARD_REQUIRED YES) 

find_package(MPI REQUIRED)

include_directories(${MPI_INCLUDE_PATH})

if(MPI_COMPILE_FLAGS)
  SET(COMPILE_FLAGS "${COMPILE_FLAGS} ${MPI_COMPILE_FLAGS}" )
endif()

if(MPI_LINK_FLAGS)
  SET(LINK_FLAGS "${LINK_FLAGS} ${MPI_LINK_FLAGS}" )
endif()

#MESSAGE( STATUS "Adding accfft-${PETSC_VERSION} ...")
ExternalProject_Add(
    accfft
    SOURCE_DIR ${CMAKE_BINARY_DIR}/accfft-src
    BINARY_DIR ${CMAKE_BINARY_DIR}/accfft-build
    GIT_REPOSITORY https://github.com/amirgholami/accfft.git
    GIT_TAG origin/master
    # BUILD_COMMAND make
    INSTALL_COMMAND make install
    CMAKE_ARGS 
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}/accfft-build 
        -DFFTW_ROOT=${FFTW_DIR} 
        -DFFTW_USE_STATIC_LIBS=true 
        -DBUILD_GPU=false 
        -DBUILD_SHARED=false 
)

SET( ACCFFT_DIR ${CMAKE_BINARY_DIR}/accfft-build)
#LIST(APPEND CMAKE_PREFIX_PATH "${CMAKE_BINARY_DIR}/OpenCV-build")
SET( ENV{CMAKE_PREFIX_PATH} "${CMAKE_PREFIX_PATH};${CMAKE_BINARY_DIR}/accfft-build" )
