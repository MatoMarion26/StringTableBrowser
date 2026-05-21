# String Table Browser — Development Challenges & Fixes

A summary of every significant issue encountered during development and how each was resolved.

## Module & Startup

**Asset Registry delegate never unbound after OnFilesLoaded**
`OnFilesLoaded` could fire more than once. Fixed by calling `RemoveAll(this)` at the top of
`OnAssetRegistryFilesLoaded` so the callback is self-removing.

**AssetRegistry module loaded twice in StartupModule**
Two separate calls created a potential stale reference. Fixed by hoisting a single
`FAssetRegistryModule&` at the top of `StartupModule` and reusing it throughout.

**OnFilesLoaded not unbound during rapid shutdown**
If the editor closed before the initial asset scan finished, the delegate was never removed.
Fixed by also calling `RemoveAll(this)` in `ShutdownModule`.

**Plugin tab and menu not appearing**
The plugin template comment stubs for `RegisterMenus` and `PluginButtonClicked` were never
implemented. Fixed by implementing `FGlobalTabmanager::RegisterNomadTabSpawner` and
`UToolMenus::ExtendMenu("LevelEditor.MainMenu.Tools (invalid)")` with a proper menu entry pointing to
**Tools → String Table Browser**.

**Plugin loaded but menu entry missing**
`RegisterMenus` was called before the editor toolbar was ready. Fixed by deferring
registration via `UToolMenus::RegisterStartupCallback`.

**Dead `OnGenerateGlobalRowExtension` stub left in module after refactor**
The module declared and defined its own `OnGenerateGlobalRowExtension` method alongside the
customization's `OnGeneratePropertyRowExtension`. The module's version was a development
scaffold containing a placeholder `UE_LOG` and was never bound to the global row extension
delegate — the customization's static method was bound instead. The dead method compiled
silently, added noise, and could mislead contributors. Fixed by removing the method entirely
from both the header and the `.cpp`.

## Caching

**ForceRebuildCache showing no entries**
After the thread-safety fix replaced `GetAsset()` with `FindObject()`, the initial cache build
found nothing because no assets were loaded in memory yet at startup. Fixed by using
`FindObject` first and falling back to `GetAsset()` (synchronous load) only inside
`ForceRebuildCache`, keeping `FindObject`-only in the incremental callbacks.

**Stale disk cache loaded silently after updates**
The JSON cache had no version field, so outdated caches were loaded without question after
schema changes. Fixed by adding a `GStringTableBrowserCacheVersion` constant and rejecting
caches with a mismatched version, triggering a full rebuild.

**No thread safety on cache mutations**
Asset Registry callbacks can fire on background threads in some engine versions. Fixed by
protecting all `GroupedCache` and `FlatCache` mutations with `FCriticalSection CacheLock`, and
adding `GetCachedEntriesCopy()` for a lock-guarded snapshot safe to consume from Slate.

**BroadcastCacheUpdated called from background thread**
Slate delegates must be fired on the game thread. Fixed by checking `IsInGameThread()` and
marshalling to the game thread via `AsyncTask(ENamedThreads::GameThread, ...)` when needed.

**BroadcastCacheUpdated captured raw `this` in AsyncTask lambda**
When marshalling to the game thread via `AsyncTask`, the lambda captured `this` directly. If
the module was destroyed before the queued task fired — during hot-reload, editor close, or
rapid plugin toggle — `this` was a dangling pointer and the broadcast fired against a destroyed
module. Fixed by removing the `this` capture entirely and re-resolving the module pointer
inside the lambda via `FStringTableBrowserModule::GetModulePtr()`, which returns `nullptr` if
the module is no longer loaded.

**Asset class comparison using short name**
`AssetClassPath.GetAssetName() == TEXT("StringTable")` could false-match other plugins. Fixed
by comparing against `UStringTable::StaticClass()->GetClassPathName()` throughout.

**`SaveCacheToDisk` called on every incremental Asset Registry event**
`OnAssetAdded`, `OnAssetRemoved`, and `OnAssetUpdated` each called `SaveCacheToDisk` directly.
Asset Registry events arrive in bursts — importing or regenerating multiple string tables fires
a separate event per asset in rapid succession, each triggering a full JSON serialisation and
disk write of the entire cache. Fixed by introducing `ScheduleDiskCacheSave`, which sets a
dirty flag and resets a configurable timer (`SaveCacheToDiskDelay`, exposed in Project
Settings). Only one write fires after the burst settles. `ForceRebuildCache` bypasses the
debounce and writes immediately since it is an explicit user action.

**`ForceRebuildCache` held `CacheLock` across `SaveCacheToDisk` file I/O**
The initial implementation wrapped the entire `ForceRebuildCache` body in a single
`FScopeLock`, including the `SaveCacheToDisk` call. `SaveCacheToDisk` performs a full
`GroupedCache` copy and a file write, neither of which require the lock — `SaveCacheToDisk`
already takes its own snapshot under a nested lock scope. Holding `CacheLock` across file I/O
unnecessarily blocked any Asset Registry callback thread that arrived during the save. Fixed by
releasing the lock immediately after `RebuildFlatCache` and calling `SaveCacheToDisk` outside
the lock scope.

**`GetCachedEntriesCopy` held `CacheLock` across N atomic ref-count increments**
`GetCachedEntriesCopy` returned `FlatCache` directly inside the `FScopeLock` scope, meaning
all N `TSharedPtr` copy constructions — each an atomic increment — happened while the lock was
held. For a cache with thousands of entries this produced an unnecessarily long lock hold that
blocked concurrent Asset Registry write callbacks. Fixed by assigning to a local snapshot
inside the lock scope and returning it after the lock releases, matching the pattern used in
`SaveCacheToDisk`.

**`OnAssetUpdated` did not fire on subsequent saves of string table content**
`OnAssetUpdated` fires when an asset's Asset Registry metadata — tags and dependencies —
changes. String table keys are stored as Asset Registry tags, so the first save after adding or
removing keys triggers the delegate. Saves that only change string values leave the registry
metadata identical and do not trigger it at all. The callback appeared to work correctly on the
first test but silently stopped firing on all subsequent saves that did not add or remove keys.
Fixed by supplementing `OnAssetUpdated` with `UPackage::PackageSavedWithContextEvent`
(`OnPackageSaved`), which fires unconditionally whenever a package is written to disk
regardless of whether registry metadata changed. `OnAssetUpdated` is retained for
structural/metadata changes (key additions and removals); `OnPackageSaved` handles all
value-only content changes.

## Search & Filtering

**Regex constructed per row from raw user input**
An invalid pattern (e.g. a bare `\`) caused undefined behaviour on every row. Fixed by
pre-compiling the pattern once in `ApplyFilterAndSort`, wrapping construction in a try/catch,
and skipping all rows if the pattern is invalid.

**FRegexMatcher::Sanitize used for whole-word escaping**
This internal API has no public contract. Fixed by replacing it with a custom `EscapeRegex`
helper that manually escapes all ICU regex metacharacters.

**Match Case ignored in Whole Word and Regex modes**
Both regex paths used the raw pattern with no case flag. Fixed by prefixing patterns with
`(?i)` when `bMatchCase` is false, and omitting the prefix when it is true.

**Search target always included all three fields**
Before scope toggles were added, the search target concatenated Key, Value, and TableId
unconditionally. Fixed by building the target dynamically from only the fields the user has
enabled, and returning `true` for all entries when no scope is selected.

**Main browser showed nothing on empty search**
After the picker was added with "empty = show nothing" behaviour, the main browser accidentally
inherited it. Fixed by branching the two widgets: empty search shows everything in the main
browser and nothing in the picker.

**`MakeToggle` lambda duplicated verbatim across both widget `.cpp` files**
Both `SStringTableBrowser::Construct` and `SStringTableBrowserPickerDropdown::Construct`
defined an identical local `MakeToggle` lambda that wrapped a checkbox builder and wired the
setter into `Filter.Compile()` followed by the local apply function. Fixed by promoting the
checkbox builder to a static `FStringTableBrowserHelpers::MakeFilterCheckBox` free function in
a shared helper header, taking a label, tooltip, default state, and a callback. Both widgets
call the shared function directly.

## Slate & UI

**SortData called directly from OnSortColumnHeader**
Calling `SortData` independently of `ApplyFilterAndSort` could sort a partially rebuilt list.
Fixed by routing through `ApplyFilterAndSort` so filter and sort state are always consistent.

**Action column too narrow for two buttons**
Adding the Copy Key button caused it to overflow. Fixed by widening the Action column from
`100` to `180` fixed width.

**Action column text buttons replaced with icons**
Text labels ("Edit Table", "Copy Key") were too wide for a compact column. Replaced with
`SimpleButton`-style icon buttons. Icon names were centralised in a `StringTableBrowserIcons`
namespace within `StringTableBrowserTypes.h` so both `SStringTableBrowserRow` and
`SPickerRow` reference the same constants without bare string literals. A shared static helper
was introduced so all three buttons share identical sizing, padding, and style without
repetition. Column width reduced to `110` to fit three icon buttons.

**Inline MakeIconButton lambda violated code standard**
The initial implementation used a local lambda inside `GenerateWidgetForColumn` to build icon
buttons. Lambdas that capture nothing and are reused are better expressed as static file-scope
helpers. Promoted to `static TSharedRef<SWidget> MakeActionIconButton(...)` alongside the
existing `MakeFilterCheckBox`, matching the established pattern.

**Regex compiled once but pattern moved into TOptional incorrectly**
`MoveTemp` on the test pattern left it in a valid-but-unspecified state before assignment.
Fixed by constructing the final `FRegexPattern` directly into `CompiledPattern` after
validation.

**`SPickerRow` used raw string literals instead of shared column name constants**
The main browser defined a `StringTableBrowserColumns` namespace with named `FName` constants.
`SPickerRow` bypassed this entirely, using bare string literals (`"Key"`, `"Value"`,
`"Source"`, `"Actions"`) for column comparisons in `GenerateWidgetForColumn`. A column name
change would silently break the picker row while the main browser still compiled. Fixed by
moving `StringTableBrowserColumns` to `StringTableBrowserTypes.h` and using it in both row
widgets.

**`FStringTableBrowserEntry` struct and shared constants duplicated or scattered**
`FStringTableBrowserEntry` was declared in `StringTableBrowserModule.h`, making it a
transitive dependency of every file that only needed the data type. Column name constants and
icon name constants were defined locally in `.cpp` files, preventing sharing. Fixed by
extracting all shared data types, column constants (`StringTableBrowserColumns`), and icon
constants (`StringTableBrowserIcons`) into a dedicated `StringTableBrowserTypes.h` header with
no heavy dependencies, included only where needed.

**Platform-specific include in `SStringTableBrowserPickerDropdown`**
`SStringTableBrowserPickerDropdown.cpp` included `Windows/WindowsPlatformApplicationMisc.h`
directly. Both widgets use `FPlatformApplicationMisc::ClipboardCopy`, which is declared in the
cross-platform `HAL/PlatformApplicationMisc.h`. The Windows-specific include compiled
correctly on Windows but would fail on Mac and Linux. Fixed by extracting the clipboard copy
logic into `FStringTableBrowserHelpers::CopyStringTableEntry` and including the HAL header
there, removing the platform-specific include from both widget files.

**`SPickerRow` slots set both `.FillWidth()` and `.AutoWidth()` on the same slot**
The Apply and Copy button slots in `SPickerRow` set both `.FillWidth(1)` and `.AutoWidth()`,
which are contradictory. `.AutoWidth()` takes precedence in Slate when both are present, making
`.FillWidth()` dead code that implied a layout intention that was never applied. Fixed by
removing `.FillWidth()` from both slots, leaving only `.AutoWidth()`.

## Search Performance

**Filter ran on every keystroke causing UI lag on large datasets**
With thousands of entries, running `ApplyFilterAndSort` synchronously on every key press caused
perceptible lag. Fixed by debouncing via `RegisterActiveTimer` — the filter only runs after the
last keystroke. If the user types again before the timer fires, `UnRegisterActiveTimer` cancels
the pending callback and a fresh timer starts. Toggle changes bypass the debounce and apply
immediately. The debounce delay is now configurable via `SearchDebounceDelay` in Project
Settings rather than hardcoded in the widget.

## Reference Viewer

**IAssetManagerEditorModule availability not checked before use**
Calling `IAssetManagerEditorModule::Get()` when the module is not loaded would assert. Fixed
by guarding with `IAssetManagerEditorModule::IsAvailable()` before calling `Get()`, matching
Unreal's own convention for optional editor modules.

## Menu Registration

**Browser entry needed in String Table and Widget Blueprint editors**
The initial implementation only registered the Tools menu entry in the Level Editor. Fixed by
extracting a `RegisterInToolsMenu(FName)` lambda inside `RegisterMenus()` and calling it for
`"LevelEditor.MainMenu.Tools (invalid)"`, `"AssetEditor.StringTableEditor.MainMenu.Tools (invalid)"`, and
`"AssetEditor.WidgetBlueprintEditor.MainMenu.Tools (invalid)"`. The same `AddMenuEntry` definition is
reused for all three, avoiding duplication.

## Empty State

**No feedback when search returns no results**
When `FilteredEntries` was empty the list view showed a blank area with no explanation. Fixed
by wrapping the `SListView` in an `SOverlay` and adding a `STextBlock` that becomes visible
via `Visibility_Lambda` when the list is empty. Two distinct messages are shown: "Type to
search..." when the search box is empty (picker only), and "No entries match your search."
when a search returned no results. `HitTestInvisible` is used rather than `Visible` so the
text does not block interaction with the list.

## Details Panel Integration

**IPropertyTypeCustomization for "TextProperty" replaced native FText UI**
Registering a type customization for `"TextProperty"` completely replaced Unreal's own
`FTextCustomization`, removing the localization flag and string table picker. Every attempt to
render alongside the native widget caused conflicts. Fixed by abandoning
`IPropertyTypeCustomization` entirely and switching to `IDetailCustomization` on `UObject`,
which appends rows without replacing anything.

**Correct property type name was "TextProperty" not "Text"**
The initial registration used `"Text"` which was silently ignored. Found by logging
`FTextProperty::StaticClass()->GetFName()` at runtime.

**Customization registered before engine's own customizations**
With `LoadingPhase: Default`, the plugin registered before `FTextCustomization`, which then
overwrote it. Fixed by changing the loading phase to `PostEngineInit` in `.uplugin`.

**Neither CustomizeHeader nor CustomizeChildren being called**
Even with the correct type name, the native FText customization was winning the registration
race. Confirmed by the PostEngineInit fix above resolving it.

**TSharedRef member initialised from null TSharedPtr**
`TSharedRef<IPropertyHandle> PropertyHandleRef = TSharedPtr<IPropertyHandle>().ToSharedRef()`
asserted immediately on construction because `ToSharedRef()` on a null pointer is illegal.
Fixed by removing the unused member entirely — the handle is passed directly through method
arguments.

**FindProperty does not exist on IDetailCategoryBuilder**
`Category.FindProperty(PropHandle)` was called to retrieve an existing row, but this API
doesn't exist. Fixed by using `Category.AddProperty(PropHandle)` which both registers the
property and returns the `IDetailPropertyRow&` directly.

**CustomizeChildren being implemented caused native children to disappear**
Implementing `CustomizeChildren` at all signals to Unreal that the plugin owns child rendering,
suppressing the native ones. Fixed by leaving it as an empty required override and doing all
work in `CustomizeDetails` via `AddProperty`.

**Apply button not setting the FText value**
`EnumerateRawData` writes to raw property memory bypass the transaction system entirely, even
when surrounded by `NotifyPreChange` / `NotifyPostChange`. The value appeared to change on
screen but didn't persist across undo and didn't properly dirty the asset. Fixed by replacing
the raw write entirely with `FScopedTransaction` (opens a named undo bracket) + `Obj->Modify()`
(captures the before-state for each selected object) + `PropertyHandle->SetValue(NewValue)`
(the correct typed API that goes through the full property pipeline). Undo now correctly
appears in **Edit → Undo** as "Set String Table Reference".

**Wrong TableId passed to FText::FromStringTable**
`FStringTableViewerEntry::TableId` stores the asset's short name, which doesn't always match
the ID registered in `FStringTableRegistry`. Fixed by deriving the table ID from
`Item->AssetPath.GetAssetPathString()` in `OnApplyClicked`.

**Dropdown spawn position covered the property row**
The dropdown opened exactly at the cursor position, obscuring the row being edited. Fixed by
adding a `FVector2D(0.f, 20.f)` offset to the spawn location.

**FSlateApplication::FindWidgetInAllWindows does not exist**
Used to resolve the button widget path for `PushMenu`. This API doesn't exist on
`FSlateApplication`. Fixed by using `GetCachedGeometry()` on the button widget to derive the
anchor position, then passing an empty `FWidgetPath()` to `PushMenu`.

**Apply button text unreadable at column width**
The fixed column width was too narrow for the "Apply" label. Fixed by replacing the text with
a `Icons.Check` icon and keeping the description in the tooltip.

**Apply column had no header label**
The column header was set to an empty `LOCTEXT`. Fixed by giving it the label `"Apply"`.

**Unused `FString LastSearchText` instance member**
`FTextStringTableBrowserDetailCustomization` declared a `FString LastSearchText` instance
member. Both placement paths create per-row `TSharedPtr<FString>` instances for last-search
persistence and never write to or read from the class member. It was a leftover from an
earlier design where state was shared at the customization level rather than per-row. Removed.

**`FPropertyRowExtensionButton` provides no widget size controls**
After confirming via logging that `OnGeneratePropertyRowExtension` was being called and
`OutExtensionButtons` was growing correctly, the button was still not visible. Root cause:
`FPropertyRowExtensionButton` exposes only data fields (`Icon`, `Label`, `ToolTip`,
`UIAction`). Unreal builds the button widget internally with no size or padding controls
available to the caller. In UE 5.3+, extension buttons render with both icon and label text
side by side, making the button wider than the extension bar column.

**Extension bar column does not resize for exactly two buttons (UE bug)**
With the label cleared, further investigation revealed a more fundamental issue: the extension
bar column in `SDetailSingleItemRow` has a fixed width sized for one button. When exactly two
buttons are present — the plugin button and the Reset-to-Default button — the column does not
expand and the second button is clipped. This does not occur with one button (fits) or three or
more buttons (UE's overflow dropdown activates). The 2-button case falls into a gap between the
single-button layout and the overflow threshold. There is no API to force column expansion from
plugin code. Addressed by inserting the plugin's button at index 0 of `OutExtensionButtons`,
ensuring it occupies the leftmost position with the best chance of remaining visible.

**`ExtensionContent()` slot and `FPropertyRowExtensionButton` both unsuitable for reliable
cross-plugin use**
The original `CustomWidget().ExtensionContent()` approach was abandoned because MVVM overrides
it (last writer wins). The replacement `FPropertyRowExtensionButton` approach via
`GetGlobalRowExtensionDelegate()` is the architecturally correct multi-plugin solution but is
broken in practice by the 2-button column sizing bug above. The conclusion: the extension bar
area has no fully reliable cross-plugin solution within the current UE API. The `NameContent()`
slot used by the `NextToLabel` mode is the only placement that is always fully visible,
correctly sized, and conflict-free with MVVM and all other plugins.

**`NextToLabel` made the default placement**
`ExtensionBar` was the original default on the assumption that the right-side placement was
preferable. Given the 2-button column sizing bug and the prevalence of MVVM in shipping
projects, `NextToLabel` is now the default. The setting remains available so teams can opt into
`ExtensionBar` if they specifically want right-side placement and are certain their project will
not hit the 2-button case.

## Shared Filter Refactor

**Duplicated filter logic between browser and picker**
`PassesFilter`, `EscapeRegex`, and the compiled regex state existed only in
`SStringTableBrowser` and would have needed copying into `SStringTableBrowserPickerDropdown`.
Fixed by extracting everything into a standalone `FStringTableSearchFilter` struct that both
widgets own as a member, guaranteeing identical behaviour.

## Plugin Compatibility

**ExtensionContent() slot overridden by MVVM plugin**
`ExtensionContent()` on a Details panel row accepts only a single widget. When MVVM is
enabled, its customization registers after ours and overwrites our button entirely, making it
disappear. Investigated `ExtensionContent()` and the `RegisterExtension` API but found neither
existed on `FPropertyEditorModule` in UE 5.7. The correct API, found by inspecting
`PropertyEditorModule.h` directly, is `GetGlobalRowExtensionDelegate()` which returns an
`FOnGenerateGlobalRowExtension` multicast delegate. Each plugin binds independently by
appending to `OutExtensionButtons` — Unreal collects all registered buttons and renders them
together in a shared bar, so multiple plugins coexist without conflict. The `IDetailCustomization`
path was kept alongside the extension delegate path to support the "Next to Label" placement
option.

## Plugin Settings

**Button placement needed to be configurable without code changes**
Different projects have different plugin combinations — some need the Extension Bar path to
avoid MVVM conflicts, others prefer the button always visible next to the label. Rather than
hardcoding one approach, `UStringTableBrowserSettings`, a subclass of `UDeveloperSettings`,
was added. This surfaces automatically under **Edit → Project Settings → Plugins → String
Table Browser** with no manual UI code. Both the global row extension delegate and the
`IDetailCustomization` are always registered; each checks `ButtonPlacement` at runtime and
returns early if it is not the active mode. Changing the setting takes effect the next time a
Details panel rebuilds its layout — no re-registration needed.

**Search debounce delay and disk cache save delay hardcoded**
`SearchDebounceDelay` was initially a `static constexpr float` in `SStringTableBrowser.h`,
invisible to non-programmer team members and requiring a recompile to adjust. The disk cache
save debounce had no delay member at all. Both values were promoted to `UPROPERTY` fields on
`UStringTableBrowserSettings` — `SearchDebounceDelay` and `SaveCacheToDiskDelay` — making
them configurable from Project Settings per project without touching code.
 