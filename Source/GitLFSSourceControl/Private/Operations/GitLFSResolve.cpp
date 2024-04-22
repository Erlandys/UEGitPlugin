// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#include "GitLFSResolve.h"

#include "GitLFSCommandHelpers.h"
#include "GitLFSSourceControlUtils.h"
#include "GitLFSSourceControlCommand.h"
#include "GitLFSSourceControlModule.h"

#define LOCTEXT_NAMESPACE "GitSourceControl"

bool FGitLFSResolveWorker::Execute(FGitLFSSourceControlCommand& Command, FGitLFSCommandHelpers& Helpers)
{
	if (Command.Files.Num() == 0)
	{
		return true;
	}

	// mark the conflicting files as resolved:
	TArray<FString> Results;
	Command.bCommandSuccessful = Helpers.RunAdd(false, Command.Files, Results, Command.ResultInfo.ErrorMessages);

	// now update the status of our files
	FGitLFSSourceControlUtils::RunUpdateStatus(Command, Command.Files, Command.ResultInfo.ErrorMessages, States);

	return Command.bCommandSuccessful;
}

#undef LOCTEXT_NAMESPACE
