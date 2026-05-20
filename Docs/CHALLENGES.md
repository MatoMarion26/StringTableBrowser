# String Table Browser — Development Challenges & Fixes

A summary of every significant issue encountered during development and how each was resolved.

---

## Module & Startup

**Asset Registry delegate never unbound after OnFilesLoaded**
`OnFilesLoaded` could fire more than once. Fixed by calling `RemoveAll(this)` at the top of `OnAssetRegistryFilesLoaded` so the callback is self-removing.

**AssetRegistry module loaded twice in StartupModule**
Two separate `LoadModuleChecked` calls created a potential stale reference. Fixed by hoisting a single `FAssetRegistryModule&` at the top of `StartupModule` and reusing it throughout.

**OnFilesLoaded not unbound during rapid shutdown**
If the editor closed before the initial asset scan finished, the delegate was never removed. Fixed by also calling `RemoveAll(this)` in `ShutdownModule`.

**Plugin tab and menu not appearing**
The plugin template comment stubs for `RegisterMenus` and `PluginButtonClicked` were never implemented. Fixed by implementing `FGlobalTabmanager::RegisterNomadTabSpawner` and `UToolMenus::ExtendMenu("LevelEditor.MainMenu.Tools")` with a proper menu entry pointing to **Tools → String Table Browser**.

**Plugin loaded but menu entry missing**
`RegisterMenus` was called before the editor toolbar was ready. Fixed by deferring registration via `UToolMenus::RegisterStartupCallback`.

---

## Caching

**ForceRebuildCache showing no entries**
After the thread-safety fix replaced `GetAsset()` with `FindObject()`, the initial cache build found nothing because no assets were loaded in memory yet at startup. Fixed by using `FindObject` first and falling back to `GetAsset()` (synchronous load) only inside `ForceRebuildCache`, keeping `FindObject`-only in the incremental callbacks.

**Stale disk cache loaded silently after updates**
The JSON cache had no version field, so outdated caches were loaded without question after schema changes. Fixed by adding a `GStringTableBrowserCacheVersion` constant and rejecting caches with a mismatched version, triggering a full rebuild.

**No thread safety on cache mutations**
Asset Registry callbacks can fire on background threads in some engine versions. Fixed by protecting all `GroupedCache` and `FlatCache` mutations with `FCriticalSection CacheLock`, and adding `GetCachedEntriesCopy()` for a lock-guarded snapshot safe to consume from Slate.

**BroadcastCacheUpdated called from background thread**
Slate delegates must be fired on the game thread. Fixed by checking `IsInGameThread()` and marshalling to the game thread via `AsyncTask(ENamedThreads::GameThread, ...)` when needed.

**Asset class comparison using short name**
`AssetClassPath.GetAssetName() == TEXT("StringTable")` could false-match other plugins. Fixed by comparing against `UStringTable::StaticClass()->GetClassPathName()` throughout.

---

## Search & Filtering

**Regex constructed per row from raw user input**
An invalid pattern (e.g. a bare `[`) caused undefined behaviour on every row. Fixed by pre-compiling the pattern once in `ApplyFilterAndSort`, wrapping construction in a try/catch, and skipping all rows if the pattern is invalid.

**FRegexMatcher::Sanitize used for whole-word escaping**
This internal API has no public contract. Fixed by replacing it with a custom `EscapeRegex` helper that manually escapes all ICU regex metacharacters.

**Match Case ignored in Whole Word and Regex modes**
Both regex paths used the raw pattern with no case flag. Fixed by prefixing patterns with `(?i)` when `bMatchCase` is false, and omitting the prefix when it is true.

**Search target always included all three fields**
Before scope toggles were added, the search target concatenated Key, Value, and TableId unconditionally. Fixed by building the target dynamically from only the fields the user has enabled, and returning `true` for all entries when no scope is selected.

**Main browser showed nothing on empty search**
After the picker was added with "empty = show nothing" behaviour, the main browser accidentally inherited it. Fixed by branching the two widgets: empty search shows everything in the main browser and nothing in the picker.

---

## Slate & UI

**SortData called directly from OnSortColumnHeader**
Calling `SortData` independently of `ApplyFilterAndSort` could sort a partially rebuilt list. Fixed by routing `OnSortColumnHeader` through `ApplyFilterAndSort` so filter and sort state are always consistent.

**Action column too narrow for two buttons**
Adding the Copy Key button caused it to overflow. Fixed by widening the Action column from `100` to `180` fixed width.

**Action column text buttons replaced with icons**
Text labels ("Edit Table", "Copy Key") were too wide for a compact column. Replaced with `SimpleButton`-style icon buttons using `Icons.Edit`, `Icons.Clipboard`, and `Icons.Find` brushes. A shared `MakeActionIconButton` static helper was introduced so all three buttons share identical sizing, padding, and style without repetition. Column width reduced to `110` to fit three icon buttons.

**Inline MakeIconButton lambda violated code standard**
The initial implementation used a local lambda inside `GenerateWidgetForColumn` to build icon buttons. Lambdas that capture nothing and are reused are better expressed as static file-scope helpers. Promoted to `static TSharedRef<SWidget> MakeActionIconButton(...)` alongside the existing `MakeFilterCheckBox`, matching the established pattern.

**Regex compiled once but pattern moved into TOptional incorrectly**
`MoveTemp` on the test pattern left it in a valid-but-unspecified state before assignment. Fixed by constructing the final `FRegexPattern` directly into `CompiledPattern` after validation.

---

## Search Performance

**Filter ran on every keystroke causing UI lag on large datasets**
With thousands of entries, running `ApplyFilterAndSort` synchronously on every key press caused perceptible lag. Fixed by debouncing via `RegisterActiveTimer(0.15f, ...)` — the filter only runs 150ms after the last keystroke. If the user types again before the timer fires, `UnRegisterActiveTimer` cancels the pending callback and a fresh timer starts. Toggle changes bypass the debounce and apply immediately.

---

## Reference Viewer

**IAssetManagerEditorModule availability not checked before use**
Calling `IAssetManagerEditorModule::Get()` when the module is not loaded would assert. Fixed by guarding with `IAssetManagerEditorModule::IsAvailable()` before calling `Get()`, matching Unreal's own convention for optional editor modules.

---

## Menu Registration

**Browser entry needed in String Table and Widget Blueprint editors**
The initial implementation only registered the Tools menu entry in the Level Editor. Fixed by extracting a `RegisterInToolsMenu(FName)` lambda inside `RegisterMenus()` and calling it for `"LevelEditor.MainMenu.Tools"`, `"AssetEditor.StringTableEditor.MainMenu.Tools"`, and `"AssetEditor.WidgetBlueprintEditor.MainMenu.Tools"`. The same `AddMenuEntry` definition is reused for all three, avoiding duplication.

---

## Empty State

**No feedback when search returns no results**
When `FilteredEntries` was empty the list view showed a blank area with no explanation. Fixed by wrapping the `SListView` in an `SOverlay` and adding a `STextBlock` that becomes visible via `Visibility_Lambda` when the list is empty. Two distinct messages are shown: "Type to search..." when the search box is empty (picker only), and "No entries match your search." when a search returned no results. `HitTestInvisible` is used rather than `Visible` so the text does not block interaction with the list.

---

## Details Panel Integration

**IPropertyTypeCustomization for "TextProperty" replaced native FText UI**
Registering a type customization for `"TextProperty"` completely replaced Unreal's own `FTextCustomization`, removing the localization flag and string table picker. Every attempt to render `CreatePropertyValueWidget` alongside the native widget caused conflicts. Fixed by abandoning `IPropertyTypeCustomization` entirely and switching to `IDetailCustomization` on `UObject`, which appends rows without replacing anything.

**Correct property type name was "TextProperty" not "Text"**
The initial registration used `"Text"` which was silently ignored. Found by logging `FTextProperty::StaticClass()->GetFName()` at runtime.

**Customization registered before engine's own customizations**
With `LoadingPhase: Default`, the plugin registered before `FTextCustomization`, which then overwrote it. Fixed by changing the `.uplugin` loading phase to `PostEngineInit`.

**Neither CustomizeHeader nor CustomizeChildren being called**
Even with the correct type name, the native FText customization was winning the registration race. Confirmed by the PostEngineInit fix above resolving it.

**TSharedRef member initialised from null TSharedPtr**
`TSharedRef<IPropertyHandle> PropertyHandleRef = TSharedPtr<IPropertyHandle>().ToSharedRef()` asserted immediately on construction because `ToSharedRef()` on a null pointer is illegal. Fixed by removing the unused member entirely — the handle is passed directly through method arguments.

**FindProperty does not exist on IDetailCategoryBuilder**
`Category.FindProperty(PropHandle)` was called to retrieve an existing row, but this API doesn't exist. Fixed by using `Category.AddProperty(PropHandle)` which both registers the property and returns the `IDetailPropertyRow&` directly.

**CustomizeChildren being implemented caused native children to disappear**
Implementing `CustomizeChildren` at all signals to Unreal that the plugin owns child rendering, suppressing the native ones. Fixed by leaving `CustomizeChildren` as an empty required override and doing all work in `CustomizeHeader` / via `AddProperty`.

**Apply button not setting the FText value**
`EnumerateRawData` writes to raw property memory bypass the transaction system entirely, even when surrounded by `NotifyPreChange`/`NotifyPostChange`. The value appeared to change on screen but didn't persist across undo and didn't properly dirty the asset. Fixed by replacing the raw write entirely with `FScopedTransaction` (opens a named undo bracket) + `Obj->Modify()` (captures the before-state for each selected object) + `PropertyHandle->SetValue(NewValue)` (the correct typed API that goes through the full property pipeline). Undo now correctly appears in **Edit → Undo** as "Set String Table Reference".

**Wrong TableId passed to FText::FromStringTable**
`FStringTableViewerEntry::TableId` stores the asset's short name, which doesn't always match the ID registered in `FStringTableRegistry`. Fixed by deriving the table ID from `Item->AssetPath.GetAssetPathString()` in `OnApplyClicked`.

**Dropdown spawn position covered the property row**
The dropdown opened exactly at the cursor position, obscuring the row being edited. Fixed by adding a `FVector2D(0.f, 20.f)` offset to the spawn location.

**FSlateApplication::FindWidgetInAllWindows does not exist**
Used to resolve the button widget path for `PushMenu`. This API doesn't exist on `FSlateApplication`. Fixed by using `GetCachedGeometry()` on the button widget to derive the anchor position, then passing an empty `FWidgetPath()` to `PushMenu`.

**Apply button text unreadable at column width**
The fixed column width was too narrow for the "Apply" label. Fixed by replacing the text with a `Icons.Check` icon and keeping the description in the tooltip.

**Apply column had no header label**
The column header was set to an empty `LOCTEXT`. Fixed by giving it the label `"Apply"`.

---

## Shared Filter Refactor

**Duplicated filter logic between browser and picker**
`PassesFilter`, `EscapeRegex`, and the compiled regex state existed only in `SStringTableBrowser` and would have needed copying into `SStringTableBrowserPickerDropdown`. Fixed by extracting everything into a standalone `FStringTableSearchFilter` struct that both widgets own as a member, guaranteeing identical behaviour.

---

## Plugin Compatibility

**ExtensionContent() slot overridden by MVVM plugin**
`ExtensionContent()` on a Details panel row accepts only a single widget. When MVVM is enabled, its customization registers after ours and overwrites our button entirely, making it disappear. Investigated `FPropertyRowExtensionButton` and the `RegisterExtension` API but found neither existed on `FPropertyEditorModule` in UE 5.7. The correct API, found by inspecting `PropertyEditorModule.h` directly, is `GetGlobalRowExtensionDelegate()` which returns an `FOnGenerateGlobalRowExtension` multicast delegate. Each plugin binds independently by appending to `OutExtensionButtons` — Unreal collects all registered buttons and renders them together in a shared bar, so multiple plugins coexist without conflict. The `IDetailCustomization` path was kept alongside the extension delegate path to support the "Next to Label" placement option.

---

## Plugin Settings

**Button placement needed to be configurable without code changes**
Different projects have different plugin combinations — some need the Extension Bar path to avoid MVVM conflicts, others prefer the button always visible next to the label. Rather than hardcoding one approach, a `UStringTableBrowserSettings` subclass of `UDeveloperSettings` was added. This surfaces automatically under **Edit → Project Settings → Plugins → String Table Browser** with no manual UI code. Both the global row extension delegate and the `IDetailCustomization` are always registered; each checks `ButtonPlacement` at runtime and returns early if it is not the active mode. Changing the setting takes effect the next time a Details panel rebuilds its layout — no re-registration needed.
