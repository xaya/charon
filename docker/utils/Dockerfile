# Builds a Docker image with just the Charon utility binaries (charon-server
# and charon-client) and the required runtime libraries for them.  This
# provides a minimal image size if they are enough (and the Charon library
# is not integrated directly with a GSP).

FROM xaya/charon AS build

# Collect the minimal required stuff and the binaries together.
WORKDIR /jail
RUN mkdir -p log bin share/charon
RUN cp /usr/local/bin/charon-* bin/
RUN for b in bin/*; do cpld $b lib64; done

# Include also the letsencrypt root CA, which the official
# server chat.xaya.io uses.
RUN cp /usr/local/share/charon/letsencrypt.pem share/charon/

# Build the final image.  We use alpine as base so that a shell and other
# stuff is already present, and can be used e.g. to run entrypoint scripts.
FROM alpine
COPY --from=build /jail /usr/local/
ENV LD_LIBRARY_PATH "/usr/local/lib64"
LABEL description="Minimal image with charon-client and charon-server"
VOLUME ["/log"]
ENV GLOG_log_dir "/log"
