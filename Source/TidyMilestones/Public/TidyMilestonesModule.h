#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FTidyMilestonesModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
};
