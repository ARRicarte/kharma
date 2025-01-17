#!/bin/bash

# Make script for KHARMA
# Used to decide flags and call cmake
# Usage:
# ./make.sh [option1] [option2]

# clean: BUILD by re-running cmake, restarting the make process from nothing.
#        That is, "./make.sh clean" == "make clean" + "make"
#        Always use 'clean' when switching Release<->Debug or OpenMP<->CUDA
# cuda:  Build for GPU with CUDA. Must have 'nvcc' in path
# sycl:  Build for GPU with SYCL. Must have 'icpx' in path
# debug: Configure with debug flags: mostly array bounds checks
#        Note, though, many sanity checks during the run are
#        actually *runtime* parameters e.g. verbose, flag_verbose, etc
# trace: Configure with execution tracing: print at the beginning and end
#        of most host-side function calls during a step
# skx:   Compile specifically for Skylake nodes on Stampede2

# Processors to use.  Leave blank for all.  Be a good citizen.
NPROC=

### Load machine-specific configurations ###
# This segment sources a series of machine-specific
# definitions from the machines/ directory.
# If the current machine isn't listed, this script
# and/or Kokkos will attempt to guess the host architecture,
# which should suffice to compile but may not provide optimal
# performance.

# See e.g. tacc.sh for an example to get started writing one,
# or specify any options you need manually below

# Example Kokkos_ARCH options:
# CPUs: WSM, HSW, BDW, SKX, KNL, AMDAVX, ZEN2, ZEN3, POWER9
# ARM: ARMV80, ARMV81, ARMV8_THUNDERX2, A64FX
# GPUs: KEPLER35, VOLTA70, TURING75, AMPERE80, INTEL_GEN

# HOST_ARCH=
# DEVICE_ARCH=
# C_NATIVE=
# CXX_NATIVE=

# Less common options:
# PREFIX_PATH=
# EXTRA_FLAGS=

HOST=$(hostname -f)
ARGS="$*"
for machine in machines/*.sh
do
  source $machine
done

# If we haven't special-cased already, guess an architecture
# This ends up fine on most x86 architectures
# Exceptions:
# 1. GPUs, obviously
# 2. AVX512 (Intel on HPC or Gen10+ consumer)
# 3. AMD EPYC Zen2, Zen3
# However, you may have better luck commenting these tests and letting Kokkos decide
if [[ -z "$HOST_ARCH" ]]; then
  if grep GenuineIntel /proc/cpuinfo >/dev/null 2>&1; then
    HOST_ARCH="HSW"
  fi
  if grep AuthenticAMD /proc/cpuinfo >/dev/null 2>&1; then
    HOST_ARCH="AMDAVX"
  fi
fi

# Add some flags only if they're set
if [[ -v HOST_ARCH ]]; then
  EXTRA_FLAGS="-DKokkos_ARCH_${HOST_ARCH}=ON $EXTRA_FLAGS"
fi
if [[ -v DEVICE_ARCH ]]; then
  EXTRA_FLAGS="-DKokkos_ARCH_${DEVICE_ARCH}=ON $EXTRA_FLAGS"
fi
if [[ -v PREFIX_PATH ]]; then
  EXTRA_FLAGS="-DCMAKE_PREFIX_PATH=$PREFIX_PATH $EXTRA_FLAGS"
fi
if [[ "$ARGS" == *"trace"* ]]; then
  EXTRA_FLAGS="-DTRACE=1 $EXTRA_FLAGS"
fi

### Enivoronment Prep ###
if [[ "$(which python3 2>/dev/null)" == *"conda"* ]]; then
  echo "It looks like you have Anaconda loaded."
  echo "Anaconda forces a serial version of HDF5 which may make this compile impossible."
  echo "If you run into trouble, deactivate your environment with 'conda deactivate'"
fi
# Save arguments
echo "$ARGS" > make_args
# Choose configuration
if [[ "$ARGS" == *"debug"* ]]; then
  TYPE=Debug
else
  TYPE=Release
fi

### Build HDF5 ###
if [[ "$ARGS" == *"hdf5"* ]]; then
  cd external
  if [ ! -f hdf5-* ]; then
    wget https://hdf-wordpress-1.s3.amazonaws.com/wp-content/uploads/manual/HDF5/HDF5_1_12_0/source/hdf5-1.12.0.tar.gz
    tar xf hdf5-1.12.0.tar.gz
  fi
  cd hdf5-1.12.0/
  make clean
  CC=mpicc ./configure --enable-parallel --prefix=$PWD/../hdf5
  make -j$NPROC
  make install
  make clean
  exit
fi

### Build KHARMA ###
SCRIPT_DIR=$( dirname "$0" )
cd $SCRIPT_DIR
SCRIPT_DIR=$PWD

# Strongly prefer icc for OpenMP compiles
# I would try clang but it would break all Macs
if [[ -z "$CXX_NATIVE" ]]; then
  if which icpx >/dev/null 2>&1; then
    CXX_NATIVE=icpx
    C_NATIVE=icx
  elif which icpc >/dev/null 2>&1; then
    CXX_NATIVE=icpc
    C_NATIVE=icc
    # Avoid warning on nvcc pragmas Intel doesn't like
    export CXXFLAGS="-Wno-unknown-pragmas $CXXFLAGS"
    #export CFLAGS="-qopenmp"
  elif which CC >/dev/null 2>&1; then
    CXX_NATIVE=CC
    C_NATIVE=cc
    #export CXXFLAGS="-Wno-unknown-pragmas" # TODO if Cray->Intel in --version
  elif which xlC >/dev/null 2>&1; then
    CXX_NATIVE=xlC
    C_NATIVE=xlc
  else
    CXX_NATIVE=g++
    C_NATIVE=gcc
  fi
fi

# CUDA loop options: MANUAL1D_LOOP > MDRANGE_LOOP, TPTTR_LOOP & TPTTRTVR_LOOP don't compile
# Inner loop must be TVR_INNER_LOOP
# OpenMP loop options for KNL:
# Outer: SIMDFOR_LOOP;MANUAL1D_LOOP;MDRANGE_LOOP;TPTTR_LOOP;TPTVR_LOOP;TPTTRTVR_LOOP
# Inner: SIMDFOR_INNER_LOOP;TVR_INNER_LOOP
if [[ "$ARGS" == *"sycl"* ]]; then
  export CXX=icpx
  export CC=icx
  OUTER_LAYOUT="MANUAL1D_LOOP"
  INNER_LAYOUT="TVR_INNER_LOOP"
  ENABLE_OPENMP="ON"
  ENABLE_CUDA="OFF"
  ENABLE_SYCL="ON"
  ENABLE_HIP="OFF"
elif [[ "$ARGS" == *"hip"* ]]; then
  export CXX=hipcc
  # Is there a hipc?
  OUTER_LAYOUT="MANUAL1D_LOOP"
  INNER_LAYOUT="TVR_INNER_LOOP"
  ENABLE_OPENMP="ON"
  ENABLE_CUDA="OFF"
  ENABLE_SYCL="OFF"
  ENABLE_HIP="ON"
elif [[ "$ARGS" == *"cuda"* ]]; then
  export CC="$C_NATIVE"
  export CXX="$SCRIPT_DIR/bin/nvcc_wrapper"
  export NVCC_WRAPPER_DEFAULT_COMPILER="$CXX_NATIVE"
  if [[ "$ARGS" == *"dryrun"* ]]; then
    export CXXFLAGS="-dryrun $CXXFLAGS"
    echo "Dry-running with $CXXFLAGS"
  fi
  export NVCC_WRAPPER_DEFAULT_COMPILER="$CXX_NATIVE"
  # I've occasionally needed this. CUDA version thing?
  #export CXXFLAGS="--expt-relaxed-constexpr $CXXFLAGS"
  OUTER_LAYOUT="MANUAL1D_LOOP"
  INNER_LAYOUT="TVR_INNER_LOOP"
  ENABLE_OPENMP="ON"
  ENABLE_CUDA="ON"
  ENABLE_SYCL="OFF"
  ENABLE_HIP="OFF"
elif [[ "$ARGS" == *"clanggpu"* ]]; then
  export CXX="clang++"
  export CC="clang"
  OUTER_LAYOUT="MANUAL1D_LOOP"
  INNER_LAYOUT="TVR_INNER_LOOP"
  ENABLE_OPENMP="ON"
  ENABLE_CUDA="ON"
  ENABLE_SYCL="OFF"
  ENABLE_HIP="OFF"
else
  export CXX="$CXX_NATIVE"
  export CC="$C_NATIVE"
  OUTER_LAYOUT="MDRANGE_LOOP"
  INNER_LAYOUT="SIMDFOR_INNER_LOOP"
  ENABLE_OPENMP="ON"
  ENABLE_CUDA="OFF"
  ENABLE_SYCL="OFF"
  ENABLE_HIP="OFF"
fi

# 
if [[ -v LINKER ]]; then
  LINKER="$LINKER"
else
  LINKER="$CXX"
fi

# Make build dir. Recall "clean" means "clean and build"
if [[ "$ARGS" == *"clean"* ]]; then
  rm -rf build
fi
mkdir -p build
cd build

if [[ "$ARGS" == *"clean"* ]]; then
#set -x
  cmake ..\
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_CXX_COMPILER="$CXX" \
    -DCMAKE_LINKER="$LINKER" \
    -DCMAKE_CXX_LINK_EXECUTABLE='<CMAKE_LINKER> <FLAGS> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>' \
    -DCMAKE_PREFIX_PATH="$PREFIX_PATH:$CMAKE_PREFIX_PATH" \
    -DCMAKE_BUILD_TYPE=$TYPE \
    -DPAR_LOOP_LAYOUT=$OUTER_LAYOUT \
    -DPAR_LOOP_INNER_LAYOUT=$INNER_LAYOUT \
    -DKokkos_ENABLE_OPENMP=$ENABLE_OPENMP \
    -DKokkos_ENABLE_CUDA=$ENABLE_CUDA \
    -DKokkos_ENABLE_SYCL=$ENABLE_SYCL \
    -DKokkos_ENABLE_HIP=$ENABLE_HIP \
    $EXTRA_FLAGS
#set +x
fi

make -j$NPROC

cp kharma/kharma.* ..
