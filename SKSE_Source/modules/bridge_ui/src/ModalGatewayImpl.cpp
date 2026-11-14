#include "maras/bridge_ui/ModalGatewayImpl.h"

#include "maras/bridge_ui/ConfirmationModal.h"
#include "maras/bridge_ui/PrismaModalManager.h"
#include "maras/bridge_ui/PrismaUIManager.h"

namespace MARAS::Bridge::Ui {

    namespace {

        class ModalGatewayImpl final : public MARAS::App::IModalGateway {
        public:
            std::string ShowModal(
                MARAS::App::ModalRequest request,
                MARAS::App::ModalCallback callback) override {
                return PrismaModalManager::GetSingleton().ShowModal(
                    std::move(request),
                    std::move(callback));
            }

            std::string ShowRawJsonModal(
                const std::string& modalSpecJson,
                MARAS::App::ModalCallback callback) override {
                return PrismaModalManager::GetSingleton().ShowRawJsonModal(
                    modalSpecJson,
                    std::move(callback));
            }

            bool HasActiveModal() const override {
                return PrismaModalManager::GetSingleton().HasActiveModal();
            }

            void SendActiveModalToUI(bool forceResend) override {
                auto& uiMgr = PrismaUIManager::GetSingleton();
                if (!uiMgr.IsModalDisplayBlocked()) {
                    PrismaModalManager::GetSingleton().SendActiveModalToUI(forceResend);
                }
                uiMgr.SyncViewVisibilityAndFocus();
            }

            std::int32_t CheckAndSetConfirmationCooldown(
                const char* actionType,
                const std::vector<RE::Actor*>& actors) override {
                return ConfirmationModal::CheckAndSet(actionType, actors);
            }

            RE::BSScript::LatentStatus ShowConfirmationModal(
                RE::BSScript::Internal::VirtualMachine* vm,
                RE::VMStackID stackID,
                const char* actionType,
                const std::vector<RE::Actor*>& mainActors,
                const std::vector<RE::Actor*>& secondaryActors,
                const char* paramsJson) override {
                return ConfirmationModal::Show(
                    vm,
                    stackID,
                    actionType,
                    mainActors,
                    secondaryActors,
                    paramsJson);
            }

            void Revert() override {
                PrismaModalManager::GetSingleton().Revert();
            }
        };

    }  // namespace

    MARAS::App::IModalGateway& GetModalGatewayImpl() {
        static ModalGatewayImpl instance;
        return instance;
    }

}  // namespace MARAS::Bridge::Ui
