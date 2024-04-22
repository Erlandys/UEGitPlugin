// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#include "GitLFSSourceControlSettings.h"
#include "Misc/ConfigCacheIni.h"
#include "SourceControlHelpers.h"

/** The section of the ini file we load our settings from */
static const FString SettingsSection = TEXT("GitSourceControl.GitSourceControlSettings");

FString FGitLFSSourceControlSettings::GetBinaryPath() const
{
	FScopeLock ScopeLock(&CriticalSection);

	// Return a copy to be thread-safe
	return BinaryPath;
}

bool FGitLFSSourceControlSettings::SetBinaryPath(const FString& InString)
{
	FScopeLock ScopeLock(&CriticalSection);

	const bool bChanged = BinaryPath != InString;
	if (bChanged)
	{
		BinaryPath = InString;
	}

	return bChanged;
}

bool FGitLFSSourceControlSettings::IsUsingGitLfsLocking() const
{
	FScopeLock ScopeLock(&CriticalSection);

	return bUsingGitLfsLocking;
}

bool FGitLFSSourceControlSettings::SetUsingGitLfsLocking(const bool InUsingGitLfsLocking)
{
	FScopeLock ScopeLock(&CriticalSection);

	const bool bChanged = bUsingGitLfsLocking != InUsingGitLfsLocking;
	bUsingGitLfsLocking = InUsingGitLfsLocking;
	return bChanged;
}

FString FGitLFSSourceControlSettings::GetLfsUserName() const
{
	FScopeLock ScopeLock(&CriticalSection);

	// Return a copy to be thread-safe
	return LfsUserName;
}

bool FGitLFSSourceControlSettings::SetLfsUserName(const FString& InString)
{
	FScopeLock ScopeLock(&CriticalSection);

	const bool bChanged = LfsUserName != InString;
	if (bChanged)
	{
		LfsUserName = InString;
	}
	return bChanged;
}

// This is called at startup nearly before anything else in our module: BinaryPath will then be used by the provider
void FGitLFSSourceControlSettings::LoadSettings()
{
	FScopeLock ScopeLock(&CriticalSection);

	const FString& IniFile = SourceControlHelpers::GetSettingsIni();
	GConfig->GetString(*SettingsSection, TEXT("BinaryPath"), BinaryPath, IniFile);
	GConfig->GetBool(*SettingsSection, TEXT("UsingGitLfsLocking"), bUsingGitLfsLocking, IniFile);
	GConfig->GetString(*SettingsSection, TEXT("LfsUserName"), LfsUserName, IniFile);
}

void FGitLFSSourceControlSettings::SaveSettings() const
{
	FScopeLock ScopeLock(&CriticalSection);

	const FString& IniFile = SourceControlHelpers::GetSettingsIni();
	GConfig->SetString(*SettingsSection, TEXT("BinaryPath"), *BinaryPath, IniFile);
	GConfig->SetBool(*SettingsSection, TEXT("UsingGitLfsLocking"), bUsingGitLfsLocking, IniFile);
	GConfig->SetString(*SettingsSection, TEXT("LfsUserName"), *LfsUserName, IniFile);
}
