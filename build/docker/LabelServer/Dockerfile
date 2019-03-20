# To build:
#   docker build -t labelserver -f build/docker/LabelServer/Dockerfile .
# To test:
#   docker run -t -p 8080:8080 -v /home/ben/mldata/gopro/:/mldata -v /home/ben/dev/Annotator/dims/:/dims labelserver /mldata /dims/roads-m3-cou.json

FROM ubuntu:18.04 as BuildStage

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

RUN git clone --depth 1 --single-branch --recurse-submodules https://github.com/deplinenoise/tundra.git /build/tundra \
    && cd /build/tundra \
    && make -j \
    && make install

RUN curl -sL https://deb.nodesource.com/setup_11.x | bash - \
    && apt-get install -y nodejs

COPY . /build/repo

WORKDIR /build/repo/app/photo-label
RUN npm install
RUN npm run build

WORKDIR /build/repo
RUN tundra2 linux-clang-release-default LabelServer

FROM ubuntu:18.04

RUN apt-get update && apt-get install -y \
	libproj12 \
	libomp5  \
	libpq5 \
	libunwind8 \
	libcurl4 \
	libpng16-16

COPY --from=BuildStage /build/repo/t2-output/linux-clang-release-default/*.so /usr/local/lib/
COPY --from=BuildStage /build/repo/t2-output/linux-clang-release-default/LabelServer /deploy/
COPY --from=BuildStage /build/repo/app/photo-label/dist/ /deploy/dist/
COPY --from=BuildStage /build/repo/dims/ /deploy/dims/

RUN ldconfig

ENTRYPOINT ["/deploy/LabelServer"]

