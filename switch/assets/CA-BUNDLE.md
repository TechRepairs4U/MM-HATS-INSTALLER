The RomFS file `hats-ca-bundle.pem` is the Mozilla CA certificate bundle distributed by curl:

- Source: https://curl.se/ca/cacert.pem
- Published checksum: https://curl.se/ca/cacert.pem.sha256
- Downloaded and checksum-verified: 2026-10-24
- SHA-256: `796f1389bfea13c0ed78f663b765baaff54a508e5483786bd6ece73c5e3e3cbe`

The GitHub exclusion-list request verifies the server certificate and hostname against this bundle. Refresh this file from the same HTTPS source when updating the application's trust roots. The list itself is fetched afresh on each HATS install; it is not bundled with the application.

The exclusion-list request owns a RomFS mount for its entire lifetime, including TLS setup and curl handle cleanup. Other app features mount and unmount RomFS independently; the request must not assume it is already available.

With current devkitPro newlib (validated with devkita64-newlib 4.6.0.20260123-4), unused `devoptab_list` entries must remain null. Filling them with `dotab_stdnull` consumes all free device slots: `AddDevice` fails and libnx reports `0x559` when mounting RomFS. The old workaround for [newlib issue 35](https://github.com/devkitPro/newlib/issues/35) must not be restored.
