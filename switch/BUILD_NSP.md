# Building the full installable NSP

The NSP is a real application package, not a forwarder. It contains the
compiled MM HATS INSTALLER program (`main.nso`), its NPDM metadata, NACP
control data, icon, and ROMFS. It also contains the HATS installer payload,
background and all menu icons. Both the NSP and NRO compare these files with
their bundled copies on every launch. Missing, old or corrupt support files
are repaired using staged replacements. Obsolete app themes and update files
are removed only inside `/switch/mm-tools` and `/config/mm-tools`; current
settings, downloads and recovery data are retained. The bundled theme and
payload path replace obsolete preferences.

Launch the installed title from the HOME menu. Update it by installing the
new NSP; the NRO/ZIP self-updater cannot update an installed NSP and is disabled
for this edition. No separately installed NRO is required.

NSP encryption requires a legally obtained Switch keyset. Keep the keyset
outside Git and pass it to the build only at build time:

```sh
HACBREWPACK=/path/to/hacbrewpack \
HACBREWPACK_KEYSET=/path/to/keys.dat \
./build_nsp.sh
```

If `hacbrewpack` is not supplied, the script downloads and builds it from the
upstream hacBrewPack project. Packaging runs locally; no release or announcement
workflow is enabled in the downloads repository. The keyset is never included
in the repository or release assets. The packer receives only validated `header_key`
and `key_area_key_application_00` values, preventing its legacy unknown-key
warnings from disclosing other keys.

Run `./build_nsp.sh --prepare-only` to create and inspect the NSO, metadata,
control data and bundled ROMFS without a keyset. This does not create an NSP.

The package uses application title ID `0100f0f0a1b20000`.
