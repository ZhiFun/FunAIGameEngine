#include "Audio.h"

namespace engine {

bool Audio::start() {
    if (started_) return true;
    if (backend_ == nullptr) return false;
    if (!backend_->start(mixer_)) return false;
    started_ = true;
    return true;
}

} // namespace engine
