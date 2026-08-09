# Building the full installable NSP

The NSP is a real application package, not a forwarder. It contains the
compiled MM HATS INSTALLER program (`main.nso`), its NPDM metadata, NACP
control data, icon, and ROMFS.

NSP encryption requires a legally obtained Switch keyset. Keep the keyset
outside Git and pass it to the build only at build time:

```sh
HACBREWPACK=/path/to/hacbrewpack \
HACBREWPACK_KEYSET=/path/to/keys.dat \
./build_nsp.sh
```

If `hacbrewpack` is not supplied, the script downloads and builds it from the
upstream hacBrewPack project. The release workflow can build the NSP when the
repository secret `HACBREWPACK_KEYS_B64` contains a base64-encoded keyset.
The secret is written to the runner temporarily and is never included in the
repository or release assets.

The package uses application title ID `0100f0f0a1b20000`.
