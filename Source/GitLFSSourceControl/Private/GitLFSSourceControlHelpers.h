// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "Runtime/Launch/Resources/Version.h"

#define GIT_ENGINE_VERSION ENGINE_MAJOR_VERSION * 100 + ENGINE_MINOR_VERSION

#if GIT_ENGINE_VERSION >= 500
#define GIT_UE_500_SWITCH(Before, EqualOrAfter) EqualOrAfter
#define GIT_UE_500_ONLY(...) __VA_ARGS__
#else
#define GIT_UE_500_SWITCH(Before, EqualOrAfter) Before
#define GIT_UE_500_ONLY(...)
#endif

#if GIT_ENGINE_VERSION >= 501
#define GIT_UE_501_SWITCH(Before, EqualOrAfter) EqualOrAfter
#define GIT_UE_501_ONLY(...) __VA_ARGS__
#else
#define GIT_UE_501_SWITCH(Before, EqualOrAfter) Before
#define GIT_UE_501_ONLY(...)
#endif

#if GIT_ENGINE_VERSION >= 502
#define GIT_UE_502_SWITCH(Before, EqualOrAfter) EqualOrAfter
#define GIT_UE_502_ONLY(...) __VA_ARGS__
#else
#define GIT_UE_502_SWITCH(Before, EqualOrAfter) Before
#define GIT_UE_502_ONLY(...)
#endif

#if GIT_ENGINE_VERSION >= 503
#define GIT_UE_503_SWITCH(Before, EqualOrAfter) EqualOrAfter
#define GIT_UE_503_ONLY(...) __VA_ARGS__
#else
#define GIT_UE_503_SWITCH(Before, EqualOrAfter) Before
#define GIT_UE_503_ONLY(...)
#endif

#if GIT_ENGINE_VERSION >= 504
#define GIT_UE_504_SWITCH(Before, EqualOrAfter) EqualOrAfter
#define GIT_UE_504_ONLY(...) __VA_ARGS__
#else
#define GIT_UE_504_SWITCH(Before, EqualOrAfter) Before
#define GIT_UE_504_ONLY(...)
#endif

using FEditorAppStyle = GIT_UE_501_SWITCH(FEditorStyle, FAppStyle);