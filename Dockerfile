FROM ubuntu:bionic AS builder
    RUN apt-get update
    RUN apt-get -y install wget

    WORKDIR /tmp
    RUN wget https://github.com/Kitware/CMake/releases/download/v3.12.4/cmake-3.12.4-Linux-x86_64.sh \
        -O cmake-install.sh \
        && chmod u+x cmake-install.sh \
        && ./cmake-install.sh --skip-license --prefix=/usr \
        && rm cmake-install.sh

    WORKDIR /src

    COPY install-deps.sh .
    RUN ./install-deps.sh

    COPY . .
    RUN rm -rf build \
        && mkdir build \
        && cd build \
        && cmake .. -DBUILD_CLIENT=OFF \
        && make -j$(nproc) \
        && cd ..

FROM ubuntu:bionic AS runner
    WORKDIR /opt/slog
    COPY --from=builder /src/build/slog .
    COPY --from=builder /src/slog.conf .
    ENTRYPOINT [ "./slog" ]