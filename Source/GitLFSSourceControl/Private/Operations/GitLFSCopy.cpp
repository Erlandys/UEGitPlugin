// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#include "GitLFSCopy.h"

#include "GitLFSCommand.h"
#include "GitLFSCommandHelpers.h"
#include "SourceControlOperations.h"
#include "GitLFSSourceControlUtils.h"
#include "GitLFSSourceControlCommand.h"

#define LOCTEXT_NAMESPACE "GitSourceControl"

bool FGitLFSCopyWorker::Execute(FGitLFSSourceControlCommand& Command, FGitLFSCommandHelpers& Helpers)
{
	if (Command.Files.Num() == 0)
	{
		return true;
	}

	// Copy or Move operation on a single file : Git does not need an explicit copy nor move,
	// but after a Move the Editor create a redirector file with the old asset name that points to the new asset.
	// The redirector needs to be committed with the new asset to perform a real rename.
	// => the following is to "MarkForAdd" the redirector, but it still need to be committed by selecting the whole directory and "check-in"

	Command.bCommandSuccessful = Helpers.RunAdd(false, Command.Files, Command.ResultInfo.InfoMessages, Command.ResultInfo.ErrorMessages);

	if (Command.bCommandSuccessful)
	{
		FGitLFSSourceControlUtils::CollectNewStates(Command.Files, States, EGitLFSFileState::Added, EGitLFSTreeState::Staged);
		return true;
	}

	FGitLFSSourceControlUtils::RunUpdateStatus(Command, Command.Files, Command.ResultInfo.ErrorMessages, States);

	return false;
}

#undef LOCTEXT_NAMESPACE