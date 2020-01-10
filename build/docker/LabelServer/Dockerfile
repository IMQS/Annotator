# To build:
#   docker build -t labelserver -f build/docker/LabelServer/Dockerfile .
# To test:
#   docker run -t -p 8080:8080 -v /home/ben/mldata/gopro/:/mldata -v /home/ben/dev/Annotator/dims/:/dims labelserver /mldata /dims/roads-m3-cou.json

FROM ubuntu:18.04 as BuildWWW

RUN apt-get update && apt-get install -y \
  build-essential \
  git \
  make \
  clang \
  gcc \
  curl

RUN curl -sL https://deb.nodesource.com/setup_11.x | bash - \
  && apt-get install -y nodejs

# Cache the 'npm install' phase
RUN mkdir /build
COPY app/photo-label/package.json /build/
COPY app/photo-label/package-lock.json /build/
WORKDIR /build
RUN npm install

# Build www package
COPY app/photo-label /build
RUN npm run build

FROM nvidia/cuda:10.2-cudnn7-devel-ubuntu18.04 as BuildCPP

RUN apt-get update && apt-get install -y \
  libunwind-dev \
  libbsd-dev \
  libgles2-mesa-dev \
  libproj-dev \
  yasm \
  zlib1g-dev \
  liblz4-dev \
  libpng-dev \
  libcurl4-openssl-dev \
  uuid-dev \
  libexpat1-dev \
  libpq-dev \
  libturbojpeg0-dev \
  g++ \
  build-essential \
  git \
  make \
  clang \
  gcc \
  curl

# TODO: Squash these down
RUN apt-get install -y unzip
RUN curl -o libtorch.zip https://download.pytorch.org/libtorch/cu101/libtorch-cxx11-abi-shared-with-deps-1.3.1.zip
RUN unzip -d /usr/local libtorch.zip

RUN git clone --depth 1 --single-branch --recurse-submodules https://github.com/deplinenoise/tundra.git /build/tundra \
  && cd /build/tundra \
  && make -j \
  && make install

COPY . /build/repo

WORKDIR /build/repo
RUN tundra2 linux-clang-release-default LabelServer

#RUN ls -la /build/repo/t2-output/linux-clang-release-default
#RUN /build/repo/t2-output/linux-clang-release-default/LabelServer

FROM nvidia/cuda:10.2-cudnn7-runtime-ubuntu18.04

RUN apt-get update && apt-get install -y \
  libproj12 \
  libomp5  \
  libpq5 \
  libunwind8 \
  libcurl4 \
  libpng16-16

#RUN ldconfig

#COPY --from=BuildCPP /usr/local/libtorch/ /usr/local/libtorch/

COPY --from=BuildCPP \
  /usr/local/libtorch/lib/libtorch.so \
  /usr/local/libtorch/lib/libc10.so \
  /usr/local/libtorch/lib/libc10_cuda.so \
  /usr/local/libtorch/lib/libcaffe2_nvrtc.so \
  /usr/local/libtorch/lib/libgomp-753e6e92.so.1 \
  /usr/local/libtorch/lib/libcudart-1b201d85.so.10.1 \
  /usr/local/libtorch/lib/libnvrtc-5e8a26c9.so.10.1 \
  /usr/local/libtorch/lib/libnvToolsExt-3965bdd0.so.1 \
  /deploy/

#COPY --from=BuildCPP /build/repo/t2-output/linux-clang-release-default/*.so /usr/local/lib/
COPY --from=BuildCPP \
  /build/repo/t2-output/linux-clang-release-default/libdba.so \
  /build/repo/t2-output/linux-clang-release-default/libpal.so \
  /build/repo/t2-output/linux-clang-release-default/libprojwrap.so \
  /build/repo/t2-output/linux-clang-release-default/uberlogger \
  /build/repo/t2-output/linux-clang-release-default/LabelServer \
  /deploy/

COPY --from=BuildCPP /build/repo/third_party/nvcuvid/Lib/linux/stubs/x86_64/libnvcuvid.so /usr/local/lib

COPY --from=BuildWWW /build/dist/ /deploy/dist/
COPY dims/ /deploy/dims/

#RUN ls -la /usr/local/lib

RUN ldconfig

ENV LD_LIBRARY_PATH=/deploy
ENTRYPOINT ["/deploy/LabelServer"]

