// Copyright (c) 2014-2023 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "GitLFSSourceControlChangelist.h"
#include "ISourceControlProvider.h"
#include "Misc/IQueuedWork.h"

class IGitLFSSourceControlWorker;

// Accumulated error and info messages for a revision control operation.
struct FGitLFSSourceControlResultInfo
{
	/** Append any messages from another FSourceControlResultInfo, ensuring to keep any already accumulated info. */
	void Append(const FGitLFSSourceControlResultInfo& InResultInfo)
	{
		InfoMessages.Append(InResultInfo.InfoMessages);
		ErrorMessages.Append(InResultInfo.ErrorMessages);
	}

	/** Info and/or warning message storage */
	TArray<FString> InfoMessages;

	/** Potential error message storage */
	TArray<FString> ErrorMessages;
};

// Used to execute Git commands multi-threaded.
class FGitLFSSourceControlCommand : public IQueuedWork
{
public:
	FGitLFSSourceControlCommand(
		const TSharedRef<ISourceControlOperation>& Operation,
		const TSharedRef<IGitLFSSourceControlWorker>& InWorker,
		const FSourceControlOperationComplete& InOperationCompleteDelegate = FSourceControlOperationComplete());

	//~ Begin IQueuedWork Interface
	virtual void Abandon() override;
	virtual void DoThreadedWork() override;
	//~ End IQueuedWork Interface

	/**
	 * Modify the repo root if all selected files are in a plugin subfolder, and the plugin subfolder is a git repo
	 * This supports the case where each plugin is a sub module
	 */
	void UpdateRepositoryRootIfSubmodule(const TArray<FString>& AbsoluteFilePaths);

	/**
	 * This is where the real thread work is done. All work that is done for
	 * this queued object should be done from within the call to this function.
	 */
	bool DoWork();

	void Cancel();
	bool IsCanceled() const;
	ECommandResult::Type ReturnResults();

	template<typename T>
	T& GetOperation()
	{
		return *StaticCastSharedRef<T>(Operation);
	}
public:
	/** Path to the Git binary */
	FString PathToGitBinary;

	/** Path to the root of the Unreal revision control repository: usually the ProjectDir */
	FString PathToRepositoryRoot;

	/** Path to the root of the Git repository: can be the ProjectDir itself, or any parent directory (found by the "Connect" operation) */
	FString PathToGitRoot;

	/** Tell if using the Git LFS file Locking workflow */
	bool bUsingGitLfsLocking;

	/** Operation we want to perform - contains outward-facing parameters & results */
	TSharedRef<ISourceControlOperation> Operation;

	/** The object that will actually do the work */
	TSharedRef<IGitLFSSourceControlWorker> Worker;

	/** Delegate to notify when this operation completes */
	FSourceControlOperationComplete OperationCompleteDelegate;

	/** If true, this command has been processed by the revision control thread*/
	volatile int32 bExecuteProcessed;

	/** If true, this command has been cancelled*/
	volatile int32 bCancelled;

	/**If true, the revision control command succeeded*/
	bool bCommandSuccessful;

	/** Current Commit full SHA1 */
	FString CommitId;

	/** Current Commit description's Summary */
	FString CommitSummary;

	/** Whether we are running multi-treaded or not*/
	EConcurrency::Type Concurrency;

	/** Files to perform this operation on */
	TArray<FString> Files;

	/** Ignored files by .gitignore */
	TArray<FString> Ignoredfiles;

	/** Changelist to perform this operation on */
	FGitLFSSourceControlChangelist Changelist;

	/** Potential error, warning and info message storage */
	FGitLFSSourceControlResultInfo ResultInfo;

	/** Branch names for status queries */
	TArray<FString> StatusBranchNames;
};