FROM ubuntu:24.04 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build --parallel

FROM ubuntu:24.04 AS runtime

WORKDIR /opt/breach-team
COPY --from=build /app/build/breach_team /usr/local/bin/breach_team

CMD ["/usr/local/bin/breach_team"]
