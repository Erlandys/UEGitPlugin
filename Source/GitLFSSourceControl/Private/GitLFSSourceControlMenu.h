// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "CoreMinimal.h"
#include "ISourceControlProvider.h"
#include "GitLFSSourceControlHelpers.h"

class FMenuBuilder;
class SNotificationItem;
class ISourceControlOperation;
#if GIT_ENGINE_VERSION >= 500
struct FToolMenuSection;
#else
class FExtender;
class FUICommandList;
struct FMenuBuilder;
#endif

/** Git extension of the Revision Control toolbar menu */
class FGitLFSSourceControlMenu : public TSharedFromThis<FGitLFSSourceControlMenu>
{
public:
	void Register();
	void Unregister();

public:
	/** This functions will be bound to appropriate Command. */
	void CommitClicked();
	void PushClicked();
	void SyncClicked();
	void RevertClicked();
	void RefreshClicked();

protected:
	static void RevertAllCallback(const TSharedRef<ISourceControlOperation>& InOperation, ECommandResult::Type InResult);
	static void RevertAllCancelled(TSharedRef<ISourceControlOperation> InOperation);

	/** Delegate called when a revision control operation has completed */
	void OnSourceControlOperationComplete(const TSharedRef<ISourceControlOperation>& InOperation, ECommandResult::Type InResult);

private:
	bool HaveRemoteUrl() const;

	/// Prompt to save or discard all packages
	bool SaveDirtyPackages();

	// Ask the user if they want to stash any modification and try to unstash them afterward, which could lead to conflicts
	bool StashAwayAnyModifications();

	// Unstash any modifications if a stash was made at the beginning of the Sync operation
	void ReApplyStashedModifications();

private:
	void AddMenuExtension(GIT_UE_500_SWITCH(FMenuBuilder, FToolMenuSection)& Builder);
#if GIT_ENGINE_VERSION < 500
	TSharedRef<FExtender> OnExtendLevelEditorViewMenu(const TSharedRef<FUICommandList> CommandList);
#endif

private:
	// Display an ongoing notification during the whole operation
	static void DisplayInProgressNotification(const FText& InOperationInProgressString);
	// Remove the ongoing notification at the end of the operation
	static void RemoveInProgressNotification();
	// Display a temporary success notification at the end of the operation
	static void DisplaySucessNotification(const FName& InOperationName);
	// Display a temporary failure notification at the end of the operation
	static void DisplayFailureNotification(const FName& InOperationName);

private:
#if GIT_ENGINE_VERSION < 500
	FDelegateHandle ViewMenuExtenderHandle;
#endif

	/** Was there a need to stash away modifications before Sync? */
	bool bStashMadeBeforeSync = false;

	/** Loaded packages to reload after a Sync or Revert operation */
	TArray<UPackage*> PackagesToReload;

	/** Current revision control operation from extended menu if any */
	static TWeakPtr<SNotificationItem> OperationInProgressNotification;
};
