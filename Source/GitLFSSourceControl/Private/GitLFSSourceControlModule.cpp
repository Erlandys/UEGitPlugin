// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#include "GitLFSSourceControlModule.h"

#include "AssetToolsModule.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
#include "Styling/AppStyle.h"
#else
#include "EditorStyleSet.h"
#endif
#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "Features/IModularFeatures.h"

#include "ContentBrowserModule.h"
#include "ContentBrowserDelegates.h"

#include "GitLFSSourceControlOperations.h"
#include "GitLFSSourceControlUtils.h"
#include "ISourceControlModule.h"
#include "SourceControlHelpers.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "GitSourceControl"

TArray<FString> FGitLFSSourceControlModule::EmptyStringArray;

template<typename Type>
static TSharedRef<IGitLFSSourceControlWorker, ESPMode::ThreadSafe> CreateWorker()
{
	return MakeShareable( new Type() );
}

void FGitLFSSourceControlModule::StartupModule()
{
	// Register our operations (implemented in GitSourceControlOperations.cpp by subclassing from Engine\Source\Developer\SourceControl\Public\SourceControlOperations.h)
	GitSourceControlProvider.RegisterWorker( "Connect", FGetGitSourceControlWorker::CreateStatic( &CreateWorker<FGitLFSConnectWorker> ) );
	// Note: this provider uses the "CheckOut" command only with Git LFS 2 "lock" command, since Git itself has no lock command (all tracked files in the working copy are always already checked-out).
	GitSourceControlProvider.RegisterWorker( "CheckOut", FGetGitSourceControlWorker::CreateStatic( &CreateWorker<FGitLFSCheckOutWorker> ) );
	GitSourceControlProvider.RegisterWorker( "UpdateStatus", FGetGitSourceControlWorker::CreateStatic( &CreateWorker<FGitLFSUpdateStatusWorker> ) );
	GitSourceControlProvider.RegisterWorker( "MarkForAdd", FGetGitSourceControlWorker::CreateStatic( &CreateWorker<FGitLFSMarkForAddWorker> ) );
	GitSourceControlProvider.RegisterWorker( "Delete", FGetGitSourceControlWorker::CreateStatic( &CreateWorker<FGitLFSDeleteWorker> ) );
	GitSourceControlProvider.RegisterWorker( "Revert", FGetGitSourceControlWorker::CreateStatic( &CreateWorker<FGitLFSRevertWorker> ) );
	GitSourceControlProvider.RegisterWorker( "Sync", FGetGitSourceControlWorker::CreateStatic( &CreateWorker<FGitLFSSyncWorker> ) );
	GitSourceControlProvider.RegisterWorker( "Fetch", FGetGitSourceControlWorker::CreateStatic( &CreateWorker<FGitLFSFetchWorker> ) );
	GitSourceControlProvider.RegisterWorker( "CheckIn", FGetGitSourceControlWorker::CreateStatic( &CreateWorker<FGitLFSCheckInWorker> ) );
	GitSourceControlProvider.RegisterWorker( "Copy", FGetGitSourceControlWorker::CreateStatic( &CreateWorker<FGitLFSCopyWorker> ) );
	GitSourceControlProvider.RegisterWorker( "Resolve", FGetGitSourceControlWorker::CreateStatic( &CreateWorker<FGitLFSResolveWorker> ) );
	GitSourceControlProvider.RegisterWorker( "MoveToChangelist", FGetGitSourceControlWorker::CreateStatic( &CreateWorker<FGitLFSMoveToChangelistWorker> ) );
	GitSourceControlProvider.RegisterWorker( "UpdateChangelistsStatus", FGetGitSourceControlWorker::CreateStatic( &CreateWorker<FGitLFSUpdateStagingWorker> ) );

	// load our settings
	GitSourceControlSettings.LoadSettings();

	// Bind our revision control provider to the editor
	IModularFeatures::Get().RegisterModularFeature( "SourceControl", &GitSourceControlProvider );

	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

#if ENGINE_MAJOR_VERSION >= 5
	// Register ContentBrowserDelegate Handles for UE5 EA
	// At the time of writing this UE5 is in Early Access and has no support for revision control yet. So instead we hook into the content browser..
	// .. and force a state update on the next tick for revision control. Usually the contentbrowser assets will request this themselves, but that's not working
	// Values here are 1 or 2 based on whether the change can be done immediately or needs to be delayed as unreal needs to work through its internal delegates first
	// >> Technically you wouldn't need to use `GetOnAssetSelectionChanged` -- but it's there as a safety mechanism. States aren't forceupdated for the first path that loads
	// >> Making sure we force an update on selection change that acts like a just in case other measures fail
	CbdHandle_OnFilterChanged = ContentBrowserModule.GetOnFilterChanged().AddLambda( [this]( const FARFilter&, bool ) { GitSourceControlProvider.TicksUntilNextForcedUpdate = 2; } );
	CbdHandle_OnSearchBoxChanged = ContentBrowserModule.GetOnSearchBoxChanged().AddLambda( [this]( const FText&, bool ){ GitSourceControlProvider.TicksUntilNextForcedUpdate = 1; } );
	CbdHandle_OnAssetSelectionChanged = ContentBrowserModule.GetOnAssetSelectionChanged().AddLambda( [this]( const TArray<FAssetData>&, bool ) { GitSourceControlProvider.TicksUntilNextForcedUpdate = 1; } );
	CbdHandle_OnAssetPathChanged = ContentBrowserModule.GetOnAssetPathChanged().AddLambda( [this]( const FString& ) { GitSourceControlProvider.TicksUntilNextForcedUpdate = 2; } );
#endif

	TArray<FContentBrowserMenuExtender_SelectedAssets>& CBAssetMenuExtenderDelegates = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();
	CBAssetMenuExtenderDelegates.Add(FContentBrowserMenuExtender_SelectedAssets::CreateRaw( this, &FGitLFSSourceControlModule::OnExtendContentBrowserAssetSelectionMenu ));
	CbdHandle_OnExtendAssetSelectionMenu = CBAssetMenuExtenderDelegates.Last().GetHandle();
}

void FGitLFSSourceControlModule::ShutdownModule()
{
	// shut down the provider, as this module is going away
	GitSourceControlProvider.Close();

	// unbind provider from editor
	IModularFeatures::Get().UnregisterModularFeature("SourceControl", &GitSourceControlProvider);


	// Unregister ContentBrowserDelegate Handles
    FContentBrowserModule & ContentBrowserModule = FModuleManager::Get().LoadModuleChecked< FContentBrowserModule >( "ContentBrowser" );
#if ENGINE_MAJOR_VERSION >= 5
	ContentBrowserModule.GetOnFilterChanged().Remove( CbdHandle_OnFilterChanged );
	ContentBrowserModule.GetOnSearchBoxChanged().Remove( CbdHandle_OnSearchBoxChanged );
	ContentBrowserModule.GetOnAssetSelectionChanged().Remove( CbdHandle_OnAssetSelectionChanged );
	ContentBrowserModule.GetOnAssetPathChanged().Remove( CbdHandle_OnAssetPathChanged );
#endif
	
	TArray<FContentBrowserMenuExtender_SelectedAssets>& CBAssetMenuExtenderDelegates = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();
	CBAssetMenuExtenderDelegates.RemoveAll([ &ExtenderDelegateHandle = CbdHandle_OnExtendAssetSelectionMenu ]( const FContentBrowserMenuExtender_SelectedAssets& Delegate ) {
		return Delegate.GetHandle() == ExtenderDelegateHandle;
	});
}

void FGitLFSSourceControlModule::SaveSettings()
{
	if (FApp::IsUnattended() || IsRunningCommandlet())
	{
		return;
	}

	GitSourceControlSettings.SaveSettings();
}

void FGitLFSSourceControlModule::SetLastErrors(const TArray<FText>& InErrors)
{
	FGitLFSSourceControlModule* Module = FModuleManager::GetModulePtr<FGitLFSSourceControlModule>("GitLFSSourceControl");
	if (Module)
	{
		Module->GetProvider().SetLastErrors(InErrors);
	}
}

TSharedRef<FExtender> FGitLFSSourceControlModule::OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets)
{
	TSharedRef<FExtender> Extender(new FExtender());
	
	Extender->AddMenuExtension(
		"AssetSourceControlActions",
		EExtensionHook::After,
		nullptr,
		FMenuExtensionDelegate::CreateRaw( this, &FGitLFSSourceControlModule::CreateGitContentBrowserAssetMenu, SelectedAssets )
	);

	return Extender;
}

void FGitLFSSourceControlModule::CreateGitContentBrowserAssetMenu(FMenuBuilder& MenuBuilder, const TArray<FAssetData> SelectedAssets)
{
	if (!FGitLFSSourceControlModule::Get().GetProvider().GetStatusBranchNames().Num())
	{
		return;
	}
	
	const TArray<FString>& StatusBranchNames = FGitLFSSourceControlModule::Get().GetProvider().GetStatusBranchNames();
	const FString& BranchName = StatusBranchNames[0];
	MenuBuilder.AddMenuEntry(
		FText::Format(LOCTEXT("StatusBranchDiff", "Diff against status branch"), FText::FromString(BranchName)),
		FText::Format(LOCTEXT("StatusBranchDiffDesc", "Compare this asset to the latest status branch version"), FText::FromString(BranchName)),
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "SourceControl.Actions.Diff"),
#else
		FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.Diff"),
#endif
		FUIAction(FExecuteAction::CreateRaw( this, &FGitLFSSourceControlModule::DiffAssetAgainstGitOriginBranch, SelectedAssets, BranchName ))
	);
}

void FGitLFSSourceControlModule::DiffAssetAgainstGitOriginBranch(const TArray<FAssetData> SelectedAssets, FString BranchName) const
{
	for (int32 AssetIdx = 0; AssetIdx < SelectedAssets.Num(); AssetIdx++)
	{
		// Get the actual asset (will load it)
		const FAssetData& AssetData = SelectedAssets[AssetIdx];

		if (UObject* CurrentObject = AssetData.GetAsset())
		{
			const FString PackagePath = AssetData.PackageName.ToString();
			const FString PackageName = AssetData.AssetName.ToString();
			DiffAgainstOriginBranch(CurrentObject, PackagePath, PackageName, BranchName);
		}
	}
}

void FGitLFSSourceControlModule::DiffAgainstOriginBranch( UObject * InObject, const FString & InPackagePath, const FString & InPackageName, const FString & BranchName ) const
{
	check(InObject);

	const FGitLFSSourceControlModule& GitSourceControl = FModuleManager::GetModuleChecked<FGitLFSSourceControlModule>("GitLFSSourceControl");
	const FString& PathToGitBinary = GitSourceControl.AccessSettings().GetBinaryPath();
	const FString& PathToRepositoryRoot = GitSourceControl.GetProvider().GetPathToRepositoryRoot();

	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();

	const FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");

	// Get the SCC state
	const FSourceControlStatePtr SourceControlState = SourceControlProvider.GetState(SourceControlHelpers::PackageFilename(InPackagePath), EStateCacheUsage::Use);

	// If we have an asset and its in SCC..
	if (SourceControlState.IsValid() && InObject != nullptr && SourceControlState->IsSourceControlled())
	{
		// Get the file name of package
		FString RelativeFileName;
#if ENGINE_MAJOR_VERSION >= 5
		if (FPackageName::DoesPackageExist(InPackagePath, &RelativeFileName))
#else
		if (FPackageName::DoesPackageExist(InPackagePath, nullptr, &RelativeFileName))
#endif
		{
			// if(SourceControlState->GetHistorySize() > 0)
			{
				TArray<FString> Errors;
				const auto& Revision = GitLFSSourceControlUtils::GetOriginRevisionOnBranch(PathToGitBinary, PathToRepositoryRoot, RelativeFileName, Errors, BranchName);

				check(Revision.IsValid());

				FString TempFileName;
				if (Revision->Get(TempFileName))
				{
					// Try and load that package
					UPackage* TempPackage = LoadPackage(nullptr, *TempFileName, LOAD_ForDiff | LOAD_DisableCompileOnLoad);
					if (TempPackage != nullptr)
					{
						// Grab the old asset from that old package
						UObject* OldObject = FindObject<UObject>(TempPackage, *InPackageName);
						if (OldObject != nullptr)
						{
							/* Set the revision information*/
							FRevisionInfo OldRevision;
							OldRevision.Changelist = Revision->GetCheckInIdentifier();
							OldRevision.Date = Revision->GetDate();
							OldRevision.Revision = Revision->GetRevision();

							FRevisionInfo NewRevision;
							NewRevision.Revision = TEXT("");

							AssetToolsModule.Get().DiffAssets(OldObject, InObject, OldRevision, NewRevision);
						}
					}
				}
			}
		}
	}
}

IMPLEMENT_MODULE( FGitLFSSourceControlModule, GitLFSSourceControl );

#undef LOCTEXT_NAMESPACE
