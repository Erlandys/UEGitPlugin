// Copyright (c) 2014-2023 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "ISourceControlRevision.h"
#include "GitLFSSourceControlHelpers.h"

/** Revision of a file, linked to a specific commit */
class FGitLFSSourceControlRevision : public ISourceControlRevision
{
public:
	//~ Begin ISourceControlRevision Interface
	virtual bool Get(FString& InOutFilename GIT_UE_500_ONLY(, EConcurrency::Type InConcurrency = EConcurrency::Synchronous)) const override;

	virtual bool GetAnnotated(TArray<FAnnotationLine>& OutLines) const override
	{
		return false;
	}
	virtual bool GetAnnotated(FString& InOutFilename) const override
	{
		return false;
	}
	virtual const FString& GetFilename() const override
	{
		return Filename;
	}
	virtual int32 GetRevisionNumber() const override
	{
		return RevisionNumber;
	}
	virtual const FString& GetRevision() const override
	{
		return ShortCommitId;
	}
	virtual const FString& GetDescription() const override
	{
		return Description;
	}
	virtual const FString& GetUserName() const override
	{
		return UserName;
	}
	virtual const FString& GetClientSpec() const override
	{
		static FString EmptyString(TEXT(""));
		return EmptyString;
	}
	virtual const FString& GetAction() const override
	{
		return Action;
	}
	virtual TSharedPtr<ISourceControlRevision> GetBranchSource() const override
	{
		// if this revision was copied/moved from some other revision
		return BranchSource;
	}
	virtual const FDateTime& GetDate() const override
	{
		return Date;
	}
	virtual int32 GetCheckInIdentifier() const override
	{
		return CommitIdNumber;
	}
	virtual int32 GetFileSize() const override
	{
		return FileSize;
	}
	//~ End ISourceControlRevision Interface

private:
	/**
	 * Run a Git "cat-file" command to dump the binary content of a revision into a file.
	 *
	 * @param	InPathToGitBinary	The path to the Git binary
	 * @param	InRepositoryRoot	The Git repository from where to run the command - usually the Game directory
	 * @param	InParameter			The parameters to the Git show command (rev:path)
	 * @param	InDumpFileName		The temporary file to dump the revision
	 * @returns true if the command succeeded and returned no errors
	*/
	static bool RunDumpToFile(
		const FString& InPathToGitBinary,
		const FString& InRepositoryRoot,
		const FString& InParameter,
		const FString& InDumpFileName);

public:
	/** The filename this revision refers to */
	FString Filename;

	/** The full hexadecimal SHA1 id of the commit this revision refers to */
	FString CommitId;

	/** The short hexadecimal SHA1 id (8 first hex char out of 40) of the commit: the string to display */
	FString ShortCommitId;

	/** The numeric value of the short SHA1 (8 first hex char out of 40) */
	int32 CommitIdNumber = 0;

	/** The index of the revision in the history (SBlueprintRevisionMenu assumes order for the "Depot" label) */
	int32 RevisionNumber = 0;

	/** The SHA1 identifier of the file at this revision */
	FString FileHash;

	/** The description of this revision */
	FString Description;

	/** The user that made the change */
	FString UserName;

	/** The action (add, edit, branch etc.) performed at this revision */
	FString Action;

	/** Source of move ("branch" in Perforce term) if any */
	TSharedPtr<FGitLFSSourceControlRevision> BranchSource;

	/** The date this revision was made */
	FDateTime Date;

	/** The size of the file at this revision */
	int32 FileSize;

	/** Dynamic repository root **/
	FString PathToRepoRoot;
};