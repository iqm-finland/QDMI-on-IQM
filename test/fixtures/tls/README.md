# Disposable TLS fixtures

These certificates and the server private key are public test data. Never use
them outside loopback tests. The CA private keys were discarded after signing.
The certificates expire in September 2126.

`ca.pem` signs `server.pem` for `127.0.0.1`. Its OpenSSL subject hash is
`83d667e8`. `unrelated.pem` belongs to a separate CA. The regression test puts
`ca.pem` in a temporary hashed directory; it never changes the host's trust
store.
