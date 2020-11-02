FROM ubuntu:20.04

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get -y update && apt-get -y install \
    libgl1-mesa-glx libglib2.0-0 libfontconfig1 libdbus-1-3 libxrender1 \
    libxcb-icccm4 libxcb-image0 libxkbcommon-x11-0 libxcb-xkb1 libxcb-xinerama0 \
    libxcb-xfixes0 libxcb-shape0 libxcb-render-util0 libxcb-keysyms1 libtinfo5 xterm\
    \
    bash-completion \
    build-essential git cmake wget tar sudo gdb ninja-build clang-format \
    \
    doxygen libboost-dev \
    \
    lcov \
    \
    vim

RUN git config --global advice.detachedHead false

# Catch2
WORKDIR /tmp
RUN git clone --recurse-submodules https://github.com/catchorg/Catch2.git \
 && cd Catch2 \
 && git checkout v2.13.2 \
 && mkdir build/ && cd build/ \
 && cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DCATCH_BUILD_TESTING=OFF -DCATCH_INSTALL_DOCS=OFF .. \
 && cmake --build . \
 && cmake --build . --target install

# benchmark
RUN git clone https://github.com/google/benchmark.git \
  && cd benchmark \
  && cmake -E make_directory "build" \
  && cmake -E chdir "build" cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DBENCHMARK_ENABLE_TESTING=OFF ../ \
  && cmake --build "build" --config Release \
  && cmake --build "build" --config Release --target install

RUN echo "if [ -f /home/$USER/.bashrc ]; then . /home/$USER/.bashrc ; fi" >> /root/.bashrc

CMD /bin/bash -l
