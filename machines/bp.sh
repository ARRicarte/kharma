
# BP's machines

# TODO toolbox break to discover enclosing hostname
if [[ "$HOST" == "toolbox"* ]]; then
  HOST=ferrum
fi
if [[ "$HOST" == "e4s"* ]]; then
  HOST=fermium
fi

if [[ $HOST == "fermium" ]]; then
  HOST_ARCH="AMDAVX"
  DEVICE_ARCH="TURING75"
  KOKKOS_NUM_DEVICES=1
  MPI_NUM_PROCS=1

  if [[ "$ARGS" == *"cuda"* ]]; then
    module purge
    module load nvhpc
    PREFIX_PATH="$HOME/libs/hdf5-nvhpc"
    MPI_EXE=mpirun

    if [[ "$ARGS" == *"cgcc"* ]]; then
      C_NATIVE=cuda-gcc
      CXX_NATIVE=cuda-g++
    elif [[ "$ARGS" == *"gcc"* ]]; then
      C_NATIVE=gcc
      CXX_NATIVE=g++
    else
      # For NVC++ once that works again
      C_NATIVE=nvc
      CXX_NATIVE=nvc++
      export CFLAGS="-mp"
      export CXXFLAGS="-mp"
    fi
  else
    if [[ "$ARGS" == *"gcc"* ]]; then
      module purge
      module load mpi/mpich-x86_64
      C_NATIVE=gcc
      CXX_NATIVE=g++
    elif [[ "$ARGS" == *"clang"* ]]; then
      module purge
      module load mpi/mpich-x86_64
      C_NATIVE=clang
      CXX_NATIVE=clang++
      PREFIX_PATH="$HOME/libs/hdf5-clang14"
    else
      module purge
      module load mpi/mpich-x86_64
      source /opt/AMD/aocc-compiler-3.2.0/setenv_AOCC.sh
      PREFIX_PATH="$HOME/libs/hdf5-aocc"
      C_NATIVE=clang
      CXX_NATIVE=clang++
    fi
  fi

  # My CUDA installs are a bit odd
  #EXTRA_FLAGS="-DKokkos_ENABLE_LIBDL=OFF $EXTRA_FLAGS"
fi

if [[ $HOST == "ferrum" ]]; then
  if [[ "$ARGS" == *"gcc"* ]]; then
    module load mpi/mpich-x86_64
    C_NATIVE="gcc"
    CXX_NATIVE="g++"
  else
    # Intel SYCL implementation "DPC++"
    module purge
    module load compiler mpi
    PREFIX_PATH="$HOME/libs/hdf5-oneapi"
  fi

  NPROC=6 # My kingdom for a RAM!

  HOST_ARCH="HSW"
  DEVICE_ARCH="INTEL_GEN"

  EXTRA_FLAGS="-DFUSE_FLUX_KERNELS=OFF -DFUSE_EMF_KERNELS=OFF -DFUSE_FLOOR_KERNELS=OFF $EXTRA_FLAGS"
fi

if [[ $HOST == "cinnabar"* ]]; then
  module purge # Handle modules inside this script
  # All my MPI stacks can use this as the call
  MPI_EXE=mpirun

  if [[ "$ARGS" == *"cuda"* ]]; then
    # Use NVHPC libraries (GPU-aware OpenMPI!)
    HOST_ARCH="HSW"
    DEVICE_ARCH="KEPLER35"
    MPI_NUM_PROCS=2
    KOKKOS_NUM_DEVICES=2
    MPI_EXTRA_ARGS="--map-by ppr:1:numa:pe=14"

    # Quash warning about my old gpus
    export NVCC_WRAPPER_CUDA_EXTRA_FLAGS="-Wno-deprecated-gpu-targets"
    # System CUDA path
    EXTRA_FLAGS="-DCUDAToolkit_INCLUDE_DIR=/usr/include/cuda $EXTRA_FLAGS"

    # Switch between g++/NVC++:
    if [[ "$ARGS" == *"gcc"* ]]; then
      module load mpi/mpich-x86_64 nvhpc-nompi
      C_NATIVE="gcc"
      CXX_NATIVE="g++"
    else
      module load nvhpc
      PREFIX_PATH="$HOME/libs/hdf5-nvhpc"
      C_NATIVE="nvc"
      CXX_NATIVE="nvc++"
      export CXXFLAGS="-mp"
    fi
  else
    HOST_ARCH="HSW"
    MPI_NUM_PROCS=1
    if [[ "$ARGS" == *"gcc"* ]]; then
      # GCC
      module load mpi/mpich-x86_64
      C_NATIVE="gcc"
      CXX_NATIVE="g++"
    else
      # Intel by default
      module load compiler mpi
      PREFIX_PATH="$HOME/libs/hdf5-oneapi"
    fi
  fi
fi
