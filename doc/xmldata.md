# Data Blobs in XML

As part of the [Charon protocol](protocol.md) and perhaps other
protocols based on it (e.g. [Democrit](https://github.com/xaya/democrit)),
payload data needs to be encoded into XML stanzas.
For Charon, this payload data is JSON, but it could also be e.g. serialised
[protocol buffers](https://developers.google.com/protocol-buffers/) or other
general data.
Especially for JSON, the data will be highly compressible, so that
it makes sense to support compressed data on the wire to improve
latency and throughput.

Here, we describe a general set of XML tags to represent data payloads
as part of XMPP stanzas used in Charon and possibly to be used in other
protocols as well.

## Overview

In general, certain tags (e.g. Charon's `<params>` or `<result>`)
may be meant to carry a blob payload.  This payload will be the representation
of a (potentially binary) string, which may itself be serialised JSON.

The payload is encoded in one or more child tags, as described below.  The
final payload is the concatenation of all the individual childs' payloads.

## Raw Strings

A tag `<raw>` can be used to directly hold a payload in the form of
XML cdata.  For instance:

    <raw>{"this is": "some serialised JSON"}</raw>

## Base64 Encoding

For binary payloads, the `<base64>` tag can be used.  Its cdata is expected
to be the [base64
encoding](https://www.openssl.org/docs/manmaster/man3/EVP_EncodeBlock.html)
of a payload string.  For example:

    <base64>VGhpcyBpcyBhbiBleGFtcGxlIHN0cmluZy4=</base64>

## zlib Compression

Payloads can also be compressed using
[zlib's utility functions](https://www.zlib.net/manual.html#Utility).
In this case, the `<zlib>` tag is used.  The compressed data should
be set as a payload itself inside the tag (e.g. using `<base64>`),
and the size of the uncompressed output must be given as well
with the `size` attribute:

    <zlib size="24">
      <base64>eJwLycgsVgCi5PzcgqLU4uLUFIWUxJJEPQBvPQjS</base64>
    </zlib>
