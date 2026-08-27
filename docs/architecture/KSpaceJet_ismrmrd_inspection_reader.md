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
discover a bounded catalog of renderable raw Cartesian coordinates before it
requests any callback-scoped payload.

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

The Viewer shares its arrShow display and dimension-strip base between image
and raw K-space presentation, but standard image geometry is intentionally
not an arbitrary plane browser: native `x` and `y` are the first two fixed
`:` columns, in that order. `z`, `channel`, and any later visible image
dimension are fixed-coordinate selectors only and cannot be promoted to `:`.
This preserves the standard meaning of an ISMRMRD image while still providing
the same active-dimension, `+`/`-`, and wheel navigation mechanics used for
fixed K-space coordinates.

The implementation validates the standard physical HDF5 image layout
`[record, channel, z, y, x]` against the selected image header before the
binding reads pixels. `ImageDataType` preserves the standard ISMRMRD data-type
code; consumers must not assume that every image is float or magnitude.

## Cartesian K-space display semantics

The reader itself does not render K-space. Its header-only and payload callback
boundaries allow `ksj-viewer` to build one bounded raw **Cartesian** display
plane without creating a second MRI format or retaining source payloads.

The Viewer parses the container's standard ISMRMRD XML encoding and discovers
a bounded catalog of actual compatible Cartesian source coordinates for the
selected acquisition type. The **Data** selector lists only types with such a
catalog. It defaults to **Imaging data** when renderable, otherwise to the
first renderable standard auxiliary flag-membership type: Noise measurement,
Parallel calibration, Navigation, Phase correction, High-performance feedback,
Real-time feedback, Dummy scan, Surface-coil correction, Phase-stabilization
reference, or Phase stabilization. It requires both a Cartesian XML trajectory
and zero `trajectory_dimensions`; it rejects non-Cartesian samples rather than
misrepresenting radial, spiral, or other trajectory-space data as a Cartesian
matrix.

Imaging data means a record with no auxiliary membership; it retains
`parallel_calibration_and_imaging`, while a calibration-only record belongs to
Parallel calibration. Each auxiliary choice is an independent standard-flag
membership predicate, so a record carrying multiple flags, such as Navigation
and Surface-coil correction, deliberately appears in each relevant explicit
choice and option counts can overlap. Header discovery, bounded catalog
aggregation, and the payload callback all apply the same selected-type
predicate. A selected container with no renderable Imaging coordinate instead
defaults to the first renderable auxiliary type, so the active Data entry is
always real data rather than an empty category. An Imaging record outside the
declared XML encoding limit for any mapped acquisition counter is ineligible:
`kspace_encode_step_1`, `kspace_encode_step_2`, average, slice, contrast,
phase, repetition, set, segment, and `user_0`–`user_7`. Readout, raw coil, and
encoding-space reference are not inferred from an unrelated XML limit. An
explicitly selected auxiliary type uses its observed phase-encode range because
its indices may legitimately lie outside the imaging limit.

The Viewer presents the catalog through an arrShow-style dimension strip, not
a source-record or plane-list selector. Exactly two distinct dimensions carry
the `:` selection tags. The initial selections are **Readout** × **Phase
encode**; left-clicking a column's lower extent chooses or replaces the blue
tag and right-clicking chooses or replaces the red tag. Tag colour identifies
the replacement target even when the selected columns have moved. The Viewer derives
geometry only afterwards: it scans the displayed columns from left to right,
so the first selected column is X and the second is Y. The displaced selection
becomes a fixed observed value. Any varying observed
dimension may take either role: **Raw coil**, **Encoding space**,
**Partition**, **Average**, **Slice**, **Contrast**, **Physiological phase**,
**Repetition**, **Set**, **Segment**, and source-defined **User 0** through
**User 7** are not a separate plane model. A non-axis field appears only when
it has more than one observed value and shows `+`, its current zero-based
source value, `-`, and its observed-value count. Axis fields remain visible
and show `:`. Every changer has a concise top label—**RO**, **PE**, **Co**,
**Enc**, **Par**, **Avg**, **Slc**, **Con**, **Pha**, **Rep**, **Set**, **Seg**,
or **U0**–**U7**—while its full ISMRMRD meaning is in the tooltip.

The catalog contains only actual observed and renderable coordinates with zero
trajectory dimensions; it never derives a Cartesian product from XML limits.
When a user selects a fixed value or changes a `:` selection tag, the Viewer resolves
to an actual compatible sparse coordinate and resynchronizes the other fixed
fields if needed. It never exposes a source record, ordinal, or Frame. Normal
wheel and `+`/`-` step the active fixed dimension without wrapping; `Left` and
`Right` choose another fixed dimension; `Ctrl` + wheel remains canvas zoom. If
the two axis columns are the only visible dimensions, no active browsing
dimension exists.

Each display axis maps directly to raw source data: **Readout** is
`sample - center_sample` after excluding `discard_pre`/`discard_post` samples;
**Phase encode** is `idx.kspace_encode_step_1`; **Raw coil** is the payload
channel; and every remaining dimension is a standard acquisition-header
field. Thus **Readout** × **Raw coil** with **Phase encode** fixed to `0`
actually reads the raw samples/channels for that coordinate. If coil is fixed,
exactly one raw channel is read; if coil is an axis, participating channels are
separate axis values. A one-to-one display cell contains its raw complex
sample; repeated raw coordinates and source coordinates grouped by bounded
downsampling use an explicit complex arithmetic mean rather than
last-write-wins behavior. Empty cells are complex zero and are counted
explicitly. When Phase encode is a display axis, imaging uses every coordinate
in the declared XML imaging range and auxiliary data uses the inclusive
observed range, so a missing acquired line remains an empty bin rather than
shifting neighboring lines. The bounded real and imaginary planes then enter the shared
app-local `ArrShowDisplay` renderer: Magnitude, Real, Imaginary, Complex or
Phase, with the applicable `Gray(256)` or `martin_phase(256)` C/W mapping. It
never applies RSS, a logarithmic intensity transform, Fourier transform,
gridding, or reconstruction. The result is a visualization derivative only,
never a reconstructed image or MRD artifact.

The current presentation applies independent hard bounds: at most 16,384
matching acquisition lines, 32 Mi complex source values, 16,384 observed
values per source/display dimension, 2,048 pixels along each output axis, and
2 Mi output cells. It reports dynamic X/Y axes, source/display geometry,
fixed observed coordinates, coil mode, empty cells, and multi-contribution
cells in its local JSON derivative. CSV uses bounded display-bin X/Y coordinate
ranges and contribution counts. Limit overflow, a malformed payload, a source
change during read, or a non-finite source sample fails deterministically
rather than silently discarding raw information.

## Qt boundary and KSpaceJet extensions

`ksj-viewer` builds an explicitly bounded, UI-owned display conversion inside a
reader callback. Image cine and arrShow-style C/W changes trigger another
bounded read of the selected standard image; they do not retain source pixels.
Normal-value and phase C/W remain independent, and the next plane can reset
C/W, retain it relatively, or retain it absolutely. That conversion is a
visualization derivative, not a new MRI artifact. Its presentation layer separately parses a public
`PipelineDefinition`; it does not resolve, load, compile, or execute it.

The Qt hierarchy has a separate selection and activation boundary. Opening an
MRD initially shows the semantic tree plus the HDFView-style **General Object
Info** page; the typed-data area remains hidden. Selecting a semantic tree item
updates that inspector page. General Object Info presents Name, Path, Type, and Access in a compact read-only form,
followed by standard dataset semantics and member tables. The small semantics
table fits its actual styled Qt row heights and frame rather than estimating a
fixed row height, so General Object Info never clips an object-semantic row;
the enclosing General Object Info scroll surface owns any outer overflow.
Explicit `Inspect`
or `Open As…` activates the selected verified object and may then request a
header-only acquisition record, build a bounded Cartesian K-space header index,
or request one bounded image-plane callback. The XML typed view opens only for
an explicitly inspected standard Header/XML object. Its default **XML Tree**
mode renders that owned, bounded header as a hierarchical element view with a
compact header summary. An explicit mode switch replaces that same content
area with **XML Text**, a syntax-highlighted read-only textual XML
presentation; the tree and text are not simultaneous side-by-side panes. Text
indentation may be normalized for presentation only: it never rewrites or
writes source XML, and this is not an arbitrary HDF5/XML browser. There is no file-level
metadata or image-series dashboard. Image series remain an
**Images** semantic object and a raw acquisition source may correctly have no
reconstructed image series. In that case, and when no standard waveform storage
is discovered, the semantic tree omits the respective child rather than showing
an unavailable `(none)` pseudo-object. A separately opened, successfully parsed
`PipelineDefinition` appears as the root-level **Pipeline** semantic source;
when no pipeline is open, no empty pipeline placeholder is shown. Native HDF5
object attributes are not a Viewer surface. Standard per-image
`MetaAttributes` remain in Image details. This remains a restricted
standard-MRD inspector, not a generic HDF5 browser or editor.

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
