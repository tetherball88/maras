#pragma once

#include "maras/app/ModalGateway.h"

namespace MARAS::Bridge::Ui {

    /// Returns the live Prisma modal gateway implementation owned by `modules/bridge_ui`.
    [[nodiscard]] MARAS::App::IModalGateway& GetModalGatewayImpl();

}  // namespace MARAS::Bridge::Ui
