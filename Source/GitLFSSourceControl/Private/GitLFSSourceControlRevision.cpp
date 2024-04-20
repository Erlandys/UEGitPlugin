// Copyright (c) 2014-2023 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#include "GitLFSSourceControlRevision.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "GitLFSSourceControlModule.h"
#include "GitLFSSourceControlUtils.h"
#include "ISourceControlModule.h"

#define LOCTEXT_NAMESPACE "GitSourceControl"

#if ENGINE_MAJOR_VERSION >= 5
bool FGitLFSSourceControlRevision::Get( FString& InOutFilename, EConcurrency::Type InConcurrency ) const
{
	if (InConcurrency != EConcurrency::Synchronous)
	{
		UE_LOG(LogSourceControl, Warning, TEXT("Only EConcurrency::Synchronous is tested/supported for this operation."));
	}
#else
bool FGitLFSSourceControlRevision::Get( FString& InOutFilename ) const
{
#endif
	const FGitLFSSourceControlModule* GitSourceControl = FGitLFSSourceControlModule::GetThreadSafe();
	if (!GitSourceControl)
	{
		return false;
	}
	const FGitLFSSourceControlProvider& Provider = GitSourceControl->GetProvider();
	const FString PathToGitBinary = Provider.GetGitBinaryPath();
	FString PathToRepositoryRoot = Provider.GetPathToRepositoryRoot();
	// the repo root can be customised if in a plugin that has it's own repo
	if (PathToRepoRoot.Len())
	{
		PathToRepositoryRoot = PathToRepoRoot;
	}

	// if a filename for the temp file wasn't supplied generate a unique-ish one
	if(InOutFilename.Len() == 0)
	{
		// create the diff dir if we don't already have it (Git wont)
		IFileManager::Get().MakeDirectory(*FPaths::DiffDir(), true);
		// create a unique temp file name based on the unique commit Id
		const FString TempFileName = FString::Printf(TEXT("%stemp-%s-%s"), *FPaths::DiffDir(), *CommitId, *FPaths::GetCleanFilename(Filename));
		InOutFilename = FPaths::ConvertRelativePathToFull(TempFileName);
	}

	// Diff against the revision
	const FString Parameter = FString::Printf(TEXT("%s:%s"), *CommitId, *Filename);

	bool bCommandSuccessful;
	if(FPaths::FileExists(InOutFilename))
	{
		bCommandSuccessful = true; // if the temp file already exists, reuse it directly
	}
	else
	{
		bCommandSuccessful = GitLFSSourceControlUtils::RunDumpToFile(PathToGitBinary, PathToRepositoryRoot, Parameter, InOutFilename);
	}
	return bCommandSuccessful;
}

bool FGitLFSSourceControlRevision::GetAnnotated( TArray<FAnnotationLine>& OutLines ) const
{
	return false;
}

bool FGitLFSSourceControlRevision::GetAnnotated( FString& InOutFilename ) const
{
	return false;
}

const FString& FGitLFSSourceControlRevision::GetFilename() const
{
	return Filename;
}

int32 FGitLFSSourceControlRevision::GetRevisionNumber() const
{
	return RevisionNumber;
}

const FString& FGitLFSSourceControlRevision::GetRevision() const
{
	return ShortCommitId;
}

const FString& FGitLFSSourceControlRevision::GetDescription() const
{
	return Description;
}

const FString& FGitLFSSourceControlRevision::GetUserName() const
{
	return UserName;
}

const FString& FGitLFSSourceControlRevision::GetClientSpec() const
{
	static FString EmptyString(TEXT(""));
	return EmptyString;
}

const FString& FGitLFSSourceControlRevision::GetAction() const
{
	return Action;
}

TSharedPtr<class ISourceControlRevision, ESPMode::ThreadSafe> FGitLFSSourceControlRevision::GetBranchSource() const
{
	// if this revision was copied/moved from some other revision
	return BranchSource;
}

const FDateTime& FGitLFSSourceControlRevision::GetDate() const
{
	return Date;
}

int32 FGitLFSSourceControlRevision::GetCheckInIdentifier() const
{
	return CommitIdNumber;
}

int32 FGitLFSSourceControlRevision::GetFileSize() const
{
	return FileSize;
}

#undef LOCTEXT_NAMESPACE
