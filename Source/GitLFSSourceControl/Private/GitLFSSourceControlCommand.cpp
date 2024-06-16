// Copyright (c) 2014-2023 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#include "GitLFSSourceControlCommand.h"

#include "Modules/ModuleManager.h"
#include "GitLFSSourceControlModule.h"
#include "GitLFSSourceControlUtils.h"
#include "ISourceControlModule.h"

FGitLFSSourceControlCommand::FGitLFSSourceControlCommand(
	const TSharedRef<ISourceControlOperation>& Operation,
	const TSharedRef<IGitLFSSourceControlWorker>& InWorker,
	const FSourceControlOperationComplete& InOperationCompleteDelegate)
	: Operation(Operation)
	, Worker(InWorker)
	, OperationCompleteDelegate(InOperationCompleteDelegate)
	, bExecuteProcessed(0)
	, bCancelled(0)
	, bCommandSuccessful(false)
	, Concurrency(EConcurrency::Synchronous)
{
	// cache the providers settings here
	const FGitLFSSourceControlModule& GitSourceControl = FGitLFSSourceControlModule::Get();
	const TSharedPtr<FGitLFSSourceControlProvider>& Provider = GitSourceControl.GetProvider();
	PathToGitBinary = Provider->GetGitBinaryPath();
	bUsingGitLfsLocking = Provider->UsesCheckout();
	PathToRepositoryRoot = Provider->GetPathToRepositoryRoot();
	PathToGitRoot = Provider->GetPathToGitRoot();
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void FGitLFSSourceControlCommand::Abandon()
{
	FPlatformAtomics::InterlockedExchange(&bExecuteProcessed, 1);
}

void FGitLFSSourceControlCommand::DoThreadedWork()
{
	Concurrency = EConcurrency::Asynchronous;
	DoWork();
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void FGitLFSSourceControlCommand::UpdateRepositoryRootIfSubmodule(const TArray<FString>& AbsoluteFilePaths)
{
	FString NewPath = PathToRepositoryRoot;
	// note this is not going to support operations where selected files are in different repositories

	for (const FString& FilePath : AbsoluteFilePaths)
	{
		FString TestPath = FilePath;
		if (!TestPath.StartsWith(PathToRepositoryRoot))
		{
			continue;
		}

		while (!FPaths::IsSamePath(TestPath, PathToRepositoryRoot))
		{
			// Iterating over path directories, looking for .git
			TestPath = FPaths::GetPath(TestPath);

			if (TestPath.IsEmpty())
			{
				// early out if empty directory string to prevent infinite loop
				UE_LOG(LogSourceControl, Error, TEXT("Can't find directory path for file :%s"), *FilePath);
				break;
			}

			const FString GitTestPath = TestPath + "/.git";
			if (FPaths::FileExists(GitTestPath) ||
				FPaths::DirectoryExists(GitTestPath))
			{
				FString RetNormalized = NewPath;
				FPaths::NormalizeDirectoryName(RetNormalized);

				FString PathToRepositoryRootNormalized = PathToRepositoryRoot;
				FPaths::NormalizeDirectoryName(PathToRepositoryRootNormalized);

				if (!FPaths::IsSamePath(RetNormalized, PathToRepositoryRootNormalized) &&
					NewPath != GitTestPath)
				{
					UE_LOG(LogSourceControl, Error, TEXT("Selected files belong to different submodules"));
					return;
				}

				NewPath = TestPath;
				break;
			}
		}
	}
	PathToRepositoryRoot = NewPath;
}

bool FGitLFSSourceControlCommand::DoWork()
{
	bCommandSuccessful = Worker->ExecuteImpl(*this);
	FPlatformAtomics::InterlockedExchange(&bExecuteProcessed, 1);

	return bCommandSuccessful;
}

void FGitLFSSourceControlCommand::Cancel()
{
	FPlatformAtomics::InterlockedExchange(&bCancelled, 1);
}

bool FGitLFSSourceControlCommand::IsCanceled() const
{
	return bCancelled != 0;
}

ECommandResult::Type FGitLFSSourceControlCommand::ReturnResults()
{
	// Save any messages that have accumulated
	for (const auto& String : ResultInfo.InfoMessages)
	{
		Operation->AddInfoMessge(FText::FromString(String));
	}
	for (const auto& String : ResultInfo.ErrorMessages)
	{
		Operation->AddErrorMessge(FText::FromString(String));
	}

	// run the completion delegate if we have one bound
	const ECommandResult::Type Result = bCancelled ? ECommandResult::Cancelled : (bCommandSuccessful ? ECommandResult::Succeeded : ECommandResult::Failed);
	(void) OperationCompleteDelegate.ExecuteIfBound(Operation, Result);

	return Result;
}