#pragma once

namespace engine {
class Engine;
}

namespace engine {

// 一个游戏就是一个 Game 子类。生命周期见 docs/ENGINE_API.md
class Game {
public:
    virtual ~Game() {}
    virtual const char* name() const = 0;
    virtual void on_start(Engine& e)  = 0;
    virtual void on_update(Engine& e, float dt) = 0;
    virtual void on_render(Engine& e) = 0;
};

} // namespace engine
