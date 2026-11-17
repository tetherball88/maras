#pragma once

namespace MARAS::PapyrusInterface {

    // Registers the native 'maras_sanity' ('msanity') console command on an unused engine
    // SCRIPT_FUNCTION slot so the Skyrim SE console can invoke TTM_SanitySuite scenarios
    // and MARAS JSON state dumps directly without external console mods or .esp edits.
    bool RegisterSanityConsoleCommand();

}  // namespace MARAS::PapyrusInterface
