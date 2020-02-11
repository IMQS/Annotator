FROM nvidia/cuda:10.2-cudnn7-devel-ubuntu18.04

RUN apt-get update
RUN apt-get install -y \
	git \
	yasm \
	clang \
	make \
	cmake \
	libcurl4-openssl-dev \
	liblz4-dev \
	zlib1g-dev \
	libpng-dev \
	libjpeg-turbo8-dev \
	libproj-dev \
	libunwind-dev \
	libbsd-dev \
	uuid-dev \
	libglib2.0-dev \
	wget \
	unzip

RUN wget -O libtorch.zip https://download.pytorch.org/libtorch/cu101/libtorch-cxx11-abi-shared-with-deps-1.4.0.zip \
	&& unzip -d /usr/local libtorch.zip \
	&& rm libtorch.zip

# libglib2.0-dev is for lensfun

RUN git clone https://git.videolan.org/git/ffmpeg/nv-codec-headers.git /build/nv-codec-headers \
	&& cd /build/nv-codec-headers \
	&& make \
	&& make install

# github mirror is faster than git.ffmpeg.org
#RUN git clone https://git.ffmpeg.org/ffmpeg.git /build/ffmpeg
RUN git clone --depth 1 --branch n4.0.2 https://github.com/FFmpeg/FFmpeg.git /build/fmmpeg
RUN mv /build/fmmpeg /build/ffmpeg

RUN cd /build/ffmpeg \
	&& ./configure --enable-pic --disable-static --enable-shared --enable-cuda --enable-cuvid --enable-nvenc --extra-cflags=-I/usr/local/cuda/include --extra-ldflags=-L/usr/local/cuda/lib64 \
	&& make -j \
	&& make install

RUN git clone --depth 1 --branch 3.4.1 https://github.com/opencv/opencv.git /build/opencv \
	&& git clone --depth 1 --branch 3.4.1 https://github.com/opencv/opencv_contrib.git /build/opencv_contrib

RUN mkdir /build/opencv/build \
	&& cd /build/opencv/build \
	&& cmake -D WITH_CUDA=OFF -D OPENCV_EXTRA_MODULES_PATH=/build/opencv_contrib/modules -D CMAKE_BUILD_TYPE=Release -D CMAKE_INSTALL_PREFIX=/usr/local -D ENABLE_CXX11=ON -D BUILD_LIST=calib3d,core,features2d,xfeatures2d .. \
	&& make -j \
	&& make install

#&& cmake -D WITH_CUDA=ON -D OPENCV_EXTRA_MODULES_PATH=/build/opencv_contrib/modules -D CMAKE_BUILD_TYPE=Release -D CMAKE_INSTALL_PREFIX=/usr/local -D ENABLE_CXX11=ON -D BUILD_LIST=calib3d,core,cudaoptflow,cudev,features2d,xfeatures2d .. \

# For lensfun
RUN git clone --depth 1 --branch v0.3.95 https://github.com/lensfun/lensfun.git /build/lensfun
COPY third_party/lensfun-heapfix.patch /build/lensfun
RUN cd /build/lensfun \
	&& git apply lensfun-heapfix.patch
RUN mkdir /build/lensfun/build \
	&& cd /build/lensfun/build \
	&& cmake -DCMAKE_BUILD_TYPE=Release .. \
	&& make -j \
	&& make install

RUN git clone --depth 1 --single-branch --recurse-submodules https://github.com/deplinenoise/tundra.git /build/tundra \
	&& cd /build/tundra \
	&& make -j \
	&& make install

# I don't know why I can't just install libturbojpeg from apt

RUN git clone --depth 1 --single-branch --branch 2.0.0 https://github.com/libjpeg-turbo/libjpeg-turbo.git /build/libjpeg-turbo
RUN cd /build/libjpeg-turbo \
	&& mkdir build \
	&& cd build \
	&& cmake -G"Unix Makefiles" -DCMAKE_INSTALL_PREFIX=/usr/local .. \
	&& make -j \
	&& make install

# TODO: move these installs to the top (when you're iterating on this again)
RUN apt-get install -y libegl1-mesa-dev

# Sigh! This doesn't work, because you need to be logged in to download this SDK
# So instead, we just embed those files inside our git repo.
#RUN cd /tmp \
# && curl -o nvideo.zip https://developer.nvidia.com/designworks/video_codec_sdk/downloads/v9.0 \
# && unzip nvideo.zip \
# && cd Video_Codec_SDK_* \
# && cp Lib/linux/stubs/x86_64/libnvcuvid.so /usr/local/lib \
# && cp Lib/linux/stubs/x86_64/libnvidia-encode.so /usr/local/lib

COPY build/docker/roads-cppbuild/libnvcuvid.so /usr/local/lib/
COPY build/docker/roads-cppbuild/libnvidia-encode.so /usr/local/lib/


