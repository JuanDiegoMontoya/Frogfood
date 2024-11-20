#pragma once
#include "ClassImplMacros.h"
#include "Game.h"

class PlayerHead final : public Head
{
public:
  NO_COPY_NO_MOVE(PlayerHead);
  void VariableUpdate(DeltaTime dt, World& world) override;

private:
};