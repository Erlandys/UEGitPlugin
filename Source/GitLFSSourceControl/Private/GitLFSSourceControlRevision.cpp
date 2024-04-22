// Copyright (c) 2014-2023 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#include "GitLFSSourceControlRevision.h"
#include "ISourceControlModule.h"
#include "GitLFSSourceControlModule.h"

bool FGitLFSSourceControlRevision::Get(FString& InOutFilename GIT_UE_500_ONLY(, const EConcurrency::Type InConcurrency)) const
{
#if GIT_ENGINE_VERSION >= 500
	if (InConcurrency != EConcurrency::Synchronous)
	{
		UE_LOG(LogSourceControl, Warning, TEXT("Only EConcurrency::Synchronous is tested/supported for this operation."));
	}
#endif

	const FGitLFSSourceControlModule* Module = FGitLFSSourceControlModule::GetThreadSafe();
	if (!Module)
	{
		return false;
	}

	const TSharedPtr<FGitLFSSourceControlProvider>& Provider = Module->GetProvider();
	if (!ensure(Provider))
	{
		return false;
	}

	// if a filename for the temp file wasn't supplied generate a unique-ish one
	if (InOutFilename.IsEmpty())
	{
		// create the diff dir if we don't already have it (Git wont)
		IFileManager::Get().MakeDirectory(*FPaths::DiffDir(), true);

		// create a unique temp file name based on the unique commit Id
		const FString TempFileName = FString::Printf(TEXT("%stemp-%s-%s"), *FPaths::DiffDir(), *CommitId, *FPaths::GetCleanFilename(Filename));
		InOutFilename = FPaths::ConvertRelativePathToFull(TempFileName);
	}

	// Diff against the revision
	const FString Parameter = FString::Printf(TEXT("%s:%s"), *CommitId, *Filename);

	if (FPaths::FileExists(InOutFilename))
	{
		return true;
	}

	return
		RunDumpToFile(
			Provider->GetGitBinaryPath(),
			PathToRepoRoot.IsEmpty() ? Provider->GetPathToRepositoryRoot() : PathToRepoRoot,
			Parameter,
			InOutFilename);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

bool FGitLFSSourceControlRevision::RunDumpToFile(
	const FString& InPathToGitBinary,
	const FString& InRepositoryRoot,
	const FString& InParameter,
	const FString& InDumpFileName)
{
	int32 ReturnCode = -1;
	FString FullCommand;

	if (!InRepositoryRoot.IsEmpty())
	{
		// Specify the working copy (the root) of the git repository (before the command itself)
		FullCommand = TEXT("-C \"");
		FullCommand += InRepositoryRoot;
		FullCommand += TEXT("\" ");
	}

	// then the git command itself
	// Newer versions (2.9.3.windows.2) support smudge/clean filters used by Git LFS, git-fat, git-annex, etc
	FullCommand += TEXT("cat-file --filters ");

	// Append to the command the parameter
	FullCommand += InParameter;

	const bool bLaunchDetached = false;
	const bool bLaunchHidden = true;
	const bool bLaunchReallyHidden = bLaunchHidden;

	void* PipeRead = nullptr;
	void* PipeWrite = nullptr;

	verify(FPlatformProcess::CreatePipe(PipeRead, PipeWrite));

	UE_LOG(LogSourceControl, Log, TEXT("RunDumpToFile: 'git %s'"), *FullCommand);

	FString PathToGitOrEnvBinary = InPathToGitBinary;
#if PLATFORM_MAC
	// The Cocoa application does not inherit shell environment variables, so add the path expected to have git-lfs to PATH
	const FString PathEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("PATH"));
	const FString GitInstallPath = FPaths::GetPath(InPathToGitBinary);

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
		FullCommand = FString::Printf(TEXT("PATH=\"%s%s%s\" \"%s\" %s"), *GitInstallPath, FPlatformMisc::GetPathVarDelimiter(), *PathEnv, *InPathToGitBinary, *FullCommand);
	}
#endif

#if GIT_ENGINE_VERSION >= 500 && 0
	FProcHandle ProcessHandle = FPlatformProcess::CreateProc(*PathToGitOrEnvBinary, *FullCommand, bLaunchDetached, bLaunchHidden, bLaunchReallyHidden, nullptr, 0, *InRepositoryRoot, PipeWrite, nullptr, nullptr);
#else
	FProcHandle ProcessHandle = FPlatformProcess::CreateProc(*PathToGitOrEnvBinary, *FullCommand, bLaunchDetached, bLaunchHidden, bLaunchReallyHidden, nullptr, 0, *InRepositoryRoot, PipeWrite);
#endif

	ON_SCOPE_EXIT
	{
		FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
	};

	if (!ProcessHandle.IsValid())
	{
		UE_LOG(LogSourceControl, Error, TEXT("Failed to launch 'git cat-file'"));
	}

	FPlatformProcess::Sleep(0.01f);

	TArray<uint8> BinaryFileContent;
	bool bRemovedLFSMessage = false;

	while (FPlatformProcess::IsProcRunning(ProcessHandle))
	{
		TArray<uint8> BinaryData;
		FPlatformProcess::ReadPipeToArray(PipeRead, BinaryData);
		if (BinaryData.Num() > 0)
		{
			// @todo: this is hacky!
			if (BinaryData[0] == 68) // Check for D in "Downloading"
			{
				if (BinaryData[BinaryData.Num() - 1] == 10) // Check for newline
				{
					BinaryData.Reset();
					bRemovedLFSMessage = true;
				}
			}
			else
			{
				BinaryFileContent.Append(MoveTemp(BinaryData));
			}
		}
	}

	TArray<uint8> BinaryData;
	FPlatformProcess::ReadPipeToArray(PipeRead, BinaryData);
	if (BinaryData.Num() > 0)
	{
		// @todo: this is hacky!
		if (!bRemovedLFSMessage && BinaryData[0] == 68) // Check for D in "Downloading"
		{
			int32 NewLineIndex = 0;
			for (int32 Index = 0; Index < BinaryData.Num(); Index++)
			{
				if (BinaryData[Index] == 10) // Check for newline
				{
					NewLineIndex = Index;
					break;
				}
			}
			if (NewLineIndex > 0)
			{
				BinaryData.RemoveAt(0, NewLineIndex + 1);
			}
		}
		else
		{
			BinaryFileContent.Append(MoveTemp(BinaryData));
		}
	}

	FPlatformProcess::GetProcReturnCode(ProcessHandle, &ReturnCode);
	if (ReturnCode == 0)
	{
		// Save buffer into temp file
		if (FFileHelper::SaveArrayToFile(BinaryFileContent, *InDumpFileName))
		{
			UE_LOG(LogSourceControl, Log, TEXT("Wrote '%s' (%do)"), *InDumpFileName, BinaryFileContent.Num());
		}
		else
		{
			UE_LOG(LogSourceControl, Error, TEXT("Could not write %s"), *InDumpFileName);
			ReturnCode = -1;
		}
	}
	else
	{
		UE_LOG(LogSourceControl, Error, TEXT("DumpToFile: ReturnCode=%d"), ReturnCode);
	}

	FPlatformProcess::CloseProc(ProcessHandle);

	return ReturnCode == 0;
}
