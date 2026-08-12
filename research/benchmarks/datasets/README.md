# Research reconstruction datasets

This is the sole local storage boundary for large public MRI reconstruction
inputs used by research, interoperability, and paper-baseline work. It is not
a product input directory, a test-fixture directory, or a third-party source
tree.

```text
datasets/
  manifests/          versioned dataset identity, source, hashes, and workflow locks
  license-evidence/   versioned source-license and redistribution assessments
  raw/                ignored immutable downloads and same-volume .staging/
  canonical/          ignored reviewed ISMRMRD inputs for cross-framework replay
  derived/            ignored baseline-specific artifacts; never product inputs
  raw/.staging/       ignored same-volume partial downloads
```

Only manifests, license evidence, and the fetch tool are committed. Raw data,
reference images, converted inputs, outputs, and benchmark artifacts are all
ignored. A public URL does not grant redistribution rights: do not add any
dataset payload to Git, Git LFS, release archives, or paper artifacts unless
the manifest's redistribution review explicitly permits it.

## Initial Gadgetron reference case

`gadgetron-simple-gre-ismrmrd-dump` is a frozen Gadgetron integration case
from commit `1d14c4cd380c57563500b27f5135d2c887e52de4`. Its standard HDF5/MRD
raw input is `simple_gre/simple_gre_in_20220831.mrd`; the same official case
uses `simple_gre/ref_20240810.mrd` as the Gadgetron image reference. The
additional HDF5 dump reference is included so the upstream integration case can
also validate capture fidelity.

The Gadgetron data index makes these files publicly downloadable and publishes
MD5 values, but it does not state a separate dataset license. Therefore this
first dataset is deliberately marked `redistribution_status=unclear` and is
local-only pending clarification. See
[the license evidence](license-evidence/gadgetron-integration-data.md) and the
[dataset manifest](manifests/gadgetron-simple-gre-ismrmrd-dump.json).

## Cross-platform download tool

[`fetch_dataset.py`](../tools/fetch_dataset.py) is repository-maintained and
requires Python 3.9 or newer. It uses only the Python standard library
(`pathlib`, `tempfile`, `urllib`, and `hashlib`): no Bash, PowerShell, `curl`,
or third-party Python package is required. The same manifest, destination
layout, restart-safe atomic staging rule, and byte/MD5/SHA-256 verification apply on
Linux and Windows. Python must have access to its normal trusted CA
certificates because every production source and redirect must remain HTTPS.

After the developer bootstrap, fetch and validate with the project-managed Python runner:

```bash
# Linux
tools/devenv/linux/run.sh python research/benchmarks/tools/fetch_dataset.py list
tools/devenv/linux/run.sh python research/benchmarks/tools/fetch_dataset.py fetch \
  --id gadgetron-simple-gre-ismrmrd-dump --accept-source-terms
tools/devenv/linux/run.sh python research/benchmarks/tools/fetch_dataset.py verify \
  --id gadgetron-simple-gre-ismrmrd-dump
```

```powershell
# Windows PowerShell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\run.ps1 python research/benchmarks/tools/fetch_dataset.py list
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\run.ps1 python research/benchmarks/tools/fetch_dataset.py fetch `
  --id gadgetron-simple-gre-ismrmrd-dump --accept-source-terms
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\run.ps1 python research/benchmarks/tools/fetch_dataset.py verify `
  --id gadgetron-simple-gre-ismrmrd-dump
```

This workspace contains a verified local copy. Fresh clones must use the fetch
command; it performs streaming HTTPS download and checks the frozen byte count,
upstream MD5, and SHA-256 before publishing a file into `raw/`.

The platform-neutral self-test uses a local HTTP fixture and never downloads
MRI data:

```bash
# Linux
tools/devenv/linux/run.sh python -m unittest discover -s research/benchmarks/tests -p test_fetch_dataset.py
```

```powershell
# Windows PowerShell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\run.ps1 python -m unittest discover -s research/benchmarks/tests -p test_fetch_dataset.py
```

To reproduce the corresponding upstream Gadgetron integration test, use a
Gadgetron checkout/build at the commit pinned in the manifest, with Python
support enabled and `GADGETRON_HOME`/`ISMRMRD_HOME` set.

Linux shell:

```bash
gadgetron_source=/path/to/gadgetron
python3 "$gadgetron_source/test/integration/run_gadgetron_test.py" \
  --data-folder "$PWD/research/benchmarks/datasets/raw/gadgetron-integration-1d14c4cd" \
  --template-folder "$gadgetron_source/test/integration/config" \
  --test-folder /tmp/ksj-gadgetron-simple-gre \
  "$gadgetron_source/test/integration/cases/ismrmrd_dump_gadget_test.cfg"
```

Windows PowerShell:

```powershell
$gadgetronSource = 'C:\src\gadgetron'
py -3 "$gadgetronSource\test\integration\run_gadgetron_test.py" `
  --data-folder (Join-Path $PWD 'research\benchmarks\datasets\raw\gadgetron-integration-1d14c4cd') `
  --template-folder "$gadgetronSource\test\integration\config" `
  --test-folder (Join-Path $env:TEMP 'ksj-gadgetron-simple-gre') `
  "$gadgetronSource\test\integration\cases\ismrmrd_dump_gadget_test.cfg"
```

This command is an upstream correctness/interoperability check, not a
KSpaceJet performance result. Any cross-framework performance run must still
follow the frozen comparison protocol and use canonical inputs outside its
timed region.
