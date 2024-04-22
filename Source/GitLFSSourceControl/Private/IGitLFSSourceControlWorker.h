// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "GitLFSCommandHelpers.h"
#include "Templates/SharedPointer.h"
#include "GitLFSSourceControlState.h"
#include "GitLFSSourceControlUtils.h"
#include "GitLFSSourceControlCommand.h"

class FGitLFSSourceControlCommand;

#define GENERATED_WORKER_BODY(OperationName) \
public: \
	virtual FName GetName() const override \
	{ \
		static FName Result(OperationName); \
		return Result; \
	} \
	static FName GetStaticName() \
	{ \
		static FName Result(OperationName); \
		return Result; \
	}

class IGitLFSSourceControlWorker
{
public:
	/**
	 * Name describing the work that this worker does. Used for factory method hookup.
	 */
	virtual FName GetName() const = 0;

private:
	bool ExecuteImpl(FGitLFSSourceControlCommand& Command)
	{
		// We want to ensure that we are executing the right command
		if (!ensure(Command.Operation->GetName() == GetName()))
		{
			return false;
		}

		FGitLFSCommandHelpers Helpers(Command);
		return Execute(Command, Helpers);
	}

public:
	/**
	 * Function that actually does the work. Can be executed on another thread.
	 */
	virtual bool Execute(FGitLFSSourceControlCommand& Command, FGitLFSCommandHelpers& Helpers) = 0;

	/**
	 * Updates the state of any items after completion (if necessary). This is always executed on the main thread.
	 * @returns true if states were updated
	 */
	virtual bool UpdateStates() const
	{
		return FGitLFSSourceControlUtils::UpdateCachedStates(States);
	}

public:
	virtual ~IGitLFSSourceControlWorker() = default;

protected:
	TMap<const FString, FGitLFSState> States;

	friend class FGitLFSSourceControlCommand;
};