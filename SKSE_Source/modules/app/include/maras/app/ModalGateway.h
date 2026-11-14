#pragma once

#include "PCH.h"
#include "maras/app/ModalTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace MARAS::App {

    class IModalGateway {
    public:
        virtual ~IModalGateway() = default;

        virtual std::string ShowModal(ModalRequest request, ModalCallback callback) = 0;
        virtual std::string ShowRawJsonModal(const std::string& modalSpecJson, ModalCallback callback) = 0;
        virtual bool HasActiveModal() const = 0;
        virtual void SendActiveModalToUI(bool forceResend = false) = 0;

        virtual std::int32_t CheckAndSetConfirmationCooldown(const char* actionType,
                                                             const std::vector<RE::Actor*>& actors) = 0;
        virtual RE::BSScript::LatentStatus ShowConfirmationModal(RE::BSScript::Internal::VirtualMachine* vm,
                                                                 RE::VMStackID stackID,
                                                                 const char* actionType,
                                                                 const std::vector<RE::Actor*>& mainActors,
                                                                 const std::vector<RE::Actor*>& secondaryActors,
                                                                 const char* paramsJson) = 0;

        virtual void Revert() = 0;
    };

}  // namespace MARAS::App
