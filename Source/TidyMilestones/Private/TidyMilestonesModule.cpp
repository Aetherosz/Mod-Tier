#include "TidyMilestonesModule.h"

#include "TidyMilestones.h"

void FTidyMilestonesModule::StartupModule()
{
	FTidyMilestones::RegisterHooks();
}

IMPLEMENT_MODULE(FTidyMilestonesModule, TidyMilestones)
