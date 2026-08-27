# `ksj-viewer`

`ksj-viewer` is KSpaceJet's local, read-only Qt Widgets inspection application.
It is standard-ISMRMRD-first: a normal `.mrd`, `.h5`, `.hdf5`, or `.ismrmrd`
file is opened in place. The viewer never converts it to a KSpaceJet-private
format and never requires a group named `/dataset`.

## Standard MRD containers

Opening a file performs a bounded recursive HDF5 discovery pass. The navigation
tree keeps each container's visible label to its absolute HDF5 path; its
semantic child entries, tooltip, and General Object Info disclose standard
acquisition, image-series, waveform, and XML-header content:

- An XML-header container can expose standard metadata, acquisitions/k-space,
  waveforms, and named image series at any HDF5 path.
- A standalone standard image-series container with direct `header`,
  `attributes`, and `data` datasets can be selected and viewed even when it
  has no XML-header or raw-acquisition owner.
- A standard image artifact containing XML plus image series but no raw
  acquisitions is also directly viewable. This is the normal shape of a
  result-only ISMRMRD artifact.

The navigation tree chooses a discovered container rather than asking the user
to type a group name. Opening an MRD initially shows that semantic tree and a
single HDFView-style primary inspector: **Object Attribute Info** and
**General Object Info** are always present, while contextual **K-space**,
**XML**, and **Image** tabs appear only for the selected, active standard MRD
object. **Pipeline** is a peer tab only after a `PipelineDefinition` has been
opened successfully; there is no empty Pipeline placeholder or a second,
lower typed-data tab bar. Tree selection remains non-destructive with respect
to raw payloads: it updates object information and applicable tabs only.
Selecting a contextual tab, `Inspect`, `Open As…`, a double click, or the
semantic-object context menu explicitly activates its bounded inspection
surface; rendering K-space or image pixels still requires its explicit
bounded inspection action. Moving to another container clears K-space and
image display derivatives. The Pipeline view is independent, remains
parse-only, and renders only the bounded authored DAG—not a resolved or
runtime execution graph.

## Desktop workflow

The native C++/Qt shell follows the useful desktop workflow of HDFView without
copying or linking its Java/SWT implementation:

The Viewer opens its main workbench maximized by default, filling the desktop
work area while retaining the native minimize, maximize, and close controls.
This changes only the desktop presentation; standard MRD input remains local
and read-only.

1. Use **File** or `Ctrl+O` to open a local standard MRD file. The compact
   current-file bar also accepts a local path on Enter. **Recent Files** keeps
   the five most recently successfully opened local MRD or PipelineDefinition
   files, preserves their source type so each is reopened correctly, and
   survives restart; missing local entries are discarded on the next start.
   After any successful local MRD or PipelineDefinition open, the Viewer also
   stores that file's parent directory in user settings and uses it as the
   default folder for later Open dialogs, including after a restart; cancelling
   or a failed open does not replace it. URLs are rejected. The initial
   workspace is the semantic tree plus the selected object's inspector, not a
   file-level dashboard.
2. Inspect the left **ISMRMRD File Hierarchy**. It contains only recursively
   verified standard ISMRMRD semantic objects, not a generic HDF5 traversal.
   A container with no reconstructed image series or standard waveform storage
   has no corresponding **Images** or **Waveforms** tree entry: unavailable
   content is omitted instead of being shown as a disabled `(none)` action.
   The separate JSON source appears as a root-level **Pipeline** entry only
   after **File → Open pipeline** has successfully parsed a
   `PipelineDefinition`; it is not an empty MRD dataset placeholder.
3. Select an object to see its read-only **General Object Info** page. It
   follows HDFView's compact object form: Name, Path, Type, and Access, then
   standard dataset semantics and standard ISMRMRD member tables. The compact
   semantic table derives its height from the actual styled Qt rows and table
   frame, so its content is not clipped; any outer overflow belongs to the
   General Object Info scroll surface. **Object
   Attribute Info** separately shows the actual native HDF5 Attributes attached
   to the selected concrete standard-MRD object. Names and type/shape remain
   visible under a bounded read-only policy; oversized values are explicitly
   shown as omitted previews, and an object with no HDF5 Attributes says so
   explicitly. The semantic **Images** collection is not itself one HDF5
   object, and standard image `MetaAttributes` belong only in Image details,
   not in this tab. This is still not an arbitrary-path HDF5 attribute browser
   or editor. The bounded XML presentation belongs to the contextual **XML**
   tab: select the semantic Header/XML object and explicitly inspect it,
   rather than expecting a file-level metadata dashboard.
4. **K-space** follows General Object Info in the same primary tab group;
   **XML**, **Image**, and a successfully opened **Pipeline** are its peer
   contextual tabs. No second lower tab strip duplicates these pages. Use
   **Inspect**, **Open As…**, a double click, the context menu, or an
   applicable contextual tab to open an XML header, a Cartesian K-space view
   with selected ISMRMRD plane-coordinate metadata, an `image_x` image view, or a
   PipelineDefinition authored-DAG view. The File/Tools/Help menus, toolbar, info panel,
   flat split-pane layout, and dense tables follow the same hierarchy →
   inspector → bounded data-view pattern.

The bottom **Info** panel and status bar record open, close, inspection, and
export actions. The interface has no generic object editor, source-file save,
URL loader, or non-MRD format support.

Formal reconstruction output uses only standard ISMRMRD image series such as
`image_0`, `image_1`, and so on; it never needs `/ksj_recon`. The authored
PipelineDefinition remains a separate required JSON input. If a future product
case truly needs to embed pipeline material in an explicitly derived file, it
may reserve one optional `/ksj_pipeline` extension, but that extension is never
a prerequisite for opening, viewing, or reconstructing a standard ISMRMRD
file. The viewer never changes the source file.

## Read-only views and exports

- **XML** opens only after the selected standard Header/XML object is
  explicitly inspected. It opens in **XML Tree** mode: a bounded hierarchical
  element view with a compact header summary. An explicit view switch changes
  the same content area to **XML Text** mode, a syntax-highlighted read-only
  textual XML presentation; the two modes are not shown side by side. Text
  indentation may be normalized for presentation only: it never rewrites or
  writes the source XML. This is not an arbitrary HDF5/XML browser;
  it remains limited to the selected standard ISMRMRD header and its configured
  read bound. It does not show a file-level overview or an empty image-series
  table.
- Image series belong to the semantic **Images** object and its Image typed
  view. A raw-acquisition source normally has zero reconstructed image series;
  that is expected data state, not missing viewer content.
- **K-space** is a bounded raw Cartesian K-space viewer, not a single-readout
  `sample × channel` projection. Its **Data** selector defaults to renderable
  formal **Imaging data** when available; otherwise it uses the first
  renderable auxiliary type, such as Noise measurement, Navigation, or
  Surface-coil correction. Its arrShow-style compact dimension strip always
  assigns exactly two distinct `:` selection tags to the display plane,
  initially **Readout** × **Phase encode**. Left-clicking a column's bottom
  extent cell chooses or replaces the blue tag; right-clicking chooses or
  replaces the red tag. The tag colour identifies the replacement target, not
  a column position: the Viewer then scans dimension columns from left to
  right: the first selected column is X and the second is Y. The displaced
  column becomes a real observed index. Thus **Raw coil** can be an axis—for
  example **Readout** × **Raw coil** with **Phase encode** fixed at `0`—rather
  than a separate coil control. Every other varying standard field is an
  observed, sparse coordinate; singleton coordinates are not shown as
  controls. Each changer has a concise top abbreviation and retains its full
  semantic name in its tooltip. The Viewer never exposes a source record,
  ordinal, or Frame selector. A selected auxiliary type is explicit raw data,
  never silently mixed into the imaging grid. The plane refreshes
  automatically when Data, either `:` selection tag, a fixed dimension, or a display
  setting changes; no separate Render command is needed. Its compact first
  control row contains Data, display, C/W, and View controls; the full-width
  second **Dimensions** row contains the arrShow-style observed-value changers
  so they do not crowd display controls. When **Phase encode** is an axis, the
  Viewer preserves the declared XML imaging range (or the inclusive observed
  auxiliary range): missing phase lines remain empty bins instead of being
  compacted into adjacent rows.
- Image inspection uses the same arrShow dimension-strip and display base, but
  the standard native **X** and **Y** columns are fixed `:` axes and cannot be
  reassigned. **Z**, **Channel**, and any later visible image dimension are
  fixed-coordinate selectors only. It offers ordinal cine, component
  selection, independent normal/phase C/W, zoom, pixel probe, and histogram;
  each update rereads the selected standard image and retains only bounded
  presentation data.
- Waveform storage is discovered and labelled, but waveform sample inspection
  is not implemented yet.
- **File → Open pipeline** parses a public `PipelineDefinition`, displays its
  bounded authored DAG (ingress, Provider-alias/operator nodes, egress, explicit
  data edges, and dashed calibration dependencies), and retains canonical
  authored JSON for inspection. It never resolves, loads, compiles, or
  executes a Provider; the graph does not assert contract ports, resources,
  scheduling, or runtime state.
- PNG, SVG, CSV, and JSON exports are marked `visualization-derivative`; they
  are local display data, not MRI artifacts. The exporter rejects `.mrd`,
  `.h5`, `.hdf5`, and `.ismrmrd` destinations so a display derivative cannot
  be mistaken for an MRI artifact.

The viewer never reconstructs, loads a Provider, connects a gateway, or
becomes a runtime/data-plane dependency.

### Shared arrShow display foundation

Image and K-space share the app-local `ArrShowDisplay` renderer and native Qt
Widgets `InspectionCanvas`. They are a bounded C++/Qt port of the applicable
display and interaction semantics in [arrShow](https://github.com/tsumpf/arrShow),
with the required attribution and Boost Software License 1.0 text in
[Third-party notices](THIRD_PARTY_NOTICES.md).

This is deliberately the viewer-facing part of arrShow, not a MATLAB
environment: KSpaceJet does not embed MATLAB, distribute arrShow MATLAB source
or assets, read or mutate a MATLAB workspace, or add destructive FFT, crop,
ROI, coil-combine, script, or array-edit operations.

The common display contract is:

- The component chooser is **Magnitude**, **Real**, **Imaginary**,
  **Complex**, and **Phase**, in arrShow's order. Complex source data defaults
  to **Complex**. Real source data exposes Magnitude and Real only; an
  unavailable complex-only selection falls back to Real rather than inventing
  an imaginary component.
- Magnitude, Real, and Imaginary use `Gray(256)` C/W. Complex uses arrShow's
  `complex2rgb` convention: `martin_phase(256)` indexes phase and magnitude
  C/W controls brightness, including its raw-magnitude pure-phase rule and
  `Cmin`-clamp/`Cmax`-divide convention. Phase uses the same 256-tone
  `martin_phase` map against its own phase C/W. Phase is shown in degrees by
  default and can be switched to radians.
- Range calculation is **Min / max** by default, with the optional symmetric
  percentile range (98% initially). Normal-value C/W and phase C/W are
  independent. The **C/W across planes** control preserves all three
  arrShow policies: **Reset per plane** uses the next plane's full current
  range, **Keep relative** carries C/W as fractions of that range, and
  **Keep absolute** retains center/width in the active component's units.
- Hover shows a crosshair and the current bounded presentation's coordinates,
  real part, imaginary part, magnitude, and phase when available. Drag with
  the left button to pan; `Ctrl` + wheel zooms around the cursor in 1.5× steps;
  **Fit** (Image) or **Reset** (K-space), or `F`, returns to the fit scale.
  Mouse wheel and `+`/`-` step the active fixed dimension on both Image and
  K-space; `Left`/`Right` choose another fixed dimension. On K-space, the
  lower extent cell changes one of the two `:` selection tags; the selected
  columns' left-to-right order defines X/Y. On Image those extent cells are
  locked because native X/Y define the image plane. Neither action steps
  source records or a Frame.
  Middle-drag adjusts the active
  C/W with live redraw, and double-click or `0` restores the current plane's
  full-range C/W.

The canvas and renderer own only a bounded `QImage` plus bounded display and
export rows. They never retain borrowed `InspectionReader` image pixels or
acquisition views, and all CSV/JSON/PNG/SVG output remains a
`visualization-derivative`, not an MRI artifact. When a Complex or Phase
presentation includes RGB CSV columns, those are explicitly labelled
C/W-dependent display derivatives; its real/imaginary/magnitude/phase columns
remain raw bounded samples.

### Cartesian K-space Viewer

The K-space view needs a standard ISMRMRD Cartesian XML encoding. It first
indexes headers without materializing their payloads. The **Data** selector
lists only standard flag-membership types with a renderable compatible
Cartesian coordinate catalog: Noise measurement, Parallel calibration,
Navigation, Phase correction, High-performance feedback, Real-time feedback,
Dummy scan, Surface-coil correction, Phase-stabilization reference, and Phase
stabilization. It prefers **Imaging data** when renderable; otherwise its
first stable auxiliary entry becomes the default. The selected type determines
the available renderable Cartesian coordinates and all subsequently included
records; it is a standard-flag filter, not a source-record selector.

The Imaging data filter contains records without an auxiliary membership and
retains `parallel_calibration_and_imaging`, because that record explicitly
contributes to imaging. A calibration-only record is instead in Parallel
calibration. Auxiliary filters are independent memberships: a record carrying,
for example, both Navigation and Surface-coil-correction flags appears in both
explicit choices, and their displayed counts may overlap. The viewer never
mixes different selected types in one grid.

If a selected container contains only auxiliary acquisitions, its first
renderable auxiliary type is selected and rendered immediately; the Viewer
never starts on an empty `Imaging data (0)` entry. Imaging data keeps the XML
`kspace_encode_step_1` bound as a hard eligibility check for every included
line. A candidate coordinate containing an out-of-bound imaging line is
skipped in favour of another independent valid coordinate when one exists; if
none exists, Imaging data is not selectable. An explicitly selected auxiliary
type uses its observed `kspace_encode_step_1` range instead, since auxiliary
lines may legitimately lie outside the imaging encoding limit. The catalog
keeps every dimension as an actual, sparse source coordinate, so unrelated
slices, contrasts, cardiac phases, repetitions, or partitions are never
silently mixed. The control strip follows arrShow's five-cell changer: a top
abbreviation, `+`, current value (or `:`), `-`, and bottom observed extent. It
starts with **Readout** and **Phase encode** as the two `:` selections.
Left-click a bottom extent to choose or replace the blue selection tag;
right-click chooses or replaces the red tag. The tag colour identifies the
current selection even after it has moved to another column. The two selected columns are
always normalized in displayed-column order: left is X and right is Y. **Raw
coil**, **Encoding space**, **Partition**, **Average**, **Slice**,
**Contrast**, **Physiological phase**, **Repetition**, **Set**, **Segment**,
and source-defined **User 0** through **User 7** appear only when they have
more than one actual observed value, unless they currently hold a `:` selection.
A non-axis column shows its actual zero-based source value; singleton
coordinates create no unnecessary column. Every displayed changer
has a concise top label: **RO** (Readout), **PE** (Phase encode), **Co** (Raw
coil), **Enc** (Encoding space), **Par** (Partition), **Avg** (Average), **Slc**
(Slice), **Con** (Contrast), **Pha** (Physiological phase), **Rep**
(Repetition), **Set**, **Seg** (Segment), and **U0**–**U7** (source-defined
User 0–7). Its full semantic name remains available through the control
tooltip. The **Data** selector is a standard-flag membership filter, not a
tensor axis or a source-record selector.

Each displayed coordinate value is the zero-based value carried by the source,
not a one-based viewer ordinal. The strip contains only actual, bounded,
renderable coordinates. It never constructs a Cartesian product from XML
limits: choosing a fixed value or replacing a `:` selection switches to an actual
compatible sparse coordinate and synchronizes the other fixed controls when a
requested combination is unavailable. There is no separate coil selector,
acquisition-record selector, or Frame concept.

Each selected display axis has a direct raw source: **Readout** is each
included acquisition's valid sample coordinate
`sample - center_sample`; **Phase encode** is
`idx.kspace_encode_step_1`; **Raw coil** is the payload channel; all other
dimensions are standard header fields. Therefore any two observed dimensions
can form the raw plane. If **Raw coil** becomes an axis, every participating
channel is rendered independently; if it is fixed, exactly one raw channel is
read. Imaging's XML phase-encode bound remains an input-eligibility check even
when Phase encode is a fixed coordinate or is not displayed. **Physiological
phase** is a cardiac/physiological counter; **Partition** is a 3-D partition
coordinate. **Average** remains the standard average counter (a protocol may
use it for NEX/NSA), and **Contrast** remains the standard contrast counter
(for example, an echo number in a multi-echo protocol); the Viewer does not
infer either nickname. The eight **User counters** remain source-defined. The
full selected `:` columns, normalized X/Y axes, and fixed coordinates remain in JSON export provenance
rather than a permanent side panel.

There is no RSS or other coil-combine projection, no logarithmic intensity
transform, and no Fourier transform. A one-to-one display cell contains its
raw complex sample; only repeated raw cells or bounded downsampling use an
explicitly reported complex mean. No later acquisition silently overwrites an
earlier one. Empty cells are complex zero, and the JSON export reports
occupied, empty, and multi-contribution-cell counts.

The same `ArrShowDisplay` foundation used by Image renders the selected raw
complex K-space plane. It defaults to **Complex**, so `martin_phase(256)`
shows raw phase and magnitude C/W controls brightness; Magnitude, Real,
Imaginary, and Phase are also available. The view is still raw K-space, not a
reconstructed image, inverse Fourier transform, or new MRD artifact. CSV rows
describe bounded *display bins*, including represented X/Y axis coordinate
ranges, real/imaginary/magnitude/phase values, and contribution counts.
Complex and Phase views append explicitly
labelled `red`/`green`/`blue` display-derivative columns; those colours follow
the active C/W, while the raw complex columns remain unchanged.

The current view accepts at most 16,384 matching acquisition lines and 32 Mi
complex source values, and creates no more than 2,048 pixels along either axis
or 2 Mi display cells. It retains only the resulting display derivative and
owned header records, never a raw-acquisition cache. A limit, malformed shape,
or non-finite value fails the render rather than silently truncating source
data.

This is intentionally a Cartesian-only view. It rejects an acquisition with
trajectory samples, a non-Cartesian XML encoding, or matching records with
inconsistent Cartesian geometry. It does not grid radial, spiral, or other
trajectory-space data into a fabricated Cartesian matrix; a future
trajectory-space viewer must be an explicit separate mode.

See the [ISMRMRD inspection reader contract](../../docs/architecture/KSpaceJet_ismrmrd_inspection_reader.md)
for the ownership model, bounded discovery, image axes, and metadata rules.

On Windows, the target uses the native GUI subsystem, so starting it from
Explorer does not create a separate console window. Qt's `windeployqt`
deploys the dynamic platform plugin required by the application. When the
viewer is launched from an existing PowerShell or Command Prompt, it still
inherits that terminal's stdout/stderr handles for the automation checks below:

```powershell
ksj-viewer --ui-smoke --format json
ksj-viewer --export-smoke --format json
```

`--export-smoke` creates, atomically publishes, and reads back a temporary PNG
display derivative. It is used by the Windows install smoke to verify that the
deployed Qt runtime can write PNG even though optional `imageformats` plugins
are not installed.
