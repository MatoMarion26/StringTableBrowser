# String Table Browser — How It Works

A plain-language explanation of the plugin's architecture for developers who are not familiar
with Unreal's Slate UI framework or editor tooling APIs.

## The Big Picture

The plugin does three things:

**Maintains a searchable database** of every string table entry in your project, kept up to
date automatically.

**Provides two ways to interact with that database** — a standalone panel you can open from
the Tools menu, and a search button injected into every FText property in the Details panel.

**Exposes a Project Settings page** so teams can configure how the Details panel button is
placed and tune performance parameters without touching code.

## Part 1 — The Module (`FStringTableBrowserModule`)

Think of the module as the plugin's "brain". It is the first thing Unreal loads when the
plugin starts, and the last thing unloaded when the editor closes. Everything else in the
plugin depends on it.

### What a Module Is

Every feature in Unreal — including the editor itself — is organized into modules. A module
has two lifecycle methods: `StartupModule` (runs when the plugin loads) and `ShutdownModule`
(runs when it unloads). You use these to set things up and tear them down cleanly.

### The Static Helper

The module exposes a static `GetModulePtr()` helper:

```cpp
static FStringTableBrowserModule* GetModulePtr()
{
    return FModuleManager::GetModulePtr<FStringTableBrowserModule>("StringTableBrowser");
}
```

This is the only correct way for Slate widgets and other code to access the module. It returns
`nullptr` if the module is not loaded, avoiding the verbose and error-prone
`IsModuleLoaded` / `GetModuleChecked` pattern that was used previously. Every widget that
subscribes to `OnCacheUpdated` or calls `GetCachedEntriesCopy` uses `GetModulePtr()` as its
entry point.

### The Cache

String table assets can contain hundreds or thousands of entries spread across many files.
Scanning all of them every time the panel opens would be slow. Instead, the module builds a
cache — an in-memory copy of all entries — and keeps it updated.

The cache has two layers:

**GroupedCache** — a dictionary (`TMap`) where each key is a string table asset's package name
and each value is the list of entries inside it. This structure makes it easy to add, remove,
or update a single table without rebuilding everything.

**FlatCache** — a flat list of all entries from all tables. This is what the UI reads from.
It is rebuilt from `GroupedCache` after every mutation.

The cache is also **written to disk** as a JSON file (`Saved/StringTableBrowserCache.json`).
On the next editor launch, this file is loaded first so the panel is populated instantly
without scanning all assets again. The file includes a version number — if the plugin has been
updated and the schema changed, the old file is discarded and a fresh scan runs.

### Debounced Disk Writes

`SaveCacheToDisk` is not called directly from Asset Registry event handlers. Instead, each
handler calls `ScheduleDiskCacheSave`, which sets a dirty flag and resets a configurable timer
(`SaveCacheToDiskDelay`, exposed in Project Settings). Only one write fires after the burst
settles. `ForceRebuildCache` bypasses the debounce and writes immediately since it is triggered
explicitly by the user. `BroadcastCacheUpdated` is called at the end of `SaveCacheToDisk`,
coupling the UI refresh notification to the disk write cycle.

### Staying Up To Date

Unreal has an **Asset Registry** — a service that knows about every asset in the project
without having to load them all into memory. The module subscribes to three events on it:

`OnAssetAdded` — fires when a new string table is created or imported.

`OnAssetRemoved` — fires when one is deleted.

`OnAssetUpdated` — fires when the asset's **registry metadata** (tags, dependencies) changes.
This covers structural edits such as adding or removing keys, since string table keys are
stored as Asset Registry tags.

The module also subscribes to:

`UPackage::PackageSavedWithContextEvent` (`OnPackageSaved`) — fires unconditionally whenever
any package is saved to disk, regardless of whether registry metadata changed. This catches
**value-only edits** that `OnAssetUpdated` misses: saving a string table after changing only
string values leaves the registry tags identical and never triggers `OnAssetUpdated`. Both
events are subscribed simultaneously; `OnPackageSaved` walks the saved package with
`ForEachObjectWithPackage` to check whether it contains a `UStringTable` before acting.

When any of these fire, the module updates only the affected table in `GroupedCache`, rebuilds
`FlatCache`, schedules the JSON write, and then tells any open UI to refresh.

### Thread Safety

The Asset Registry callbacks can sometimes arrive from a background thread. If the UI is
reading from `FlatCache` at the same moment the background thread is writing to it, the program
can crash or produce corrupted data. To prevent this, all writes to the cache are wrapped in a
`FCriticalSection` — a lock that only allows one thread to access the data at a time. The UI
always reads a **copy** of the cache (not the live version) so it never holds the lock for
long.

`GetCachedEntriesCopy` acquires the lock, copies `FlatCache` into a local snapshot, releases
the lock, and then returns the snapshot. All `TSharedPtr` reference-count increments happen
after the lock is released, not inside it — keeping the lock hold as short as possible.

All cache mutation lock scopes end before any I/O or delegate broadcast. `SaveCacheToDisk`
takes a snapshot of `GroupedCache` under the lock and does all serialisation outside it.

### Telling the UI to Refresh

The module has a **delegate** called `OnCacheUpdated`. A delegate is essentially a list of
functions to call when something happens — like an event system. When the cache changes, the
module broadcasts this delegate. Any UI widget that has subscribed to it will receive the
notification and redraw itself. Because Slate is not thread-safe, the broadcast is always
marshalled to the game thread before firing.

The broadcast is made from within `SaveCacheToDisk` after the file is written. When the
broadcast needs to be marshalled to the game thread via `AsyncTask`, the lambda re-resolves the
module pointer via `GetModulePtr()` rather than capturing `this` — avoiding a dangling pointer
if the module is destroyed before the queued task fires.

## Part 2 — The Shared Types (`StringTableBrowserTypes.h`)

A lightweight header with no heavy dependencies that is included wherever shared data types
are needed.

**`FStringTableBrowserEntry`** — a single row of data displayed in the browser and picker.
Stores `TableId` (short asset name), `AssetPath` (soft reference to the owning `UStringTable`
asset — stored as a soft path so assets are not kept loaded just because they appear in the
cache), `Key`, and `Value`.

**`StringTableBrowserColumns`** — a namespace of `inline const FName` constants holding the
column identifiers (`Key`, `Value`, `Source`, `Actions`) used by both `SStringTableBrowserRow`
and `SPickerRow`. Centralising these prevents silent mismatches if a column name changes.

**`StringTableBrowserIcons`** — a namespace of `inline const FName` constants holding the
Slate brush names used for action buttons (`Edit`, `Copy`, `FindReferences`, `Check`,
`OpenBrowserSearch`, `OpenBrowserWindow`). Both row widgets and the Details customization
reference the same constants rather than bare string literals.

## Part 3 — The Shared Helpers (`FStringTableBrowserHelpers`)

A static helper class that holds logic used by more than one widget, preventing duplication.

**`MakeFilterCheckBox`** — builds a labelled toggle checkbox whose state is forwarded to the
caller via a callback. Used for mode toggles (Match Case, Whole Word, Regex) and scope toggles
(Keys, Values, String Tables) in both `SStringTableBrowser` and
`SStringTableBrowserPickerDropdown`. Previously a duplicated local lambda in each widget's
`Construct`; promoted to a shared static function when the picker was added.

**`CopyStringTableEntry`** — copies the `LOCTABLE()` reference for a given entry to the
clipboard using the cross-platform `FPlatformApplicationMisc::ClipboardCopy`. Used by both the
main browser and the picker. The reference uses `AssetPath.ToString()` as the table identifier
because `FStringTableRegistry` registers tables by their full object path, not by short name —
so the asset path is what `LOCTABLE()` and `FText::FromStringTable` resolve against at runtime.

## Part 4 — The Shared Filter (`FStringTableSearchFilter`)

Both UI widgets — the main panel and the Details panel dropdown — need identical search
behaviour. Rather than writing the logic twice, it lives in a single struct that both widgets
own as a member.

### What It Holds

- The current search text
- Toggle states: Match Case, Whole Word, Regex
- Scope states: whether to search Keys, Values, and/or Table names
- A compiled regex pattern (built once per search change, not once per row)
- A validity flag for the pattern

### How It Works

When the user changes the search text or any toggle, the widget calls `Compile()`. This builds
and validates the regex pattern if needed. Then for every entry in the cache, the widget calls
`PassesFilter(Entry)`, which returns true or false based on the current state.

Building the pattern once per change (rather than once per row) is important for performance.
A project with thousands of string table entries would otherwise rebuild the pattern thousands
of times per keystroke.

### Why Regex Validation Matters

If the user types an invalid regular expression (for example, a lone `\` with no closing `)`),
constructing the pattern would normally crash. The `Compile()` method wraps pattern
construction in a try/catch block. If construction fails, the pattern is marked invalid and
`PassesFilter` returns false for every entry — the list goes empty and the user can see their
input is not working, without the editor crashing.

## Part 5 — The Main Panel (`SStringTableBrowser`)

This is the standalone panel you open from **Tools → String Table Browser**.

### What Slate Is

Slate is Unreal's UI framework. Instead of dragging and dropping widgets in a visual editor,
you describe the entire UI in C++ using a special syntax that looks like nested building
blocks. Every widget is either a layout container (like a vertical box or horizontal box) or a
leaf widget (like a button or text field).

Widgets are reference-counted — they are created with `SNew(...)` and stored as `TSharedPtr`
or `TSharedRef`. When nothing holds a reference to a widget anymore, it is automatically
destroyed.

### The Widget Tree

The panel is a `SCompoundWidget` — a widget that contains exactly one child, which is itself a
tree of other widgets. The tree looks like this:

```
SVerticalBox
├── Row 1: SHorizontalBox (search box + mode toggles + refresh button)
├── Row 2: SHorizontalBox ("Search in:" label + scope checkboxes)
└── SOverlay
    ├── SListView (the results table)
    └── STextBlock (empty state message — visible only when results are empty)
```

### The List View

`SListView` is a virtualised list — it only creates row widgets for the entries currently
visible on screen, not for all thousands of entries at once. This is important for performance.
It reads from `FilteredEntries` (a `TArray` of entry pointers) and calls `GenerateRow` for
each visible item to produce the actual widget.

### Sorting

The header row has clickable column headers. Clicking a header calls `OnSortColumnHeader`,
which updates the sort column and direction, then calls `ApplyFilterAndSort`. The sort is a
standard array sort with a comparison lambda that reads the current sort column and direction.

### Debounced Search

Rather than calling `ApplyFilterAndSort` on every keystroke, `OnSearchTextChanged` starts a
timer using `RegisterActiveTimer`. If the user types again before the timer fires, the previous
timer is cancelled via `UnRegisterActiveTimer` and a fresh one starts. When the timer fires it
calls `ApplyFilterAndSort` and returns `EActiveTimerReturnType::Stop`, which tells Slate to
remove the timer automatically.

The debounce interval is read from `UStringTableBrowserSettings::Get()->SearchDebounceDelay`
at the point the timer is registered, so it can be tuned from Project Settings without
recompilation. Toggle changes (Match Case, Whole Word, Regex, scope) bypass the debounce and
run the filter immediately since they fire at most once per click.

### Empty State Overlay

The results list is wrapped in an `SOverlay`. When `FilteredEntries` is empty and the search
text is non-empty, a centred "No entries match your search." text block becomes visible over
the list area using `Visibility_Lambda`. `HitTestInvisible` is used rather than `Visible` so
the overlay text doesn't intercept click events on the list underneath.

### Lifecycle

When the panel is constructed, it subscribes to `OnCacheUpdated` on the module using `AddSP`
— a "shared pointer" binding that automatically unsubscribes when the widget is destroyed,
preventing dangling references. The module is accessed via `GetModulePtr()`. When the panel is
destroyed, it manually unsubscribes as well for safety.

## Part 6 — The Picker Dropdown (`SStringTablePickerDropdown`)

This is the compact search window that opens when you click the search icon on an FText
property in the Details panel.

### How It Differs From the Main Panel

| | Main Browser | Picker Dropdown |
|---|---|---|
| Empty search | Shows all entries | Shows "Type to search..." message |
| No results | Shows "No entries match your search." | Same |
| Columns | Key, Value, Source, Edit, Copy, References | Key, Value, Source, Apply, Copy |
| Apply action | Copies LOCTABLE() to clipboard | Binds the FText property |
| Sorting | User-controlled via headers | Not sortable (results are small) |
| Size | Full dockable tab | Fixed 700×380px floating window |

### How It Opens

`FSlateApplication::PushMenu()` is the Unreal API for showing a floating panel anchored to a
position on screen. It handles z-ordering (the dropdown appears on top of everything else),
dismissal when the user clicks outside, and focus capture. The dropdown is given a 20px
downward offset from the cursor so it doesn't cover the property row being edited.

### The Apply Button

Each row has an Apply button (shown as a ✓ icon). Clicking it fires the `OnEntryPicked`
delegate, passing the table ID and key back to the customization that opened the dropdown. The
customization then writes the value to the property and closes the dropdown by calling
`FSlateApplication::DismissAllMenus()`.

### Pre-Populating the Search

When the dropdown opens, it checks if the FText property already has a value. If it does, that
value is used as the initial search text on first open. After that, whatever the user last
typed is remembered — so reopening the dropdown picks up where they left off, even if the
property value has changed.

### The "Open String Table Browser" Button

The footer contains a button that calls
`FGlobalTabmanager::TryInvokeTab("StringTableBrowser")`. The Tab Manager is Unreal's system
for managing dockable panels. `TryInvokeTab` either focuses an already-open tab or spawns a
new one. The dropdown then dismisses itself.

## Part 7 — The Details Panel Integration (`FTextStringTableDetailCustomization`)

This is what injects the search button into every FText property row in the Details panel.

### What the Details Panel Is

When you select an Actor or asset in Unreal, the Details panel shows all of its editable
properties. Each property gets a row with a label on the left and an editor widget on the
right. Unreal builds this panel automatically by reflecting over the object's properties.

### What IDetailCustomization Is

`IDetailCustomization` is an interface that lets you intercept and modify how a specific
class's properties are displayed in the Details panel. You register a customization for a class
name, and Unreal calls `CustomizeDetails` on it whenever that class (or any subclass) is
selected.

The plugin registers against `"Object"` — the base class of everything in Unreal — so the
customization runs for every selected object. Inside `CustomizeDetails`, the plugin walks all
property categories, finds every `FTextProperty`, and injects the search button according to
the active placement mode.

### Why Not IPropertyTypeCustomization

The more obvious API would be `IPropertyTypeCustomization` — a customization for a specific
property type rather than a class. This was tried first, but Unreal's own FText customization
also registers here, and the two registrations conflict. The class-level customization approach
avoids this entirely because it appends to existing rows rather than replacing them.

### How the Button Is Injected — Next to Label (default)

For each FText property found, the plugin calls `Category.AddProperty(PropHandle)` to get the
`IDetailPropertyRow&` for that property. It then calls `Row.CustomWidget()` to take control of
the row's layout, placing the native name and value widgets exactly where they were, and adding
the search button inside `.NameContent()` to the right of the property label using an
`SHorizontalBox`. The value content is preserved unchanged.

### How the Button Is Injected — Extension Bar (opt-in)

The plugin binds `OnGeneratePropertyRowExtension` to
`FPropertyEditorModule::GetGlobalRowExtensionDelegate()`. This multicast delegate is called by
Unreal for every property row — each plugin appends to `OutExtensionButtons` independently,
and Unreal renders all registered buttons together in a shared bar on the right side of the
row.

The plugin inserts its button at index 0 of `OutExtensionButtons` to maximise the chance of
visibility. This is necessary because of a UE layout bug: when exactly two extension buttons
are present (the plugin's button and the built-in Reset-to-Default button), the extension bar
column does not resize to fit both, and the second button is clipped. Inserting at index 0
ensures the plugin's button occupies the leftmost position. The overflow dropdown that UE
activates at three or more buttons resolves the issue naturally, but the two-button case has no
clean fix from plugin code.

**Known limitation:** This behaviour places the plugin's button before any other plugin's
buttons in the array. The extension bar column sizing issue and the lack of widget size control
in `FPropertyRowExtensionButton` are UE-side constraints. Teams using MVVM or other plugins
that heavily populate the extension bar should use the default Next to Label placement instead.

### How Both Paths Coexist

Both paths are always registered at startup. Each checks
`UStringTableBrowserSettings::Get()->ButtonPlacement` at the point it would add a button and
returns early if it is not the active mode. This means no re-registration is needed when the
setting changes — the change takes effect the next time a Details panel rebuilds its layout.

### How Apply Sets the Property

Setting an FText property correctly through the editor involves more than just writing a value.
The property system needs to know what the value was **before** the change (for undo/redo),
what the value is **after** the change, and that the asset is now **dirty** and needs saving.

This is done in three steps:

`FScopedTransaction` — opens a named undo bracket. Unreal records everything that happens
inside it as a single undoable action, which appears in **Edit → Undo** as "Set String Table
Reference". The transaction is committed automatically when the scoped object goes out of
scope.

`Obj->Modify()` — called on each outer object before writing. This registers the object with
the transaction system and captures its before-state, which is what Unreal restores on undo.

`PropertyHandle->SetValue(NewValue)` — the typed overload that goes through the full property
pipeline, handling dirty marking, multi-object editing, and change notification correctly.

`FText::FromStringTable` creates a proper string table binding — not a plain string copy of
the value, but a live reference that updates if the string table is edited and is fully
compatible with Unreal's localization pipeline. The full asset path string is used as the table
identifier because `FStringTableRegistry` registers tables by their full object path, not by
short name.

## Part 8 — Plugin Settings (`UStringTableBrowserSettings`)

### What `UDeveloperSettings` Is

`UDeveloperSettings` is an Unreal base class that automatically surfaces a settings page under
**Edit → Project Settings** for any subclass. You define `UPROPERTY` fields with `Config` and
`EditAnywhere`, and Unreal handles rendering the form, saving to a `.ini` file, and loading on
startup — no manual UI code required.

The settings are stored in `Config/DefaultStringTableBrowser.ini` using the class name as the
section, making them project-scoped and version-control friendly.

### Available Settings

**`ButtonPlacement`** — controls where the search button appears on `FText` property rows.

`NextToLabel` *(default)* — places the button inside the property name column, to the right of
the label. Conflict-free with MVVM and all other plugins. The button is always fully visible.

`ExtensionBar` — adds the button to the shared right-side extension bar via
`GetGlobalRowExtensionDelegate()`. Use only if you specifically want the right-side placement
and are not using MVVM or other plugins that also add buttons to FText rows, as a UE layout
bug clips the button when exactly two extension buttons are present.

**`SearchDebounceDelay`** — the interval in seconds the search filter waits after the last
keystroke before running. Read by `SStringTableBrowser` when registering the active timer.
Default is 0.15 seconds. Increasing this helps on very large datasets; decreasing it gives
more immediate feedback on smaller ones.

**`SaveCacheToDiskDelay`** — the interval in seconds the debounce timer waits after the last
Asset Registry event before writing the cache to disk. Default is 0.5 seconds. Events that
arrive in bursts (e.g. large imports) are batched into a single write. `ForceRebuildCache`
always writes immediately, bypassing this delay.

## How Everything Connects

```
FStringTableBrowserModule
│
├── Maintains GroupedCache + FlatCache
├── Listens to Asset Registry events (OnAssetAdded, OnAssetRemoved, OnAssetUpdated)
├── Listens to UPackage::PackageSavedWithContextEvent (OnPackageSaved)
│   └── Catches value-only string edits that OnAssetUpdated misses
├── Debounces disk writes via ScheduleDiskCacheSave (SaveCacheToDiskDelay from settings)
├── Broadcasts OnCacheUpdated delegate (from SaveCacheToDisk, always on game thread)
├── Exposes GetModulePtr() for safe access from widgets and lambdas
├── Registers Tools menu entry in Level Editor,
│   String Table Editor, and Widget Blueprint Editor
│
├── SStringTableBrowser (main panel)
│   ├── Accesses module via GetModulePtr()
│   ├── Subscribes to OnCacheUpdated (AddSP — auto-unsubscribes on destroy)
│   ├── Owns FStringTableSearchFilter
│   ├── Debounces search via RegisterActiveTimer (SearchDebounceDelay from settings)
│   └── Reads cache via GetCachedEntriesCopy()
│
├── FTextStringTableBrowserDetailCustomization
│   ├── Bound to GetGlobalRowExtensionDelegate() — Extension Bar path
│   │   ├── Active when ButtonPlacement == ExtensionBar
│   │   └── Inserts button at index 0 of OutExtensionButtons
│   ├── Registered via RegisterCustomClassLayout("Object") — Next to Label path
│   │   └── Active when ButtonPlacement == NextToLabel
│   └── Both paths open SStringTableBrowserPickerDropdown on click
│       ├── Accesses module via GetModulePtr()
│       ├── Owns FStringTableSearchFilter (same logic, separate instance)
│       ├── Reads cache via GetCachedEntriesCopy()
│       └── Fires OnEntryPicked → writes FText property via handle
│
├── UStringTableBrowserSettings
│   ├── Subclass of UDeveloperSettings
│   ├── Surfaces under Edit → Project Settings → Plugins → String Table Browser
│   ├── ButtonPlacement — read at runtime by both Detail Customization paths
│   ├── SearchDebounceDelay — read by SStringTableBrowser when registering timer
│   └── SaveCacheToDiskDelay — read by ScheduleDiskCacheSave when setting timer
│
├── FStringTableSearchFilter (shared struct, not a class)
│   ├── Holds all toggle and search state
│   ├── Compiles regex once per change
│   └── PassesFilter() called once per cache entry
│
├── StringTableBrowserTypes.h (shared data types, no heavy dependencies)
│   ├── FStringTableBrowserEntry — the cache row data type
│   ├── StringTableBrowserColumns — shared FName column identifiers
│   └── StringTableBrowserIcons — shared FName brush identifiers
│
└── FStringTableBrowserHelpers (shared static utilities)
    ├── MakeFilterCheckBox — builds labelled toggle checkboxes for both widgets
    └── CopyStringTableEntry — clipboard copy logic, used by both widgets
```

The cache flows in one direction — from the module outward to the UI. The UI never writes to
the cache. User actions (Apply, Copy Key, Edit Table) either write to a property, copy to the
clipboard, or open an asset editor — none of them touch the cache directly.

## Key Unreal Concepts Used

| Concept | What it is | Where it's used |
|---|---|---|
| `TSharedPtr` `TSharedRef` | Reference-counted smart pointers — automatically manage object lifetime | Every Slate widget and cache entry |
| `TArray` | A dynamic array, like `std::vector` | `FlatCache`, `FilteredEntries` |
| `TMap` | A dictionary, like `std::unordered_map` | `GroupedCache` |
| `TOptional` | A value that may or may not exist | Compiled regex pattern |
| `FCriticalSection` | A mutex — prevents concurrent access to shared data | Cache read/write locking |
| `FSoftObjectPath` | A reference to an asset that doesn't load it into memory | Stored per entry so assets stay unloaded |
| `FText` | Unreal's localisation-aware string type | The property type being bound |
| `FName` | An interned, case-insensitive string used for identifiers | Table IDs, column names, asset names, icon names |
| `DECLARE_MULTICAST_DELEGATE` | A one-to-many event system | `OnCacheUpdated` |
| `IDetailCustomization` | Interface for customising the Details panel layout | FText row injection (both placement paths) |
| `FOnGenerateGlobalRowExtension` | Global delegate that adds buttons to any property row | FText row injection (Extension Bar path) |
| `IPropertyHandle` | A handle to a property that supports undo, multi-edit, and dirty marking | Writing the FText binding |
| `FGlobalTabmanager` | Manages all dockable editor tabs | Opening/focusing the main panel |
| `FSlateApplication` | The top-level application object for all UI | Showing and dismissing the dropdown |
| `RegisterActiveTimer` | Schedules a Slate widget callback to fire after a delay | Search debounce (150ms default) |
| `FTimerHandle` / `GetTimerManager` | Schedules a game-thread callback to fire after a delay | Disk cache save debounce |
| `UPackage::PackageSavedWithContextEvent` | Fires whenever any package is saved to disk, unconditionally | Detecting value-only string table edits |
| `IAssetManagerEditorModule` | Provides access to the native Reference Viewer UI | Opening the Reference Viewer for a string table asset |
| `UDeveloperSettings` | Base class that auto-surfaces a Project Settings page | `UStringTableBrowserSettings` |
| `FStringTableBrowserHelpers` | Plugin-internal static utilities shared across widgets | Checkbox builder, clipboard copy |
 