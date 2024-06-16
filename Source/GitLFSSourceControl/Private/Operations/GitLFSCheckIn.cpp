// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#include "GitLFSCheckIn.h"

#include "GitLFSCommandHelpers.h"
#include "SourceControlOperations.h"
#include "GitLFSSourceControlUtils.h"
#include "GitLFSSourceControlCommand.h"
#include "GitLFSSourceControlModule.h"
#include "ISourceControlModule.h"
#include "Data/GitLFSScopedTempFile.h"

#define LOCTEXT_NAMESPACE "GitSourceControl"

bool FGitLFSCheckInWorker::Execute(FGitLFSSourceControlCommand& Command, FGitLFSCommandHelpers& Helpers)
{
	TSharedRef<FCheckIn> Operation = Command.GetOperation<FCheckIn>();

	// make a temp file to place our commit message in
	bool bDoCommit = Command.Files.Num() > 0;
	const FText& CommitMsg = bDoCommit ? Operation->GetDescription() : FText();
	FGitLFSScopedTempFile CommitMsgFile(CommitMsg);
	if (CommitMsgFile.GetFilename().Len() > 0)
	{
		const TSharedPtr<FGitLFSSourceControlProvider>& Provider = FGitLFSSourceControlModule::Get().GetProvider();
		if (!Provider)
		{
			return false;
		}

		if (bDoCommit)
		{
			const TArray<FString>& FilesToCommit = FGitLFSSourceControlUtils::RelativeFilenames(Command.Files, Command.PathToRepositoryRoot);

			// If no files were committed, this is false, so we treat it as if we never wanted to commit in the first place.
			bDoCommit = Helpers.RunAdd(false, FilesToCommit, Command.ResultInfo.InfoMessages, Command.ResultInfo.ErrorMessages);
			bDoCommit &= Helpers.RunCommit(CommitMsgFile, FilesToCommit, Command.ResultInfo.InfoMessages, Command.ResultInfo.ErrorMessages);
		}

		// If we commit, we can push up the deleted state to gone
		if (bDoCommit)
		{
			// Remove any deleted files from status cache
			TArray<TSharedRef<ISourceControlState>> LocalStates;
			Provider->GetState(Command.Files, LocalStates, EStateCacheUsage::Use);
			for (const TSharedRef<ISourceControlState>& State : LocalStates)
			{
				if (State->IsDeleted())
				{
					Provider->RemoveFileFromCache(State->GetFilename());
				}
			}
			Operation->SetSuccessMessage(ParseCommitResults(Command.ResultInfo.InfoMessages));

			const FString& Message = Command.ResultInfo.InfoMessages.Num() > 0 ? Command.ResultInfo.InfoMessages[0] : TEXT("");
			UE_LOG(LogSourceControl, Log, TEXT("commit successful: %s"), *Message);

			Helpers.GetCommitInfo(Command.CommitId, Command.CommitSummary);
		}

		// Collect difference between the remote and what we have on top of remote locally. This is to handle unpushed commits other than the one we just did.
		// Doesn't matter that we're not synced. Because our local branch is always based on the remote.
		TArray<FString> CommittedFiles;
		FString BranchName;
		bool bDiffSuccess;
		if (Helpers.GetRemoteBranchName(BranchName))
		{
			bDiffSuccess = Helpers.RunDiff({ "--name-only", FString::Printf(TEXT("%s...HEAD"), *BranchName), "--" }, CommittedFiles, Command.ResultInfo.ErrorMessages);
		}
		else
		{
			// Get all non-remote commits and list out their files
			bDiffSuccess = Helpers.GetLog({"--branches", "--not" "--remotes", "--name-only", "--pretty="}, {}, CommittedFiles, Command.ResultInfo.ErrorMessages);

			// Dedup files list between commits
			CommittedFiles = TSet<FString>{ CommittedFiles }.Array();
		}

		bool bUnpushedFiles = true;
		TSet<FString> FilesToCheckIn{ Command.Files };
		if (bDiffSuccess)
		{
			// Only push if we have a difference (any commits at all, not just the one we just did)
			bUnpushedFiles = CommittedFiles.Num() > 0;
			CommittedFiles = FGitLFSSourceControlUtils::AbsoluteFilenames(CommittedFiles, Command.PathToRepositoryRoot);
			FilesToCheckIn.Append(CommittedFiles.FilterByPredicate(FGitLFSCommandHelpers::IsFileLFSLockable));
		}

		TArray<FString> PulledFiles;

		// If we have unpushed files, push
		if (bUnpushedFiles)
		{
			// TODO: configure remote
			Command.bCommandSuccessful = Helpers.RunPush({ TEXT("-u"), TEXT("origin"), TEXT("HEAD") }, Command.ResultInfo.InfoMessages, Command.ResultInfo.ErrorMessages);

			if (!Command.bCommandSuccessful)
			{
				// if out of date, pull first, then try again
				bool bWasOutOfDate = false;
				for (const FString& PushError : Command.ResultInfo.ErrorMessages)
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
					if (Helpers.FetchRemote(false,  Command.ResultInfo.InfoMessages, Command.ResultInfo.ErrorMessages))
					{
						// Update local with latest
						if (Helpers.PullOrigin({}, PulledFiles, Command.ResultInfo.InfoMessages, Command.ResultInfo.ErrorMessages))
						{
							Command.bCommandSuccessful = Helpers.RunPush({ TEXT("-u"), TEXT("origin"), TEXT("HEAD") }, Command.ResultInfo.InfoMessages, Command.ResultInfo.ErrorMessages);
						}
					}

					// Our push still wasn't successful
					if (!Command.bCommandSuccessful &&
						!Provider->bPendingRestart)
					{
						// If it fails, just let the user do it
						const FText PushFailMessage(LOCTEXT("GitPush_OutOfDate_Msg",
							"Git Push failed because there are changes you need to pull.\n\n"
							"An attempt was made to pull, but failed, because while the Unreal Editor is "
							"open, files cannot always be updated.\n\n"
							"Please exit the editor, and update the project again."));
						const FText PushFailTitle(LOCTEXT("GitPush_OutOfDate_Title", "Git Pull Required"));
						FMessageDialog::Open(EAppMsgType::Ok, PushFailMessage, GIT_UE_503_SWITCH(&PushFailTitle, PushFailTitle));
						UE_LOG(LogSourceControl, Log, TEXT("Push failed because we're out of date, prompting user to resolve manually"));
					}
				}
			}
		}
		else
		{
			Command.bCommandSuccessful = true;
		}

		// git-lfs: unlock files
		if (Command.bUsingGitLfsLocking)
		{
			// If we successfully pushed (or didn't need to push), unlock the files marked for check in
			if (Command.bCommandSuccessful)
			{
				// unlock files: execute the LFS command on relative filenames
				// (unlock only locked files, that is, not Added files)
				Helpers.UnlockFiles(FGitLFSSourceControlUtils::GetLockedFiles(FilesToCheckIn.Array()), true, Command.ResultInfo.InfoMessages, Command.ResultInfo.ErrorMessages);
			}
		}

		// Collect all the files we touched through the pull update
		if (bUnpushedFiles &&
			PulledFiles.Num())
		{
			FilesToCheckIn.Append(PulledFiles);
		}

		// Before, we added only lockable files from CommittedFiles. But now, we want to update all files, not just lockables.
		FilesToCheckIn.Append(CommittedFiles);

		// now update the status of our files
		FGitLFSSourceControlUtils::RunUpdateStatus(Command, FilesToCheckIn.Array(), Command.ResultInfo.ErrorMessages, States);

		return Command.bCommandSuccessful;
	}

	Command.bCommandSuccessful = false;

	return false;
}

FText FGitLFSCheckInWorker::ParseCommitResults(const TArray<FString>& InResults)
{
	if (InResults.Num() >= 1)
	{
		const FString& FirstLine = InResults[0];
		return FText::Format(LOCTEXT("CommitMessage", "Commited {0}."), FText::FromString(FirstLine));
	}

	return LOCTEXT("CommitMessageUnknown", "Submitted revision.");
}

#undef LOCTEXT_NAMESPACE
