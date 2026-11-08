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

Launch the installed title from the HOME menu. From v2.1.7 onward, it checks
GitHub's Latest release for an NSP, downloads and verifies it, installs it in
the app's existing storage and reloads automatically. X checks for updates
manually. No separately installed NRO or on-console key file is required.
Versions v2.1.6 and earlier need one manual NSP installation to gain this feature.

Self-update validates the GitHub digest, complete package, content hashes and
application title ID. It stages new content before publishing metadata, restores
old metadata if publication fails, and keeps the running content until the new
app boots. A checked cleanup journal then removes only obsolete content belonging
to this application. Physical Switch testing remains necessary for installation
and automatic relaunch.

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
