// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#include "GitLFSMoveToChangelist.h"

#include "GitLFSCommandHelpers.h"
#include "GitLFSSourceControlUtils.h"
#include "GitLFSSourceControlCommand.h"
#include "GitLFSSourceControlModule.h"

#define LOCTEXT_NAMESPACE "GitSourceControl"

bool FGitLFSMoveToChangelistWorker::Execute(FGitLFSSourceControlCommand& Command, FGitLFSCommandHelpers& Helpers)
{
	if (Command.Files.Num() == 0)
	{
		return true;
	}

	const FGitLFSSourceControlChangelist& DestChangelist = Command.Changelist;

	bool bResult = false;
	if (DestChangelist.GetName().Equals(TEXT("Staged")))
	{
		bResult = Helpers.RunAdd(false, Command.Files, Command.ResultInfo.InfoMessages, Command.ResultInfo.ErrorMessages);
	}
	else if (DestChangelist.GetName().Equals(TEXT("Working")))
	{
		bResult = Helpers.RunRestore(true, Command.Files, Command.ResultInfo.InfoMessages, Command.ResultInfo.ErrorMessages);
	}
	
	if (bResult)
	{
		FGitLFSSourceControlUtils::RunUpdateStatus(Command, Command.Files, Command.ResultInfo.ErrorMessages, States);
	}

	return bResult;
}

#undef LOCTEXT_NAMESPACE
