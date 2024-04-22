// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#include "GitLFSDelete.h"

#include "GitLFSCommandHelpers.h"
#include "SourceControlOperations.h"
#include "GitLFSSourceControlUtils.h"
#include "GitLFSSourceControlCommand.h"
#include "GitLFSSourceControlModule.h"

#define LOCTEXT_NAMESPACE "GitSourceControl"

bool FGitLFSDeleteWorker::Execute(FGitLFSSourceControlCommand& Command, FGitLFSCommandHelpers& Helpers)
{
	// If we have nothing to process, exit immediately
	if (Command.Files.Num() == 0)
	{
		return true;
	}

	Command.bCommandSuccessful = Helpers.RunRemove(Command.Files, Command.ResultInfo.InfoMessages, Command.ResultInfo.ErrorMessages);

	if (Command.bCommandSuccessful)
	{
		FGitLFSSourceControlUtils::CollectNewStates(Command.Files, States, EGitLFSFileState::Deleted, EGitLFSTreeState::Staged);
	}
	else
	{
		FGitLFSSourceControlUtils::RunUpdateStatus(Command, Command.Files, Command.ResultInfo.ErrorMessages, States);
	}

	return Command.bCommandSuccessful;
}

#undef LOCTEXT_NAMESPACE
