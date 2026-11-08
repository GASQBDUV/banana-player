# Third-party code

Vendored copies; nothing here is built by a package manager. Each file is the one from the release named below.

| Name | Version | Source | Licence | Files (sha256) |
|---|---|---|---|---|
| cpp-httplib | v0.65.0 (tag) | https://github.com/yhirose/cpp-httplib/tree/v0.65.0 (archive: `https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.65.0.tar.gz`, sha256 `6fda8f23ff811a751c2b6e694fe2f1aa1625df53a317110329ef57a18f9fa5fb`) | MIT (`httplib/LICENSE`) | `httplib/httplib.h` `a1e19ffaa8509c92fe9b29a1ade08ccfe296eb53ad6379e53f3d281268fd0aa1`<br>`httplib/httplib.cc` `0dab47b72d124ebc33fb61d5e020d3bc8b542df18155f8ded678315d1a4b8547`<br>`httplib/LICENSE` `4b45cbe16d7b71b89ae6127e26e0d90a029198ca5e958ad8e3d0b8bbed364d8b` |
| nayuki QR-Code-generator (C++) | v1.8.0 (tag) | https://github.com/nayuki/QR-Code-generator/tree/v1.8.0 (archive sha256 `2ec0a4d33d6f521c942eeaf473d42d5fe139abcfa57d2beffe10c5cf7d34ae60`) | MIT (`qrcodegen/LICENSE`, the licence text from the project's Readme) | `qrcodegen/qrcodegen.hpp` `b779c3b156cf7a57ce789d6fee4fc991ccc2913774d26c909d22bb8f26b2a793`<br>`qrcodegen/qrcodegen.cpp` `1f3b3fcdac6954c32cf583ccd02ec9b5901f756a38c461acedc70be4a77d3757`<br>`qrcodegen/LICENSE` `922604ed086384188865235374fc876de25f13fdfab9a8dce41ad0833e1edc19` |
| htm (standalone with Preact) | 3.1.1 (npm `htm@3.1.1`) | https://unpkg.com/htm@3.1.1/preact/standalone.module.js | Apache-2.0 (`web/vendor/LICENSE-htm.txt`) | `web/vendor/htm-preact-standalone.module.js` `72284e8e9079c87817145df1110f74e8a2aa040b2fc384922e18dfcb46fc1fd7`<br>`web/vendor/LICENSE-htm.txt` `740725f7252e750af735d0028cc534970772f513331e9f68150fede8fb3ce00f` |
| SortableJS | 1.15.7 (npm `sortablejs`, latest 1.x) | https://unpkg.com/sortablejs@1.15.7/Sortable.min.js (npm tarball sha256 `66cbfc8fa18860783584fe0c346baf6cbf3399da72d23d21053425ba232abed4`) | MIT (`web/vendor/LICENSE-Sortable.txt`) | `web/vendor/Sortable.min.js` `bf4241bc73fef7f11c59a283a69fe8051cdd31c6d8ff5a2b9ba219e7831fcf76`<br>`web/vendor/LICENSE-Sortable.txt` `e94dfc31e800d169257569db270457c9f028440c9ccae41e7eb78b2db18f1298` |

`httplib/httplib.h` and `httplib/httplib.cc` are the output of the project's `split.py` run on its `httplib.h`
(`python3 split.py -o <dir>`), so they're not byte-identical to the archive's files.

The fonts served by the web dashboard (`assets/fonts/PixelifySans-Regular.ttf`, `Inter-Regular.ttf`) are
SIL OFL 1.1; their licences are in `assets/fonts/licenses/` and are copied into the bundle.
