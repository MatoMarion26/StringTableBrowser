# String Table Browser — How It Works

A plain-language explanation of the plugin's architecture for developers who are not familiar with Unreal's Slate UI framework or editor tooling APIs.

---

## The Big Picture

The plugin does three things:

1. **Maintains a searchable database** of every string table entry in your project, kept up to date automatically.
2. **Provides two ways to interact with that database** — a standalone panel you can open from the Tools menu, and a search button injected into every FText property in the Details panel.
3. **Exposes a Project Settings page** so teams can configure how the Details panel button is placed without touching code.

---

## Part 1 — The Module (`FStringTableBrowserModule`)

Think of the module as the plugin's "brain". It is the first thing Unreal loads when the plugin starts, and the last thing unloaded when the editor closes. Everything else in the plugin depends on it.

### What a Module Is

Every feature in Unreal — including the editor itself — is organized into modules. A module has two lifecycle methods: `StartupModule` (runs when the plugin loads) and `ShutdownModule` (runs when it unloads). You use these to set things up and tear them down cleanly.

### The Cache

String table assets can contain hundreds or thousands of entries spread across many files. Scanning all of them every time the panel opens would be slow. Instead, the module builds a cache — an in-memory copy of all entries — and keeps it updated.

The cache has two layers:

- **GroupedCache** — a dictionary (`TMap`) where each key is a string table asset's package name and each value is the list of entries inside it. This structure makes it easy to add, remove, or update a single table without rebuilding everything.
- **FlatCache** — a flat list of all entries from all tables. This is what the UI reads from. It's rebuilt from `GroupedCache` whenever anything changes.

The cache is also **written to disk** as a JSON file (`Saved/StringTableBrowserCache.json`). On the next editor launch, this file is loaded first so the panel is populated instantly without scanning all assets again. The file includes a version number — if the plugin has been updated and the schema changed, the old file is discarded and a fresh scan runs.

### Staying Up To Date

Unreal has an **Asset Registry** — a service that knows about every asset in the project without having to load them all into memory. The module subscribes to three events on it:

- `OnAssetAdded` — fires when a new string table is created or imported
- `OnAssetRemoved` — fires when one is deleted
- `OnAssetUpdated` — fires when one is modified and saved

When any of these fire, the module updates only the affected table in `GroupedCache`, rebuilds `FlatCache`, saves the JSON, and then tells any open UI to refresh.

### Thread Safety

The Asset Registry callbacks can sometimes arrive from a background thread. If the UI is reading from `FlatCache` at the same moment the background thread is writing to it, the program can crash or produce corrupted data. To prevent this, all writes to the cache are wrapped in a `FCriticalSection` — a lock that only allows one thread to access the data at a time. The UI always reads a **copy** of the cache (not the live version) so it never holds the lock for long.

### Telling the UI to Refresh

The module has a **delegate** called `OnCacheUpdated`. A delegate is essentially a list of functions to call when something happens — like an event system. When the cache changes, the module broadcasts this delegate. Any UI widget that has subscribed to it will receive the notification and redraw itself. Because Slate (the UI framework) is not thread-safe, the broadcast is always marshalled to the game thread before firing.

---

## Part 2 — The Shared Filter (`FStringTableSearchFilter`)

Both UI widgets — the main panel and the Details panel dropdown — need identical search behaviour. Rather than writing the logic twice, it lives in a single struct that both widgets own as a member.

### What It Holds

- The current search text
- Toggle states: Match Case, Whole Word, Regex
- Scope states: whether to search Keys, Values, and/or Table names
- A compiled regex pattern (built once per search change, not once per row)
- A validity flag for the pattern

### How It Works

When the user changes the search text or any toggle, the widget calls `Compile()`. This builds and validates the regex pattern if needed. Then for every entry in the cache, the widget calls `PassesFilter(Entry)`, which returns true or false based on the current state.

Building the pattern once per change (rather than once per row) is important for performance. A project with thousands of string table entries would otherwise rebuild the pattern thousands of times per keystroke.

### Why Regex Validation Matters

If the user types an invalid regular expression (for example, a lone `[` with no closing `]`), constructing the pattern would normally crash. The `Compile()` method wraps pattern construction in a try/catch block. If construction fails, the pattern is marked invalid and `PassesFilter` returns false for every entry — the list goes empty and the user can see their input is not working, without the editor crashing.

---

## Part 3 — The Main Panel (`SStringTableBrowser`)

This is the standalone panel you open from **Tools → String Table Browser**.

### What Slate Is

Slate is Unreal's UI framework. Instead of dragging and dropping widgets in a visual editor, you describe the entire UI in C++ using a special syntax that looks like nested building blocks. Every widget is either a layout container (like a vertical box or horizontal box) or a leaf widget (like a button or text field).

Widgets are reference-counted — they are created with `SNew(...)` and stored as `TSharedPtr` or `TSharedRef`. When nothing holds a reference to a widget anymore, it is automatically destroyed.

### The Widget Tree

The panel is a `SCompoundWidget` — a widget that contains exactly one child, which is itself a tree of other widgets. The tree looks like this:

```
SVerticalBox
├── Row 1: SHorizontalBox (search box + mode toggles + refresh button)
├── Row 2: SHorizontalBox ("Search in:" label + scope checkboxes)
└── SOverlay
    ├── SListView (the results table)
    └── STextBlock (empty state message — visible only when results are empty)
```

### The List View

`SListView` is a virtualised list — it only creates row widgets for the entries currently visible on screen, not for all thousands of entries at once. This is important for performance. It reads from `FilteredEntries` (a `TArray` of entry pointers) and calls `GenerateRow` for each visible item to produce the actual widget.

### Sorting

The header row has clickable column headers. Clicking a header calls `OnSortColumnHeader`, which updates the sort column and direction, then calls `ApplyFilterAndSort`. The sort is a standard array sort with a comparison lambda that reads the current sort column and direction.

### Debounced Search

Rather than calling `ApplyFilterAndSort` on every keystroke, `OnSearchTextChanged` starts a 150ms `RegisterActiveTimer`. If the user types again before the timer fires, the previous timer is cancelled via `UnRegisterActiveTimer` and a fresh one starts. When the timer fires it calls `ApplyFilterAndSort` and returns `EActiveTimerReturnType::Stop`, which tells Slate to remove the timer automatically.

This means the filter only runs once per typing pause rather than on every key press, which is imperceptible to the user but meaningful on a dataset of thousands of entries. Toggle changes (Match Case, Whole Word, Regex, scope) bypass the debounce and run the filter immediately since they fire at most once per click.

### Empty State Overlay

The results list is wrapped in an `SOverlay`. When `FilteredEntries` is empty and the search text is non-empty, a centred "No entries match your search." text block becomes visible over the list area using `Visibility_Lambda`. `HitTestInvisible` is used rather than `Visible` so the overlay text doesn't intercept click events on the list underneath.

### Lifecycle

When the panel is constructed, it subscribes to `OnCacheUpdated` on the module using `AddSP` — a "shared pointer" binding that automatically unsubscribes when the widget is destroyed, preventing dangling references. When the panel is destroyed, it manually unsubscribes as well for safety.

---

## Part 4 — The Picker Dropdown (`SStringTablePickerDropdown`)

This is the compact search window that opens when you click the search icon on an FText property in the Details panel.

### How It Differs From the Main Panel

| | Main Browser | Picker Dropdown |
|---|---|---|
| Empty search | Shows all entries | Shows "Type to search..." message |
| No results | Shows "No entries match your search." | Shows "No entries match your search." |
| Columns | Key, Value, Source, Edit, Copy, References | Key, Value, Source, Apply, Copy |
| Apply action | Copies LOCTABLE() to clipboard | Binds the FText property |
| Sorting | User-controlled via headers | Not sortable (results are small) |
| Size | Full dockable tab | Fixed 700×380px floating window |

### How It Opens

`FSlateApplication::PushMenu()` is the Unreal API for showing a floating panel anchored to a position on screen. It handles z-ordering (the dropdown appears on top of everything else), dismissal when the user clicks outside, and focus capture. The dropdown is given a 20px downward offset from the cursor so it doesn't cover the property row being edited.

### The Apply Button

Each row has an Apply button (shown as a ✓ icon). Clicking it fires the `OnEntryPicked` delegate, passing the table ID and key back to the customization that opened the dropdown. The customization then writes the value to the property and closes the dropdown by calling `FSlateApplication::DismissAllMenus()`.

### Pre-Populating the Search

When the dropdown opens, it checks if the FText property already has a value. If it does, that value is used as the initial search text on first open. After that, whatever the user last typed is remembered — so reopening the dropdown picks up where they left off, even if the property value has changed.

### The "Open String Table Browser" Button

The footer contains a button that calls `FGlobalTabmanager::TryInvokeTab("StringTableBrowser")`. The Tab Manager is Unreal's system for managing dockable panels. `TryInvokeTab` either focuses an already-open tab or spawns a new one. The dropdown then dismisses itself.

---

## Part 5 — The Details Panel Integration (`FTextStringTableDetailCustomization`)

This is what injects the search button into every FText property row in the Details panel.

### What the Details Panel Is

When you select an Actor or asset in Unreal, the Details panel shows all of its editable properties. Each property gets a row with a label on the left and an editor widget on the right. Unreal builds this panel automatically by reflecting over the object's properties.

### What IDetailCustomization Is

`IDetailCustomization` is an interface that lets you intercept and modify how a specific class's properties are displayed in the Details panel. You register a customization for a class name, and Unreal calls `CustomizeDetails` on it whenever that class (or any subclass) is selected.

The plugin registers against `"Object"` — the base class of everything in Unreal — so the customization runs for every selected object. Inside `CustomizeDetails`, the plugin walks all property categories, finds every `FTextProperty`, and appends the search button to its row.

### Why Not IPropertyTypeCustomization

The more obvious API would be `IPropertyTypeCustomization` — a customization for a specific property type rather than a class. This was tried first, but Unreal's own FText customization also registers here, and the two registrations conflict. The class-level customization approach avoids this entirely because it appends to existing rows rather than replacing them.

### How the Button Is Injected

For each FText property found, the plugin calls `Category.AddProperty(PropHandle)` to get the `IDetailPropertyRow&` for that property. It then calls `Row.CustomWidget()` to take control of the row's layout, placing the native name and value widgets exactly where they were, and adding the search button in the `.ExtensionContent()` slot — a special slot that appends widgets to the far right of the row without disturbing the rest of it. This is the same slot Unreal uses for its own reset-to-default arrow button.

### How Apply Sets the Property

Setting an FText property correctly through the editor involves more than just writing a value. The property system needs to know:

- What the value was **before** the change (for undo/redo)
- What the value is **after** the change
- That the asset is now **dirty** and needs saving

This is done in three steps:

1. `FScopedTransaction` — opens a named undo bracket. Unreal records everything that happens inside it as a single undoable action, which appears in **Edit → Undo** as "Set String Table Reference". The transaction is committed automatically when the scoped object goes out of scope.
2. `Obj->Modify()` — called on each outer object before writing. This registers the object with the transaction system and captures its before-state, which is what Unreal restores on undo.
3. `PropertyHandle->SetValue(NewValue)` — the typed `FText` overload that goes through the full property pipeline, handling dirty marking, multi-object editing, and change notification correctly.

`FText::FromStringTable` creates a proper string table binding — not a plain string copy of the value, but a live reference that updates if the string table is edited and is fully compatible with Unreal's localization pipeline.

> **Why not `EnumerateRawData`?** Writing directly to raw property memory bypasses the transaction system entirely, even when surrounded by `NotifyPreChange`/`NotifyPostChange`. The value appears to change but doesn't persist across undo or properly dirty the asset. `SetValue()` is the correct typed API and handles all of this internally.

---

## Part 6 — Plugin Settings (`UStringTableBrowserSettings`)

### What `UDeveloperSettings` Is

`UDeveloperSettings` is an Unreal base class that automatically surfaces a settings page under **Edit → Project Settings** for any subclass. You define UPROPERTY fields with `Config` and `EditAnywhere`, and Unreal handles rendering the form, saving to a `.ini` file, and loading on startup — no manual UI code required.

The settings are stored in `Config/DefaultStringTableBrowser.ini` using the class name as the section, making them project-scoped and version-control friendly.

### The Button Placement Setting

The single setting `ButtonPlacement` is an `EStringTableBrowserButtonPlacement` enum with two values:

**Extension Bar** (default) — the button is added via `GetGlobalRowExtensionDelegate()`. Unreal collects all registered extension buttons from all plugins and renders them together in a shared bar on the right of the property row. This means the String Table Browser button and the MVVM plugin's button both appear without either overwriting the other.

**Next to Property Label** — the button is injected via `IDetailCustomization` on `UObject`, placed inside the `NameContent` slot to the right of the property label using a `SHorizontalBox`. This approach is always visible regardless of other plugins, but occupies name column space.

### How Both Paths Coexist

Both the global row extension delegate binding and the `IDetailCustomization` registration are always active. Each path checks `UStringTableBrowserSettings::Get()->ButtonPlacement` at the point it would add a button and returns early if it's not the active mode. This means:

- No re-registration needed when the setting changes
- Changing the setting takes effect the next time a Details panel rebuilds its layout
- The runtime cost of the inactive path is a single enum comparison per property row

---

## How Everything Connects

```
FStringTableBrowserModule
│
├── Maintains GroupedCache + FlatCache
├── Listens to Asset Registry events
├── Broadcasts OnCacheUpdated delegate
├── Registers Tools menu entry in Level Editor,
│   String Table Editor, and Widget Blueprint Editor
│
├── SStringTableBrowser (main panel)
│   ├── Subscribes to OnCacheUpdated
│   ├── Owns FStringTableSearchFilter
│   ├── Debounces search via RegisterActiveTimer (150ms)
│   └── Reads cache via GetCachedEntriesCopy()
│
├── FTextStringTableBrowserDetailCustomization
│   ├── Bound to GetGlobalRowExtensionDelegate() — Extension Bar path
│   │   └── Active when ButtonPlacement == ExtensionBar
│   ├── Registered via RegisterCustomClassLayout("Object") — Label path
│   │   └── Active when ButtonPlacement == NextToLabel
│   └── Both paths open SStringTableBrowserPickerDropdown on click
│       ├── Owns FStringTableSearchFilter (same logic, separate instance)
│       ├── Reads cache via GetCachedEntriesCopy()
│       └── Fires OnEntryPicked → writes FText property via handle
│
├── UStringTableBrowserSettings
│   ├── Subclass of UDeveloperSettings
│   ├── Surfaces under Edit → Project Settings → Plugins
│   └── ButtonPlacement — read at runtime by both Detail Customization paths
│
└── FStringTableSearchFilter (shared struct, not a class)
    ├── Holds all toggle and search state
    ├── Compiles regex once per change
    └── PassesFilter() called once per cache entry
```

The cache flows in one direction — from the module outward to the UI. The UI never writes to the cache. User actions (Apply, Copy Key, Edit Table) either write to a property, copy to the clipboard, or open an asset editor — none of them touch the cache directly.

---

## Key Unreal Concepts Used

| Concept | What it is | Where it's used |
|---|---|---|
| `TSharedPtr` / `TSharedRef` | Reference-counted smart pointers — automatically manage object lifetime | Every Slate widget and cache entry |
| `TArray` | A dynamic array, like `std::vector` | `FlatCache`, `FilteredEntries` |
| `TMap` | A dictionary, like `std::unordered_map` | `GroupedCache` |
| `TOptional` | A value that may or may not exist | Compiled regex pattern |
| `FCriticalSection` | A mutex — prevents concurrent access to shared data | Cache read/write locking |
| `FSoftObjectPath` | A reference to an asset that doesn't load it into memory | Stored per entry so assets stay unloaded |
| `FText` | Unreal's localisation-aware string type | The property type being bound |
| `FName` | An interned, case-insensitive string used for identifiers | Table IDs, column names, asset names |
| `DECLARE_MULTICAST_DELEGATE` | A one-to-many event system | `OnCacheUpdated` |
| `IDetailCustomization` | Interface for customising the Details panel layout | FText row injection (Next to Label path) |
| `FOnGenerateGlobalRowExtension` | Global delegate that adds buttons to any property row | FText row injection (Extension Bar path) |
| `IPropertyHandle` | A handle to a property that supports undo, multi-edit, and dirty marking | Writing the FText binding |
| `FGlobalTabmanager` | Manages all dockable editor tabs | Opening/focusing the main panel |
| `FSlateApplication` | The top-level application object for all UI | Showing and dismissing the dropdown |
| `RegisterActiveTimer` | Schedules a Slate widget callback to fire after a delay | 150ms search debounce |
| `IAssetManagerEditorModule` | Provides access to the native Reference Viewer UI | Opening the Reference Viewer for a string table asset |
| `UDeveloperSettings` | Base class that auto-surfaces a Project Settings page | `UStringTableBrowserSettings` |
