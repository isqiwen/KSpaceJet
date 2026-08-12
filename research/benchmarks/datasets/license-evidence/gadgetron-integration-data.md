# Gadgetron integration-data access and license evidence

Reviewed: 2026-08-11

Dataset: `gadgetron-simple-gre-ismrmrd-dump`

The input and references are indexed by Gadgetron's pinned integration-data
manifest:

- Repository: <https://github.com/gadgetron/gadgetron>
- Pinned revision: `1d14c4cd380c57563500b27f5135d2c887e52de4`
- Data index: <https://raw.githubusercontent.com/gadgetron/gadgetron/1d14c4cd380c57563500b27f5135d2c887e52de4/test/integration/data.json>
- Blob host: <https://gadgetrondata.blob.core.windows.net/gadgetrontestdata/>

The index identifies each source file and its MD5 checksum, establishing an
official public acquisition path. Its SHA-256 is
`99e654eab55e8b87ddee98379e3422afdb2a7faf8d7ef3ba855ab87f3acec1cb`.
It contains no data-license or redistribution field. The reviewed blob
responses identify object metadata such as ETag and last-modified time, but do
not provide a dataset license. Gadgetron's software license at
<https://github.com/gadgetron/gadgetron/blob/1d14c4cd380c57563500b27f5135d2c887e52de4/LICENSE>
(SHA-256 `49de64f7fecbc7f0088a9a63cf1477422b5fcdd5bc4c872f4b4fa181f3fb623d`)
governs code and is not evidence that these MRI data can be redistributed.

Accordingly, this repository records:

```text
publicly_accessible = true
dataset_license      = not stated by source
redistribution_status = unclear
local_only            = true
```

The payload is ignored by Git. Do not package, publish, mirror, or upload it
until a separate provenance, privacy, and redistribution review establishes
permission. The manifest pins source and local checksums so other users can
fetch the same objects directly from the source instead.
