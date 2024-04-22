// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#include "GitLFSSourceControlModule.h"

#include "Misc/App.h"
#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "Modules/ModuleManager.h"
#include "ContentBrowserDelegates.h"
#include "Features/IModularFeatures.h"

#include "ISourceControlModule.h"
#include "SourceControlHelpers.h"
#include "GitLFSSourceControlUtils.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#include "Operations/GitLFSCopy.h"
#include "Operations/GitLFSSync.h"
#include "Operations/GitLFSFetch.h"
#include "Operations/GitLFSDelete.h"
#include "Operations/GitLFSRevert.h"
#include "Operations/GitLFSCheckIn.h"
#include "Operations/GitLFSConnect.h"
#include "Operations/GitLFSResolve.h"
#include "Operations/GitLFSCheckOut.h"
#include "Operations/GitLFSMarkForAdd.h"
#include "Operations/GitLFSUpdateStatus.h"
#include "Operations/GitLFSUpdateStaging.h"
#include "Operations/GitLFSMoveToChangelist.h"

#if GIT_ENGINE_VERSION >= 501
#include "Styling/AppStyle.h"
#else
#include "EditorStyleSet.h"
#endif

#define LOCTEXT_NAMESPACE "GitSourceControl"

void FGitLFSSourceControlModule::StartupModule()
{
	Provider = MakeShared<FGitLFSSourceControlProvider>();

	// Register our operations (implemented in GitSourceControlOperations.cpp by subclassing from Engine\Source\Developer\SourceControl\Public\SourceControlOperations.h)
	Provider->RegisterWorker<FGitLFSConnectWorker>();

	// Note:->this provider uses the "CheckOut" command only with Git LFS 2 "lock" command, since Git itself has no lock command (all tracked files in the working copy are always already checked-out).
	Provider->RegisterWorker<FGitLFSCheckOutWorker>();

	Provider->RegisterWorker<FGitLFSUpdateStatusWorker>();
	Provider->RegisterWorker<FGitLFSMarkForAddWorker>();
	Provider->RegisterWorker<FGitLFSDeleteWorker>();
	Provider->RegisterWorker<FGitLFSRevertWorker>();
	Provider->RegisterWorker<FGitLFSSyncWorker>();
	Provider->RegisterWorker<FGitLFSFetchWorker>();
	Provider->RegisterWorker<FGitLFSCheckInWorker>();
	Provider->RegisterWorker<FGitLFSCopyWorker>();
	Provider->RegisterWorker<FGitLFSResolveWorker>();
	Provider->RegisterWorker<FGitLFSMoveToChangelistWorker>();
	Provider->RegisterWorker<FGitLFSUpdateStagingWorker>();

	// load our settings
	GitSourceControlSettings.LoadSettings();

	// Bind our revision control provider to the editor
	IModularFeatures::Get().RegisterModularFeature("SourceControl", &*Provider);

	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

#if ENGINE_MAJOR_VERSION >= 5
	// Register ContentBrowserDelegate Handles for UE5 EA
	// At the time of writing this UE5 is in Early Access and has no support for revision control yet. So instead we hook into the content browser..
	// .. and force a state update on the next tick for revision control. Usually the contentbrowser assets will request this themselves, but that's not working
	// Values here are 1 or 2 based on whether the change can be done immediately or needs to be delayed as unreal needs to work through its internal delegates first
	// >> Technically you wouldn't need to use `GetOnAssetSelectionChanged` -- but it's there as a safety mechanism. States aren't forceupdated for the first path that loads
	// >> Making sure we force an update on selection change that acts like a just in case other measures fail
	OnFilterChangedHandle = ContentBrowserModule.GetOnFilterChanged().AddLambda( [this](const FARFilter&, bool)
	{
		Provider->TicksUntilNextForcedUpdate = 2;
	} );
	OnSearchBoxChangedHandle = ContentBrowserModule.GetOnSearchBoxChanged().AddLambda( [this](const FText&, bool)
	{
		Provider->TicksUntilNextForcedUpdate = 1;
	} );
	OnAssetSelectionChangedHandle = ContentBrowserModule.GetOnAssetSelectionChanged().AddLambda( [this](const TArray<FAssetData>&, bool)
	{
		Provider->TicksUntilNextForcedUpdate = 1;
	} );
	OnAssetPathChangedHandle = ContentBrowserModule.GetOnAssetPathChanged().AddLambda( [this](const FString&)
	{
		Provider->TicksUntilNextForcedUpdate = 2;
	} );
#endif

	TArray<FContentBrowserMenuExtender_SelectedAssets>& CBAssetMenuExtenderDelegates = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();
	CBAssetMenuExtenderDelegates.Add(FContentBrowserMenuExtender_SelectedAssets::CreateRaw( this, &FGitLFSSourceControlModule::OnExtendContentBrowserAssetSelectionMenu ));
	OnExtendAssetSelectionMenuHandle = CBAssetMenuExtenderDelegates.Last().GetHandle();
}

void FGitLFSSourceControlModule::ShutdownModule()
{
	// shut down the provider, as this module is going away
	Provider->Close();

	// unbind provider from editor
	IModularFeatures::Get().UnregisterModularFeature("SourceControl", &*Provider);

	Provider = nullptr;

	// Unregister ContentBrowserDelegate Handles
    FContentBrowserModule & ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
#if ENGINE_MAJOR_VERSION >= 5
	ContentBrowserModule.GetOnFilterChanged().Remove(OnFilterChangedHandle);
	ContentBrowserModule.GetOnSearchBoxChanged().Remove(OnSearchBoxChangedHandle);
	ContentBrowserModule.GetOnAssetSelectionChanged().Remove(OnAssetSelectionChangedHandle);
	ContentBrowserModule.GetOnAssetPathChanged().Remove(OnAssetPathChangedHandle);
#endif
	
	TArray<FContentBrowserMenuExtender_SelectedAssets>& CBAssetMenuExtenderDelegates = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();
	CBAssetMenuExtenderDelegates.RemoveAll([this](const FContentBrowserMenuExtender_SelectedAssets& Delegate)
	{
		return Delegate.GetHandle() == OnExtendAssetSelectionMenuHandle;
	});
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void FGitLFSSourceControlModule::SaveSettings() const
{
	if (FApp::IsUnattended() ||
		IsRunningCommandlet())
	{
		return;
	}

	GitSourceControlSettings.SaveSettings();
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

TSharedRef<FExtender> FGitLFSSourceControlModule::OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets)
{
	TSharedRef<FExtender> Extender(new FExtender());
	
	Extender->AddMenuExtension(
		"AssetSourceControlActions",
		EExtensionHook::After,
		nullptr,
		FMenuExtensionDelegate::CreateRaw(this, &FGitLFSSourceControlModule::CreateGitContentBrowserAssetMenu, SelectedAssets)
	);

	return Extender;
}

void FGitLFSSourceControlModule::CreateGitContentBrowserAssetMenu(FMenuBuilder& MenuBuilder, const TArray<FAssetData> SelectedAssets)
{
	const TSharedPtr<FGitLFSSourceControlProvider> GitProvider = Get().GetProvider();
	if (!GitProvider)
	{
		return;
	}

	if (!GitProvider->GetStatusBranchNames().Num())
	{
		return;
	}
	
	const TArray<FString>& StatusBranchNames = GitProvider->GetStatusBranchNames();
	const FString& BranchName = StatusBranchNames[0];
	MenuBuilder.AddMenuEntry(
		FText::Format(LOCTEXT("StatusBranchDiff", "Diff against status branch"), FText::FromString(BranchName)),
		FText::Format(LOCTEXT("StatusBranchDiffDesc", "Compare this asset to the latest status branch version"), FText::FromString(BranchName)),
		FSlateIcon(FGitLFSSourceControlUtils::GetAppStyleName(), "SourceControl.Actions.Diff"),
		FUIAction(FExecuteAction::CreateRaw(this, &FGitLFSSourceControlModule::DiffAssetAgainstGitOriginBranch, SelectedAssets, BranchName))
	);
}

void FGitLFSSourceControlModule::DiffAssetAgainstGitOriginBranch(const TArray<FAssetData> SelectedAssets, FString BranchName) const
{
	for (const FAssetData& Asset : SelectedAssets)
	{
		UObject* CurrentObject = Asset.GetAsset();
		if (!CurrentObject)
		{
			continue;
		}

		const FString PackagePath = Asset.PackageName.ToString();
		const FString PackageName = Asset.AssetName.ToString();
		DiffAgainstOriginBranch(CurrentObject, PackagePath, PackageName, BranchName);
	}
}

void FGitLFSSourceControlModule::DiffAgainstOriginBranch(UObject* InObject, const FString& InPackagePath, const FString& InPackageName, const FString& BranchName)
{
	check(InObject);

	const FGitLFSSourceControlModule& Module = Get();
	const TSharedPtr<FGitLFSSourceControlProvider> Provider = Module.GetProvider();
	if (!ensure(Provider))
	{
		return;
	}

	// Get the SCC state
	const FSourceControlStatePtr SourceControlState = ISourceControlModule::Get().GetProvider().GetState(SourceControlHelpers::PackageFilename(InPackagePath), EStateCacheUsage::Use);

	// If we have an asset and its in SCC..
	if (!SourceControlState.IsValid() ||
		!InObject ||
		!SourceControlState->IsSourceControlled())
	{
		return;
	}

	// Get the file name of package
	FString RelativeFileName;
#if GIT_ENGINE_VERSION >= 500
	if (!FPackageName::DoesPackageExist(InPackagePath, &RelativeFileName))
#else
	if (!FPackageName::DoesPackageExist(InPackagePath, nullptr, &RelativeFileName))
#endif
	{
		return;
	}

	const FGitLFSCommandHelpers Helpers(*Provider);
	TArray<FString> Errors;
	const TSharedPtr<ISourceControlRevision> Revision = GetOriginRevisionOnBranch(Helpers, RelativeFileName, Errors);
	if (!ensure(Revision.IsValid()))
	{
		return;
	}

	FString TempFileName;
	if (!Revision->Get(TempFileName))
	{
		return;
	}

	// Try and load that package
	UPackage* TempPackage = LoadPackage(nullptr, *TempFileName, LOAD_ForDiff | LOAD_DisableCompileOnLoad);
	if (!TempPackage)
	{
		return;
	}

	// Grab the old asset from that old package
	UObject* OldObject = FindObject<UObject>(TempPackage, *InPackageName);
	if (!OldObject)
	{
		return;
	}

	/* Set the revision information*/
	FRevisionInfo OldRevision;
	OldRevision.Changelist = Revision->GetCheckInIdentifier();
	OldRevision.Date = Revision->GetDate();
	OldRevision.Revision = Revision->GetRevision();

	FRevisionInfo NewRevision;
	NewRevision.Revision = TEXT("");

	const FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");
	AssetToolsModule.Get().DiffAssets(OldObject, InObject, OldRevision, NewRevision);
}

TSharedPtr<ISourceControlRevision> FGitLFSSourceControlModule::GetOriginRevisionOnBranch(
	const FGitLFSCommandHelpers& Helpers,
	const FString& RelativeFileName,
	TArray<FString>& OutErrorMessages)
{
	TArray<TSharedRef<FGitLFSSourceControlRevision>> History;

	TArray<FString> Output;
	if (Helpers.RunShow(Output, OutErrorMessages))
	{
		FGitLFSSourceControlUtils::ParseLogResults(Output, History);
	}

	if (History.Num() == 0)
	{
		return nullptr;
	}

	FString AbsoluteFileName = FPaths::ConvertRelativePathToFull(RelativeFileName);
	AbsoluteFileName.RemoveFromStart(Helpers.GetRepositoryRoot());

	if (AbsoluteFileName[0] == '/')
	{
		AbsoluteFileName.RemoveAt(0);
	}

	History[0]->Filename = AbsoluteFileName;

	return History[0];
}

IMPLEMENT_MODULE(FGitLFSSourceControlModule, GitLFSSourceControl);

#undef LOCTEXT_NAMESPACE
