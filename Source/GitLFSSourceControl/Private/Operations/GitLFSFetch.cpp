// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#include "GitLFSFetch.h"

#include "GitLFSCommandHelpers.h"
#include "SourceControlOperations.h"
#include "GitLFSSourceControlUtils.h"
#include "GitLFSSourceControlCommand.h"
#include "GitLFSSourceControlModule.h"

#define LOCTEXT_NAMESPACE "GitSourceControl"

bool FGitLFSFetchWorker::Execute(FGitLFSSourceControlCommand& Command, FGitLFSCommandHelpers& Helpers)
{
	Command.bCommandSuccessful = Helpers.FetchRemote(Command.bUsingGitLfsLocking, Command.ResultInfo.InfoMessages, Command.ResultInfo.ErrorMessages);
	if (!Command.bCommandSuccessful)
	{
		return false;
	}

	const FGitLFSFetchOperation& Operation = Command.GetOperation<FGitLFSFetchOperation>();
	if (Operation.bUpdateStatus)
	{
		// Now update the status of all our files
		const TArray<FString> ProjectDirs
		{
			FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()),
			FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir()),
			FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath())
		};

		FGitLFSSourceControlUtils::RunUpdateStatus(Command, ProjectDirs, Command.ResultInfo.ErrorMessages, States);
	}

	return Command.bCommandSuccessful;
}

#undef LOCTEXT_NAMESPACE
