# ISMRMRD inspection reader

`KSpaceJet::ismrmrd::InspectionReader` is the read-only, bounded inspection
boundary for one selected standard ISMRMRD HDF5 container. It supports standard
MRD as it exists on disk; it is not a reconstruction route, Provider API,
gateway protocol, or a second MRI artifact format.

The canonical implementation and acceptance status are in the
[project plan](KSpaceJet_project_plan_and_acceptance.md). This document records
the public ownership and format contract for the P8 inspection reader.

## Standard-first container discovery

`discover_mrd_containers(path, limits, containers, error)` recursively scans
HDF5 groups and returns sorted absolute paths. It never assumes `/dataset` is
present or preferred. Discovery accepts two standard shapes:

- An XML-header MRD container: direct standard `xml`, optional acquisition
  `data`, optional `waveforms`, and zero or more named standard image series.
- A standalone standard image-series group: direct `header`, `attributes`, and
  `data` datasets. It can be viewed even when no XML-header/acquisition
  container owns it.

Each `InspectionMrdContainerDescriptor` reports whether standard header, raw
acquisition, waveform, and image content is present, plus bounded acquisition
and image-series counts. Discovery does not materialize XML, raw samples,
waveforms, metadata, or pixels.

Traversal follows hard HDF5 links only, de-duplicates group objects by their
HDF5 object token, and is bounded by link-count, group-count, nesting-depth,
group-path, and link-name limits in `InspectionReadLimits`. It neither follows
external/soft links nor creates or changes source-file content.

`open(path, container_path, limits, error)` accepts a path returned by
discovery. A normal XML-header container exposes XML, optional raw
acquisitions, and named image series. A standalone image-series container has
an empty XML header, zero acquisitions, and one image series named by its leaf
group; image records and pixels remain fully readable through the same public
API. Neither form requires KSpaceJet-private groups or metadata.

## Public model and lifetime

The public API exposes only KSpaceJet value types:

- `InspectionDatasetMetadata` owns XML, acquisition count, and image-series
  descriptors.
- `InspectionAcquisitionView` contains a copied standard acquisition header and
  borrowed samples/trajectory for one zero-based acquisition ordinal.
- `InspectionImageRecord` owns a copied standard image header and standard
  `MetaAttributes`, including multiple values under one attribute name.
- `ImagePixelsView` contains the image data type, dimensions, and borrowed raw
  pixel bytes for one `{series_id, ordinal}` image.

The API never exposes an HDF5 handle, an `ISMRMRD::Image`, or an
`ISMRMRD::MetaContainer`. Storage ordinal is zero-based and distinct from
`InspectionImageHeader::image_index`.

XML, metadata, copied headers, and MetaAttributes are owned results.
Acquisition samples, trajectory, and image pixels are valid only while their
synchronous consumer callback executes. The reader does not retain an
acquisition, image payload, or whole-file pixel cache.

Each reader receives `InspectionReadLimits`. Before materializing a payload,
the implementation checks standard HDF5 rank, record count, named fixed-header
compound fields, logical shape, VLEN element type, integer arithmetic, and the
corresponding byte/count limit. Malformed, out-of-range, and oversized input
therefore fails deterministically instead of retaining an entire MRD file.

Standard acquisition and image headers are validated by field name, scalar
type, fixed-array extent, and a small storage bound, then HDF5 maps them into
native ISMRMRD memory. File member order, padding, and byte order are not a
KSpaceJet ABI. An incomplete or non-standard compound is rejected before any
payload becomes visible to a consumer.

Reads are non-reentrant. If a consumer moves or assigns its
`InspectionReader`, the in-flight callback keeps its old source state alive
until it returns; it never leaves a borrowed span or HDF5 handle dangling.

The narrow HDF5 preflight is private implementation detail. Actual acquisition
and image-payload decoding remains in the official ISMRMRD binding, preserving
standard semantics rather than defining a KSpaceJet serialization.

## Image semantics

`InspectionImageHeader::matrix_size` and `field_of_view_mm` use standard
`[x, y, z]` order. `ImagePixelsView::dimensions` is `[x, y, z, channel]`, and
the logical byte sequence is x-fastest:

```text
x + X * (y + Y * (z + Z * channel))
```

The implementation validates the standard physical HDF5 image layout
`[record, channel, z, y, x]` against the selected image header before the
binding reads pixels. `ImageDataType` preserves the standard ISMRMRD data-type
code; consumers must not assume that every image is float or magnitude.

## Qt boundary and KSpaceJet extensions

`ksj-viewer` builds an explicitly bounded, UI-owned display conversion inside a
reader callback. Image cine and auto/manual window-level trigger another
bounded read of the selected standard image; they do not retain source pixels.
That conversion is a visualization derivative, not a new MRI artifact. Its
presentation layer separately parses a public
`PipelineDefinition`; it does not resolve, load, compile, or execute it.

The Qt hierarchy has a separate selection and activation boundary. Opening an
MRD initially shows the semantic tree plus HDFView-style **Object Attribute
Info** and **General Object Info**; the typed-data area remains hidden.
Selecting a semantic tree item updates only those inspector pages. General
Object Info presents Name, Path, Type, and Access in a compact read-only form,
followed by standard dataset semantics and member tables. Explicit `Inspect`
or `Open As…` activates the selected verified object and may then request a
header-only acquisition record or one bounded image-plane callback. The XML
typed view opens only for an explicitly inspected Header/XML object; there is
no file-level metadata or image-series dashboard. Image series remain an
**Images** semantic object and a raw acquisition source may correctly have no
reconstructed image series. Object Attribute Info is a read-only table of
already-decoded standard Image `MetaAttributes`; it is not a generic HDF5
attribute browser or editor. This preserves the same bounded ownership rule at
the desktop interaction boundary.

The reader itself has no Qt dependency and performs no rendering, export,
Pipeline parsing, reconstruction, Provider loading, or gateway activity.
Waveform storage is classified during discovery, but waveform sample inspection
is not implemented yet.

Formal reconstructed images use only standard ISMRMRD image series (`image_0`,
`image_1`, and so on) with standard image headers, pixels, and
MetaAttributes—there is no `/ksj_recon` result group. The authored
PipelineDefinition is a separate required JSON input. If a future product case
requires embedding pipeline material in an explicitly derived file, it may add
one optional `/ksj_pipeline` group; it is never required for standard-file
discovery or reading, never changes the interpretation of standard content,
and is never written into a user-provided source file.
