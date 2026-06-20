#pragma once

#ifdef _DEBUG
#define DEBUG
#else
//#define SHIPPING
#endif

#define _THREADPOOL_
#define _CRTDBG_MAP_ALLOC

#define pRelese(pointer) { if (pointer) { delete pointer; pointer = nullptr; } }
