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
#include "StringTableBrowserTypes.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "UObject/TextProperty.h"

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
    TArray<FPropertyRowExtensionButton>& OutExtensionButtons
)
{
    // Only active when the setting is ExtensionBar
    if (UStringTableBrowserSettings::Get()->ButtonPlacement !=
        EStringTableBrowserButtonPlacement::ExtensionBar
    )
    {
        return;
    }

    if (!InArgs.PropertyHandle.IsValid() || 
    	!InArgs.PropertyHandle->GetProperty() ||
        !InArgs.PropertyHandle->GetProperty()->IsA<FTextProperty>()
    )
    {
        return;
    }

	UE_LOG(LogStringTableBrowser, Verbose, TEXT("StringTableBrowser: ExtensionBar - Property: %s"), *InArgs.PropertyHandle->GetProperty()->GetName());

    TSharedPtr<IPropertyHandle> PropertyHandle = InArgs.PropertyHandle;
    TSharedPtr<FString> LastSearchText = MakeShared<FString>();
    {
        FText CurrentValue;
        PropertyHandle->GetValue(CurrentValue);
        *LastSearchText = CurrentValue.ToString();
    }

	FPropertyRowExtensionButton Button;
    Button.Icon    = FSlateIcon(FAppStyle::GetAppStyleSetName(), StringTableBrowserIcons::OpenBrowserSearch);
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
	
	OutExtensionButtons.Insert(MoveTemp(Button), 0);
}

// -------------------------------------------------------------------------
// Next-to-label path
// -------------------------------------------------------------------------

TSharedRef<IDetailCustomization> FTextStringTableBrowserDetailCustomization::MakeInstance()
{
    return MakeShared<FTextStringTableBrowserDetailCustomization>();
}

void FTextStringTableBrowserDetailCustomization::CustomizeDetails(
    IDetailLayoutBuilder& DetailBuilder
)
{
    // Only active when the setting is NextToLabel
    if (UStringTableBrowserSettings::Get()->ButtonPlacement != EStringTableBrowserButtonPlacement::NextToLabel)
    {
        return;
    }
    
    TFunction<void(TSharedRef<IPropertyHandle>, IDetailCategoryBuilder&)> ProcessPropertyHandle =
        [&](TSharedRef<IPropertyHandle> PropertyHandle, IDetailCategoryBuilder& Category)
	    {
		    if (!PropertyHandle->GetProperty() || !PropertyHandle->GetProperty()->IsA<FTextProperty>())
            {
			    // Recurse into children to find FText properties and go to the next iteration.
			    uint32 NumChildren = 0;
    		    PropertyHandle->GetNumChildren(NumChildren);

    		    for (uint32 i = 0; i < NumChildren; ++i)
    		    {
        		    TSharedPtr<IPropertyHandle> ChildHandle = PropertyHandle->GetChildHandle(i);
        		    if (ChildHandle.IsValid())
        		    {
            		    ProcessPropertyHandle(ChildHandle.ToSharedRef(), Category);
        		    }
    		    }
		        return;
            }

            UE_LOG(LogStringTableBrowser, Verbose, TEXT("StringTableBrowser: NextToLabel - Property: %s"), *PropertyHandle->GetProperty()->GetName());
            
            FText CurrentValue;
            PropertyHandle->GetValue(CurrentValue);

            // Per-row last search state — shared with the button lambda
            TSharedPtr<FString> RowLastSearchText =
                MakeShared<FString>(CurrentValue.ToString());

            TSharedPtr<SWidget> NameWidget;
            TSharedPtr<SWidget> ValueWidget;

            IDetailPropertyRow& Row = Category.AddProperty(PropertyHandle);
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
                    .OnClicked_Lambda([PropertyHandle, RowLastSearchText]()
                    {
                        OpenPickerDropdown(PropertyHandle, RowLastSearchText);
                        return FReply::Handled();
                    })
                    [
                        SNew(SImage)
                        .Image(FAppStyle::GetBrush(StringTableBrowserIcons::OpenBrowserSearch))
                        .DesiredSizeOverride(FVector2D(14.0f, 14.0f))
                    ]
                ]
            ]
            .ValueContent()
            .MinDesiredWidth(250.0f)
            [
                ValueWidget.ToSharedRef()
            ];
	    };
    
    TArray<FName> CategoryNames;
    DetailBuilder.GetCategoryNames(CategoryNames);

    for (const FName& CategoryName : CategoryNames)
    {
        IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(CategoryName);

        TArray<TSharedRef<IPropertyHandle>> CategoryProperties;
        Category.GetDefaultProperties(CategoryProperties);

        for (const TSharedRef<IPropertyHandle>& PropertyHandle : CategoryProperties)
		{
			ProcessPropertyHandle(PropertyHandle, Category);
		}
    }
}

#undef LOCTEXT_NAMESPACE