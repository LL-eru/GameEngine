#pragma once

#include "../../Interface/HostServices.hxx"

#ifdef GE_PLUGIN
HostServices* GetHostServices();
#else
#include "CoreInit.hxx"
inline HostServices* GetHostServices() { return CoreGetHostServices(); }
#endif
