#!/bin/bash

# Run this script to create new SSL certs for pistache HTTPS unit tests.
# Passphrases are disabled on all keys.  Do not use these certs/keys in
# production; they are for unit tests only!

# Requires openssl-1.1.1 or greater (for 'extensions' support).

DAYS=3650
BITS=2048

trap "echo 'aborting'; exit 255" 2 3
log() {
  echo -e "\x1b[32m$1\x1b[0m"
}

log "Create rootCA.key and rootCA.crt"
openssl req -x509 -newkey rsa:${BITS} -sha256 -days ${DAYS} -nodes \
  -keyout rootCA.key -out rootCA.crt -subj "/CN=pistache.io" \
  -addext "subjectAltName=IP:127.0.0.1" || exit $?

log "Create server.key"
openssl genrsa -out server.key ${BITS} || exit $?

log "Create server.csr"
openssl req -new -sha256 -key server.key \
  -subj "/C=US/ST=WA/O=Pistache/CN=server" \
  -out server.csr || exit $?

log "Create server.crt"
openssl x509 -req -in server.csr -days ${DAYS} -sha256 \
  -CA rootCA.crt -CAkey rootCA.key -set_serial 01 \
  -extensions server -out server.crt || exit $?

rm -f server.csr || exit $?

log "Create client.key"
openssl genrsa -out client.key ${BITS} || exit $?

log "Create client.csr"
openssl req -new -sha256 -key client.key \
  -subj "/C=US/ST=WA/O=Pistache/CN=client" \
  -out client.csr || exit $?

log "Create client.crt"
openssl x509 -req -in client.csr -days ${DAYS} -sha256 \
  -CA rootCA.crt -CAkey rootCA.key -set_serial 02 \
  -extensions client -out client.crt || exit $?

rm -f client.csr || exit $?

log "Verify server certificate"
openssl verify -purpose sslserver -CAfile rootCA.crt server.crt || exit $?

log "Verify client certificate"
openssl verify -purpose sslclient -CAfile rootCA.crt client.crt || exit $?

log "done"
