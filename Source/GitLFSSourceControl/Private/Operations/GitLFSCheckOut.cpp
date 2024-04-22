// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#include "GitLFSCheckOut.h"

#include "GitLFSCommand.h"
#include "GitLFSCommandHelpers.h"
#include "GitLFSSourceControlUtils.h"
#include "GitLFSSourceControlCommand.h"
#include "GitLFSSourceControlModule.h"
#include "Data/GitLFSLockedFilesCache.h"

#define LOCTEXT_NAMESPACE "GitSourceControl"

bool FGitLFSCheckOutWorker::Execute(FGitLFSSourceControlCommand& Command, FGitLFSCommandHelpers& Helpers)
{
	// If we have nothing to process, exit immediately
	if (Command.Files.Num() == 0)
	{
		return true;
	}

	if (!Command.bUsingGitLfsLocking)
	{
		Command.bCommandSuccessful = false;
		return Command.bCommandSuccessful;
	}

	const TSharedPtr<FGitLFSSourceControlProvider> Provider = FGitLFSSourceControlModule::Get().GetProvider();
	if (!ensure(Provider))
	{
		Command.bCommandSuccessful = false;
		return Command.bCommandSuccessful;
	}

	// lock files: execute the LFS command on relative filenames
	const TArray<FString>& RelativeFiles = FGitLFSSourceControlUtils::RelativeFilenames(Command.Files, Command.PathToGitRoot);

	{
		const TArray<FString>& LockableRelativeFiles = RelativeFiles.FilterByPredicate(FGitLFSCommandHelpers::IsFileLFSLockable);

		if (LockableRelativeFiles.Num() < 1)
		{
			Command.bCommandSuccessful = true;
			return Command.bCommandSuccessful;
		}

		Command.bCommandSuccessful = Helpers.LockFiles(LockableRelativeFiles, Command.ResultInfo.InfoMessages, Command.ResultInfo.ErrorMessages);
	}

	const FString& LockUser = Provider->GetLockUser();
	if (!Command.bCommandSuccessful)
	{
		return false;
	}

	TArray<FString> AbsoluteFiles;
	for (const FString& RelativeFile : RelativeFiles)
	{
		FString AbsoluteFile = FPaths::Combine(Command.PathToGitRoot, RelativeFile);
		FGitLFSLockedFilesCache::AddLockedFile(AbsoluteFile, LockUser);
		FPaths::NormalizeFilename(AbsoluteFile);
		AbsoluteFiles.Add(AbsoluteFile);
	}

	FGitLFSSourceControlUtils::CollectNewStates(AbsoluteFiles, States, EGitLFSFileState::Unset, EGitLFSTreeState::Unset, EGitLFSLockState::Locked);
	for (auto& It : States)
	{
		It.Value.LockUser = LockUser;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE