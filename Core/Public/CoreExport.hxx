#pragma once

#ifdef GE_BUILD_CORE
#define GE_API __declspec(dllexport)
#else
#define GE_API __declspec(dllimport)
#endif
