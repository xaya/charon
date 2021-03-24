# Builds a Docker image that has charon (and its dependencies)
# built and installed into /usr/local.  It also includes libxayagame;
# Charon is in principle independent, but most usecases will be GSPs that
# need both.
#
# This image is meant for development with Charon, e.g. as base image for
# building binaries that use Charon as library.  We also provide a separate
# and much smaller image with just the Charon binaries for usecases where
# they are enough.

FROM xaya/libxayagame AS build
RUN apk add --no-cache \
  autoconf \
  autoconf-archive \
  automake \
  build-base \
  gflags-dev \
  libtool \
  pkgconfig \
  subversion

# Move over the source files.
COPY . /usr/src/charon/

# We need custom patches applied to gloox, so build it from source.
WORKDIR /usr/src/gloox
RUN svn checkout svn://svn.camaya.net/gloox/trunk .
RUN cat /usr/src/charon/data/gloox-patches/*.diff | patch -p0
RUN ./autogen.sh && ./configure --without-tests && make && make install-strip

# Build and install Charon itself.
WORKDIR /usr/src/charon
RUN ./autogen.sh && ./configure && make && make install-strip

# For the final image, just copy over all built / installed stuff.
FROM xaya/libxayagame
COPY --from=build /usr/local /usr/local/
LABEL description="Development image with Charon and libxayagame"
