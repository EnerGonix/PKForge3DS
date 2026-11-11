// Every screen of the app, built through these factories.
#pragma once

#include "app.hpp"
#include "pkx/PKX.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

class OpenSave;
namespace pksm
{
    class Sav;
}

namespace screens
{
    std::unique_ptr<Screen> home();
    // The title screen, pushed over home at start; any touch, A or START leaves it.
    std::unique_ptr<Screen> title(std::function<void()> ready);
    // Boxes of a save beside the Bank; no save opens the Bank alone.
    std::unique_ptr<Screen> storage(std::shared_ptr<OpenSave> save);
    // The same, opening straight on the save's events (Mystery Gift, World & events): home's Events tile.
    std::unique_ptr<Screen> storageEvents(std::shared_ptr<OpenSave> save);
    // Edits a copy; done receives the edited Pokémon when the user keeps the changes.
    // step (optional) fetches the previous (-1) or next (+1) Pokémon of the same box for the plate's arrows:
    // it fills pk, where and done for it, false when there is none.
    using EditorStep = std::function<bool(int delta, std::unique_ptr<pksm::PKX>& pk, std::string& where,
        std::function<void(std::unique_ptr<pksm::PKX>)>& done)>;
    std::unique_ptr<Screen> editor(std::unique_ptr<pksm::PKX> pk, std::string where, pksm::Sav* sav,
        std::function<void(std::unique_ptr<pksm::PKX>)> done, EditorStep step = nullptr);
    std::unique_ptr<Screen> bag(std::shared_ptr<OpenSave> save);
    std::unique_ptr<Screen> trainer(std::shared_ptr<OpenSave> save);
    std::unique_ptr<Screen> about();
    // Music and effects on or off, and the speaker test.
    void soundMenu();
    // The About screen's easter egg (the Konami code there).
    std::unique_ptr<Screen> badApple();
    // The Poképark: Bank Pokémon living on the park map.
    std::unique_ptr<Screen> park();
    // A Pokémon's bytes; done gets the edited one (checksum redone) when the user keeps it.
    std::unique_ptr<Screen> hex(std::unique_ptr<pksm::PKX> pk, std::string where,
        std::function<void(std::unique_ptr<pksm::PKX>)> done);

    struct PickItem
    {
        int id;
        std::string name;
        std::string detail;
    };
    enum class PickPreview
    {
        None,
        Species,
        Move,
        Item
    };
    // A searchable list; chosen gets the picked id.
    std::unique_ptr<Screen> picker(std::string title, std::vector<PickItem> items, int current,
        std::function<void(int)> chosen, PickPreview preview = PickPreview::None);
}
