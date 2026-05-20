// Copyright (c) 2026 Mato Marion. All Rights Reserved.

#include "FTextStringTableBrowserDetailCustomization.h"
#include "StringTableBrowserSettings.h"
#include "SStringTableBrowserPickerDropdown.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailPropertyRow.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"

#define LOCTEXT_NAMESPACE "FTextStringTableBrowserDetailCustomization"

// -------------------------------------------------------------------------
// Shared helpers
// -------------------------------------------------------------------------

/** Opens the picker dropdown anchored 20px below the cursor. */
void FTextStringTableBrowserDetailCustomization::OpenPickerDropdown(
    TSharedPtr<IPropertyHandle> PropertyHandle,
    TSharedPtr<FString> LastSearchText
)
{
    if (LastSearchText->IsEmpty())
    {
        FText CurrentValue;
        PropertyHandle->GetValue(CurrentValue);
        *LastSearchText = CurrentValue.ToString();
    }

    TSharedRef<SStringTableBrowserPickerDropdown> Dropdown =
        SNew(SStringTableBrowserPickerDropdown)
        .InitialSearchText(*LastSearchText)
        .OnSearchTextChanged_Lambda([LastSearchText](const FString& NewText)
        {
            *LastSearchText = NewText;
        })
        .OnEntryPicked_Lambda([PropertyHandle](FName TableId, FString Key)
        {
            const FScopedTransaction Transaction(
                LOCTEXT("SetStringTableReference", "Set String Table Reference"));

            TArray<UObject*> OuterObjects;
            PropertyHandle->GetOuterObjects(OuterObjects);
            for (UObject* Object : OuterObjects)
            {
                if (Object) { Object->Modify(); }
            }

            const FText NewValue = FText::FromStringTable(TableId, Key);
            PropertyHandle->SetValue(NewValue, EPropertyValueSetFlags::DefaultFlags);
        });

    const FVector2D SpawnLocation =
        FSlateApplication::Get().GetCursorPos() + FVector2D(0.0f, 20.0f);

    FSlateApplication::Get().PushMenu(
        FSlateApplication::Get().GetActiveTopLevelWindow().ToSharedRef(),
        FWidgetPath(),
        Dropdown,
        SpawnLocation,
        FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));

    Dropdown->FocusSearchBox();
}

// -------------------------------------------------------------------------
// Extension bar path
// -------------------------------------------------------------------------

void FTextStringTableBrowserDetailCustomization::OnGeneratePropertyRowExtension(
    const FOnGenerateGlobalRowExtensionArgs& InArgs,
    TArray<FPropertyRowExtensionButton>&     OutExtensionButtons)
{
    // Only active when the setting is ExtensionBar
    if (UStringTableBrowserSettings::Get()->ButtonPlacement !=
        EStringTableBrowserButtonPlacement::ExtensionBar)
    {
        return;
    }

    if (!InArgs.PropertyHandle.IsValid())
    {
        return;
    }

    if (!InArgs.PropertyHandle->GetProperty() ||
        !InArgs.PropertyHandle->GetProperty()->IsA<FTextProperty>())
    {
        return;
    }

    TSharedPtr<IPropertyHandle> PropertyHandle = InArgs.PropertyHandle;
    TSharedPtr<FString> LastSearchText = MakeShared<FString>();
    {
        FText CurrentValue;
        PropertyHandle->GetValue(CurrentValue);
        *LastSearchText = CurrentValue.ToString();
    }

    FPropertyRowExtensionButton& Button = OutExtensionButtons.AddDefaulted_GetRef();
    Button.Icon    = FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Search");
    Button.Label   = LOCTEXT("SearchBtnLabel",   "Search String Tables");
    Button.ToolTip = LOCTEXT("SearchBtnTooltip",
        "Search all String Tables and bind this FText property to the "
        "selected entry as a proper string table reference.");

    Button.UIAction = FUIAction(
        FExecuteAction::CreateLambda([PropertyHandle, LastSearchText]()
        {
            OpenPickerDropdown(PropertyHandle, LastSearchText);
        }),
        FCanExecuteAction()
    );
}

// -------------------------------------------------------------------------
// Next-to-label path
// -------------------------------------------------------------------------

TSharedRef<IDetailCustomization> FTextStringTableBrowserDetailCustomization::MakeInstance()
{
    return MakeShared<FTextStringTableBrowserDetailCustomization>();
}

void FTextStringTableBrowserDetailCustomization::CustomizeDetails(
    IDetailLayoutBuilder& DetailBuilder)
{
    // Only active when the setting is NextToLabel
    if (UStringTableBrowserSettings::Get()->ButtonPlacement !=
        EStringTableBrowserButtonPlacement::NextToLabel)
    {
        return;
    }

    TArray<FName> CategoryNames;
    DetailBuilder.GetCategoryNames(CategoryNames);

    for (const FName& CategoryName : CategoryNames)
    {
        IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(CategoryName);

        TArray<TSharedRef<IPropertyHandle>> CategoryProperties;
        Category.GetDefaultProperties(CategoryProperties);

        for (const TSharedRef<IPropertyHandle>& PropHandle : CategoryProperties)
        {
            if (!PropHandle->GetProperty() ||
                !PropHandle->GetProperty()->IsA<FTextProperty>())
            {
                continue;
            }

            FText CurrentValue;
            PropHandle->GetValue(CurrentValue);

            // Per-row last search state — shared with the button lambda
            TSharedPtr<FString> RowLastSearchText =
                MakeShared<FString>(CurrentValue.ToString());

            TSharedPtr<SWidget> NameWidget;
            TSharedPtr<SWidget> ValueWidget;

            IDetailPropertyRow& Row = Category.AddProperty(PropHandle);
            Row.GetDefaultWidgets(NameWidget, ValueWidget);

            Row.CustomWidget(/*bShowChildren=*/true)
            .NameContent()
            [
                SNew(SHorizontalBox)

                // Native property label — preserved exactly
                + SHorizontalBox::Slot().FillWidth(1.0f)
                [
                    NameWidget.ToSharedRef()
                ]

                // Search button to the right of the label
                + SHorizontalBox::Slot()
                  .AutoWidth()
                  .VAlign(VAlign_Center)
                  .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                [
                    SNew(SButton)
                    .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                    .ToolTipText(LOCTEXT("SearchBtnTooltip",
                        "Search all String Tables and bind this FText property "
                        "to the selected entry as a proper string table reference."))
                    .ContentPadding(FMargin(2.0f))
                    .OnClicked_Lambda([PropHandle, RowLastSearchText]()
                    {
                        OpenPickerDropdown(PropHandle, RowLastSearchText);
                        return FReply::Handled();
                    })
                    [
                        SNew(SImage)
                        .Image(FAppStyle::GetBrush("Icons.Search"))
                        .DesiredSizeOverride(FVector2D(14.0f, 14.0f))
                    ]
                ]
            ]
            .ValueContent()
            .MinDesiredWidth(250.0f)
            [
                ValueWidget.ToSharedRef()
            ];
        }
    }
}

#undef LOCTEXT_NAMESPACE