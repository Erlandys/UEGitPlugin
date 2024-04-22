// Copyright (c) 2014-2022 Sebastien Rombauts (sebastien.rombauts@gmail.com)

#include "GitLFSSourceControlConsole.h"
#include "GitLFSCommand.h"
#include "GitLFSSourceControlModule.h"
#include "HAL/IConsoleManager.h"
#include "ISourceControlModule.h"

// Auto-registered console commands:
// No re-register on hot reload, and unregistered only once on editor shutdown.
static FAutoConsoleCommand GExecuteGitConsoleCommand(TEXT("git"),
	TEXT("Git Command Line Interface.\n")
	TEXT("Run any 'git' command directly from the Unreal Editor Console.\n")
	TEXT("Type 'git help' to get a command list."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&FGitLFSSourceControlConsole::ExecuteGitConsoleCommand));

void FGitLFSSourceControlConsole::ExecuteGitConsoleCommand(const TArray<FString>& Args)
{
	FGitLFSSourceControlModule& Module = FGitLFSSourceControlModule::Get();
	const TSharedPtr<FGitLFSSourceControlProvider> Provider = Module.GetProvider();
	if (!Provider)
	{
		return;
	}

	const FString& PathToGitBinary = Module.GetSettings().GetBinaryPath();
	const FString& RepositoryRoot = Provider->GetPathToRepositoryRoot();

	// The first argument is the command to send to git, the following ones are forwarded as parameters for the command
	TArray<FString> Parameters = Args;
	FString Command;
	if (Args.Num() > 0)
	{
		Command = Args[0];
		Parameters.RemoveAt(0);
	}
	else
	{
		// If no command is provided, use "help" to emulate the behavior of the git CLI
		Command = TEXT("help");
	}

	FString Output;

	RUN_GIT_COMMAND("")
	.Command(Command)
	.RepositoryRoot(RepositoryRoot)
	.PathToGit(PathToGitBinary)
	.Parameters(Parameters)
	.ResultString(Output);

	UE_LOG(LogSourceControl, Log, TEXT("Output:\n%s"), *Output);
}
