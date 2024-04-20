// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#include "GitLFSSourceControlMenu.h"

#include "GitLFSSourceControlModule.h"
#include "GitLFSSourceControlProvider.h"
#include "GitLFSSourceControlOperations.h"
#include "GitLFSSourceControlUtils.h"

#include "ISourceControlModule.h"
#include "ISourceControlOperation.h"
#include "SourceControlOperations.h"

#include "LevelEditor.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Misc/MessageDialog.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
#include "Styling/AppStyle.h"
#else
#include "EditorStyleSet.h"
#endif

#include "PackageTools.h"
#include "FileHelpers.h"

#include "Logging/MessageLog.h"
#include "SourceControlHelpers.h"
#include "SourceControlWindows.h"

#if ENGINE_MAJOR_VERSION == 5
#include "ToolMenus.h"
#include "ToolMenuContext.h"
#include "ToolMenuMisc.h"
#endif

#include "UObject/Linker.h"

static const FName GitSourceControlMenuTabName(TEXT("GitSourceControlMenu"));

#define LOCTEXT_NAMESPACE "GitSourceControl"

TWeakPtr<SNotificationItem> FGitLFSSourceControlMenu::OperationInProgressNotification;

void FGitLFSSourceControlMenu::Register()
{
#if ENGINE_MAJOR_VERSION >= 5
	FToolMenuOwnerScoped SourceControlMenuOwner("GitSourceControlMenu");
	if (UToolMenus* ToolMenus = UToolMenus::Get())
	{
		UToolMenu* SourceControlMenu = ToolMenus->ExtendMenu("StatusBar.ToolBar.SourceControl");
		FToolMenuSection& Section = SourceControlMenu->AddSection("GitSourceControlActions", LOCTEXT("GitSourceControlMenuHeadingActions", "Git"), FToolMenuInsert(NAME_None, EToolMenuInsertType::First));

		AddMenuExtension(Section);
	}
#else
	// Register the extension with the level editor
	FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor"));
	if (LevelEditorModule)
	{
		FLevelEditorModule::FLevelEditorMenuExtender ViewMenuExtender = FLevelEditorModule::FLevelEditorMenuExtender::CreateRaw(this, &FGitLFSSourceControlMenu::OnExtendLevelEditorViewMenu);
		auto& MenuExtenders = LevelEditorModule->GetAllLevelEditorToolbarSourceControlMenuExtenders();
		MenuExtenders.Add(ViewMenuExtender);
		ViewMenuExtenderHandle = MenuExtenders.Last().GetHandle();
	}
#endif
}

void FGitLFSSourceControlMenu::Unregister()
{
#if ENGINE_MAJOR_VERSION >= 5
	if (UToolMenus* ToolMenus = UToolMenus::Get())
	{
		UToolMenus::Get()->UnregisterOwnerByName("GitSourceControlMenu");
	}
#else
	// Unregister the level editor extensions
	FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor");
	if (LevelEditorModule)
	{
		LevelEditorModule->GetAllLevelEditorToolbarSourceControlMenuExtenders().RemoveAll([=](const FLevelEditorModule::FLevelEditorMenuExtender& Extender) { return Extender.GetHandle() == ViewMenuExtenderHandle; });
	}
#endif
}

bool FGitLFSSourceControlMenu::HaveRemoteUrl() const
{
	const FGitLFSSourceControlModule& GitSourceControl = FGitLFSSourceControlModule::Get();
	return !GitSourceControl.GetProvider().GetRemoteUrl().IsEmpty();
}

/// Prompt to save or discard all packages
bool FGitLFSSourceControlMenu::SaveDirtyPackages()
{
	const bool bPromptUserToSave = true;
	const bool bSaveMapPackages = true;
	const bool bSaveContentPackages = true;
	const bool bFastSave = false;
	const bool bNotifyNoPackagesSaved = false;
	const bool bCanBeDeclined = true; // If the user clicks "don't save" this will continue and lose their changes
	bool bHadPackagesToSave = false;

	bool bSaved = FEditorFileUtils::SaveDirtyPackages(bPromptUserToSave, bSaveMapPackages, bSaveContentPackages, bFastSave, bNotifyNoPackagesSaved, bCanBeDeclined, &bHadPackagesToSave);

	// bSaved can be true if the user selects to not save an asset by unchecking it and clicking "save"
	if (bSaved)
	{
		TArray<UPackage*> DirtyPackages;
		FEditorFileUtils::GetDirtyWorldPackages(DirtyPackages);
		FEditorFileUtils::GetDirtyContentPackages(DirtyPackages);
		bSaved = DirtyPackages.Num() == 0;
	}

	return bSaved;
}

// Ask the user if they want to stash any modification and try to unstash them afterward, which could lead to conflicts
bool FGitLFSSourceControlMenu::StashAwayAnyModifications()
{
	bool bStashOk = true;

	FGitLFSSourceControlModule& GitSourceControl = FGitLFSSourceControlModule::Get();
	const FGitLFSSourceControlProvider& Provider = GitSourceControl.GetProvider();
	const FString& PathToRespositoryRoot = Provider.GetPathToRepositoryRoot();
	const FString& PathToGitBinary = Provider.GetGitBinaryPath();
	const TArray<FString> ParametersStatus{"--porcelain --untracked-files=no"};
	TArray<FString> InfoMessages;
	TArray<FString> ErrorMessages;
	// Check if there is any modification to the working tree
	const bool bStatusOk = GitLFSSourceControlUtils::RunCommand(TEXT("status"), PathToGitBinary, PathToRespositoryRoot, ParametersStatus, FGitLFSSourceControlModule::GetEmptyStringArray(), InfoMessages, ErrorMessages);
	if ((bStatusOk) && (InfoMessages.Num() > 0))
	{
		// Ask the user before stashing
		const FText DialogText(LOCTEXT("SourceControlMenu_Stash_Ask", "Stash (save) all modifications of the working tree? Required to Sync/Pull!"));
		const EAppReturnType::Type Choice = FMessageDialog::Open(EAppMsgType::OkCancel, DialogText);
		if (Choice == EAppReturnType::Ok)
		{
			const TArray<FString> ParametersStash{ "save \"Stashed by Unreal Engine Git Plugin\"" };
			bStashMadeBeforeSync = GitLFSSourceControlUtils::RunCommand(TEXT("stash"), PathToGitBinary, PathToRespositoryRoot, ParametersStash, FGitLFSSourceControlModule::GetEmptyStringArray(), InfoMessages, ErrorMessages);
			if (!bStashMadeBeforeSync)
			{
				FMessageLog SourceControlLog("SourceControl");
				SourceControlLog.Warning(LOCTEXT("SourceControlMenu_StashFailed", "Stashing away modifications failed!"));
				SourceControlLog.Notify();
			}
		}
		else
		{
			bStashOk = false;
		}
	}

	return bStashOk;
}

// Unstash any modifications if a stash was made at the beginning of the Sync operation
void FGitLFSSourceControlMenu::ReApplyStashedModifications()
{
	if (bStashMadeBeforeSync)
	{
		FGitLFSSourceControlModule& GitSourceControl = FGitLFSSourceControlModule::Get();
		FGitLFSSourceControlProvider& Provider = GitSourceControl.GetProvider();
		const FString& PathToRespositoryRoot = Provider.GetPathToRepositoryRoot();
		const FString& PathToGitBinary = Provider.GetGitBinaryPath();
		const TArray<FString> ParametersStash{ "pop" };
		TArray<FString> InfoMessages;
		TArray<FString> ErrorMessages;
		const bool bUnstashOk = GitLFSSourceControlUtils::RunCommand(TEXT("stash"), PathToGitBinary, PathToRespositoryRoot, ParametersStash, FGitLFSSourceControlModule::GetEmptyStringArray(), InfoMessages, ErrorMessages);
		if (!bUnstashOk)
		{
			FMessageLog SourceControlLog("SourceControl");
			SourceControlLog.Warning(LOCTEXT("SourceControlMenu_UnstashFailed", "Unstashing previously saved modifications failed!"));
			SourceControlLog.Notify();
		}
	}
}

void FGitLFSSourceControlMenu::SyncClicked()
{
	if (!OperationInProgressNotification.IsValid())
	{
		// Ask the user to save any dirty assets opened in Editor
		const bool bSaved = SaveDirtyPackages();
		if (bSaved)
		{
			FGitLFSSourceControlModule& GitSourceControl = FGitLFSSourceControlModule::Get();
			FGitLFSSourceControlProvider& Provider = GitSourceControl.GetProvider();

			// Launch a "Sync" operation
			TSharedRef<FSync, ESPMode::ThreadSafe> SyncOperation = ISourceControlOperation::Create<FSync>();
#if ENGINE_MAJOR_VERSION >= 5
			const ECommandResult::Type Result = Provider.Execute(SyncOperation, FSourceControlChangelistPtr(), FGitLFSSourceControlModule::GetEmptyStringArray(), EConcurrency::Asynchronous,
																 FSourceControlOperationComplete::CreateRaw(this, &FGitLFSSourceControlMenu::OnSourceControlOperationComplete));
#else
			const ECommandResult::Type Result = Provider.Execute(SyncOperation, FGitLFSSourceControlModule::GetEmptyStringArray(), EConcurrency::Asynchronous,
																 FSourceControlOperationComplete::CreateRaw(this, &FGitLFSSourceControlMenu::OnSourceControlOperationComplete));
#endif
			if (Result == ECommandResult::Succeeded)
			{
				// Display an ongoing notification during the whole operation (packages will be reloaded at the completion of the operation)
				DisplayInProgressNotification(SyncOperation->GetInProgressString());
			}
			else
			{
				// Report failure with a notification and Reload all packages
				DisplayFailureNotification(SyncOperation->GetName());
			}
		}
		else
		{
			FMessageLog SourceControlLog("SourceControl");
			SourceControlLog.Warning(LOCTEXT("SourceControlMenu_Sync_Unsaved", "Save All Assets before attempting to Sync!"));
			SourceControlLog.Notify();
		}
	}
	else
	{
		FMessageLog SourceControlLog("SourceControl");
		SourceControlLog.Warning(LOCTEXT("SourceControlMenu_InProgress", "Revision control operation already in progress"));
		SourceControlLog.Notify();
	}
}

void FGitLFSSourceControlMenu::CommitClicked()
{
	if (OperationInProgressNotification.IsValid())
	{
		FMessageLog SourceControlLog("SourceControl");
		SourceControlLog.Warning(LOCTEXT("SourceControlMenu_InProgress", "Revision control operation already in progress"));
		SourceControlLog.Notify();
		return;
	}
	
	FLevelEditorModule & LevelEditorModule = FModuleManager::Get().LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	FSourceControlWindows::ChoosePackagesToCheckIn(nullptr);
}

void FGitLFSSourceControlMenu::PushClicked()
{
	if (!OperationInProgressNotification.IsValid())
	{
		// Launch a "Push" Operation
		FGitLFSSourceControlModule& GitSourceControl = FGitLFSSourceControlModule::Get();
		FGitLFSSourceControlProvider& Provider = GitSourceControl.GetProvider();
		TSharedRef<FCheckIn, ESPMode::ThreadSafe> PushOperation = ISourceControlOperation::Create<FCheckIn>();
#if ENGINE_MAJOR_VERSION >= 5
		const ECommandResult::Type Result = Provider.Execute(PushOperation, FSourceControlChangelistPtr(), FGitLFSSourceControlModule::GetEmptyStringArray(), EConcurrency::Asynchronous, FSourceControlOperationComplete::CreateRaw(this, &FGitLFSSourceControlMenu::OnSourceControlOperationComplete));
#else
		const ECommandResult::Type Result = Provider.Execute(PushOperation, FGitLFSSourceControlModule::GetEmptyStringArray(), EConcurrency::Asynchronous, FSourceControlOperationComplete::CreateRaw(this, &FGitLFSSourceControlMenu::OnSourceControlOperationComplete));
#endif
		if (Result == ECommandResult::Succeeded)
		{
			// Display an ongoing notification during the whole operation
			DisplayInProgressNotification(PushOperation->GetInProgressString());
		}
		else
		{
			// Report failure with a notification
			DisplayFailureNotification(PushOperation->GetName());
		}
	}
	else
	{
		FMessageLog SourceControlLog("SourceControl");
		SourceControlLog.Warning(LOCTEXT("SourceControlMenu_InProgress", "Revision control operation already in progress"));
		SourceControlLog.Notify();
	}
}

void FGitLFSSourceControlMenu::RevertClicked()
{
	if (OperationInProgressNotification.IsValid())
	{
		FMessageLog SourceControlLog("SourceControl");
		SourceControlLog.Warning(LOCTEXT("SourceControlMenu_InProgress", "Revision control operation already in progress"));
		SourceControlLog.Notify();
		return;
	}

	// Ask the user before reverting all!
	const FText DialogText(LOCTEXT("SourceControlMenu_Revert_Ask", "Revert all modifications of the working tree?"));
	const EAppReturnType::Type Choice = FMessageDialog::Open(EAppMsgType::OkCancel, DialogText);
	if (Choice != EAppReturnType::Ok)
	{
		return;
	}

	// make sure we update the SCC status of all packages (this could take a long time, so we will run it as a background task)
	const TArray<FString> Filenames {
		FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()),
		FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir()),
		FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath())
	};

	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	FSourceControlOperationRef Operation = ISourceControlOperation::Create<FUpdateStatus>();
#if ENGINE_MAJOR_VERSION >= 5
	SourceControlProvider.Execute(Operation, FSourceControlChangelistPtr(), Filenames, EConcurrency::Asynchronous, FSourceControlOperationComplete::CreateStatic(&FGitLFSSourceControlMenu::RevertAllCallback));
#else
	SourceControlProvider.Execute(Operation, Filenames, EConcurrency::Asynchronous, FSourceControlOperationComplete::CreateStatic(&FGitLFSSourceControlMenu::RevertAllCallback));
#endif

	FNotificationInfo Info(LOCTEXT("SourceControlMenuRevertAll", "Checking for assets to revert..."));
	Info.bFireAndForget = false;
	Info.ExpireDuration = 0.0f;
	Info.FadeOutDuration = 1.0f;

	if (SourceControlProvider.CanCancelOperation(Operation))
	{
		Info.ButtonDetails.Add(FNotificationButtonInfo(
			LOCTEXT("SourceControlMenuRevertAll_CancelButton", "Cancel"),
			LOCTEXT("SourceControlMenuRevertAll_CancelButtonTooltip", "Cancel the revert operation."),
			FSimpleDelegate::CreateStatic(&FGitLFSSourceControlMenu::RevertAllCancelled, Operation)
		));
	}

	OperationInProgressNotification = FSlateNotificationManager::Get().AddNotification(Info);
	if (OperationInProgressNotification.IsValid())
	{
		OperationInProgressNotification.Pin()->SetCompletionState(SNotificationItem::CS_Pending);
	}
}

void FGitLFSSourceControlMenu::RevertAllCallback(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult)
{
	if (InResult != ECommandResult::Succeeded)
	{
		return;
	}

	// Get a list of all the checked out packages
	TArray<FString> PackageNames;
	TArray<UPackage*> LoadedPackages;
	TMap<FString, FSourceControlStatePtr> PackageStates;
	FEditorFileUtils::FindAllSubmittablePackageFiles(PackageStates, true);

	for (TMap<FString, FSourceControlStatePtr>::TConstIterator PackageIter(PackageStates); PackageIter; ++PackageIter)
	{
		const FString PackageName = *PackageIter.Key();
		const FSourceControlStatePtr CurPackageSCCState = PackageIter.Value();

		UPackage* Package = FindPackage(nullptr, *PackageName);
		if (Package != nullptr)
		{
			LoadedPackages.Add(Package);

			if (!Package->IsFullyLoaded())
			{
				FlushAsyncLoading();
				Package->FullyLoad();
			}
			ResetLoaders(Package);
		}

		PackageNames.Add(PackageName);
	}

	const auto FileNames = SourceControlHelpers::PackageFilenames(PackageNames);

	// Launch a "Revert" Operation
	FGitLFSSourceControlModule& GitSourceControl = FGitLFSSourceControlModule::Get();
	FGitLFSSourceControlProvider& Provider = GitSourceControl.GetProvider();
	const TSharedRef<FRevert, ESPMode::ThreadSafe> RevertOperation = ISourceControlOperation::Create<FRevert>();
#if ENGINE_MAJOR_VERSION >= 5
	const auto Result = Provider.Execute(RevertOperation, FSourceControlChangelistPtr(), FileNames);
#else
	const auto Result = Provider.Execute(RevertOperation, FileNames);
#endif

	RemoveInProgressNotification();
	if (Result != ECommandResult::Succeeded)
	{
		DisplayFailureNotification(TEXT("Revert"));
	}
	else
	{
		DisplaySucessNotification(TEXT("Revert"));
	}

	GitLFSSourceControlUtils::ReloadPackages(LoadedPackages);
#if ENGINE_MAJOR_VERSION >= 5
	Provider.Execute(ISourceControlOperation::Create<FUpdateStatus>(), FSourceControlChangelistPtr(), FGitLFSSourceControlModule::GetEmptyStringArray(), EConcurrency::Asynchronous);
#else
	Provider.Execute(ISourceControlOperation::Create<FUpdateStatus>(), FGitLFSSourceControlModule::GetEmptyStringArray(), EConcurrency::Asynchronous);
#endif
}

void FGitLFSSourceControlMenu::RefreshClicked()
{
	if (!OperationInProgressNotification.IsValid())
	{
		FGitLFSSourceControlModule& GitSourceControl = FGitLFSSourceControlModule::Get();
		FGitLFSSourceControlProvider& Provider = GitSourceControl.GetProvider();
		// Launch an "GitFetch" Operation
		TSharedRef<FGitLFSFetch, ESPMode::ThreadSafe> RefreshOperation = ISourceControlOperation::Create<FGitLFSFetch>();
		RefreshOperation->bUpdateStatus = true;
#if ENGINE_MAJOR_VERSION >= 5
		const ECommandResult::Type Result = Provider.Execute(RefreshOperation, FSourceControlChangelistPtr(), FGitLFSSourceControlModule::GetEmptyStringArray(), EConcurrency::Asynchronous,
															 FSourceControlOperationComplete::CreateRaw(this, &FGitLFSSourceControlMenu::OnSourceControlOperationComplete));
#else
		const ECommandResult::Type Result = Provider.Execute(RefreshOperation, FGitLFSSourceControlModule::GetEmptyStringArray(), EConcurrency::Asynchronous,
															 FSourceControlOperationComplete::CreateRaw(this, &FGitLFSSourceControlMenu::OnSourceControlOperationComplete));
#endif
		if (Result == ECommandResult::Succeeded)
		{
			// Display an ongoing notification during the whole operation
			DisplayInProgressNotification(RefreshOperation->GetInProgressString());
		}
		else
		{
			// Report failure with a notification
			DisplayFailureNotification(RefreshOperation->GetName());
		}
	}
	else
	{
		FMessageLog SourceControlLog("SourceControl");
		SourceControlLog.Warning(LOCTEXT("SourceControlMenu_InProgress", "Revision control operation already in progress"));
		SourceControlLog.Notify();
	}
}

// Display an ongoing notification during the whole operation
void FGitLFSSourceControlMenu::DisplayInProgressNotification(const FText& InOperationInProgressString)
{
	if (!OperationInProgressNotification.IsValid())
	{
		FNotificationInfo Info(InOperationInProgressString);
		Info.bFireAndForget = false;
		Info.ExpireDuration = 0.0f;
		Info.FadeOutDuration = 1.0f;
		OperationInProgressNotification = FSlateNotificationManager::Get().AddNotification(Info);
		if (OperationInProgressNotification.IsValid())
		{
			OperationInProgressNotification.Pin()->SetCompletionState(SNotificationItem::CS_Pending);
		}
	}
}

void FGitLFSSourceControlMenu::RevertAllCancelled(FSourceControlOperationRef InOperation)
{
	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	SourceControlProvider.CancelOperation(InOperation);

	if (OperationInProgressNotification.IsValid())
	{
		OperationInProgressNotification.Pin()->ExpireAndFadeout();
	}

	OperationInProgressNotification.Reset();
}

// Remove the ongoing notification at the end of the operation
void FGitLFSSourceControlMenu::RemoveInProgressNotification()
{
	if (OperationInProgressNotification.IsValid())
	{
		OperationInProgressNotification.Pin()->ExpireAndFadeout();
		OperationInProgressNotification.Reset();
	}
}

// Display a temporary success notification at the end of the operation
void FGitLFSSourceControlMenu::DisplaySucessNotification(const FName& InOperationName)
{
	const FText NotificationText = FText::Format(
		LOCTEXT("SourceControlMenu_Success", "{0} operation was successful!"),
		FText::FromName(InOperationName)
	);
	FNotificationInfo Info(NotificationText);
	Info.bUseSuccessFailIcons = true;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
	Info.Image = FAppStyle::GetBrush(TEXT("NotificationList.SuccessImage"));
#else
	Info.Image = FEditorStyle::GetBrush(TEXT("NotificationList.SuccessImage"));
#endif
	
	FSlateNotificationManager::Get().AddNotification(Info);
#if UE_BUILD_DEBUG
	UE_LOG(LogSourceControl, Log, TEXT("%s"), *NotificationText.ToString());
#endif
}

// Display a temporary failure notification at the end of the operation
void FGitLFSSourceControlMenu::DisplayFailureNotification(const FName& InOperationName)
{
	const FText NotificationText = FText::Format(
		LOCTEXT("SourceControlMenu_Failure", "Error: {0} operation failed!"),
		FText::FromName(InOperationName)
	);
	FNotificationInfo Info(NotificationText);
	Info.ExpireDuration = 8.0f;
	FSlateNotificationManager::Get().AddNotification(Info);
	UE_LOG(LogSourceControl, Error, TEXT("%s"), *NotificationText.ToString());
}

void FGitLFSSourceControlMenu::OnSourceControlOperationComplete(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult)
{
	RemoveInProgressNotification();

	if ((InOperation->GetName() == "Sync") || (InOperation->GetName() == "Revert"))
	{
		// Unstash any modifications if a stash was made at the beginning of the Sync operation
		ReApplyStashedModifications();
		// Reload packages that where unlinked at the beginning of the Sync/Revert operation
		GitLFSSourceControlUtils::ReloadPackages(PackagesToReload);
	}

	// Report result with a notification
	if (InResult == ECommandResult::Succeeded)
	{
		DisplaySucessNotification(InOperation->GetName());
	}
	else
	{
		DisplayFailureNotification(InOperation->GetName());
	}
}

#if ENGINE_MAJOR_VERSION >= 5
void FGitLFSSourceControlMenu::AddMenuExtension(FToolMenuSection& Builder)
#else
void FGitLFSSourceControlMenu::AddMenuExtension(FMenuBuilder& Builder)
#endif
{
	Builder.AddMenuEntry(
#if ENGINE_MAJOR_VERSION >= 5
		"GitPush",
#endif
		LOCTEXT("GitPush",				"Push pending local commits"),
		LOCTEXT("GitPushTooltip",		"Push all pending local commits to the remote server."),
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "SourceControl.Actions.Submit"),
#else
		FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.Submit"),
#endif
		FUIAction(
			FExecuteAction::CreateRaw(this, &FGitLFSSourceControlMenu::PushClicked),
			FCanExecuteAction::CreateRaw(this, &FGitLFSSourceControlMenu::HaveRemoteUrl)
		)
	);

	Builder.AddMenuEntry(
#if ENGINE_MAJOR_VERSION >= 5
		"GitSync",
#endif
		LOCTEXT("GitSync",				"Pull"),
		LOCTEXT("GitSyncTooltip",		"Update all files in the local repository to the latest version of the remote server."),
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "SourceControl.Actions.Sync"),
#else
		FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.Sync"),
#endif
		FUIAction(
			FExecuteAction::CreateRaw(this, &FGitLFSSourceControlMenu::SyncClicked),
			FCanExecuteAction::CreateRaw(this, &FGitLFSSourceControlMenu::HaveRemoteUrl)
		)
	);

	Builder.AddMenuEntry(
#if ENGINE_MAJOR_VERSION >= 5
		"GitRevert",
#endif
		LOCTEXT("GitRevert",			"Revert"),
		LOCTEXT("GitRevertTooltip",		"Revert all files in the repository to their unchanged state."),
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "SourceControl.Actions.Revert"),
#else
		FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.Revert"),
#endif
		FUIAction(
			FExecuteAction::CreateRaw(this, &FGitLFSSourceControlMenu::RevertClicked),
			FCanExecuteAction()
		)
	);

	Builder.AddMenuEntry(
#if ENGINE_MAJOR_VERSION >= 5
		"GitRefresh",
#endif
		LOCTEXT("GitRefresh",			"Refresh"),
		LOCTEXT("GitRefreshTooltip",	"Update the revision control status of all files in the local repository."),
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "SourceControl.Actions.Refresh"),
#else
		FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.Refresh"),
#endif
		FUIAction(
			FExecuteAction::CreateRaw(this, &FGitLFSSourceControlMenu::RefreshClicked),
			FCanExecuteAction()
		)
	);
}

#if ENGINE_MAJOR_VERSION < 5
TSharedRef<FExtender> FGitLFSSourceControlMenu::OnExtendLevelEditorViewMenu(const TSharedRef<FUICommandList> CommandList)
{
	TSharedRef<FExtender> Extender(new FExtender());

	Extender->AddMenuExtension(
		"SourceControlActions",
		EExtensionHook::After,
		nullptr,
		FMenuExtensionDelegate::CreateRaw(this, &FGitLFSSourceControlMenu::AddMenuExtension));

	return Extender;
}
#endif

#undef LOCTEXT_NAMESPACE
