// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "CoreMinimal.h"
#include "IGitLFSSourceControlWorker.h"

// Lock (check-out) a set of files using Git LFS 2.
class FGitLFSCheckOutWorker : public IGitLFSSourceControlWorker
{
	GENERATED_WORKER_BODY("CheckOut")

public:
	//~ Begin IGitLFSSourceControlWorker Interface
	virtual bool Execute(FGitLFSSourceControlCommand& Command, FGitLFSCommandHelpers& Helpers) override;
	//~ End IGitLFSSourceControlWorker Interface
};