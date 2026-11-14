#pragma once

#include "maras/app/UiBridgeGateway.h"

namespace MARAS::Bridge::Ui {

    /// Returns the live Prisma UI bridge gateway implementation owned by `modules/bridge_ui`.
    [[nodiscard]] MARAS::App::IUiBridgeGateway& GetUiBridgeGatewayImpl();

}  // namespace MARAS::Bridge::Ui
