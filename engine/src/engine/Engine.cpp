#include "Engine.h"

namespace engine {

void Engine::frame(Game& game, float dt) {
    game.on_update(*this, dt);
    game.on_render(*this);
    display.present();
    // 边沿(按下/松开)只在一个逻辑帧内有效, 帧末清掉
    input.pressed  = 0;
    input.released = 0;
}

} // namespace engine
