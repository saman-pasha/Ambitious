#!/bin/sh
# tests/net/certs.sh DIR -- a test authority and a server certificate it signs, made
# by the openssl CLI into DIR: ca.crt (the authority), srv.crt and srv.key (for
# localhost, by name only: no address is in it). P-256 keys, two days' validity.
dir=$1
mkdir -p "$dir"
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -keyout "$dir/ca.key" -out "$dir/ca.crt" -days 2 \
  -subj "/CN=Ambitious test authority" -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign" 2> /dev/null
openssl req -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -keyout "$dir/srv.key" -out "$dir/srv.csr" -subj "/CN=localhost" 2> /dev/null
printf 'subjectAltName=DNS:localhost\nbasicConstraints=CA:FALSE\nextendedKeyUsage=serverAuth\n' > "$dir/srv.ext"
openssl x509 -req -in "$dir/srv.csr" -CA "$dir/ca.crt" -CAkey "$dir/ca.key" -CAcreateserial -out "$dir/srv.crt" -days 2 \
  -extfile "$dir/srv.ext" 2> /dev/null
