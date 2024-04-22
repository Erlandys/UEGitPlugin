// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#include "GitLFSRevert.h"

#include "GitLFSCommandHelpers.h"
#include "SourceControlOperations.h"
#include "GitLFSSourceControlUtils.h"
#include "GitLFSSourceControlCommand.h"
#include "GitLFSSourceControlModule.h"

#define LOCTEXT_NAMESPACE "GitSourceControl"

bool FGitLFSRevertWorker::Execute(FGitLFSSourceControlCommand& Command, FGitLFSCommandHelpers& Helpers)
{
	if (Command.Files.Num() == 0 &&
		Command.Ignoredfiles.Num() > 0)
	{
		return true;
	}

	Command.bCommandSuccessful = true;

	// Filter files by status
	TArray<FString> MissingFiles;
	TArray<FString> AllExistingFiles;
	TArray<FString> OtherThanAddedExistingFiles;
	GetMissingVsExistingFiles(Command.Files, MissingFiles, AllExistingFiles, OtherThanAddedExistingFiles);

	if (Command.Files.Num() == 0)
	{
		TArray<FString> Parms;
		Parms.Add(TEXT("--hard"));
		Command.bCommandSuccessful &= Helpers.RunReset(true, Command.ResultInfo.InfoMessages, Command.ResultInfo.ErrorMessages);

		Parms.Reset(2);
		Parms.Add(TEXT("-f")); // force
		Parms.Add(TEXT("-d")); // remove directories
		Command.bCommandSuccessful &= Helpers.RunClean(true, true, Command.ResultInfo.InfoMessages, Command.ResultInfo.ErrorMessages);
	}
	else
	{
		// "Added" files that have been deleted needs to be removed from revision control
		Command.bCommandSuccessful &= Helpers.RunRemove(MissingFiles, Command.ResultInfo.InfoMessages, Command.ResultInfo.ErrorMessages);
		if (AllExistingFiles.Num() > 0)
		{
			// reset and revert any changes already added to the index
			Command.bCommandSuccessful &= Helpers.RunReset(false, Command.ResultInfo.InfoMessages, Command.ResultInfo.ErrorMessages);
			Command.bCommandSuccessful &= Helpers.RunCheckout(AllExistingFiles, Command.ResultInfo.InfoMessages, Command.ResultInfo.ErrorMessages);
		}
		if (OtherThanAddedExistingFiles.Num() > 0)
		{
			// revert any changes in working copy (this would fails if the asset was in "Added" state, since after "reset" it is now "untracked")
			// may need to try a few times due to file locks from prior operations
			bool bCheckoutSuccess = false;
			constexpr int32 Attempts = 10;
			for (int32 Index = 0; Index < Attempts; Index++)
			{
				if (Helpers.RunCheckout(OtherThanAddedExistingFiles, Command.ResultInfo.InfoMessages, Command.ResultInfo.ErrorMessages))
				{
					bCheckoutSuccess = true;
					break;
				}

				FPlatformProcess::Sleep(0.1f);
			}
			
			Command.bCommandSuccessful &= bCheckoutSuccess;
		}
	}

	if (Command.bUsingGitLfsLocking &&
		Command.bCommandSuccessful)
	{
		// unlock files: execute the LFS command on relative filenames
		// (unlock only locked files, that is, not Added files)
		Helpers.UnlockFiles(FGitLFSSourceControlUtils::GetLockedFiles(OtherThanAddedExistingFiles), true, Command.ResultInfo.InfoMessages, Command.ResultInfo.ErrorMessages);
	}

	// If no files were specified (full revert), refresh all relevant files instead of the specified files (which is an empty list in full revert)
	// This is required so that files that were "Marked for add" have their status updated after a full revert.
	TArray<FString> FilesToUpdate = Command.Files;
	if (Command.Files.Num() == 0)
	{
		for (const FString& File : MissingFiles)
		{
			FilesToUpdate.Add(File);
		}
		for (const FString& File : AllExistingFiles)
		{
			FilesToUpdate.Add(File);
		}
		for (const FString& File : OtherThanAddedExistingFiles)
		{
			FilesToUpdate.Add(File);
		}
	}

	// now update the status of our files
	FGitLFSSourceControlUtils::RunUpdateStatus(Command, FilesToUpdate, Command.ResultInfo.ErrorMessages, States);

	return Command.bCommandSuccessful;
}

void FGitLFSRevertWorker::GetMissingVsExistingFiles(
	const TArray<FString>& InFiles,
	TArray<FString>& OutMissingFiles,
	TArray<FString>& OutAllExistingFiles,
	TArray<FString>& OutOtherThanAddedExistingFiles)
{
	FGitLFSSourceControlModule& GitSourceControl = FGitLFSSourceControlModule::Get();
	const TSharedPtr<FGitLFSSourceControlProvider>& Provider = GitSourceControl.GetProvider();
	if (!Provider)
	{
		return;
	}

	const TArray<FString> Files = InFiles.Num() > 0 ? InFiles : Provider->GetFilesInCache();

	TArray<TSharedRef<ISourceControlState>> LocalStates;
	Provider->GetState(Files, LocalStates, EStateCacheUsage::Use);
	for (const TSharedRef<ISourceControlState>& State : LocalStates)
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

#undef LOCTEXT_NAMESPACE
