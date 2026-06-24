#pragma once

#include "CoreExport.hxx"
#include "../../Interface/HostServices.hxx"

GE_API void CoreInitEditor();
GE_API void CoreInitGame();
GE_API void CoreShutdown();
GE_API HostServices* CoreGetHostServices();
