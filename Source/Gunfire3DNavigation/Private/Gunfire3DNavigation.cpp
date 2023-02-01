// Copyright Gunfire Games, LLC. All Rights Reserved.

#include "Gunfire3DNavData.h"

#include "Engine/Console.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"

#if WITH_EDITOR
#include "ISettingsModule.h"
#include "Settings/ProjectPackagingSettings.h"
#include "GameDelegates.h"
#endif //WITH_EDITOR

class FGunfire3DNavigation : public IModuleInterface
{
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

#if WITH_EDITOR
	void ModifyCookDelegate(TConstArrayView<const ITargetPlatform*> InTargetPlatforms, TArray<FName>& InOutPackagesToCook, TArray<FName>& InOutPackagesToNeverCook);
#endif
	void ShowNavType(const TArray<FString>& Args);
	void PopulateAutoCompleteEntries(TArray<FAutoCompleteCommand>& AutoCompleteList);

#if WITH_EDITOR
	FDelegateHandle CookDelegate;
#endif
};

IMPLEMENT_MODULE(FGunfire3DNavigation, Gunfire3DNavigation)
#define LOCTEXT_NAMESPACE "Gunfire3DNavigation"

void FGunfire3DNavigation::StartupModule()
{
	static FAutoConsoleCommand CCmdOpen = FAutoConsoleCommand(
		TEXT("ShowNavType"),
		TEXT("Toggles visiblity on the specified nav type"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FGunfire3DNavigation::ShowNavType));

	// This code will execute after your module is loaded into memory (but after global variables are initialized, of course.)
	UConsole::RegisterConsoleAutoCompleteEntries.AddRaw(this, &FGunfire3DNavigation::PopulateAutoCompleteEntries);

#if WITH_EDITOR
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->RegisterSettings("Project", "Plugins", "Gunfire 3D Navigation",
			LOCTEXT("Gunfire3DNavigationHeading", "Gunfire 3D Navigation"),
			LOCTEXT("DescriptionDescription", "Settings for Gunfire 3D Navigation Plugin"),
			GetMutableDefault<AGunfire3DNavData>()
		);
	}

	CookDelegate = FGameDelegates::Get().GetModifyCookDelegate().AddRaw(this, &FGunfire3DNavigation::ModifyCookDelegate);
#endif //WITH_EDITOR
}

void FGunfire3DNavigation::ShutdownModule()
{
#if WITH_EDITOR
	FGameDelegates::Get().GetModifyCookDelegate().Remove(CookDelegate);
#endif
}

#if WITH_EDITOR

void FGunfire3DNavigation::ModifyCookDelegate(TConstArrayView<const ITargetPlatform*> InTargetPlatforms, TArray<FName>& InOutPackagesToCook, TArray<FName>& InOutPackagesToNeverCook)
{
	// TODO: Add our debug material to the packages to cook
}

#endif

void FGunfire3DNavigation::ShowNavType(const TArray<FString>& Args)
{
	if (Args.Num() > 0)
	{
		for (TActorIterator<ANavigationData> Itr(GWorld); Itr; ++Itr)
		{
			ANavigationData* NavData = *Itr;

			const bool ShowType = (NavData->GetConfig().GetName().ToString() == Args[0]);

			NavData->SetNavRenderingEnabled(ShowType);

			if (AGunfire3DNavData* Volume = Cast<AGunfire3DNavData>(NavData))
				Volume->RequestDrawingUpdate(true);
		}
	}
}

void FGunfire3DNavigation::PopulateAutoCompleteEntries(TArray<FAutoCompleteCommand>& AutoCompleteList)
{
	const UNavigationSystemV1* NavSysCDO = (*GEngine->NavigationSystemClass != nullptr)
		? GetDefault<UNavigationSystemV1>(GEngine->NavigationSystemClass)
		: GetDefault<UNavigationSystemV1>();

	for (const FNavDataConfig& Config : NavSysCDO->GetSupportedAgents())
	{
		AutoCompleteList.AddDefaulted();
		FAutoCompleteCommand& AutoCompleteCommand = AutoCompleteList.Last();
		AutoCompleteCommand.Command = FString::Printf(TEXT("ShowNavType %s"), *Config.GetName().ToString());
	}
}

#undef LOCTEXT_NAMESPACE

