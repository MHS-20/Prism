FROM gcc:13-bookworm AS builder
WORKDIR /build

RUN apt-get update && apt-get install -y cmake && rm -rf /var/lib/apt/lists/*

COPY CMakeLists.txt .
COPY server/ server/
COPY client/ client/

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build

FROM debian:bookworm-slim
COPY --from=builder /build/build/prism-server /usr/local/bin/prism-server
EXPOSE 1234
CMD ["prism-server"]
