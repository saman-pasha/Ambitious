#!/bin/sh
# tests/net/certs.sh DIR -- a test authority and the server certificates it signs, made by the
# openssl CLI into DIR: ca.crt (the authority); srv.crt and srv.key (for localhost, by name only:
# no address is in it); pool.crt and pool.key (localhost, a.localhost, b.localhost, 127.0.0.1 and
# 127.0.0.2: the hosts a connection pooled by IP may carry); c.crt and c.key (c.localhost alone);
# and spki, the base64 SHA-256 of each one's public key, comma-separated, as Chromium's
# --ignore-certificate-errors-spki-list takes them (it then holds the certificates valid, which
# pooling asks of a connection). P-256 keys, two days' validity.
dir=$1
mkdir -p "$dir"
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -keyout "$dir/ca.key" -out "$dir/ca.crt" -days 2 \
  -subj "/CN=Ambitious test authority" -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign" 2> /dev/null
# cert NAME CN SAN: NAME.crt and NAME.key, signed by the authority
cert() {
  openssl req -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -keyout "$dir/$1.key" -out "$dir/$1.csr" -subj "/CN=$2" 2> /dev/null
  printf 'subjectAltName=%s\nbasicConstraints=CA:FALSE\nextendedKeyUsage=serverAuth\n' "$3" > "$dir/$1.ext"
  openssl x509 -req -in "$dir/$1.csr" -CA "$dir/ca.crt" -CAkey "$dir/ca.key" -CAcreateserial -out "$dir/$1.crt" -days 2 \
    -extfile "$dir/$1.ext" 2> /dev/null
  openssl x509 -in "$dir/$1.crt" -pubkey -noout | openssl pkey -pubin -outform der 2> /dev/null | openssl dgst -sha256 -binary | base64
}
s=$(cert srv localhost DNS:localhost)
p=$(cert pool localhost DNS:localhost,DNS:a.localhost,DNS:b.localhost,IP:127.0.0.1,IP:127.0.0.2)
c=$(cert c c.localhost DNS:c.localhost)
printf '%s,%s,%s\n' "$s" "$p" "$c" > "$dir/spki"
