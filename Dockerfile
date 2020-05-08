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
RUN apt-get update && apt-get install -y \
  autoconf \
  autoconf-archive \
  automake \
  build-essential \
  libtool \
  pkg-config \
  subversion

# We need to install Gloox from source (until the pubsub changes done by
# Xaya make it into a Debian package).
WORKDIR /usr/src/gloox
RUN svn checkout svn://svn.camaya.net/gloox/trunk .
RUN ./autogen.sh && ./configure --without-tests && make && make install-strip

# Build and install Charon itself.
WORKDIR /usr/src/charon
COPY . .
RUN ./autogen.sh && ./configure && make && make install-strip

# For the final image, just copy over all built / installed stuff.
FROM xaya/libxayagame
COPY --from=build /usr/local /usr/local/
RUN ldconfig
LABEL description="Debian-based image that includes Charon and libxayagame for Xaya development"
