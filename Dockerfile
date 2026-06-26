FROM alpine:3.24 AS builder

SHELL ["sh", "-eux", "-c"]

RUN apk update
RUN apk add \
    build-base \
    clang \
    compiler-rt \
    musl-dev \
    linux-headers \
    curl \
    bash \
    git
RUN curl -fsSL https://bun.com/install | bash && \
    ln -sf ~/.bun/bin/bun /usr/bin/bun

COPY . /src
WORKDIR /src

RUN cd ./web && \
    bun install && \
    bun run build && \
    cd .. && \
    MODE=rel make wr

FROM scratch

COPY --from=builder /src/wr /

ENTRYPOINT ["/wr"]
