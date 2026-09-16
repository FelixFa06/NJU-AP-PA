#include "game/host_session.h"

int main() {
    uno::HostSession session("Host");
    session.set_prompt_enabled(false);
    return session.run();
}
