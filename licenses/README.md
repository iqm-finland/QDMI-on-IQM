# Third-party notices

IQM's code remains licensed under Apache-2.0 WITH LLVM-exception. Each
third-party component's terms apply only where that component is included. The
package metadata lists the license files without a single license expression,
since the bundled components differ by platform.

The following inventory was checked against the seven wheels from
[CI run 38349975712](https://github.com/iqm-finland/QDMI-on-IQM/actions/runs/38349975712)
(commit `510de2a`, 13 October 2026). Recheck it when dependencies or build
images change, including the libraries added by wheel repair.

| Component     | Version | Included in                                  | License text                                                             |
| :------------ | :------ | :------------------------------------------- | :----------------------------------------------------------------------- |
| CPR           | 1.14.2  | All wheels, statically linked                | [MIT](LICENSE-cpr)                                                       |
| nlohmann/json | 3.12.0  | All wheels, compiled headers                 | [MIT](LICENSE-nlohmann-json)                                             |
| curl          | 8.13.0  | Linux and Windows wheels, statically linked  | [curl](LICENSE-curl)                                                     |
| OpenSSL       | 1.1.1k  | manylinux wheels, `libssl` and `libcrypto`   | [OpenSSL and SSLeay](LICENSE-openssl-1.1)                                |
| OpenSSL       | 3.5.9   | musllinux wheels, `libssl` and `libcrypto`   | [Apache-2.0](LICENSE-openssl-3)                                          |
| GCC runtime   | 14.2.0  | musllinux wheels, `libgcc_s` and `libstdc++` | [GPL-3.0](LICENSE-gpl-3.0) with [GCC exception 3.1](LICENSE-gcc-runtime) |

macOS wheels link to Apple's system curl. Windows wheels use Schannel, and wheel
repair adds no DLLs on either Windows architecture. Linux wheels link to the
system zlib; it is not bundled. CPR configures zlib-ng 2.1.3 during the build,
but it is not linked into these wheels. QDMI 1.4.0 headers retain their
Apache-2.0 WITH LLVM-exception notices and use the project's
[license text](../LICENSE).

## Sources

License texts are copied verbatim from the corresponding upstream versions:

- [CPR 1.14.2](https://github.com/libcpr/cpr/blob/1.14.2/LICENSE)
- [nlohmann/json 3.12.0](https://github.com/nlohmann/json/blob/v3.12.0/LICENSE.MIT)
- [curl 8.13.0](https://github.com/curl/curl/blob/curl-8_13_0/COPYING)
- [OpenSSL 1.1.1k](https://github.com/openssl/openssl/blob/OpenSSL_1_1_1k/LICENSE)
- [OpenSSL 3.5.9](https://github.com/openssl/openssl/blob/openssl-3.5.9/LICENSE.txt)
- GCC 14.2.0:
  [GPL](https://github.com/gcc-mirror/gcc/blob/releases/gcc-14.2.0/COPYING3) and
  [runtime exception](https://github.com/gcc-mirror/gcc/blob/releases/gcc-14.2.0/COPYING.RUNTIME).
  The musllinux runtime libraries come from Alpine's GCC package; its
  [build recipe and patches](https://gitlab.alpinelinux.org/alpine/aports/-/tree/3.22-stable/main/gcc)
  accompany the [GCC 14.2.0 source](https://ftp.gnu.org/gnu/gcc/gcc-14.2.0/).

Source builds may use different system libraries. Dependencies installed
separately by Python package managers carry their own license notices.
