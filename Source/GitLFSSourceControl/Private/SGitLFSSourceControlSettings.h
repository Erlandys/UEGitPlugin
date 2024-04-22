// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "ISourceControlProvider.h"
#include "GitLFSSourceControlHelpers.h"

class SNotificationItem;

namespace ETextCommit
{
	enum Type GIT_UE_502_ONLY(: int);
}

enum class ECheckBoxState : uint8;

class SGitLFSSourceControlSettings : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGitLFSSourceControlSettings)
		{
		}

	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs);
	virtual ~SGitLFSSourceControlSettings() override;

private:
	void ConstructBasedOnEngineVersion();

private:
	/** Delegates to get Git binary path from/to settings */
	FString GetBinaryPathString() const;
	void OnBinaryPathPicked(const FString& PickedPath) const;

private:
	/** Delegate to get repository root, user name and email from provider */
	FText GetPathToRepositoryRoot() const;
	FText GetUserName() const;
	FText GetUserEmail() const;

private:
	EVisibility MustInitializeGitRepository() const;
	bool CanInitializeGitRepository() const;
	bool CanUseGitLfsLocking() const;

private:
	/** Delegate to initialize a new Git repository */
	FReply OnClickedInitializeGitRepository();

	void OnCheckedCreateGitIgnore(ECheckBoxState NewCheckedState);
	void OnCheckedCreateGitAttributes(ECheckBoxState NewCheckedState);

private:
	/** Delegates to create a README.md file */
	void OnCheckedCreateReadme(ECheckBoxState NewCheckedState);
	bool GetAutoCreateReadme() const;
	void OnReadmeContentCommited(const FText& InText, ETextCommit::Type InCommitType);
	FText GetReadmeContent() const;

private:
	void OnCheckedUseGitLfsLocking(ECheckBoxState NewCheckedState);
	ECheckBoxState IsUsingGitLfsLocking() const;
	bool GetIsUsingGitLfsLocking() const;

	void OnLfsUserNameCommited(const FText& InText, ETextCommit::Type InCommitType);
	FText GetLfsUserName() const;

private:
	void OnCheckedInitialCommit(ECheckBoxState NewCheckedState);
	bool GetAutoInitialCommit() const;
	void OnInitialCommitMessageCommited(const FText& InText, ETextCommit::Type InCommitType);
	FText GetInitialCommitMessage() const;

private:
	void OnRemoteUrlCommited(const FText& InText, ETextCommit::Type InCommitType);
	FText GetRemoteUrl() const;

private:
	/** Launch initial asynchronous add and commit operations */
	void LaunchMarkForAddOperation(const TArray<FString>& InFiles);
	/** Launch an asynchronous "CheckIn" operation and start another ongoing notification */
	void LaunchCheckInOperation();

	/** Delegate called when a Revision control operation has completed: launch the next one and manage notifications */
	void OnSourceControlOperationComplete(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult);

private:
	// Display an ongoing notification during the whole operation
	void DisplayInProgressNotification(const FSourceControlOperationRef& InOperation);
	// Remove the ongoing notification at the end of the operation
	void RemoveInProgressNotification();
	// Display a temporary success notification at the end of the operation
	void DisplaySuccessNotification(const FSourceControlOperationRef& InOperation);
	// Display a temporary failure notification at the end of the operation
	void DisplayFailureNotification(const FSourceControlOperationRef& InOperation);

private:
	bool bAutoCreateGitIgnore = true;
	bool bAutoCreateReadme = true;
	FText ReadmeContent;
	bool bAutoCreateGitAttributes = false;
	bool bAutoInitialCommit = true;
	FText InitialCommitMessage;
	FText RemoteUrl;

	/** Asynchronous operation progress notifications */
	TWeakPtr<SNotificationItem> OperationInProgressNotification;
};
