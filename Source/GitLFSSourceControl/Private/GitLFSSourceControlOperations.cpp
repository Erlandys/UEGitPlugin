// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#include "GitLFSSourceControlOperations.h"

#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "SourceControlOperations.h"
#include "ISourceControlModule.h"
#include "GitLFSSourceControlModule.h"
#include "GitLFSSourceControlCommand.h"
#include "GitLFSSourceControlUtils.h"
#include "SourceControlHelpers.h"
#include "Logging/MessageLog.h"
#include "Misc/MessageDialog.h"
#include "HAL/PlatformProcess.h"
#include "GenericPlatform/GenericPlatformFile.h"
#if ENGINE_MAJOR_VERSION >= 5
#include "HAL/PlatformFileManager.h"
#else
#include "HAL/PlatformFilemanager.h"
#endif

#include <thread>

#define LOCTEXT_NAMESPACE "GitSourceControl"

FName FGitLFSConnectWorker::GetName() const
{
	return "Connect";
}

bool FGitLFSConnectWorker::Execute(FGitLFSSourceControlCommand& InCommand)
{
	// The connect worker checks if we are connected to the remote server.
	check(InCommand.Operation->GetName() == GetName());
	TSharedRef<FConnect, ESPMode::ThreadSafe> Operation = StaticCastSharedRef<FConnect>(InCommand.Operation);

	// Skip login operations, since Git does not have to login.
	// It's not a big deal for async commands though, so let those go through.
	// More information: this is a heuristic for cases where UE is trying to create
	// a valid Perforce connection as a side effect for the connect worker. For Git,
	// the connect worker has no side effects. It is simply a query to retrieve information
	// to be displayed to the user, like in the revision control settings or on init.
	// Therefore, there is no need for synchronously establishing a connection if not there.
	if (InCommand.Concurrency == EConcurrency::Synchronous)
	{
		InCommand.bCommandSuccessful = true;
		return true;
	}

	// Check Git availability
	// We already know that Git is available if PathToGitBinary is not empty, since it is validated then.
	if (InCommand.PathToGitBinary.IsEmpty())
	{
		const FText& NotFound = LOCTEXT("GitNotFound", "Failed to enable Git revision control. You need to install Git and ensure the plugin has a valid path to the git executable.");
		InCommand.ResultInfo.ErrorMessages.Add(NotFound.ToString());
		Operation->SetErrorText(NotFound);
		InCommand.bCommandSuccessful = false;
		return false;
	}

	// Get default branch: git remote show
	
	TArray<FString> Parameters {
		TEXT("-h"), // Only limit to branches
		TEXT("-q") // Skip printing out remote URL, we don't use it
	};
	
	// Check if remote matches our refs.
	// Could be useful in the future, but all we want to know right now is if connection is up.
	// Parameters.Add("--exit-code");
	TArray<FString> InfoMessages;
	TArray<FString> ErrorMessages;
	InCommand.bCommandSuccessful = GitLFSSourceControlUtils::RunCommand(TEXT("ls-remote"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, FGitLFSSourceControlModule::GetEmptyStringArray(), FGitLFSSourceControlModule::GetEmptyStringArray(), InfoMessages, ErrorMessages);
	if (!InCommand.bCommandSuccessful)
	{
		const FText& NotFound = LOCTEXT("GitRemoteFailed", "Failed Git remote connection. Ensure your repo is initialized, and check your connection to the Git host.");
		InCommand.ResultInfo.ErrorMessages.Add(NotFound.ToString());
		Operation->SetErrorText(NotFound);
	}

	// TODO: always return true, and enter an offline mode if could not connect to remote
	return InCommand.bCommandSuccessful;
}

bool FGitLFSConnectWorker::UpdateStates() const
{
	return false;
}

FName FGitLFSCheckOutWorker::GetName() const
{
	return "CheckOut";
}

bool FGitLFSCheckOutWorker::Execute(FGitLFSSourceControlCommand& InCommand)
{
	// If we have nothing to process, exit immediately
	if (InCommand.Files.Num() == 0)
	{
		return true;
	}

	check(InCommand.Operation->GetName() == GetName());

	if (!InCommand.bUsingGitLfsLocking)
	{
		InCommand.bCommandSuccessful = false;
		return InCommand.bCommandSuccessful;
	}

	// lock files: execute the LFS command on relative filenames
	const TArray<FString>& RelativeFiles = GitLFSSourceControlUtils::RelativeFilenames(InCommand.Files, InCommand.PathToGitRoot);

	const TArray<FString>& LockableRelativeFiles = RelativeFiles.FilterByPredicate(GitLFSSourceControlUtils::IsFileLFSLockable);

	if (LockableRelativeFiles.Num() < 1)
	{
		InCommand.bCommandSuccessful = true;
		return InCommand.bCommandSuccessful;
	}

	const bool bSuccess = GitLFSSourceControlUtils::RunLFSCommand(TEXT("lock"), InCommand.PathToGitRoot, InCommand.PathToGitBinary, FGitLFSSourceControlModule::GetEmptyStringArray(), LockableRelativeFiles, InCommand.ResultInfo.InfoMessages, InCommand.ResultInfo.ErrorMessages);
	InCommand.bCommandSuccessful = bSuccess;
	const FString& LockUser = FGitLFSSourceControlModule::Get().GetProvider().GetLockUser();
	if (bSuccess)
	{
		TArray<FString> AbsoluteFiles;
		for (const auto& RelativeFile : RelativeFiles)
		{
			FString AbsoluteFile = FPaths::Combine(InCommand.PathToGitRoot, RelativeFile);
			FGitLFSLockedFilesCache::AddLockedFile(AbsoluteFile, LockUser);
			FPaths::NormalizeFilename(AbsoluteFile);
			AbsoluteFiles.Add(AbsoluteFile);
		}

		GitLFSSourceControlUtils::CollectNewStates(AbsoluteFiles, States, EGitLFSFileState::Unset, EGitLFSTreeState::Unset, EGitLFSLockState::Locked);
		for (auto& State : States)
		{
			State.Value.LockUser = LockUser;
		}
	}

	return InCommand.bCommandSuccessful;
}

bool FGitLFSCheckOutWorker::UpdateStates() const
{
	return GitLFSSourceControlUtils::UpdateCachedStates(States);
}

static FText ParseCommitResults(const TArray<FString>& InResults)
{
	if (InResults.Num() >= 1)
	{
		const FString& FirstLine = InResults[0];
		return FText::Format(LOCTEXT("CommitMessage", "Commited {0}."), FText::FromString(FirstLine));
	}
	return LOCTEXT("CommitMessageUnknown", "Submitted revision.");
}

FName FGitLFSCheckInWorker::GetName() const
{
	return "CheckIn";
}

const FText EmptyCommitMsg;

bool FGitLFSCheckInWorker::Execute(FGitLFSSourceControlCommand& InCommand)
{
	check(InCommand.Operation->GetName() == GetName());

	TSharedRef<FCheckIn, ESPMode::ThreadSafe> Operation = StaticCastSharedRef<FCheckIn>(InCommand.Operation);

	// make a temp file to place our commit message in
	bool bDoCommit = InCommand.Files.Num() > 0;
	const FText& CommitMsg = bDoCommit ? Operation->GetDescription() : EmptyCommitMsg;
	FGitLFSScopedTempFile CommitMsgFile(CommitMsg);
	if (CommitMsgFile.GetFilename().Len() > 0)
	{
		FGitLFSSourceControlProvider& Provider = FGitLFSSourceControlModule::Get().GetProvider();

		if (bDoCommit)
		{
			FString ParamCommitMsgFilename = TEXT("--file=\"");
			ParamCommitMsgFilename += FPaths::ConvertRelativePathToFull(CommitMsgFile.GetFilename());
			ParamCommitMsgFilename += TEXT("\"");
			TArray<FString> CommitParameters {ParamCommitMsgFilename};
			const TArray<FString>& FilesToCommit = GitLFSSourceControlUtils::RelativeFilenames(InCommand.Files, InCommand.PathToRepositoryRoot);

			// If no files were committed, this is false, so we treat it as if we never wanted to commit in the first place.
			bDoCommit = GitLFSSourceControlUtils::RunCommit(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, CommitParameters,
														FilesToCommit, InCommand.ResultInfo.InfoMessages, InCommand.ResultInfo.ErrorMessages);
		}

		// If we commit, we can push up the deleted state to gone
		if (bDoCommit)
		{
			// Remove any deleted files from status cache
			TArray<TSharedRef<ISourceControlState, ESPMode::ThreadSafe>> LocalStates;
			Provider.GetState(InCommand.Files, LocalStates, EStateCacheUsage::Use);
			for (const auto& State : LocalStates)
			{
				if (State->IsDeleted())
				{
					Provider.RemoveFileFromCache(State->GetFilename());
				}
			}
			Operation->SetSuccessMessage(ParseCommitResults(InCommand.ResultInfo.InfoMessages));
			const FString& Message = (InCommand.ResultInfo.InfoMessages.Num() > 0) ? InCommand.ResultInfo.InfoMessages[0] : TEXT("");
			UE_LOG(LogSourceControl, Log, TEXT("commit successful: %s"), *Message);
			GitLFSSourceControlUtils::GetCommitInfo(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, InCommand.CommitId, InCommand.CommitSummary);
		}

		// Collect difference between the remote and what we have on top of remote locally. This is to handle unpushed commits other than the one we just did.
		// Doesn't matter that we're not synced. Because our local branch is always based on the remote.
		TArray<FString> CommittedFiles;
		FString BranchName;
		bool bDiffSuccess;
		if (GitLFSSourceControlUtils::GetRemoteBranchName(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, BranchName))
		{
			TArray<FString> Parameters {"--name-only", FString::Printf(TEXT("%s...HEAD"), *BranchName), "--"};
			bDiffSuccess = GitLFSSourceControlUtils::RunCommand(TEXT("diff"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, Parameters,
															  FGitLFSSourceControlModule::GetEmptyStringArray(), CommittedFiles, InCommand.ResultInfo.ErrorMessages);
		}
		else
		{
			// Get all non-remote commits and list out their files
			TArray<FString> Parameters {"--branches", "--not" "--remotes", "--name-only", "--pretty="};
			bDiffSuccess = GitLFSSourceControlUtils::RunCommand(TEXT("log"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, Parameters, FGitLFSSourceControlModule::GetEmptyStringArray(), CommittedFiles, InCommand.ResultInfo.ErrorMessages);
			// Dedup files list between commits
			CommittedFiles = TSet<FString>{CommittedFiles}.Array();
		}

		bool bUnpushedFiles;
		TSet<FString> FilesToCheckIn {InCommand.Files};
		if (bDiffSuccess)
		{
			// Only push if we have a difference (any commits at all, not just the one we just did)
			bUnpushedFiles = CommittedFiles.Num() > 0;
			CommittedFiles = GitLFSSourceControlUtils::AbsoluteFilenames(CommittedFiles, InCommand.PathToRepositoryRoot);
			FilesToCheckIn.Append(CommittedFiles.FilterByPredicate(GitLFSSourceControlUtils::IsFileLFSLockable));
		}
		else
		{
			// Be cautious, try pushing anyway
			bUnpushedFiles = true;
		}

		TArray<FString> PulledFiles;

		// If we have unpushed files, push
		if (bUnpushedFiles)
		{
			// TODO: configure remote
			TArray<FString> PushParameters {TEXT("-u"), TEXT("origin"), TEXT("HEAD")};
			InCommand.bCommandSuccessful = GitLFSSourceControlUtils::RunCommand(TEXT("push"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot,
																			 PushParameters, FGitLFSSourceControlModule::GetEmptyStringArray(),
																			 InCommand.ResultInfo.InfoMessages, InCommand.ResultInfo.ErrorMessages);

			if (!InCommand.bCommandSuccessful)
			{
				// if out of date, pull first, then try again
				bool bWasOutOfDate = false;
				for (const auto& PushError : InCommand.ResultInfo.ErrorMessages)
				{
					if ((PushError.Contains(TEXT("[rejected]")) && (PushError.Contains(TEXT("non-fast-forward")) || PushError.Contains(TEXT("fetch first")))) ||
						PushError.Contains(TEXT("cannot lock ref")))
					{
						// Don't do it during iteration, want to append pull results to InCommand.ResultInfo.ErrorMessages
						bWasOutOfDate = true;
						break;
					}
				}
				if (bWasOutOfDate)
				{
					// Get latest
					const bool bFetched = GitLFSSourceControlUtils::FetchRemote(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, false,
																			 InCommand.ResultInfo.InfoMessages, InCommand.ResultInfo.ErrorMessages);
					if (bFetched)
					{
						// Update local with latest
						const bool bPulled = GitLFSSourceControlUtils::PullOrigin(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot,
																			   FGitLFSSourceControlModule::GetEmptyStringArray(), PulledFiles,
																			   InCommand.ResultInfo.InfoMessages, InCommand.ResultInfo.ErrorMessages);
						if (bPulled)
						{
							InCommand.bCommandSuccessful = GitLFSSourceControlUtils::RunCommand(
								TEXT("push"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, PushParameters,
								FGitLFSSourceControlModule::GetEmptyStringArray(), InCommand.ResultInfo.InfoMessages, InCommand.ResultInfo.ErrorMessages);
						}
					}

					// Our push still wasn't successful
					if (!InCommand.bCommandSuccessful)
					{
						if (!Provider.bPendingRestart)
						{
							// If it fails, just let the user do it
							FText PushFailMessage(LOCTEXT("GitPush_OutOfDate_Msg", "Git Push failed because there are changes you need to pull.\n\n"
																				   "An attempt was made to pull, but failed, because while the Unreal Editor is "
																				   "open, files cannot always be updated.\n\n"
																				   "Please exit the editor, and update the project again."));
							FText PushFailTitle(LOCTEXT("GitPush_OutOfDate_Title", "Git Pull Required"));
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3
							FMessageDialog::Open(EAppMsgType::Ok, PushFailMessage, PushFailTitle);
#else
							FMessageDialog::Open(EAppMsgType::Ok, PushFailMessage, &PushFailTitle);
#endif
							UE_LOG(LogSourceControl, Log, TEXT("Push failed because we're out of date, prompting user to resolve manually"));
						}
					}
				}
			}
		}
		else
		{
			InCommand.bCommandSuccessful = true;
		}

		// git-lfs: unlock files
		if (InCommand.bUsingGitLfsLocking)
		{
			// If we successfully pushed (or didn't need to push), unlock the files marked for check in
			if (InCommand.bCommandSuccessful)
			{
				// unlock files: execute the LFS command on relative filenames
				// (unlock only locked files, that is, not Added files)
				TArray<FString> LockedFiles;
				GitLFSSourceControlUtils::GetLockedFiles(FilesToCheckIn.Array(), LockedFiles);
				if (LockedFiles.Num() > 0)
				{
					const TArray<FString>& FilesToUnlock = GitLFSSourceControlUtils::RelativeFilenames(LockedFiles, InCommand.PathToGitRoot);

					if (FilesToUnlock.Num() > 0)
					{
						// Not strictly necessary to succeed, so don't update command success
						const bool bUnlockSuccess = GitLFSSourceControlUtils::RunLFSCommand(TEXT("unlock"), InCommand.PathToGitRoot, InCommand.PathToGitBinary,
																						 FGitLFSSourceControlModule::GetEmptyStringArray(), FilesToUnlock,
																						 InCommand.ResultInfo.InfoMessages, InCommand.ResultInfo.ErrorMessages);
						if (bUnlockSuccess)
						{
							for (const auto& File : LockedFiles)
							{
								FGitLFSLockedFilesCache::RemoveLockedFile(File);
							}
						}
					}
				}
#if 0
				for (const FString& File : FilesToCheckIn.Array())
				{
					FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*File, true);
				}
#endif
			}
		}

		// Collect all the files we touched through the pull update
		if (bUnpushedFiles && PulledFiles.Num())
		{
			FilesToCheckIn.Append(PulledFiles);
		}
		// Before, we added only lockable files from CommittedFiles. But now, we want to update all files, not just lockables.
		FilesToCheckIn.Append(CommittedFiles);

		// now update the status of our files
		TMap<FString, FGitLFSSourceControlState> UpdatedStates;
		bool bSuccess = GitLFSSourceControlUtils::RunUpdateStatus(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, InCommand.bUsingGitLfsLocking,
															   FilesToCheckIn.Array(), InCommand.ResultInfo.ErrorMessages, UpdatedStates);
		if (bSuccess)
		{
			GitLFSSourceControlUtils::CollectNewStates(UpdatedStates, States);
		}
		GitLFSSourceControlUtils::RemoveRedundantErrors(InCommand, TEXT("' is outside repository"));
		return InCommand.bCommandSuccessful;
	}

	InCommand.bCommandSuccessful = false;

	return false;
}

bool FGitLFSCheckInWorker::UpdateStates() const
{
	return GitLFSSourceControlUtils::UpdateCachedStates(States);
}

FName FGitLFSMarkForAddWorker::GetName() const
{
	return "MarkForAdd";
}

bool FGitLFSMarkForAddWorker::Execute(FGitLFSSourceControlCommand& InCommand)
{
	// If we have nothing to process, exit immediately
	if (InCommand.Files.Num() == 0)
	{
		return true;
	}

	check(InCommand.Operation->GetName() == GetName());

	InCommand.bCommandSuccessful = GitLFSSourceControlUtils::RunCommand(TEXT("add"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, FGitLFSSourceControlModule::GetEmptyStringArray(), InCommand.Files, InCommand.ResultInfo.InfoMessages, InCommand.ResultInfo.ErrorMessages);

	if (InCommand.bCommandSuccessful)
	{
		GitLFSSourceControlUtils::CollectNewStates(InCommand.Files, States, EGitLFSFileState::Added, EGitLFSTreeState::Staged);
	}
	else
	{
		TMap<FString, FGitLFSSourceControlState> UpdatedStates;
		bool bSuccess = GitLFSSourceControlUtils::RunUpdateStatus(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, InCommand.bUsingGitLfsLocking, InCommand.Files, InCommand.ResultInfo.ErrorMessages, UpdatedStates);
		if (bSuccess)
		{
			GitLFSSourceControlUtils::CollectNewStates(UpdatedStates, States);
		}
		GitLFSSourceControlUtils::RemoveRedundantErrors(InCommand, TEXT("' is outside repository"));
	}

	return InCommand.bCommandSuccessful;
}

bool FGitLFSMarkForAddWorker::UpdateStates() const
{
	return GitLFSSourceControlUtils::UpdateCachedStates(States);
}

FName FGitLFSDeleteWorker::GetName() const
{
	return "Delete";
}

bool FGitLFSDeleteWorker::Execute(FGitLFSSourceControlCommand& InCommand)
{
	// If we have nothing to process, exit immediately
	if (InCommand.Files.Num() == 0)
	{
		return true;
	}

	check(InCommand.Operation->GetName() == GetName());

	InCommand.bCommandSuccessful = GitLFSSourceControlUtils::RunCommand(TEXT("rm"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, FGitLFSSourceControlModule::GetEmptyStringArray(), InCommand.Files, InCommand.ResultInfo.InfoMessages, InCommand.ResultInfo.ErrorMessages);

	if (InCommand.bCommandSuccessful)
	{
		GitLFSSourceControlUtils::CollectNewStates(InCommand.Files, States, EGitLFSFileState::Deleted, EGitLFSTreeState::Staged);
	}
	else
	{
		TMap<FString, FGitLFSSourceControlState> UpdatedStates;
		bool bSuccess = GitLFSSourceControlUtils::RunUpdateStatus(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, InCommand.bUsingGitLfsLocking, InCommand.Files, InCommand.ResultInfo.ErrorMessages, UpdatedStates);
		if (bSuccess)
		{
			GitLFSSourceControlUtils::CollectNewStates(UpdatedStates, States);
		}
		GitLFSSourceControlUtils::RemoveRedundantErrors(InCommand, TEXT("' is outside repository"));
	}

	return InCommand.bCommandSuccessful;
}

bool FGitLFSDeleteWorker::UpdateStates() const
{
	return GitLFSSourceControlUtils::UpdateCachedStates(States);
}


// Get lists of Missing files (ie "deleted"), Modified files, and "other than Added" Existing files
void GetMissingVsExistingFiles(const TArray<FString>& InFiles, TArray<FString>& OutMissingFiles, TArray<FString>& OutAllExistingFiles, TArray<FString>& OutOtherThanAddedExistingFiles)
{
	FGitLFSSourceControlModule& GitSourceControl = FGitLFSSourceControlModule::Get();
	FGitLFSSourceControlProvider& Provider = GitSourceControl.GetProvider();

	const TArray<FString> Files = (InFiles.Num() > 0) ? (InFiles) : (Provider.GetFilesInCache());

	TArray<TSharedRef<ISourceControlState, ESPMode::ThreadSafe>> LocalStates;
	Provider.GetState(Files, LocalStates, EStateCacheUsage::Use);
	for (const auto& State : LocalStates)
	{
		if (FPaths::FileExists(State->GetFilename()))
		{
			if (State->IsAdded())
			{
				OutAllExistingFiles.Add(State->GetFilename());
			}
			else if (State->IsModified())
			{
				OutOtherThanAddedExistingFiles.Add(State->GetFilename());
				OutAllExistingFiles.Add(State->GetFilename());
			}
			else if (State->CanRevert()) // for locked but unmodified files
			{
				OutOtherThanAddedExistingFiles.Add(State->GetFilename());
			}
		}
		else
		{
			// If already queued for deletion, don't try to delete again
			if (State->IsSourceControlled() && !State->IsDeleted())
			{
				OutMissingFiles.Add(State->GetFilename());
			}
		}
	}
}

FName FGitLFSRevertWorker::GetName() const
{
	return "Revert";
}

bool FGitLFSRevertWorker::Execute(FGitLFSSourceControlCommand& InCommand)
{
	InCommand.bCommandSuccessful = true;

	// Filter files by status
	TArray<FString> MissingFiles;
	TArray<FString> AllExistingFiles;
	TArray<FString> OtherThanAddedExistingFiles;
	GetMissingVsExistingFiles(InCommand.Files, MissingFiles, AllExistingFiles, OtherThanAddedExistingFiles);

	const bool bRevertAll = InCommand.Files.Num() < 1;
	if (bRevertAll)
	{
		TArray<FString> Parms;
		Parms.Add(TEXT("--hard"));
		InCommand.bCommandSuccessful &= GitLFSSourceControlUtils::RunCommand(TEXT("reset"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, Parms, FGitLFSSourceControlModule::GetEmptyStringArray(), InCommand.ResultInfo.InfoMessages, InCommand.ResultInfo.ErrorMessages);

		Parms.Reset(2);
		Parms.Add(TEXT("-f")); // force
		Parms.Add(TEXT("-d")); // remove directories
		InCommand.bCommandSuccessful &= GitLFSSourceControlUtils::RunCommand(TEXT("clean"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, Parms, FGitLFSSourceControlModule::GetEmptyStringArray(), InCommand.ResultInfo.InfoMessages, InCommand.ResultInfo.ErrorMessages);
	}
	else
	{
		if (MissingFiles.Num() > 0)
		{
			// "Added" files that have been deleted needs to be removed from revision control
			InCommand.bCommandSuccessful &= GitLFSSourceControlUtils::RunCommand(TEXT("rm"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, FGitLFSSourceControlModule::GetEmptyStringArray(), MissingFiles, InCommand.ResultInfo.InfoMessages, InCommand.ResultInfo.ErrorMessages);
		}
		if (AllExistingFiles.Num() > 0)
		{
			// reset and revert any changes already added to the index
			InCommand.bCommandSuccessful &= GitLFSSourceControlUtils::RunCommand(TEXT("reset"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, FGitLFSSourceControlModule::GetEmptyStringArray(), AllExistingFiles, InCommand.ResultInfo.InfoMessages, InCommand.ResultInfo.ErrorMessages);
			InCommand.bCommandSuccessful &= GitLFSSourceControlUtils::RunCommand(TEXT("checkout"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, FGitLFSSourceControlModule::GetEmptyStringArray(), AllExistingFiles, InCommand.ResultInfo.InfoMessages, InCommand.ResultInfo.ErrorMessages);
		}
		if (OtherThanAddedExistingFiles.Num() > 0)
		{
			// revert any changes in working copy (this would fails if the asset was in "Added" state, since after "reset" it is now "untracked")
			// may need to try a few times due to file locks from prior operations
			bool CheckoutSuccess = false;
			int32 Attempts = 10;
			while( Attempts-- > 0 )
			{
				CheckoutSuccess = GitLFSSourceControlUtils::RunCommand(TEXT("checkout"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, FGitLFSSourceControlModule::GetEmptyStringArray(), OtherThanAddedExistingFiles, InCommand.ResultInfo.InfoMessages, InCommand.ResultInfo.ErrorMessages);
				if (CheckoutSuccess)
				{
					break;
				}

				FPlatformProcess::Sleep(0.1f);
			}
			
			InCommand.bCommandSuccessful &= CheckoutSuccess;
		}
	}

	if (InCommand.bUsingGitLfsLocking)
	{
		// unlock files: execute the LFS command on relative filenames
		// (unlock only locked files, that is, not Added files)
		TArray<FString> LockedFiles;
		GitLFSSourceControlUtils::GetLockedFiles(OtherThanAddedExistingFiles, LockedFiles);
		if (LockedFiles.Num() > 0)
		{
			const TArray<FString>& RelativeFiles = GitLFSSourceControlUtils::RelativeFilenames(LockedFiles, InCommand.PathToGitRoot);
			InCommand.bCommandSuccessful &= GitLFSSourceControlUtils::RunLFSCommand(TEXT("unlock"), InCommand.PathToGitRoot, InCommand.PathToGitBinary, FGitLFSSourceControlModule::GetEmptyStringArray(), RelativeFiles,
																				 InCommand.ResultInfo.InfoMessages, InCommand.ResultInfo.ErrorMessages);
			if (InCommand.bCommandSuccessful)
			{
				for (const auto& File : LockedFiles)
				{
					FGitLFSLockedFilesCache::RemoveLockedFile(File);
				}
			}
		}
	}

	// If no files were specified (full revert), refresh all relevant files instead of the specified files (which is an empty list in full revert)
	// This is required so that files that were "Marked for add" have their status updated after a full revert.
	TArray<FString> FilesToUpdate = InCommand.Files;
	if (InCommand.Files.Num() <= 0)
	{
		for (const auto& File : MissingFiles) FilesToUpdate.Add(File);
		for (const auto& File : AllExistingFiles) FilesToUpdate.Add(File);
		for (const auto& File : OtherThanAddedExistingFiles) FilesToUpdate.Add(File);
	}

	// now update the status of our files
	TMap<FString, FGitLFSSourceControlState> UpdatedStates;
	bool bSuccess = GitLFSSourceControlUtils::RunUpdateStatus(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, InCommand.bUsingGitLfsLocking, FilesToUpdate, InCommand.ResultInfo.ErrorMessages, UpdatedStates);
	if (bSuccess)
	{
		GitLFSSourceControlUtils::CollectNewStates(UpdatedStates, States);
	}
	GitLFSSourceControlUtils::RemoveRedundantErrors(InCommand, TEXT("' is outside repository"));

	return InCommand.bCommandSuccessful;
}

bool FGitLFSRevertWorker::UpdateStates() const
{
	return GitLFSSourceControlUtils::UpdateCachedStates(States);
}

FName FGitLFSSyncWorker::GetName() const
{
	return "Sync";
}

bool FGitLFSSyncWorker::Execute(FGitLFSSourceControlCommand& InCommand)
{
	TArray<FString> Results;
	const bool bFetched = GitLFSSourceControlUtils::FetchRemote(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, false, InCommand.ResultInfo.InfoMessages, InCommand.ResultInfo.ErrorMessages);
	if (!bFetched)
	{
		return false;
	}

	InCommand.bCommandSuccessful = GitLFSSourceControlUtils::PullOrigin(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, InCommand.Files, InCommand.Files, Results, InCommand.ResultInfo.ErrorMessages);

	// now update the status of our files
	TMap<FString, FGitLFSSourceControlState> UpdatedStates;
	const bool bSuccess = GitLFSSourceControlUtils::RunUpdateStatus(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, InCommand.bUsingGitLfsLocking,
																 InCommand.Files, InCommand.ResultInfo.ErrorMessages, UpdatedStates);
	if (bSuccess)
	{
		GitLFSSourceControlUtils::CollectNewStates(UpdatedStates, States);
	}
	GitLFSSourceControlUtils::RemoveRedundantErrors(InCommand, TEXT("' is outside repository"));
	GitLFSSourceControlUtils::GetCommitInfo(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, InCommand.CommitId, InCommand.CommitSummary);

	return InCommand.bCommandSuccessful;
}

bool FGitLFSSyncWorker::UpdateStates() const
{
	return GitLFSSourceControlUtils::UpdateCachedStates(States);
}

FName FGitLFSFetch::GetName() const
{
	return "Fetch";
}

FText FGitLFSFetch::GetInProgressString() const
{
	// TODO Configure origin
	return LOCTEXT("SourceControl_Push", "Fetching from remote origin...");
}

FName FGitLFSFetchWorker::GetName() const
{
	return "Fetch";
}

bool FGitLFSFetchWorker::Execute(FGitLFSSourceControlCommand& InCommand)
{
	InCommand.bCommandSuccessful = GitLFSSourceControlUtils::FetchRemote(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, InCommand.bUsingGitLfsLocking,
																	  InCommand.ResultInfo.InfoMessages, InCommand.ResultInfo.ErrorMessages);
	if (!InCommand.bCommandSuccessful)
	{
		return false;
	}

	check(InCommand.Operation->GetName() == GetName());
	TSharedRef<FGitLFSFetch, ESPMode::ThreadSafe> Operation = StaticCastSharedRef<FGitLFSFetch>(InCommand.Operation);

	if (Operation->bUpdateStatus)
	{
		// Now update the status of all our files
		const TArray<FString> ProjectDirs {FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()),FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir()),
										   FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath())};
		TMap<FString, FGitLFSSourceControlState> UpdatedStates;
		InCommand.bCommandSuccessful = GitLFSSourceControlUtils::RunUpdateStatus(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, InCommand.bUsingGitLfsLocking,
																			  ProjectDirs, InCommand.ResultInfo.ErrorMessages, UpdatedStates);
		GitLFSSourceControlUtils::RemoveRedundantErrors(InCommand, TEXT("' is outside repository"));
		if (InCommand.bCommandSuccessful)
		{
			GitLFSSourceControlUtils::CollectNewStates(UpdatedStates, States);
		}
	}

	return InCommand.bCommandSuccessful;
}

bool FGitLFSFetchWorker::UpdateStates() const
{
	return GitLFSSourceControlUtils::UpdateCachedStates(States);
}

FName FGitLFSUpdateStatusWorker::GetName() const
{
	return "UpdateStatus";
}

bool FGitLFSUpdateStatusWorker::Execute(FGitLFSSourceControlCommand& InCommand)
{
	check(InCommand.Operation->GetName() == GetName());

	TSharedRef<FUpdateStatus, ESPMode::ThreadSafe> Operation = StaticCastSharedRef<FUpdateStatus>(InCommand.Operation);

	if(InCommand.Files.Num() > 0)
	{
		TMap<FString, FGitLFSSourceControlState> UpdatedStates;
		InCommand.bCommandSuccessful = GitLFSSourceControlUtils::RunUpdateStatus(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, InCommand.bUsingGitLfsLocking, InCommand.Files, InCommand.ResultInfo.ErrorMessages, UpdatedStates);
		GitLFSSourceControlUtils::RemoveRedundantErrors(InCommand, TEXT("' is outside repository"));
		if (InCommand.bCommandSuccessful)
		{
			GitLFSSourceControlUtils::CollectNewStates(UpdatedStates, States);
			if (Operation->ShouldUpdateHistory())
			{
				for (const auto& State : UpdatedStates)
				{
					const FString& File = State.Key;
					TGitSourceControlHistory History;

					if (State.Value.IsConflicted())
					{
						// In case of a merge conflict, we first need to get the tip of the "remote branch" (MERGE_HEAD)
						GitLFSSourceControlUtils::RunGetHistory(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, File, true,
															 InCommand.ResultInfo.ErrorMessages, History);
					}
					// Get the history of the file in the current branch
					InCommand.bCommandSuccessful &= GitLFSSourceControlUtils::RunGetHistory(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, File, false,
																						 InCommand.ResultInfo.ErrorMessages, History);
					Histories.Add(*File, History);
				}
			}
		}
	}
	else
	{
		// no path provided: only update the status of assets in Content/ directory and also Config files
		const TArray<FString> ProjectDirs {FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()), FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir()),
										   FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath())};
		TMap<FString, FGitLFSSourceControlState> UpdatedStates;
		InCommand.bCommandSuccessful = GitLFSSourceControlUtils::RunUpdateStatus(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, InCommand.bUsingGitLfsLocking, ProjectDirs, InCommand.ResultInfo.ErrorMessages, UpdatedStates);
		GitLFSSourceControlUtils::RemoveRedundantErrors(InCommand, TEXT("' is outside repository"));
		if (InCommand.bCommandSuccessful)
		{
			GitLFSSourceControlUtils::CollectNewStates(UpdatedStates, States);
		}
	}

	GitLFSSourceControlUtils::GetCommitInfo(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, InCommand.CommitId, InCommand.CommitSummary);

	// don't use the ShouldUpdateModifiedState() hint here as it is specific to Perforce: the above normal Git status has already told us this information (like Git and Mercurial)

	return InCommand.bCommandSuccessful;
}

bool FGitLFSUpdateStatusWorker::UpdateStates() const
{
	bool bUpdated = GitLFSSourceControlUtils::UpdateCachedStates(States);

	FGitLFSSourceControlModule& GitSourceControl = FModuleManager::GetModuleChecked<FGitLFSSourceControlModule>( "GitLFSSourceControl" );
	FGitLFSSourceControlProvider& Provider = GitSourceControl.GetProvider();
	const bool bUsingGitLfsLocking = Provider.UsesCheckout();

	// TODO without LFS : Workaround a bug with the Source Control Module not updating file state after a simple "Save" with no "Checkout" (when not using File Lock)
	const FDateTime Now = bUsingGitLfsLocking ? FDateTime::Now() : FDateTime::MinValue();

	// add history, if any
	for(const auto& History : Histories)
	{
		TSharedRef<FGitLFSSourceControlState, ESPMode::ThreadSafe> State = Provider.GetStateInternal(History.Key);
		State->History = History.Value;
		State->TimeStamp = Now;
		bUpdated = true;
	}

	return bUpdated;
}

FName FGitLFSCopyWorker::GetName() const
{
	return "Copy";
}

bool FGitLFSCopyWorker::Execute(FGitLFSSourceControlCommand& InCommand)
{
	check(InCommand.Operation->GetName() == GetName());

	// Copy or Move operation on a single file : Git does not need an explicit copy nor move,
	// but after a Move the Editor create a redirector file with the old asset name that points to the new asset.
	// The redirector needs to be committed with the new asset to perform a real rename.
	// => the following is to "MarkForAdd" the redirector, but it still need to be committed by selecting the whole directory and "check-in"
	InCommand.bCommandSuccessful = GitLFSSourceControlUtils::RunCommand(TEXT("add"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, FGitLFSSourceControlModule::GetEmptyStringArray(), InCommand.Files, InCommand.ResultInfo.InfoMessages, InCommand.ResultInfo.ErrorMessages);

	if (InCommand.bCommandSuccessful)
	{
		GitLFSSourceControlUtils::CollectNewStates(InCommand.Files, States, EGitLFSFileState::Added, EGitLFSTreeState::Staged);
	}
	else
	{
		TMap<FString, FGitLFSSourceControlState> UpdatedStates;
		const bool bSuccess = GitLFSSourceControlUtils::RunUpdateStatus(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, InCommand.bUsingGitLfsLocking, InCommand.Files, InCommand.ResultInfo.ErrorMessages, UpdatedStates);
		GitLFSSourceControlUtils::RemoveRedundantErrors(InCommand, TEXT("' is outside repository"));
		if (bSuccess)
		{
			GitLFSSourceControlUtils::CollectNewStates(UpdatedStates, States);
		}
	}

	return InCommand.bCommandSuccessful;
}

bool FGitLFSCopyWorker::UpdateStates() const
{
	return GitLFSSourceControlUtils::UpdateCachedStates(States);
}

FName FGitLFSResolveWorker::GetName() const
{
	return "Resolve";
}

bool FGitLFSResolveWorker::Execute( class FGitLFSSourceControlCommand& InCommand )
{
	check(InCommand.Operation->GetName() == GetName());

	// mark the conflicting files as resolved:
	TArray<FString> Results;
	InCommand.bCommandSuccessful = GitLFSSourceControlUtils::RunCommand(TEXT("add"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, FGitLFSSourceControlModule::GetEmptyStringArray(), InCommand.Files, Results, InCommand.ResultInfo.ErrorMessages);

	// now update the status of our files
	TMap<FString, FGitLFSSourceControlState> UpdatedStates;
	const bool bSuccess = GitLFSSourceControlUtils::RunUpdateStatus(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, InCommand.bUsingGitLfsLocking, InCommand.Files, InCommand.ResultInfo.ErrorMessages, UpdatedStates);
	GitLFSSourceControlUtils::RemoveRedundantErrors(InCommand, TEXT("' is outside repository"));
	if (bSuccess)
	{
		GitLFSSourceControlUtils::CollectNewStates(UpdatedStates, States);
	}

	return InCommand.bCommandSuccessful;
}

bool FGitLFSResolveWorker::UpdateStates() const
{
	return GitLFSSourceControlUtils::UpdateCachedStates(States);
}

FName FGitLFSMoveToChangelistWorker::GetName() const
{
	return "MoveToChangelist";
}

bool FGitLFSMoveToChangelistWorker::UpdateStates() const
{
	return true;
}

bool FGitLFSMoveToChangelistWorker::Execute(FGitLFSSourceControlCommand& InCommand)
{
	check(InCommand.Operation->GetName() == GetName());

	FGitLFSSourceControlChangelist DestChangelist = InCommand.Changelist;
	bool bResult = false;
	if(DestChangelist.GetName().Equals(TEXT("Staged")))
	{
		bResult = GitLFSSourceControlUtils::RunCommand(TEXT("add"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, FGitLFSSourceControlModule::GetEmptyStringArray(), InCommand.Files, InCommand.ResultInfo.InfoMessages, InCommand.ResultInfo.ErrorMessages);
	}
	else if(DestChangelist.GetName().Equals(TEXT("Working")))
	{
		TArray<FString> Parameter;
		Parameter.Add(TEXT("--staged"));
		bResult = GitLFSSourceControlUtils::RunCommand(TEXT("restore"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, Parameter, InCommand.Files, InCommand.ResultInfo.InfoMessages, InCommand.ResultInfo.ErrorMessages);
	}
	
	if (bResult)
	{
		TMap<FString, FGitLFSSourceControlState> DummyStates;
		GitLFSSourceControlUtils::RunUpdateStatus(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, InCommand.bUsingGitLfsLocking, InCommand.Files, InCommand.ResultInfo.InfoMessages, DummyStates);
	}
	return bResult;
}

FName FGitLFSUpdateStagingWorker::GetName() const
{
	return "UpdateChangelistsStatus";
}

bool FGitLFSUpdateStagingWorker::Execute(FGitLFSSourceControlCommand& InCommand)
{
	return GitLFSSourceControlUtils::UpdateChangelistStateByCommand();
}

bool FGitLFSUpdateStagingWorker::UpdateStates() const
{
	return true;
}

#undef LOCTEXT_NAMESPACE
