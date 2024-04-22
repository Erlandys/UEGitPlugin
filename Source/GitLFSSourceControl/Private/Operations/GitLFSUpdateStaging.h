// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "CoreMinimal.h"
#include "GitLFSSourceControlState.h"
#include "GitLFSSourceControlUtils.h"
#include "IGitLFSSourceControlWorker.h"

class FGitLFSUpdateStagingWorker : public IGitLFSSourceControlWorker
{
	GENERATED_WORKER_BODY("UpdateChangelistsStatus")

public:
	//~ Begin IGitLFSSourceControlWorker Interface
	virtual bool Execute(FGitLFSSourceControlCommand& Command, FGitLFSCommandHelpers& Helpers) override
	{
		return FGitLFSSourceControlUtils::UpdateChangelistStateByCommand();
	}
	virtual bool UpdateStates() const override
	{
		return true;
	}
	//~ End IGitLFSSourceControlWorker Interface
};