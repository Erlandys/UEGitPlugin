// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

/**
 * Helper struct for maintaining temporary files for passing to commands
 */
class FGitLFSScopedTempFile
{
public:
	/** Constructor - open & write string to temp file */
	FGitLFSScopedTempFile(const FText& InText);

	/** Destructor - delete temp file */
	~FGitLFSScopedTempFile();

	/** Get the filename of this temp file - empty if it failed to be created */
	const FString& GetFilename() const;

private:
	/** The filename we are writing to */
	FString Filename;
};