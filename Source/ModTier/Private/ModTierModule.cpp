#include "ModTierModule.h"

#include "ModTier.h"

void FModTierModule::StartupModule()
{
	FModTier::RegisterHooks();
}

IMPLEMENT_MODULE(FModTierModule, ModTier)
