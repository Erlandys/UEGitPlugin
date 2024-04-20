// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "CoreMinimal.h"
#include "IGitLFSSourceControlWorker.h"
#include "GitLFSSourceControlState.h"

#include "ISourceControlOperation.h"

/**
 * Internal operation used to fetch from remote
 */
class FGitLFSFetch : public ISourceControlOperation
{
public:
	// ISourceControlOperation interface
	virtual FName GetName() const override;

	virtual FText GetInProgressString() const override;

	bool bUpdateStatus = false;
};

/** Called when first activated on a project, and then at project load time.
 *  Look for the root directory of the git repository (where the ".git/" subdirectory is located). */
class FGitLFSConnectWorker : public IGitLFSSourceControlWorker
{
public:
	virtual ~FGitLFSConnectWorker() {}
	// IGitLFSSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FGitLFSSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

	/** Temporary states for results */
	TMap<const FString, FGitLFSState> States;
};

/** Lock (check-out) a set of files using Git LFS 2. */
class FGitLFSCheckOutWorker : public IGitLFSSourceControlWorker
{
public:
	virtual ~FGitLFSCheckOutWorker() {}
	// IGitLFSSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FGitLFSSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

	/** Temporary states for results */
	TMap<const FString, FGitLFSState> States;
};

/** Commit (check-in) a set of files to the local depot. */
class FGitLFSCheckInWorker : public IGitLFSSourceControlWorker
{
public:
	virtual ~FGitLFSCheckInWorker() {}
	// IGitLFSSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FGitLFSSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

	/** Temporary states for results */
	TMap<const FString, FGitLFSState> States;
};

/** Add an untracked file to revision control (so only a subset of the git add command). */
class FGitLFSMarkForAddWorker : public IGitLFSSourceControlWorker
{
public:
	virtual ~FGitLFSMarkForAddWorker() {}
	// IGitLFSSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FGitLFSSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

	/** Temporary states for results */
	TMap<const FString, FGitLFSState> States;
};

/** Delete a file and remove it from revision control. */
class FGitLFSDeleteWorker : public IGitLFSSourceControlWorker
{
public:
	virtual ~FGitLFSDeleteWorker() {}
	// IGitLFSSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FGitLFSSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

	/** Temporary states for results */
	TMap<const FString, FGitLFSState> States;
};

/** Revert any change to a file to its state on the local depot. */
class FGitLFSRevertWorker : public IGitLFSSourceControlWorker
{
public:
	virtual ~FGitLFSRevertWorker() {}
	// IGitLFSSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FGitLFSSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

	/** Temporary states for results */
	TMap<const FString, FGitLFSState> States;
};

/** Git pull --rebase to update branch from its configured remote */
class FGitLFSSyncWorker : public IGitLFSSourceControlWorker
{
public:
	virtual ~FGitLFSSyncWorker() {}
	// IGitLFSSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FGitLFSSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

	/** Temporary states for results */
	TMap<const FString, FGitLFSState> States;
};

/** Get revision control status of files on local working copy. */
class FGitLFSUpdateStatusWorker : public IGitLFSSourceControlWorker
{
public:
	virtual ~FGitLFSUpdateStatusWorker() {}
	// IGitLFSSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FGitLFSSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

public:
	/** Temporary states for results */
	TMap<const FString, FGitLFSState> States;

	/** Map of filenames to history */
	TMap<FString, TGitSourceControlHistory> Histories;
};

/** Copy or Move operation on a single file */
class FGitLFSCopyWorker : public IGitLFSSourceControlWorker
{
public:
	virtual ~FGitLFSCopyWorker() {}
	// IGitLFSSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FGitLFSSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

	/** Temporary states for results */
	TMap<const FString, FGitLFSState> States;
};

/** git add to mark a conflict as resolved */
class FGitLFSResolveWorker : public IGitLFSSourceControlWorker
{
public:
	virtual ~FGitLFSResolveWorker() {}
	virtual FName GetName() const override;
	virtual bool Execute(class FGitLFSSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

	/** Temporary states for results */
	TMap<const FString, FGitLFSState> States;
};

/** Git push to publish branch for its configured remote */
class FGitLFSFetchWorker : public IGitLFSSourceControlWorker
{
public:
	virtual ~FGitLFSFetchWorker() {}
	// IGitLFSSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FGitLFSSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

	/** Temporary states for results */
	TMap<const FString, FGitLFSState> States;
};

class FGitLFSMoveToChangelistWorker : public IGitLFSSourceControlWorker
{
public:
	virtual ~FGitLFSMoveToChangelistWorker() {}
	// IGitLFSSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FGitLFSSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;
	
	/** Temporary states for results */
	TMap<const FString, FGitLFSState> States;
};

class FGitLFSUpdateStagingWorker: public IGitLFSSourceControlWorker
{
public:
	virtual ~FGitLFSUpdateStagingWorker() {}
	// IGitLFSSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FGitLFSSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;
	
	/** Temporary states for results */
	TMap<const FString, FGitLFSState> States;
};