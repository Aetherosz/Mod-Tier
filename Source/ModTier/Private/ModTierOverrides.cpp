#include "ModTierOverrides.h"

#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "FGResearchTree.h"
#include "FGSchematic.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Buildables/FGBuildable.h"
#include "FGVehicle.h"
#include "Resources/FGBuildingDescriptor.h"
#include "Resources/FGItemDescriptor.h"
#include "Resources/FGVehicleDescriptor.h"
#include "UObject/UObjectHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Styling/SlateBrush.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogModTierOverrides, Log, All);

TArray<FModTierOverrides::FOverride> FModTierOverrides::Overrides;
bool FModTierOverrides::bEnabled = true;
bool FModTierOverrides::bHasApplied = false;

namespace
{
	const TCHAR* const NameProperty = TEXT("mDisplayName");
	/** Icon fields of the three kinds of target: brushes and plain textures. */
	const TCHAR* const BrushProperties[] = { TEXT("mSchematicIcon"), TEXT("mResearchTreeIcon") };
	const TCHAR* const TextureProperties[] = { TEXT("mPersistentBigIcon"), TEXT("mSmallIcon"), TEXT("mSmallSchematicIcon") };

	/** What was there before a replacement, to put back when the overrides are applied again. */
	struct FSavedValue
	{
		TWeakObjectPtr<UObject> Object;
		FProperty* Property = nullptr;
		TArray<uint8> Value; // text and brush properties: a copy made by the property itself
		TWeakObjectPtr<UObject> ObjectValue;
	};
	TArray<FSavedValue> SavedValues;

	/** Textures made from PNG files, by file name. Kept so a file is read once. */
	TMap<FString, TWeakObjectPtr<UTexture2D>> FileIcons;

	/** Keeps an object alive that nothing else may reference any more (a replaced original, a PNG texture). */
	void KeepAlive(UObject* Object)
	{
		if (Object && !Object->IsRooted())
		{
			Object->AddToRoot();
		}
	}

	void SaveValue(UObject* Object, FProperty* Property)
	{
		FSavedValue& Saved = SavedValues.AddDefaulted_GetRef();
		Saved.Object = Object;
		Saved.Property = Property;
		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			UObject* Original = ObjectProperty->GetObjectPropertyValue_InContainer(Object);
			KeepAlive(Original);
			Saved.ObjectValue = Original;
		}
		else
		{
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				if (StructProperty->Struct == FSlateBrush::StaticStruct())
				{
					KeepAlive(StructProperty->ContainerPtrToValuePtr<FSlateBrush>(Object)->GetResourceObject());
				}
			}
			Saved.Value.SetNumZeroed(Property->GetSize());
			Property->InitializeValue(Saved.Value.GetData());
			Property->CopyCompleteValue(Saved.Value.GetData(), Property->ContainerPtrToValuePtr<void>(Object));
		}
	}

	bool IsTargetClass(const UClass* Class)
	{
		return Class->IsChildOf(UFGSchematic::StaticClass()) || Class->IsChildOf(UFGResearchTree::StaticClass())
			|| Class->IsChildOf(UFGItemDescriptor::StaticClass());
	}

	const TCHAR* KindName(EModTierTargetKind Kind)
	{
		switch (Kind)
		{
		case EModTierTargetKind::Milestone: return TEXT("Milestone");
		case EModTierTargetKind::Research: return TEXT("MAM research");
		case EModTierTargetKind::Item: return TEXT("Item");
		default: return TEXT("Other unlock");
		}
	}
}

FString FModTierOverrides::GetDirectory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Configs") / TEXT("ModTier"));
}

void FModTierOverrides::Apply()
{
	Revert();
	IFileManager::Get().MakeDirectory(*(GetDirectory() / TEXT("Icons")), true);
	WriteTargets(); // before replacing anything, so it lists the original names
	Load();
	bHasApplied = true;
	ApplyLoaded();
}

void FModTierOverrides::SetEnabled(bool bInEnabled)
{
	if (bEnabled == bInEnabled)
	{
		return;
	}
	bEnabled = bInEnabled;
	if (bHasApplied)
	{
		Revert();
		ApplyLoaded();
	}
}

void FModTierOverrides::ApplyLoaded()
{
	if (!bEnabled)
	{
		return;
	}
	int32 Applied = 0;
	for (const FOverride& Override : Overrides)
	{
		UClass* Class = FindTargetClass(Override.Target);
		if (!Class)
		{
			UE_LOG(LogModTierOverrides, Warning, TEXT("Target not found (mod removed?): %s"), *Override.Target);
			continue;
		}
		UObject* DefaultObject = Class->GetDefaultObject();
		if (!Override.Name.IsEmpty())
		{
			SetName(DefaultObject, Override.Name);
		}
		if (!Override.Icon.IsEmpty())
		{
			if (UTexture2D* Icon = ResolveIcon(Override.Icon))
			{
				SetIcon(DefaultObject, Icon);
			}
			else
			{
				UE_LOG(LogModTierOverrides, Warning, TEXT("Icon not found for %s: %s"), *Override.Target, *Override.Icon);
			}
		}
		++Applied;
	}
	UE_LOG(LogModTierOverrides, Log, TEXT("Applied %d of %d overrides"), Applied, Overrides.Num());
}

void FModTierOverrides::Load()
{
	Overrides.Reset();
	const FString File = GetDirectory() / TEXT("Overrides.json");
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *File))
	{
		return; // no file: nothing to replace
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		UE_LOG(LogModTierOverrides, Error, TEXT("%s is not valid JSON; no overrides applied"), *File);
		// Keep the unreadable file: the next save would otherwise overwrite what the player wrote.
		IFileManager::Get().Copy(*(File + TEXT(".unreadable")), *File);
		return;
	}
	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	if (!Root->TryGetArrayField(TEXT("overrides"), Entries))
	{
		return;
	}
	for (const TSharedPtr<FJsonValue>& Entry : *Entries)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		FOverride Override;
		if (Entry.IsValid() && Entry->TryGetObject(Object) && (*Object)->TryGetStringField(TEXT("target"), Override.Target))
		{
			(*Object)->TryGetStringField(TEXT("name"), Override.Name);
			(*Object)->TryGetStringField(TEXT("icon"), Override.Icon);
			Overrides.Add(MoveTemp(Override));
		}
	}
}

void FModTierOverrides::Save()
{
	TArray<TSharedPtr<FJsonValue>> Entries;
	for (const FOverride& Override : Overrides)
	{
		const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("target"), Override.Target);
		if (!Override.Name.IsEmpty())
		{
			Object->SetStringField(TEXT("name"), Override.Name);
		}
		if (!Override.Icon.IsEmpty())
		{
			Object->SetStringField(TEXT("icon"), Override.Icon);
		}
		Entries.Add(MakeShared<FJsonValueObject>(Object));
	}
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("overrides"), Entries);
	FString Text;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text));
	IFileManager::Get().MakeDirectory(*GetDirectory(), true);
	if (!FFileHelper::SaveStringToFile(Text, *(GetDirectory() / TEXT("Overrides.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogModTierOverrides, Error, TEXT("Could not write Overrides.json"));
	}
}

FModTierOverrides::FOverride& FModTierOverrides::FindOrAddOverride(const FString& Target)
{
	for (FOverride& Override : Overrides)
	{
		if (Override.Target == Target)
		{
			return Override;
		}
	}
	FOverride& Override = Overrides.AddDefaulted_GetRef();
	Override.Target = Target;
	return Override;
}

FString FModTierOverrides::GetNameOverride(const FString& Target)
{
	for (const FOverride& Override : Overrides)
	{
		if (Override.Target == Target)
		{
			return Override.Name;
		}
	}
	return FString();
}

FString FModTierOverrides::GetIconOverride(const FString& Target)
{
	for (const FOverride& Override : Overrides)
	{
		if (Override.Target == Target)
		{
			return Override.Icon;
		}
	}
	return FString();
}

void FModTierOverrides::SetNameOverride(const FString& Target, const FString& Name)
{
	FindOrAddOverride(Target).Name = Name;
	OnOverridesChanged();
}

void FModTierOverrides::SetIconOverride(const FString& Target, const FString& IconSpec)
{
	FindOrAddOverride(Target).Icon = IconSpec;
	OnOverridesChanged();
}

void FModTierOverrides::OnOverridesChanged()
{
	Overrides.RemoveAll([](const FOverride& Override) { return Override.Name.IsEmpty() && Override.Icon.IsEmpty(); });
	Save();
	Revert();
	ApplyLoaded();
}

void FModTierOverrides::Revert()
{
	for (int32 Index = SavedValues.Num() - 1; Index >= 0; --Index) // newest first, back to the oldest value
	{
		FSavedValue& Saved = SavedValues[Index];
		if (UObject* Object = Saved.Object.Get())
		{
			if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Saved.Property))
			{
				ObjectProperty->SetObjectPropertyValue_InContainer(Object, Saved.ObjectValue.Get());
			}
			else
			{
				Saved.Property->CopyCompleteValue(Saved.Property->ContainerPtrToValuePtr<void>(Object), Saved.Value.GetData());
				Saved.Property->DestroyValue(Saved.Value.GetData());
			}
		}
	}
	SavedValues.Reset();
}

UClass* FModTierOverrides::FindTargetClass(const FString& Path)
{
	UClass* Class = LoadClass<UObject>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	return Class && IsTargetClass(Class) ? Class : nullptr;
}

UTexture2D* FModTierOverrides::ResolveIcon(const FString& Spec)
{
	if (Spec.StartsWith(TEXT("file:")))
	{
		// Only a file name: nothing outside the Icons folder is read.
		const FString FileName = FPaths::GetCleanFilename(Spec.RightChop(5));
		if (const TWeakObjectPtr<UTexture2D>* Known = FileIcons.Find(FileName))
		{
			if (Known->IsValid())
			{
				return Known->Get();
			}
		}
		UTexture2D* Texture = FImageUtils::ImportFileAsTexture2D(GetDirectory() / TEXT("Icons") / FileName);
		if (Texture)
		{
			KeepAlive(Texture);
			FileIcons.Add(FileName, Texture);
		}
		return Texture;
	}
	if (Spec.StartsWith(TEXT("copy:")))
	{
		// The icon the other target came with, also when that one has been replaced itself.
		const UClass* Source = FindTargetClass(Spec.RightChop(5));
		if (!Source)
		{
			return nullptr;
		}
		const UObject* SourceObject = Source->GetDefaultObject();
		for (const FSavedValue& Saved : SavedValues) // oldest first: the first saved value is the original
		{
			if (Saved.Object.Get() == SourceObject)
			{
				if (UTexture2D* Texture = Cast<UTexture2D>(Saved.ObjectValue.Get()))
				{
					return Texture;
				}
				const FStructProperty* StructProperty = CastField<FStructProperty>(Saved.Property);
				if (StructProperty && StructProperty->Struct == FSlateBrush::StaticStruct() && Saved.Value.Num() > 0)
				{
					if (UTexture2D* Texture = Cast<UTexture2D>(reinterpret_cast<const FSlateBrush*>(Saved.Value.GetData())->GetResourceObject()))
					{
						return Texture;
					}
				}
			}
		}
		return GetIcon(SourceObject);
	}
	return LoadObject<UTexture2D>(nullptr, *Spec, nullptr, LOAD_NoWarn | LOAD_Quiet);
}

void FModTierOverrides::GetIconFiles(TArray<FString>& Out)
{
	IFileManager::Get().FindFiles(Out, *(GetDirectory() / TEXT("Icons") / TEXT("*.png")), true, false);
	Out.Sort();
}

UTexture2D* FModTierOverrides::GetIcon(const UObject* DefaultObject)
{
	for (const TCHAR* Name : TextureProperties)
	{
		if (const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(DefaultObject->GetClass(), Name))
		{
			if (UTexture2D* Texture = Cast<UTexture2D>(Property->GetObjectPropertyValue_InContainer(DefaultObject)))
			{
				return Texture;
			}
		}
	}
	for (const TCHAR* Name : BrushProperties)
	{
		const FStructProperty* Property = FindFProperty<FStructProperty>(DefaultObject->GetClass(), Name);
		if (Property && Property->Struct == FSlateBrush::StaticStruct())
		{
			if (UTexture2D* Texture = Cast<UTexture2D>(Property->ContainerPtrToValuePtr<FSlateBrush>(DefaultObject)->GetResourceObject()))
			{
				return Texture;
			}
		}
	}
	return nullptr;
}

FText FModTierOverrides::GetDisplayName(const UClass* Class)
{
	UClass* MutableClass = const_cast<UClass*>(Class);
	if (Class->IsChildOf(UFGSchematic::StaticClass()))
	{
		return UFGSchematic::GetSchematicDisplayName(MutableClass);
	}
	if (Class->IsChildOf(UFGResearchTree::StaticClass()))
	{
		return UFGResearchTree::GetDisplayName(MutableClass);
	}
	return UFGItemDescriptor::GetItemName(MutableClass);
}

void FModTierOverrides::SetName(UObject* DefaultObject, const FString& Name)
{
	// A building or vehicle takes its name from the thing that gets built, not from its entry in the build menu.
	UClass* BuiltClass = nullptr;
	if (DefaultObject->IsA<UFGBuildingDescriptor>())
	{
		BuiltClass = UFGBuildingDescriptor::GetBuildableClass(DefaultObject->GetClass());
	}
	else if (DefaultObject->IsA<UFGVehicleDescriptor>())
	{
		BuiltClass = UFGVehicleDescriptor::GetVehicleClass(DefaultObject->GetClass());
	}
	const FText NewName = FText::FromString(Name);
	if (BuiltClass)
	{
		if (FTextProperty* Property = FindFProperty<FTextProperty>(BuiltClass, NameProperty))
		{
			UObject* BuiltDefault = BuiltClass->GetDefaultObject();
			SaveValue(BuiltDefault, Property);
			Property->SetPropertyValue_InContainer(BuiltDefault, NewName);
			// The ones already standing in the world copied the old name when they were placed.
			TArray<UObject*> Placed;
			GetObjectsOfClass(BuiltClass, Placed, false);
			for (UObject* Object : Placed)
			{
				SaveValue(Object, Property);
				Property->SetPropertyValue_InContainer(Object, NewName);
			}
		}
	}
	if (FTextProperty* Property = FindFProperty<FTextProperty>(DefaultObject->GetClass(), NameProperty))
	{
		SaveValue(DefaultObject, Property);
		Property->SetPropertyValue_InContainer(DefaultObject, NewName);
	}
}

void FModTierOverrides::SetIcon(UObject* DefaultObject, UTexture2D* Icon)
{
	for (const TCHAR* Name : TextureProperties)
	{
		if (FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(DefaultObject->GetClass(), Name))
		{
			SaveValue(DefaultObject, Property);
			Property->SetObjectPropertyValue_InContainer(DefaultObject, Icon);
		}
	}
	for (const TCHAR* Name : BrushProperties)
	{
		FStructProperty* Property = FindFProperty<FStructProperty>(DefaultObject->GetClass(), Name);
		if (Property && Property->Struct == FSlateBrush::StaticStruct())
		{
			SaveValue(DefaultObject, Property);
			Property->ContainerPtrToValuePtr<FSlateBrush>(DefaultObject)->SetResourceObject(Icon); // size and tint stay
		}
	}
}

void FModTierOverrides::GatherTargets(TArray<TSharedPtr<FModTierTarget>>& Out)
{
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (Class->HasAnyClassFlags(CLASS_Native | CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) || !IsTargetClass(Class)
			|| Class->GetName().StartsWith(TEXT("SKEL_")) || Class->GetName().StartsWith(TEXT("REINST_")))
		{
			continue;
		}
		const TSharedPtr<FModTierTarget> Target = MakeShared<FModTierTarget>();
		Target->Class = Class;
		Target->Path = Class->GetPathName();
		if (!Target->Path.StartsWith(TEXT("/Game/")))
		{
			// "/RoadMaker/Unlocks/..." -> "RoadMaker"
			Target->Path.RightChop(1).Split(TEXT("/"), &Target->Mod, nullptr);
		}
		if (Class->IsChildOf(UFGSchematic::StaticClass()))
		{
			const ESchematicType Type = UFGSchematic::GetType(Class);
			Target->Kind = Type == ESchematicType::EST_Milestone ? EModTierTargetKind::Milestone
				: Type == ESchematicType::EST_MAM ? EModTierTargetKind::Research : EModTierTargetKind::Other;
		}
		else
		{
			Target->Kind = Class->IsChildOf(UFGResearchTree::StaticClass()) ? EModTierTargetKind::Research : EModTierTargetKind::Item;
		}
		Out.Add(Target);
	}
}

void FModTierOverrides::WriteTargets()
{
	TArray<TSharedPtr<FModTierTarget>> Targets;
	GatherTargets(Targets);
	TArray<FString> Lines;
	for (const TSharedPtr<FModTierTarget>& Target : Targets)
	{
		if (!Target->Mod.IsEmpty())
		{
			Lines.Add(FString::Printf(TEXT("%-13s | %-45s | %s"), KindName(Target->Kind), *GetDisplayName(Target->Class.Get()).ToString(), *Target->Path));
		}
	}
	Lines.Sort();
	Lines.Insert(TEXT("Everything from mods whose name or icon can be replaced (loaded so far). Use the path in the last column as \"target\"."), 0);
	FFileHelper::SaveStringArrayToFile(Lines, *(GetDirectory() / TEXT("Targets.txt")), FFileHelper::EEncodingOptions::ForceUTF8);
}
