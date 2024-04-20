// Copyright (c) 2014-2022 Sebastien Rombauts (sebastien.rombauts@gmail.com)

#include "GitLFSSourceControlConsole.h"

#include "HAL/IConsoleManager.h"
#include "ISourceControlModule.h"

#include "GitLFSSourceControlModule.h"
#include "GitLFSSourceControlUtils.h"

// Auto-registered console commands:
// No re-register on hot reload, and unregistered only once on editor shutdown.
static FAutoConsoleCommand g_executeGitConsoleCommand(TEXT("git"),
	TEXT("Git Command Line Interface.\n")
	TEXT("Run any 'git' command directly from the Unreal Editor Console.\n")
	TEXT("Type 'git help' to get a command list."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&GitLFSSourceControlConsole::ExecuteGitConsoleCommand));

void GitLFSSourceControlConsole::ExecuteGitConsoleCommand(const TArray<FString>& a_args)
{
	FGitLFSSourceControlModule& GitSourceControl = FModuleManager::LoadModuleChecked<FGitLFSSourceControlModule>("GitLFSSourceControl");
	const FString& PathToGitBinary = GitSourceControl.AccessSettings().GetBinaryPath();
	const FString& RepositoryRoot = GitSourceControl.GetProvider().GetPathToRepositoryRoot();

	// The first argument is the command to send to git, the following ones are forwarded as parameters for the command
	TArray<FString> Parameters = a_args;
	FString Command;
	if (a_args.Num() > 0)
	{
		Command = a_args[0];
		Parameters.RemoveAt(0);
	}
	else
	{
		// If no command is provided, use "help" to emulate the behavior of the git CLI
		Command = TEXT("help");
	}

	FString Results;
	FString Errors;
	GitLFSSourceControlUtils::RunCommandInternalRaw(Command, PathToGitBinary, RepositoryRoot, Parameters, TArray<FString>(), Results, Errors);

	UE_LOG(LogSourceControl, Log, TEXT("Output:\n%s"), *Results);
}
