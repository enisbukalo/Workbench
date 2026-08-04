FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    mingw-w64-x86-64-dev \
    g++-mingw-w64-x86-64-posix \
    clang-15 \
    clang-format \
    clang-format-15 \
    lld-15 \
    llvm-15 \
    ninja-build \
    git && rm -rf /var/lib/apt/lists/*

ENV CXXFLAGS="-std=c++20 -Wall -Wextra -g -O3" \
    CMAKE_CXX_STANDARD=20

EXPOSE 8080

CMD ["bash"]
