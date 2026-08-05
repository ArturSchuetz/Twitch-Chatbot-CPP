FROM ubuntu:24.04 AS build

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        ca-certificates \
        cmake \
        g++ \
        make \
        libboost-all-dev \
        libssl-dev \
        nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    -DTWITCHBOT_WARNINGS_AS_ERRORS=ON \
    && cmake --build build --parallel

FROM ubuntu:24.04 AS runtime

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        ca-certificates \
        libssl3t64 \
    && rm -rf /var/lib/apt/lists/*

RUN groupadd --system --gid 10001 twitchbot \
    && useradd --system --uid 10001 --gid twitchbot --home-dir /app twitchbot

WORKDIR /app
COPY --from=build /src/build/twitch-chatbot /app/twitch-chatbot
COPY config/bot.json /app/config/bot.json

USER twitchbot
ENTRYPOINT ["/app/twitch-chatbot"]
CMD ["--config", "/app/config/bot.json"]
