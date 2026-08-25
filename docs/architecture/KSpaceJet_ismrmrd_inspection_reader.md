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
- `InspectionObjectLocator` selects only the open container itself or its
  standard `xml`, `data`, `waveforms`, or advertised named image-series
  object. It does not accept an arbitrary HDF5 path.
- `InspectionObjectAttributeDescriptor` owns the name, readable HDF5 type,
  dataspace shape, element count, and a bounded preview of one actual native
  HDF5 Attribute. Unsupported value types remain descriptors, but their source
  value is not materialized.
- `InspectionAcquisitionView` contains a copied standard acquisition header and
  borrowed samples/trajectory for one zero-based acquisition ordinal.
- `InspectionAcquisitionHeaderRecord` owns one zero-based acquisition ordinal
  and copied standard acquisition header, without samples or trajectory. It is
  the safe input to a caller-owned, explicitly bounded acquisition index.
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

`for_each_acquisition_header(consumer, error)` is the header-only companion to
payload iteration. It invokes its consumer with an owned
`InspectionAcquisitionHeaderRecord` for each standard acquisition after the
same named-field preflight, but never materializes that acquisition's samples
or trajectory. A consumer can stop the traversal normally, and can retain
records only within its own explicit bounds. `ksj-viewer` uses this path to
select a raw Cartesian frame before it requests any callback-scoped payload.

Each reader receives `InspectionReadLimits`. Before materializing a payload,
the implementation checks standard HDF5 rank, record count, named fixed-header
compound fields, logical shape, VLEN element type, integer arithmetic, and the
corresponding byte/count limit. Malformed, out-of-range, and oversized input
therefore fails deterministically instead of retaining an entire MRD file.

## Native HDF5 object attributes

`read_object_attributes(locator, attributes, error)` is the narrow inspection
API behind the Viewer **Object Attribute Info** tab. It has a deliberately
different meaning from ISMRMRD image `MetaAttributes`:

- HDF5 object attributes are generic key/value metadata physically attached to
  a selected HDF5 group or dataset, such as a dataset description or units.
- ISMRMRD image `MetaAttributes` are standard per-image metadata stored in the
  image-series `attributes` dataset; they remain part of
  `InspectionImageRecord` and Image details.

The method accepts only an `InspectionObjectLocator` for the already-open,
verified standard MRD container. It neither traverses arbitrary HDF5 paths nor
edits source content. Attribute count, name length, rank, scalar element count,
materialized value bytes, and returned preview bytes are independently bounded
by `InspectionReadLimits`. A successful empty descriptor list means that the
selected HDF5 object simply has no attached HDF5 Attributes; it is not an
error. A numeric or fixed-string value that exceeds the element or byte budget
also remains visible as a descriptor with a truncated/omitted preview, so one
large Attribute cannot hide the other structural information. Unsupported
types are reported as bounded descriptors with an unsupported preview state
instead of being silently read as another type.

The selector preserves the standard MRD distinction between raw and image-only
containers. A standalone `image_x` series can expose Attributes on that series
or its advertised image series, but cannot be asked for XML, acquisitions, or
waveforms; its pixel `data` dataset is never relabelled as raw acquisitions.
Likewise, the waveform selector accepts only a validated standard ISMRMRD
waveform dataset, not an arbitrary sibling named `waveforms`.

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

## Cartesian K-space display semantics

The reader itself does not render K-space. Its header-only and payload callback
boundaries allow `ksj-viewer` to build one bounded raw **Cartesian** display
plane without creating a second MRI format or retaining source payloads.

The Viewer selects one reference acquisition and parses the container's
standard ISMRMRD XML encoding. It requires both a Cartesian XML trajectory and
zero `trajectory_dimensions`; it rejects non-Cartesian samples rather than
misrepresenting radial, spiral, or other trajectory-space data as a Cartesian
matrix. The reference fixes this complete frame key:

- `encoding_space_ref`, `idx.kspace_encode_step_2`, `idx.average`,
  `idx.slice`, `idx.contrast`, `idx.phase`, `idx.repetition`, `idx.set`, and
  `idx.segment`;
- the eight `idx.user` counters; and
- zero trajectory dimensions and a consistent active-channel count across
  matching acquisitions.

The Viewer intentionally leaves `idx.kspace_encode_step_1` out of the frame
key because it supplies the vertical raw K-space axis. It obtains matching
headers first, then reads only those acquisition payloads one at a time. The
horizontal raw coordinate is `sample - center_sample`; source samples covered
by `discard_pre` or `discard_post` are excluded. The vertical range is the
standard XML `kspace_encode_step_1` limit when supplied, otherwise the observed
matching range. Thus the selected `kspace_encode_step_2` is fixed while the
display represents the complete selected `kspace_encode_step_1` plane.

For each source coordinate, the Viewer either sums squared complex magnitude
over all active coils (RSS mode) or uses one selected coil. It then takes the
RMS over every raw contribution that maps to a display cell. Repeated raw
coordinates and coordinates grouped by display downsampling therefore have
defined RMS aggregation instead of last-write-wins behavior. Empty cells are
zero and are counted explicitly. The displayed grayscale value is
`log10(1 + RMS magnitude)`; it is a visualization derivative only, never an
inverse transform, reconstructed image, or MRD artifact.

The current presentation applies independent hard bounds: at most 16,384
matching acquisition lines, 32 Mi complex source values, 2,048 pixels along
each output axis, and 2 Mi output cells. It reports source/display geometry,
frame key, coil mode, empty cells, and multi-contribution cells in its local
JSON derivative. CSV uses display-bin coordinate ranges and contribution counts
and is also bounded. Limit overflow, a malformed payload, inconsistent matching
headers, or a non-finite source sample fails deterministically rather than
silently discarding raw information.

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
followed by standard dataset semantics and member tables. The small semantics
table fits its actual styled Qt row heights and frame rather than estimating a
fixed row height, so General Object Info never clips an object-semantic row;
the enclosing General Object Info scroll surface owns any outer overflow.
Explicit `Inspect`
or `Open As…` activates the selected verified object and may then request a
header-only acquisition record, build a bounded Cartesian K-space header index,
or request one bounded image-plane callback. The XML typed view opens only for
an explicitly inspected Header/XML object; there is no file-level metadata or
image-series dashboard. Image series remain an
**Images** semantic object and a raw acquisition source may correctly have no
reconstructed image series. In that case, and when no standard waveform storage
is discovered, the semantic tree omits the respective child rather than showing
an unavailable `(none)` pseudo-object. A separately opened, successfully parsed
`PipelineDefinition` appears as the root-level **Pipeline** semantic source;
when no pipeline is open, no empty pipeline placeholder is shown. Object Attribute Info is a read-only table of the
selected concrete semantic object's actual bounded HDF5 Attributes; it never
repurposes Image `MetaAttributes`, which stay in Image details. The `Images`
tree item is a semantic collection rather than a single HDF5 object, so it does
not invent an aggregate attribute list. This remains a restricted standard-MRD
inspector, not a generic HDF5 attribute browser or editor.

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
