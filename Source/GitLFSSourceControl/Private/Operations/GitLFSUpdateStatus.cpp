// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#include "GitLFSUpdateStatus.h"
#include "SourceControlOperations.h"
#include "GitLFSSourceControlUtils.h"
#include "GitLFSSourceControlCommand.h"
#include "GitLFSSourceControlModule.h"

#define LOCTEXT_NAMESPACE "GitSourceControl"

bool FGitLFSUpdateStatusWorker::Execute(FGitLFSSourceControlCommand& Command, FGitLFSCommandHelpers& Helpers)
{
	const FUpdateStatus& Operation = Command.GetOperation<FUpdateStatus>();

	Command.bCommandSuccessful = true;

	if (Command.Files.Num() > 0)
	{
		TMap<FString, FGitLFSSourceControlState> UpdatedStates;
		Command.bCommandSuccessful = FGitLFSSourceControlUtils::RunUpdateStatus(Command, Command.Files, Command.ResultInfo.ErrorMessages, States, &UpdatedStates);
		if (Command.bCommandSuccessful &&
			Operation.ShouldUpdateHistory())
		{
			for (const auto& It : UpdatedStates)
			{
				const FString& File = It.Key;
				TArray<TSharedRef<FGitLFSSourceControlRevision>> History;

				if (It.Value.IsConflicted())
				{
					// In case of a merge conflict, we first need to get the tip of the "remote branch" (MERGE_HEAD)
					FGitLFSSourceControlUtils::GetHistory(Command, File, true, Command.ResultInfo.ErrorMessages, History);
				}
				// Get the history of the file in the current branch
				Command.bCommandSuccessful &= FGitLFSSourceControlUtils::GetHistory(Command, File, false, Command.ResultInfo.ErrorMessages, History);
				Histories.Add(*File, History);
			}
		}
	}
	else if (Command.Ignoredfiles.Num() == 0)
	{
		// no path provided: only update the status of assets in Content/ directory and also Config files
		const TArray<FString> ProjectDirs
		{
			FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()),
			FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir()),
			FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath())
		};

		Command.bCommandSuccessful = FGitLFSSourceControlUtils::RunUpdateStatus(Command, ProjectDirs, Command.ResultInfo.ErrorMessages, States);
	}

	Helpers.GetCommitInfo(Command.CommitId, Command.CommitSummary);

	// don't use the ShouldUpdateModifiedState() hint here as it is specific to Perforce: the above normal Git status has already told us this information (like Git and Mercurial)

	return Command.bCommandSuccessful;
}

bool FGitLFSUpdateStatusWorker::UpdateStates() const
{
	FGitLFSSourceControlModule& GitSourceControl = FGitLFSSourceControlModule::Get();
	const TSharedPtr<FGitLFSSourceControlProvider>& Provider = GitSourceControl.GetProvider();
	if (!ensure(Provider))
	{
		return false;
	}

	bool bUpdated = FGitLFSSourceControlUtils::UpdateCachedStates(States);

	const bool bUsingGitLfsLocking = Provider->UsesCheckout();

	// TODO without LFS : Workaround a bug with the Source Control Module not updating file state after a simple "Save" with no "Checkout" (when not using File Lock)
	const FDateTime Now = bUsingGitLfsLocking ? FDateTime::Now() : FDateTime::MinValue();

	// add history, if any
	for (const auto& It : Histories)
	{
		const TSharedRef<FGitLFSSourceControlState> State = Provider->GetStateInternal(It.Key);
		State->History = It.Value;
		State->TimeStamp = Now;
		bUpdated = true;
	}

	return bUpdated;
}

#undef LOCTEXT_NAMESPACE
