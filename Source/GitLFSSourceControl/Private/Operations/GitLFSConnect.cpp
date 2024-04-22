// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#include "GitLFSConnect.h"

#include "GitLFSCommand.h"
#include "GitLFSCommandHelpers.h"
#include "SourceControlOperations.h"
#include "GitLFSSourceControlUtils.h"
#include "GitLFSSourceControlCommand.h"

#define LOCTEXT_NAMESPACE "GitSourceControl"

bool FGitLFSConnectWorker::Execute(FGitLFSSourceControlCommand& Command, FGitLFSCommandHelpers& Helpers)
{
	// The connect worker checks if we are connected to the remote server.
	FConnect& Operation = Command.GetOperation<FConnect>();

	// Skip login operations, since Git does not have to login.
	// It's not a big deal for async commands though, so let those go through.
	// More information: this is a heuristic for cases where UE is trying to create
	// a valid Perforce connection as a side effect for the connect worker. For Git,
	// the connect worker has no side effects. It is simply a query to retrieve information
	// to be displayed to the user, like in the revision control settings or on init.
	// Therefore, there is no need for synchronously establishing a connection if not there.
	if (Command.Concurrency == EConcurrency::Synchronous)
	{
		Command.bCommandSuccessful = true;
		return true;
	}

	// Check Git availability
	// We already know that Git is available if PathToGitBinary is not empty, since it is validated then.
	if (Command.PathToGitBinary.IsEmpty())
	{
		const FText& NotFound = LOCTEXT("GitNotFound", "Failed to enable Git revision control. You need to install Git and ensure the plugin has a valid path to the git executable.");
		Command.ResultInfo.ErrorMessages.Add(NotFound.ToString());
		Operation.SetErrorText(NotFound);
		Command.bCommandSuccessful = false;
		return false;
	}

	// Get default branch: git remote show

	// Check if remote matches our refs.
	// Could be useful in the future, but all we want to know right now is if connection is up.
	// Parameters.Add("--exit-code");
	Command.bCommandSuccessful = Helpers.RunLSRemote(false, true);

	if (!Command.bCommandSuccessful)
	{
		const FText& NotFound = LOCTEXT("GitRemoteFailed", "Failed Git remote connection. Ensure your repo is initialized, and check your connection to the Git host.");
		Command.ResultInfo.ErrorMessages.Add(NotFound.ToString());
		Operation.SetErrorText(NotFound);
	}

	// TODO: always return true, and enter an offline mode if could not connect to remote
	return Command.bCommandSuccessful;
}

#undef LOCTEXT_NAMESPACE