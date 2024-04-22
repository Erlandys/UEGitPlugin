// Fill out your copyright notice in the Description page of Project Settings.

#include "GitLFSCommand.h"

#include "Interfaces/IPluginManager.h"

bool FGitLFSCommand::Run(FArguments& Args)
{
	return BatchRuns(Args);
}

#ifndef GIT_USE_CUSTOM_LFS
#define GIT_USE_CUSTOM_LFS 1
#endif

bool FGitLFSCommand::RunLFS(FArguments& Args)
{
#if GIT_USE_CUSTOM_LFS
	FString BaseDir = FGitLFSSourceControlModule::GetPlugin()->GetBaseDir();
#if PLATFORM_WINDOWS
	Args.InternalPathToGit = FString::Printf(TEXT("%s/git-lfs.exe"), *BaseDir);
#elif PLATFORM_MAC
#if ENGINE_MAJOR_VERSION >= 5
#if PLATFORM_MAC_ARM64
	Args.InternalPathToGit = FString::Printf(TEXT("%s/git-lfs-mac-arm64"), *BaseDir);
#else
	Args.InternalPathToGit = FString::Printf(TEXT("%s/git-lfs-mac-amd64"), *BaseDir);
#endif
#else
	Args.InternalPathToGit = FString::Printf(TEXT("%s/git-lfs-mac-amd64"), *BaseDir);
#endif
#elif PLATFORM_LINUX
	Args.InternalPathToGit = FString::Printf(TEXT("%s/git-lfs"), *BaseDir);
#else
	ensureMsgf(false, TEXT("Unhandled platform for LFS binary!"));
	Args.InternalCommand = TEXT("lfs ") + Args.InternalCommand;
#endif
#else
	Args.InternalCommand = TEXT("lfs ") + Args.InternalCommand;
#endif

	return BatchRuns(Args);
}

bool FGitLFSCommand::BatchRuns( FArguments& Args)
{
	constexpr int32 MaxFilesPerBatch = 50;

	if (Args.InternalFiles.Num() <= MaxFilesPerBatch)
	{
		return RunImpl(Args);
	}

	bool bResult = true;

	// Batch files up so we don't exceed command-line limits
	int32 FileCount = 0;
	while (FileCount < Args.InternalFiles.Num())
	{
		TArray<FString> FilesInBatch;
		for (int32 Index = 0; FileCount < Args.InternalFiles.Num() && Index < MaxFilesPerBatch; Index++, FileCount++)
		{
			FilesInBatch.Add(Args.InternalFiles[FileCount]);
		}

		FArguments BatchArgs = Args;
		BatchArgs.InternalFiles = FilesInBatch;
		bResult &= RunImpl(BatchArgs);
	}
	return bResult;
}

bool FGitLFSCommand::RunImpl(FArguments& Args)
{
	int32 ReturnCode = 0;
	FString FullCommand;
	FString LogableCommand; // short version of the command for logging purpose

	if (!Args.InternalRepositoryRoot.IsEmpty())
	{
		FString RepositoryRoot = Args.InternalRepositoryRoot;

		// Detect a "migrate asset" scenario (a "git add" command is applied to files outside the current project)
		if (Args.InternalFiles.Num() > 0 &&
			!FPaths::IsRelative(Args.InternalFiles[0]) &&
			!Args.InternalFiles[0].StartsWith(Args.InternalRepositoryRoot))
		{
			// in this case, find the git repository (if any) of the destination Project
			FString DestinationRepositoryRoot;
			if (FGitLFSSourceControlUtils::FindRootDirectory(FPaths::GetPath(Args.InternalFiles[0]), DestinationRepositoryRoot))
			{
				// if found use it for the "add" command (else not, to avoid producing one more error in logs)
				RepositoryRoot = DestinationRepositoryRoot;
			}
		}

		// Specify the working copy (the root) of the git repository (before the command itself)
		FullCommand = TEXT("-C \"");
		FullCommand += RepositoryRoot;
		FullCommand += TEXT("\" ");
	}
	// then the git command itself ("status", "log", "commit"...)
	LogableCommand += Args.InternalCommand;

	// Append to the command all parameters, and then finally the files
	for (const FString& Parameter : Args.InternalParameters)
	{
		LogableCommand += TEXT(" ");
		LogableCommand += Parameter;
	}
	for (const FString& File : Args.InternalFiles)
	{
		LogableCommand += TEXT(" \"");
		LogableCommand += File;
		LogableCommand += TEXT("\"");
	}
	// Also, Git does not have a "--non-interactive" option, as it auto-detects when there are no connected standard input/output streams

	FullCommand += LogableCommand;

#if UE_BUILD_DEBUG
	UE_LOG(LogSourceControl, Log, TEXT("RunCommand: 'git %s'"), *LogableCommand);
#endif

	FString PathToGitOrEnvBinary = Args.InternalPathToGit;
#if PLATFORM_MAC
	// The Cocoa application does not inherit shell environment variables, so add the path expected to have git-lfs to PATH
	const FString PathEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("PATH"));
	const FString GitInstallPath = FPaths::GetPath(Args.InternalPathToGit);

	TArray<FString> PathArray;
	PathEnv.ParseIntoArray(PathArray, FPlatformMisc::GetPathVarDelimiter());
	bool bHasGitInstallPath = false;
	for (const FString& Path : PathArray)
	{
		if (GitInstallPath.Equals(Path, ESearchCase::CaseSensitive))
		{
			bHasGitInstallPath = true;
			break;
		}
	}

	if (!bHasGitInstallPath)
	{
		PathToGitOrEnvBinary = FString("/usr/bin/env");
		FullCommand = FString::Printf(TEXT("PATH=\"%s%s%s\" \"%s\" %s"), *GitInstallPath, FPlatformMisc::GetPathVarDelimiter(), *PathEnv, *Args.InternalPathToGit, *FullCommand);
	}
#endif

	FString ResultString;
	FString ErrorString;
	FPlatformProcess::ExecProcess(*PathToGitOrEnvBinary, *FullCommand, &ReturnCode, &ResultString, &ErrorString);

#if UE_BUILD_DEBUG
	// TODO: add a setting to easily enable Verbose logging
	UE_LOG(LogSourceControl, Verbose, TEXT("RunCommand(%s):\n%s"), *Args.InternalCommand, *ResultString);
	if (ReturnCode != Args.InternalExpectedReturnCode)
	{
		UE_LOG(LogSourceControl, Warning, TEXT("RunCommand(%s) ReturnCode=%d:\n%s"), *Args.InternalCommand, ReturnCode, *ErrorString);
	}
#endif

	// Move push/pull progress information from the error stream to the info stream
	if (ReturnCode == Args.InternalExpectedReturnCode &&
		ErrorString.Len() > 0)
	{
		ResultString.Append(ErrorString);
		ErrorString = {};
	}

	if (Args.InternalReturnCode)
	{
		*Args.InternalReturnCode = ReturnCode;
	}

	if (Args.InternalResults)
	{
		TArray<FString> Results;
		ResultString.ParseIntoArray(Results, TEXT("\n"), true);
		Args.InternalResults->Append(Results);
	}

	if (Args.InternalErrors)
	{
		TArray<FString> Errors;
		ErrorString.ParseIntoArray(Errors, TEXT("\n"), true);
		Args.InternalErrors->Append(Errors);
	}

	if (Args.InternalResultString)
	{
		*Args.InternalResultString = ResultString;
	}

	return ReturnCode == Args.InternalExpectedReturnCode;
}
