# Source code offer

**Multishine (GPL-3.0-or-later).** The complete corresponding source of the files we distribute (card files, overlay
files, data pack, card download, the loader save) is this repository, at the tag `rel-N` of release N.

**Mozilla Public License 2.0 parts.** The loader save is built with files of melee-gci-compiler (practicaltas,
https://github.com/practicaltas/melee-gci-compiler): `ace_exploit.mgc`, `datacopy_table.mgc` and the UCF code sets of
its `ucf0.84/ucf_data/` folder, under MPL-2.0 (`LICENSES/MPL-2.0.txt`). Their Source Code Form is the submodule
`third_party/melee-gci-compiler` at the commit recorded in this repository, unchanged; our own loader files that include
them are in `offline/cardloader/` (GPL-3.0-or-later). The loader save as a whole is a Larger Work (MPL-2.0 section 3.3).

Questions about the source: open an issue in this repository.
