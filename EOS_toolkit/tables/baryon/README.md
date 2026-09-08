# Original finite-temperature baryon tables

These unmodified data files are by H. Shen, F. Ji, J. N. Hu and K. Sumiyoshi,
*Equation of state for simulations of core-collapse supernovae and neutron-star
mergers*, published 2020-01-20,
[doi:10.5281/zenodo.3612487](https://doi.org/10.5281/zenodo.3612487).
The author archive explicitly licenses the data under
[Creative Commons Attribution 4.0 International](https://creativecommons.org/licenses/by/4.0/).
The [license text](LICENSE-CC-BY-4.0.txt) applies to these data, separately from
ARCH's source-code license. Preserve attribution, the source link and license
when redistributing; identify any subsequent modifications. No endorsement is implied.

| File | Author model | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| [eos2.tab](eos2.tab) | EOS2, TM1 | 143166842 | `d52d37d30fec10ffb5279689a172e61a7ebb3538a4acaf2270dc32469c0d3c58` |
| [eos4.tab](eos4.tab) | EOS4, TM1e | 143167115 | `5ee37819f873387af9c38207bcada72a48abe695b9491df9ad5c6a5e69487c34` |

Files were extracted byte-for-byte from `eos2.tab.zip` and `eos4.tab.zip` in the
linked author record; ARCH has not converted, resampled or retuned them. The
archive SHA-256 values are respectively
`c4df343f8f5446851ed5c74d735400429b5491250ec5bda85cb061e6ba141f3c` and
`1c47911219a72862a27d4eb3eec765cd38857564f91249d886cb1b8295a8d720`.
Use `git lfs pull` to obtain the actual tables instead of LFS pointers.
Zero-temperature and zero-proton-fraction auxiliary products are not included:
they do not meet this finite-temperature/logarithmic-coordinate interface.
The separately processed `HShenEOS.h5` product is not distributed here.

## Use and physical scope

Set `eos_type = tabular` and `eos_table_path` to either file. One shared
16-column-format reader identifies the source schema by content. The existing
EOS dispatcher arranges loading, component completion and source/dependency
identity; there is no model-name-dependent EOS policy. Missing electrons and
positrons use the existing Timmes table, selectable with `eos_helm_table_path`;
missing photons use its shared analytic photon formula. Neither ions nor
baryon Coulomb terms are added a second time.

The source conventions are documented in the author's
[EOS4 guide](https://user.numazu-ct.ac.jp/~sumi/eos/table4/guide_EOS4.pdf).
Its fixed baryon mass and distinct printed F/E reference constants are honored
by the format reader. Assembly preserves native F, P and entropy as one
interpolating potential's constraints; it does not force the independently
rounded source energy to match by fitting a variable zero point. Conserved
energy receives one constant, query-independent positive reference, while
pressure and all derivatives remain unchanged by that reference.

Each source has 110 density, 91 temperature and 65 electron-fraction nodes.
Native source-coordinate inconsistencies and component-domain limits are
masked and propagated through derivative stencils. With the bundled Timmes
table, EOS2 excludes 75126 of 650650 nodal derivative stencils and EOS4 excludes
74918. Valid query cells require all participating stencils; nodal counts are
not valid-volume fractions. In particular, Timmes does not cover the entire
high-density source domain. Queries outside support, nonpositive P/E/cv/sound
speed and nonunique temperature inversions fail explicitly; no extrapolation
or floor silently repairs them.

These are nuclear-equilibrium EOS products and cannot be combined with ARCH's
independent kinetic nuclear-energy source or NSE projection. See the
[tabular contract](../../../src/physics/eos/TabularEOS.md) and
[bounded scientific checks](../../../validation/eos/README.md) for supported
couplings and measured accuracy. Successful parsing and nodal identities do
not establish full-domain interpolation accuracy or a supernova simulation.
