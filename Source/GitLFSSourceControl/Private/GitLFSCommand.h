// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GitLFSSourceControlCommand.h"
#include "GitLFSSourceControlModule.h"

#define RUN_GIT_COMMAND(Command, ...) \
	FGitLFSCommand() <<= FGitLFSCommand::FArguments(TEXT(Command))

#define RUN_LFS_COMMAND(Command, ...) \
	FGitLFSCommand() >>= FGitLFSCommand::FArguments(TEXT(Command))

struct FGitLFSCommand
{
public:
	struct FArguments
	{
		FArguments(const FString& Command)
			: InternalCommand(Command)
		{
			if (FGitLFSSourceControlModule* Module = FGitLFSSourceControlModule::GetThreadSafe())
			{
				if (const TSharedPtr<FGitLFSSourceControlProvider>& Provider = Module->GetProvider())
				{
					InternalPathToGit = Provider->GetGitBinaryPath();
					InternalRepositoryRoot = Provider->GetPathToRepositoryRoot();
				}
			}
		}
		FArguments(FString&& Command)
			: InternalCommand(MoveTemp(Command))
		{
			if (FGitLFSSourceControlModule* Module = FGitLFSSourceControlModule::GetThreadSafe())
			{
				if (const auto& Provider = Module->GetProvider())
				{
					InternalPathToGit = Provider->GetGitBinaryPath();
					InternalRepositoryRoot = Provider->GetPathToRepositoryRoot();
				}
			}
		}

		FArguments& Command(const FString& Command)
		{
			InternalCommand = Command;
			return *this;
		}
		FArguments& PathToGit(const FString& PathToGit)
		{
			InternalPathToGit = PathToGit;
			return *this;
		}
		FArguments& RepositoryRoot(const FString& RepositoryRoot)
		{
			InternalRepositoryRoot = RepositoryRoot;
			return *this;
		}
		FArguments& Parameters(const TArray<FString>& Parameters)
		{
			InternalParameters.Append(Parameters);
			return *this;
		}
		FArguments& Parameter(const FString& Parameter)
		{
			InternalParameters.Add(Parameter);
			return *this;
		}
		FArguments& Files(const TArray<FString>& Files)
		{
			InternalFiles.Append(Files);
			return *this;
		}
		FArguments& File(const FString& File)
		{
			InternalFiles.Add(File);
			return *this;
		}
		FArguments& SCCommand(const FGitLFSSourceControlCommand& Command)
		{
			InternalPathToGit = Command.PathToGitBinary;
			InternalRepositoryRoot = Command.PathToRepositoryRoot;
			return *this;
		}
		FArguments& Results(TArray<FString>& Results)
		{
			InternalResults = &Results;
			return *this;
		}
		FArguments& ResultString(FString& Result)
		{
			InternalResultString = &Result;
			return *this;
		}
		FArguments& Errors(TArray<FString>& Errors)
		{
			InternalErrors = &Errors;
			return *this;
		}
		FArguments& ExpectedReturnCode(const int32 Code)
		{
			InternalExpectedReturnCode = Code;
			return *this;
		}
		FArguments& ReturnCode(int32& OutReturnCode)
		{
			InternalReturnCode = &OutReturnCode;
			return *this;
		}

	private:
		FString InternalCommand;
		FString InternalPathToGit;
		FString InternalRepositoryRoot;
		TArray<FString> InternalParameters;
		TArray<FString> InternalFiles;
		TArray<FString>* InternalResults = nullptr;
		FString* InternalResultString = nullptr;
		TArray<FString>* InternalErrors = nullptr;
		int32* InternalReturnCode = nullptr;
		int32 InternalExpectedReturnCode = 0;
		friend struct FGitLFSCommand;
	};
public:
	bool operator<<=(FArguments& Args) const &&
	{
		return Run(Args);
	}
	bool operator>>=(FArguments& Args) const &&
	{
		return RunLFS(Args);
	}

private:
	static bool Run(FArguments& Args);
	static bool RunLFS(FArguments& Args);
	static bool BatchRuns(FArguments& Args);
	static bool RunImpl(FArguments& Args);
};
