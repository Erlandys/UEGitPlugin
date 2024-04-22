// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#include "GitLFSSourceControlMenu.h"

#include "GitLFSCommand.h"
#include "Operations/GitLFSFetch.h"
#include "GitLFSSourceControlModule.h"

#include "FileHelpers.h"
#include "ISourceControlModule.h"
#include "SourceControlHelpers.h"
#include "SourceControlWindows.h"
#include "SourceControlOperations.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"

#if GIT_ENGINE_VERSION >= 501
#include "Styling/AppStyle.h"
#else
#include "EditorStyleSet.h"
#endif

#if GIT_ENGINE_VERSION >= 500
#include "ToolMenus.h"
#include "ToolMenuContext.h"
#include "ToolMenuMisc.h"
#else
#include "LevelEditor.h"
#endif

#define LOCTEXT_NAMESPACE "GitSourceControl"

TWeakPtr<SNotificationItem> FGitLFSSourceControlMenu::OperationInProgressNotification;

void FGitLFSSourceControlMenu::Register()
{
#if GIT_ENGINE_VERSION >= 500
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
		FLevelEditorModule::FLevelEditorMenuExtender ViewMenuExtender = FLevelEditorModule::FLevelEditorMenuExtender::CreateSP(this, &FGitLFSSourceControlMenu::OnExtendLevelEditorViewMenu);
		TArray<FLevelEditorModule::FLevelEditorMenuExtender>& MenuExtenders = LevelEditorModule->GetAllLevelEditorToolbarSourceControlMenuExtenders();
		MenuExtenders.Add(ViewMenuExtender);
		ViewMenuExtenderHandle = MenuExtenders.Last().GetHandle();
	}
#endif
}

void FGitLFSSourceControlMenu::Unregister()
{
#if GIT_ENGINE_VERSION >= 500
	if (UToolMenus* ToolMenus = UToolMenus::Get())
	{
		ToolMenus->UnregisterOwnerByName("GitSourceControlMenu");
	}
#else
	// Unregister the level editor extensions
	if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor"))
	{
		LevelEditorModule->GetAllLevelEditorToolbarSourceControlMenuExtenders().RemoveAll([=](const FLevelEditorModule::FLevelEditorMenuExtender& Extender)
		{
			return Extender.GetHandle() == ViewMenuExtenderHandle;
		});
	}
#endif
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void FGitLFSSourceControlMenu::CommitClicked()
{
	if (OperationInProgressNotification.IsValid())
	{
		FMessageLog SourceControlLog("SourceControl");
		SourceControlLog.Warning(LOCTEXT("SourceControlMenu_InProgress", "Revision control operation already in progress"));
		SourceControlLog.Notify();
		return;
	}

	FSourceControlWindows::ChoosePackagesToCheckIn(nullptr);
}

void FGitLFSSourceControlMenu::PushClicked()
{
	if (OperationInProgressNotification.IsValid())
	{
		FMessageLog SourceControlLog("SourceControl");
		SourceControlLog.Warning(LOCTEXT("SourceControlMenu_InProgress", "Revision control operation already in progress"));
		SourceControlLog.Notify();
		return;
	}

	// Launch a "Push" Operation
	FGitLFSSourceControlModule& GitSourceControl = FGitLFSSourceControlModule::Get();
	const TSharedPtr<FGitLFSSourceControlProvider>& Provider = GitSourceControl.GetProvider();
	const TSharedRef<FCheckIn> PushOperation = ISourceControlOperation::Create<FCheckIn>();
	const ECommandResult::Type Result = Provider->ExecuteNoChangeList(
		PushOperation,
		{},
		EConcurrency::Asynchronous,
		FSourceControlOperationComplete::CreateSP(this, &FGitLFSSourceControlMenu::OnSourceControlOperationComplete));

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

void FGitLFSSourceControlMenu::SyncClicked()
{
	if (OperationInProgressNotification.IsValid())
	{
		FMessageLog SourceControlLog("SourceControl");
		SourceControlLog.Warning(LOCTEXT("SourceControlMenu_InProgress", "Revision control operation already in progress"));
		SourceControlLog.Notify();
		return;
	}

	// Ask the user to save any dirty assets opened in Editor
	if (!SaveDirtyPackages())
	{
		FMessageLog SourceControlLog("SourceControl");
		SourceControlLog.Warning(LOCTEXT("SourceControlMenu_Sync_Unsaved", "Save All Assets before attempting to Sync!"));
		SourceControlLog.Notify();
		return;
	}

	FGitLFSSourceControlModule& GitSourceControl = FGitLFSSourceControlModule::Get();
	const TSharedPtr<FGitLFSSourceControlProvider>& Provider = GitSourceControl.GetProvider();

	// Launch a "Sync" operation
	const TSharedRef<FSync> SyncOperation = ISourceControlOperation::Create<FSync>();
	const ECommandResult::Type Result = Provider->ExecuteNoChangeList(
		SyncOperation,
		{},
		EConcurrency::Asynchronous,
		FSourceControlOperationComplete::CreateSP(this, &FGitLFSSourceControlMenu::OnSourceControlOperationComplete));

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
	const TArray<FString> Filenames
	{
		FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()),
		FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir()),
		FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath())
	};

	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	FSourceControlOperationRef Operation = ISourceControlOperation::Create<FUpdateStatus>();
	SourceControlProvider.Execute(
		Operation,
		GIT_UE_500_ONLY(FSourceControlChangelistPtr(),)
		Filenames,
		EConcurrency::Asynchronous,
		FSourceControlOperationComplete::CreateStatic(&FGitLFSSourceControlMenu::RevertAllCallback));

	FNotificationInfo Info(LOCTEXT("SourceControlMenuRevertAll", "Checking for assets to revert..."));
	Info.bFireAndForget = false;
	Info.ExpireDuration = 0.f;
	Info.FadeOutDuration = 1.f;

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

void FGitLFSSourceControlMenu::RefreshClicked()
{
	if (OperationInProgressNotification.IsValid())
	{
		FMessageLog SourceControlLog("SourceControl");
		SourceControlLog.Warning(LOCTEXT("SourceControlMenu_InProgress", "Revision control operation already in progress"));
		SourceControlLog.Notify();
		return;
	}

	FGitLFSSourceControlModule& GitSourceControl = FGitLFSSourceControlModule::Get();
	const TSharedPtr<FGitLFSSourceControlProvider>& Provider = GitSourceControl.GetProvider();

	// Launch an "GitFetch" Operation
	const TSharedRef<FGitLFSFetchOperation> RefreshOperation = ISourceControlOperation::Create<FGitLFSFetchOperation>();
	RefreshOperation->bUpdateStatus = true;

	const ECommandResult::Type Result = Provider->ExecuteNoChangeList(
		RefreshOperation,
		{},
		EConcurrency::Asynchronous,
		FSourceControlOperationComplete::CreateSP(this, &FGitLFSSourceControlMenu::OnSourceControlOperationComplete));

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

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

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

	for (const auto& It : PackageStates)
	{
		const FString& PackageName = It.Key;

		if (UPackage* Package = FindPackage(nullptr, *PackageName))
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

	const TArray<FString> FileNames = SourceControlHelpers::PackageFilenames(PackageNames);

	// Launch a "Revert" Operation
	const TSharedPtr<FGitLFSSourceControlProvider>& Provider = FGitLFSSourceControlModule::Get().GetProvider();
	const TSharedRef<FRevert> RevertOperation = ISourceControlOperation::Create<FRevert>();

	const ECommandResult::Type Result = Provider->ExecuteNoChangeList(RevertOperation, FileNames);

	RemoveInProgressNotification();

	if (Result != ECommandResult::Succeeded)
	{
		DisplayFailureNotification(TEXT("Revert"));
	}
	else
	{
		DisplaySucessNotification(TEXT("Revert"));
	}

	FGitLFSSourceControlUtils::ReloadPackages(LoadedPackages);
	Provider->ExecuteNoChangeList(
		ISourceControlOperation::Create<FUpdateStatus>(),
		{},
		EConcurrency::Asynchronous);
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

void FGitLFSSourceControlMenu::OnSourceControlOperationComplete(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult)
{
	RemoveInProgressNotification();

	if (InOperation->GetName() == "Sync" ||
		InOperation->GetName() == "Revert")
	{
		// Unstash any modifications if a stash was made at the beginning of the Sync operation
		ReApplyStashedModifications();

		// Reload packages that where unlinked at the beginning of the Sync/Revert operation
		FGitLFSSourceControlUtils::ReloadPackages(PackagesToReload);
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

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

bool FGitLFSSourceControlMenu::HaveRemoteUrl() const
{
	const TSharedPtr<FGitLFSSourceControlProvider>& Provider = FGitLFSSourceControlModule::Get().GetProvider();
	if (!Provider)
	{
		return false;
	}

	return !Provider->GetRemoteUrl().IsEmpty();
}

bool FGitLFSSourceControlMenu::SaveDirtyPackages()
{
	const bool bPromptUserToSave = true;
	const bool bSaveMapPackages = true;
	const bool bSaveContentPackages = true;
	const bool bFastSave = false;
	const bool bNotifyNoPackagesSaved = false;
	// If the user clicks "don't save" this will continue and lose their changes
	const bool bCanBeDeclined = true;
	bool bHadPackagesToSave = false;

	bool bSaved = FEditorFileUtils::SaveDirtyPackages(
		bPromptUserToSave,
		bSaveMapPackages,
		bSaveContentPackages,
		bFastSave,
		bNotifyNoPackagesSaved,
		bCanBeDeclined,
		&bHadPackagesToSave);

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

bool FGitLFSSourceControlMenu::StashAwayAnyModifications()
{
	const TSharedPtr<FGitLFSSourceControlProvider>& Provider = FGitLFSSourceControlModule::Get().GetProvider();
	if (!ensure(Provider))
	{
		return false;
	}

	const FGitLFSCommandHelpers Helpers(*Provider);

	TArray<FString> InfoMessages;

	// Check if there is any modification to the working tree
	const bool bStatusOk =
		RUN_GIT_COMMAND("status")
		.Parameter("--porcelain --untracked-files=no")
		.Results(InfoMessages);

	if (bStatusOk ||
		InfoMessages.Num() == 0)
	{
		return true;
	}

	// Ask the user before stashing
	const FText DialogText(LOCTEXT("SourceControlMenu_Stash_Ask", "Stash (save) all modifications of the working tree? Required to Sync/Pull!"));
	const EAppReturnType::Type Choice = FMessageDialog::Open(EAppMsgType::OkCancel, DialogText);
	if (Choice != EAppReturnType::Ok)
	{
		return false;
	}

	bStashMadeBeforeSync = Helpers.RunStash(true);
	if (!bStashMadeBeforeSync)
	{
		FMessageLog SourceControlLog("SourceControl");
		SourceControlLog.Warning(LOCTEXT("SourceControlMenu_StashFailed", "Stashing away modifications failed!"));
		SourceControlLog.Notify();
	}

	return true;
}

void FGitLFSSourceControlMenu::ReApplyStashedModifications()
{
	if (!bStashMadeBeforeSync)
	{
		return;
	}

	const TSharedPtr<FGitLFSSourceControlProvider>& Provider = FGitLFSSourceControlModule::Get().GetProvider();
	if (!ensure(Provider))
	{
		return;
	}

	const FGitLFSCommandHelpers Helpers(*Provider);
	if (!Helpers.RunStash(false))
	{
		FMessageLog SourceControlLog("SourceControl");
		SourceControlLog.Warning(LOCTEXT("SourceControlMenu_UnstashFailed", "Unstashing previously saved modifications failed!"));
		SourceControlLog.Notify();
	}
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void FGitLFSSourceControlMenu::AddMenuExtension(GIT_UE_500_SWITCH(FMenuBuilder, FToolMenuSection)& Builder)
{
	Builder.AddMenuEntry(
		GIT_UE_500_ONLY("GitPush", )
		LOCTEXT("GitPush", "Push pending local commits"),
		LOCTEXT("GitPushTooltip", "Push all pending local commits to the remote server."),
		FSlateIcon(FGitLFSSourceControlUtils::GetAppStyleName(), "SourceControl.Submit.Revert"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FGitLFSSourceControlMenu::PushClicked),
			FCanExecuteAction::CreateSP(this, &FGitLFSSourceControlMenu::HaveRemoteUrl)
		)
	);

	Builder.AddMenuEntry(
		GIT_UE_500_ONLY("GitSync", )
		LOCTEXT("GitSync", "Pull"),
		LOCTEXT("GitSyncTooltip", "Update all files in the local repository to the latest version of the remote server."),
		FSlateIcon(FGitLFSSourceControlUtils::GetAppStyleName(), "SourceControl.Actions.Sync"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FGitLFSSourceControlMenu::SyncClicked),
			FCanExecuteAction::CreateSP(this, &FGitLFSSourceControlMenu::HaveRemoteUrl)
		)
	);

	Builder.AddMenuEntry(
		GIT_UE_500_ONLY("GitRevert", )
		LOCTEXT("GitRevert", "Revert"),
		LOCTEXT("GitRevertTooltip", "Revert all files in the repository to their unchanged state."),
		FSlateIcon(FGitLFSSourceControlUtils::GetAppStyleName(), "SourceControl.Actions.Revert"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FGitLFSSourceControlMenu::RevertClicked),
			FCanExecuteAction()
		)
	);

	Builder.AddMenuEntry(
		GIT_UE_500_ONLY("GitRefresh", )
		LOCTEXT("GitRefresh", "Refresh"),
		LOCTEXT("GitRefreshTooltip", "Update the revision control status of all files in the local repository."),
		FSlateIcon(FGitLFSSourceControlUtils::GetAppStyleName(), "SourceControl.Actions.Refresh"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FGitLFSSourceControlMenu::RefreshClicked),
			FCanExecuteAction()
		)
	);
}

#if GIT_ENGINE_VERSION < 500
TSharedRef<FExtender> FGitLFSSourceControlMenu::OnExtendLevelEditorViewMenu(const TSharedRef<FUICommandList> CommandList)
{
	TSharedRef<FExtender> Extender(new FExtender());

	Extender->AddMenuExtension(
		"SourceControlActions",
		EExtensionHook::After,
		nullptr,
		FMenuExtensionDelegate::CreateSP(this, &FGitLFSSourceControlMenu::AddMenuExtension));

	return Extender;
}
#endif

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void FGitLFSSourceControlMenu::DisplayInProgressNotification(const FText& InOperationInProgressString)
{
	if (OperationInProgressNotification.IsValid())
	{
		return;
	}

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

void FGitLFSSourceControlMenu::RemoveInProgressNotification()
{
	if (OperationInProgressNotification.IsValid())
	{
		OperationInProgressNotification.Pin()->ExpireAndFadeout();
		OperationInProgressNotification.Reset();
	}
}

void FGitLFSSourceControlMenu::DisplaySucessNotification(const FName& InOperationName)
{
	const FText NotificationText = FText::Format(
		LOCTEXT("SourceControlMenu_Success", "{0} operation was successful!"),
		FText::FromName(InOperationName)
	);

	FNotificationInfo Info(NotificationText);
	Info.bUseSuccessFailIcons = true;
	Info.Image = FEditorAppStyle::GetBrush(TEXT("NotificationList.SuccessImage"));
	
	FSlateNotificationManager::Get().AddNotification(Info);
#if UE_BUILD_DEBUG
	UE_LOG(LogSourceControl, Log, TEXT("%s"), *NotificationText.ToString());
#endif
}

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

#undef LOCTEXT_NAMESPACE
