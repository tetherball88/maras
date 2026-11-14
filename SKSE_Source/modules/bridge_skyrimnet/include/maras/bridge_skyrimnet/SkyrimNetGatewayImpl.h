#pragma once

#include "maras/app/SkyrimNetGateway.h"

namespace MARAS::Bridge::SkyrimNet {

    [[nodiscard]] MARAS::App::ISkyrimNetGateway& GetSkyrimNetGatewayImpl();

}  // namespace MARAS::Bridge::SkyrimNet
