# Why build libtorch from source?
# I hit the end of the road in trying to use the nightly distro of LibTorch, when I got
# the following linker errors:
#   undefined reference to `torch::jit::load(std::__cxx11::basic_string<char.....
# That looks like a smoking gun saying that libtorch was compiled with -D_GLIBCXX_USE_CXX11_ABI=0
# So instead of switching all of our dependencies to use -D_GLIBCXX_USE_CXX11_ABI=0, I
# have chosen instead to build libtorch from source.
# See https://github.com/pytorch/pytorch/issues/17492

FROM nvidia/cuda:10.0-cudnn7-devel-ubuntu18.04

# This container builds libtorch (PyTorch C++)
# Other containers that depend on libtorch can just copy out files from here.
# The result of the build is just a bunch of .h, .so, and .a files.

RUN apt-get update \
 && apt-get upgrade -y \
 && apt-get install -y \
	curl \
	g++ \
	yasm \
	git \
	make \
	cmake \
 && rm -rf /var/lib/apt/lists/*

# Install Anaconda
RUN curl -so ~/anaconda.sh https://repo.anaconda.com/archive/Anaconda3-2018.12-Linux-x86_64.sh \
 && chmod +x ~/anaconda.sh \
 && ~/anaconda.sh -b -p ~/conda \
 && rm ~/anaconda.sh

# This is the only way I know of to "activate" a conda environment inside a Dockerfile, and
# then use that environment. You have to do it at the start of the command sequence.
RUN . ~/conda/bin/activate \
 && conda install numpy pyyaml mkl mkl-include setuptools cmake cffi typing \
 && conda install -c pytorch magma-cuda100

# This branch here is just a totally random branch that I know was working for me.
# This was the latest master, on 2019-03-30
RUN git clone --recursive https://github.com/pytorch/pytorch ~/pytorch \
 && git -C ~/pytorch checkout 96456bfa4cf9394c9c926b143cf724a09901908d \
 && git -C ~/pytorch submodule update --recursive

# This takes at least one hour on a 4 core machine
RUN . ~/conda/bin/activate \
 && cd ~/pytorch \
 && CMAKE_PREFIX_PATH=${CONDA_PREFIX:-"$(dirname $(which conda))/../"} \
 && CUDA_HOME=/usr/local/cuda-10.0 \
 && CUDNN_INCLUDE_DIR=/usr/include\
 && CUDNN_LIB_DIR=/usr/lib/x86_64-linux-gnu \
 && CUDNN_LIBRARY=/usr/lib/x86_64-linux-gnu/libcudnn.so.7 \
 && BUILD_TORCH=ON \
 && USE_CUDA=ON \
 && USE_CUDNN=ON \
 && USE_NNPACK=ON \
 && TORCH_CUDA_ARCH_LIST="3.5 5.2 6.0 6.1 7.0+PTX" \
 && TORCH_NVCC_FLAGS="-Xfatbin -compress-all" \
 && python setup.py install

FROM ubuntu:18.04
COPY --from=0 /root/conda/lib/libmkl_intel_lp64.so /usr/local/libtorch/lib/
COPY --from=0 /root/conda/lib/libmkl_gnu_thread.so /usr/local/libtorch/lib/
COPY --from=0 /root/conda/lib/libmkl_core.so /usr/local/libtorch/lib/
COPY --from=0 /root/pytorch/build/lib/ /usr/local/libtorch/lib/
COPY --from=0 /root/pytorch/build/lib.linux-x86_64-3.7/torch/include/ /usr/local/libtorch/include/
RUN echo "cache breaker"
COPY --from=0 /usr/lib/x86_64-linux-gnu/libnccl.so.2 /usr/local/libtorch/lib/
