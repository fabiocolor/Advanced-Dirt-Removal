#!/bin/sh
# Build the Linux release tarball inside a manylinux_2_28 container (AlmaLinux 8,
# glibc 2.28), so the plugin loads on Rocky/Alma Linux 8 and later and on
# Ubuntu 20.04 and later. Needs Docker. CUDA 13 kernels are compiled with the
# NVIDIA nvcc Python wheels inside the container.
#   sh installers/linux/build_portable.sh [version]
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
VERSION=${1:-2.0.0}
IMAGE=quay.io/pypa/manylinux_2_28_x86_64
docker run --rm -v "$ROOT:/src" -w /src -e VERSION="$VERSION" -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" "$IMAGE" bash -c '
  set -e
  PY=/opt/python/cp312-cp312/bin/python
  $PY -m pip install -q cmake ninja "nvidia-cuda-nvcc>=13,<14" "nvidia-cuda-runtime>=13,<14" "nvidia-cuda-crt>=13,<14"
  export CUDA_PATH=$($PY -c "import nvidia.cu13; print(list(nvidia.cu13.__path__)[0])")
  export PATH=/opt/python/cp312-cp312/bin:$CUDA_PATH/bin:$PATH CMAKE_GENERATOR=Ninja
  sh installers/linux/build_tarball.sh "$VERSION" -DADR_WITH_CUDA=ON -DADR_WITH_OPENCL=ON
  chown -R "$HOST_UID:$HOST_GID" build installers/linux/build'
