// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#include "GitLFSSync.h"

#include "GitLFSCommandHelpers.h"
#include "GitLFSSourceControlUtils.h"
#include "GitLFSSourceControlCommand.h"

#define LOCTEXT_NAMESPACE "GitSourceControl"

bool FGitLFSSyncWorker::Execute(FGitLFSSourceControlCommand& Command, FGitLFSCommandHelpers& Helpers)
{
	TArray<FString> Results;
	if (!Helpers.FetchRemote(false, Command.ResultInfo.InfoMessages, Command.ResultInfo.ErrorMessages))
	{
		return false;
	}

	if (Command.Files.Num() == 0)
	{
		return true;
	}

	Command.bCommandSuccessful = Helpers.PullOrigin(Command.Files, Command.Files, Results, Command.ResultInfo.ErrorMessages);

	// now update the status of our files
	FGitLFSSourceControlUtils::RunUpdateStatus(Command, Command.Files, Command.ResultInfo.ErrorMessages, States);
	Helpers.GetCommitInfo(Command.CommitId, Command.CommitSummary);

	return Command.bCommandSuccessful;
}

#undef LOCTEXT_NAMESPACE